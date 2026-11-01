// Bateria de testes (x64): tudo que ajuda a achar o defeito de um fechamento sem precisar reproduzir.
// Nao existe no Extensions.dll original; so roda no x64.
//
// Arquivo: <exe dir>\Crashes\bateria_<pid>_<AAAAMMDD-HHMMSS>.log (um por execucao, escrita direta, sem buffer).
//   INICIO   pid, data/hora, base e timestamp do exe e da DLL, se o WER LocalDumps esta ligado
//   EXC      excecao de 1a chance grave (AV, instrucao ilegal, pilha, heap, /GS, divisao): codigo, endereco,
//            registradores e pilha desenrolada (modulo+rva). Uma vez por endereco, no maximo 400.
//   SAIDA    toda saida do processo (RtlExitUserProcess / TerminateProcess em si mesmo): codigo, tempo de
//            vida e pilha de quem pediu. Codigo != 0 grava tambem Saida_<pid>_<hora>.dmp.
//   SOLUCO   >= 200 ms sem desenhar um quadro (microtravada; batida = OnLayerRender 0x495810): duracao + amostra da pilha principal; VIVO traz o
//            total por minuto. Travadas longas ganham mais amostras (~1 s, ~3 s, ~8 s): a de 150 ms so mostra o comeco.
//   TRAVADO  >= 20 s sem desenhar: RIP + varredura da pilha da thread principal (copiada
//            com ela suspensa por microssegundos, sem lock); RECUPEROU quando volta. Dump: procdump -h.
//   VIVO     a cada 60 s: memoria privada, working set, handles, threads (vazamento / falta de memoria).
// Fechamento sem nenhuma linha SAIDA = __fastfail / morte externa: o procdump (-e) ou o WER pegam o dump.
//   Estouro de pilha: a thread principal ganha 64 KB de reserva (SetThreadStackGuarantee) e a VEH so sinaliza a
//   thread SoHelper, que grava "EXC C00000FD" + Pilha_<pid>_<hora>.dmp. Antes as VEH estouravam de novo e o kernel
//   matava o processo sem log nem dump.
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/AscRuntime.hpp>

#if ASC_X64
#include <Windows.h>
#include <DbgHelp.h>
#include <psapi.h>
#include <MinHook.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#ifndef _NTDEF_
typedef LONG NTSTATUS;   // winternl.h/ntdef.h nao incluidos
#endif

#pragma comment(lib, "dbghelp.lib")

namespace
{
    HANDLE g_file = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION g_lock;
    char g_dir[MAX_PATH];
    ULONGLONG g_start = 0;
    DWORD g_mainThread = 0;
    thread_local bool t_inside = false;

    struct Mod { uintptr_t base, end; char name[64]; };
    Mod g_exe = {}, g_dll = {};

    void FillMod(Mod& m, HMODULE h)
    {
        MODULEINFO mi = {};
        GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi));
        m.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        m.end = m.base + mi.SizeOfImage;
        char path[MAX_PATH] = {};
        GetModuleFileNameA(h, path, MAX_PATH);
        const char* s = strrchr(path, '\\');
        strncpy_s(m.name, s ? s + 1 : path, _TRUNCATE);
    }

    void Write(const char* text)
    {
        if (g_file == INVALID_HANDLE_VALUE)
            return;
        DWORD w;
        WriteFile(g_file, text, static_cast<DWORD>(strlen(text)), &w, nullptr);
    }

    // Uma linha com hora; o lock e tentado por pouco tempo (uma excecao com o lock tomado por outra thread
    // nao pode travar o processo inteiro).
    void Line(const char* fmt, ...)
    {
        char buf[1024];
        SYSTEMTIME t;
        GetLocalTime(&t);
        int n = sprintf_s(buf, "%02u:%02u:%02u.%03u [t%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
        va_list ap;
        va_start(ap, fmt);
        n += vsnprintf(buf + n, sizeof(buf) - n - 3, fmt, ap);
        va_end(ap);
        if (n < 0 || n > static_cast<int>(sizeof(buf)) - 3)
            n = static_cast<int>(sizeof(buf)) - 3;
        buf[n++] = '\r';
        buf[n++] = '\n';
        buf[n] = 0;
        for (int i = 0; i < 200; ++i)
        {
            if (TryEnterCriticalSection(&g_lock))
            {
                Write(buf);
                LeaveCriticalSection(&g_lock);
                return;
            }
            Sleep(1);
        }
        Write(buf);   // sem lock: melhor uma linha embaralhada que nenhuma
    }

    // modulo+rva; fora do exe/DLL pergunta ao loader (pode tomar o loader lock: so fora do caminho quente).
    void Where(uintptr_t a, char* out, size_t n)
    {
        if (a >= g_exe.base && a < g_exe.end)
        {
            sprintf_s(out, n, "%s+0x%llX", g_exe.name, static_cast<unsigned long long>(a - g_exe.base));
            return;
        }
        if (a >= g_dll.base && a < g_dll.end)
        {
            sprintf_s(out, n, "%s+0x%llX", g_dll.name, static_cast<unsigned long long>(a - g_dll.base));
            return;
        }
        HMODULE h = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(a), &h) && h)
        {
            char path[MAX_PATH] = {};
            GetModuleFileNameA(h, path, MAX_PATH);
            const char* s = strrchr(path, '\\');
            sprintf_s(out, n, "%s+0x%llX", s ? s + 1 : path, static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(h)));
            return;
        }
        sprintf_s(out, n, "0x%llX", static_cast<unsigned long long>(a));
    }

    // Desenrola a partir de um CONTEXT (copia) pelo .pdata; para em codigo sem unwind info (folha / JIT).
    void Unwind(CONTEXT ctx, const char* tag, int max = 24)
    {
        for (int i = 0; i < max && ctx.Rip; ++i)
        {
            char w[160];
            Where(ctx.Rip, w, sizeof(w));
            Line("  %s #%02d %s  (rsp %llX)", tag, i, w, static_cast<unsigned long long>(ctx.Rsp));
            DWORD64 base = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
            if (!f)
            {
                // folha: o retorno esta em [rsp]
                if (IsBadReadPtr(reinterpret_cast<void*>(ctx.Rsp), 8))
                    break;
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
                continue;
            }
            PVOID data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, f, &ctx, &data, &frame, nullptr);
        }
    }

    void Stack(const char* tag)
    {
        void* frames[40];
        const USHORT n = RtlCaptureStackBackTrace(2, 40, frames, nullptr);
        for (USHORT i = 0; i < n; ++i)
        {
            char w[160];
            Where(reinterpret_cast<uintptr_t>(frames[i]), w, sizeof(w));
            Line("  %s #%02u %s", tag, i, w);
        }
    }

    void Dump(const char* kind, PEXCEPTION_POINTERS ep, DWORD tid)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char path[MAX_PATH];
        sprintf_s(path, "%s\\%s_%lu_%02u%02u%02u.dmp", g_dir, kind, GetCurrentProcessId(), t.wHour, t.wMinute, t.wSecond);
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            Line("dump %s: CreateFile falhou (%lu)", path, GetLastError());
            return;
        }
        MINIDUMP_EXCEPTION_INFORMATION ei = {tid, ep, FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
                                                     MiniDumpWithProcessThreadData | MiniDumpWithHandleData);
        const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, ep ? &ei : nullptr, nullptr, nullptr);
        CloseHandle(h);
        Line("dump %s: %s", path, ok ? "ok" : "FALHOU");
    }

    // ---- EXC: 1a chance ----
    bool Serious(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case 0xC0000374 /*heap*/: case 0xC0000409 /*/GS*/:
        case 0xC0000420 /*assert*/: case 0xC0000417 /*CRT invalid param*/: case EXCEPTION_DATATYPE_MISALIGNMENT:
            return true;
        }
        return false;
    }

    uintptr_t g_seen[400];
    std::atomic<int> g_seenCount{0};

    // Estouro de pilha: a thread que estourou nao tem pilha para formatar log nem para o MiniDumpWriteDump. Ela so
    // publica os ponteiros e espera; a thread SoHelper (pilha propria) grava a linha EXC e o dump.
    HANDLE g_soEvent = nullptr, g_soDone = nullptr;
    PEXCEPTION_POINTERS g_soEp = nullptr;
    DWORD g_soTid = 0;
    std::atomic<bool> g_soTaken{false};

    DWORD WINAPI SoHelper(void*)
    {
        WaitForSingleObject(g_soEvent, INFINITE);
        char w[160];
        Where(reinterpret_cast<uintptr_t>(g_soEp->ExceptionRecord->ExceptionAddress), w, sizeof(w));
        const CONTEXT& c = *g_soEp->ContextRecord;
        Line("EXC C00000FD (estouro de pilha) em %s  rsp %llX  t%lu%s", w, c.Rsp, g_soTid, g_soTid == g_mainThread ? "  [principal]" : "");
        Dump("Pilha", g_soEp, g_soTid);
        SetEvent(g_soDone);
        return 0;
    }

    LONG CALLBACK Veh(PEXCEPTION_POINTERS ep)
    {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_STACK_OVERFLOW && g_soEvent && !g_soTaken.exchange(true))
        {
            // nada de printf/dbghelp aqui: so 3 chamadas de kernel32
            g_soEp = ep;
            g_soTid = GetCurrentThreadId();
            SignalObjectAndWait(g_soEvent, g_soDone, 60000, FALSE);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (!Serious(code) || t_inside)
            return EXCEPTION_CONTINUE_SEARCH;
        const uintptr_t at = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        const int n = g_seenCount.load();
        for (int i = 0; i < n && i < 400; ++i)
            if (g_seen[i] == at)
                return EXCEPTION_CONTINUE_SEARCH;
        const int slot = g_seenCount.fetch_add(1);
        if (slot >= 400)
            return EXCEPTION_CONTINUE_SEARCH;
        g_seen[slot] = at;
        t_inside = true;
        char w[160];
        Where(at, w, sizeof(w));
        const CONTEXT& c = *ep->ContextRecord;
        const ULONG_PTR p0 = ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0] : 0;
        const ULONG_PTR p1 = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
        Line("EXC %08lX em %s  info %llX %llX%s", code, w, static_cast<unsigned long long>(p0), static_cast<unsigned long long>(p1),
             GetCurrentThreadId() == g_mainThread ? "  [principal]" : "");
        Line("  rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX", c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi);
        Line("  r8 %llX r9 %llX r10 %llX r11 %llX rbp %llX rsp %llX", c.R8, c.R9, c.R10, c.R11, c.Rbp, c.Rsp);
        if (code != EXCEPTION_STACK_OVERFLOW)   // sem pilha sobrando para desenrolar
            Unwind(c, "exc");
        t_inside = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ---- SAIDA ----
    typedef void (NTAPI* RtlExitUserProcess_t)(NTSTATUS);
    typedef BOOL (WINAPI* TerminateProcess_t)(HANDLE, UINT);
    RtlExitUserProcess_t g_exitOrig = nullptr;
    TerminateProcess_t g_termOrig = nullptr;
    std::atomic<bool> g_exitLogged{false};

    void LogExit(const char* via, DWORD code)
    {
        if (g_exitLogged.exchange(true))
            return;
        t_inside = true;
        Line("SAIDA via %s codigo %lu (0x%08lX) depois de %llu s%s", via, code, code, (GetTickCount64() - g_start) / 1000,
             GetCurrentThreadId() == g_mainThread ? "  [principal]" : "");
        Stack("saida");
        if (code != 0)
            Dump("Saida", nullptr, 0);
        t_inside = false;
    }

    void NTAPI ExitDetour(NTSTATUS code)
    {
        LogExit("RtlExitUserProcess", static_cast<DWORD>(code));
        g_exitOrig(code);
    }

    BOOL WINAPI TermDetour(HANDLE process, UINT code)
    {
        if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
            LogExit("TerminateProcess", code);
        return g_termOrig(process, code);
    }

    // ---- TRAVADO / VIVO ----
    HWND g_wnd = nullptr;

    BOOL CALLBACK FindWnd(HWND h, LPARAM)
    {
        DWORD pid = 0;
        // so a janela da thread que desenha (pode haver outra janela visivel do processo -- o vigia por WM_NULL
        // olhou a errada na bateria 2b: procdump viu "Hung Window" 4x e o AscBateria 0 solucos)
        const DWORD tid = GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && (!g_mainThread || tid == g_mainThread))
        {
            g_wnd = h;
            return FALSE;
        }
        return TRUE;
    }

    // Com a thread principal SUSPENSA nada que tome lock (loader, tabela de funcoes do RtlLookupFunctionEntry, heap,
    // locale do printf, dbghelp): se ela parou segurando um deles, o vigia trava junto e ela nunca e retomada --
    // foi o que congelou o jogo na bateria 2 (21:43:59). So copia contexto + pilha crua, retoma, e analisa depois.
    struct Sample
    {
        CONTEXT ctx;
        SIZE_T copied;
        ULONGLONG at;          // ms desde o ultimo quadro quando foi tirada
        uint64_t stack[4096];  // 32 KB
    };
    // 0 = TRAVADO / MainStack; 1..4 = amostras de um SOLUCO (150 ms, ~1 s, ~3 s, ~8 s)
    Sample g_samples[5];
    const ULONGLONG kSampleAt[4] = {150, 1000, 3000, 8000};

    // so captura (thread suspensa por microssegundos, sem lock nenhum)
    bool Capture(Sample& s)
    {
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainThread);
        if (!th)
            return false;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_FULL;
        bool got = false;
        SIZE_T copied = 0;
        if (SuspendThread(th) != static_cast<DWORD>(-1))
        {
            got = GetThreadContext(th, &c) != 0;
            if (got)
            {
                // ReadProcessMemory no proprio processo: sem SEH e sem lock; para no fim da pilha (pagina invalida).
                // Le por PAGINA (o 1o pedaco vai do rsp ate o fim da pagina dele): ler 4 KB a partir do rsp cruza o
                // topo da pilha quando ela esta rasa e o RPM falha inteiro -- era o "0 bytes de pilha copiados" das
                // travadas longas em ntdll (bateria 31/10 22:44-22:46, 10-16 s).
                SIZE_T off = 0;
                while (off < sizeof(s.stack))
                {
                    const uintptr_t from = c.Rsp + off;
                    SIZE_T len = 4096 - (from & 0xFFF);
                    if (len > sizeof(s.stack) - off)
                        len = sizeof(s.stack) - off;
                    SIZE_T n = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(from),
                                           reinterpret_cast<uint8_t*>(s.stack) + off, len, &n) || n != len)
                        break;
                    off += len;
                }
                copied = off;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        s.ctx = c;
        s.copied = copied;
        return got;
    }

    // ja retomada: pode logar. RIP + candidatos a retorno na pilha copiada (so exe/DLL, faixas em cache).
    void Report(const Sample& s, const char* tag, int maxFrames)
    {
        const CONTEXT& c = s.ctx;
        const SIZE_T copied = s.copied;
        char w[160];
        Where(c.Rip, w, sizeof(w));
        Line("  %s rip %s  (rsp %llX, %llu bytes de pilha copiados)", tag, w, static_cast<unsigned long long>(c.Rsp),
             static_cast<unsigned long long>(copied));
        int shown = 0;
        for (SIZE_T i = 0; i < copied / 8 && shown < maxFrames; ++i)
        {
            const uintptr_t v = static_cast<uintptr_t>(s.stack[i]);
            if ((v >= g_exe.base && v < g_exe.end) || (v >= g_dll.base && v < g_dll.end))
            {
                Where(v, w, sizeof(w));
                Line("  %s #%02d %s  (rsp+%llX, varredura)", tag, shown++, w, static_cast<unsigned long long>(i * 8));
            }
        }
    }

    void MainStack(const char* tag, int maxFrames)
    {
        if (Capture(g_samples[0]))
            Report(g_samples[0], tag, maxFrames);
    }

    void Vivo(ULONGLONG now, unsigned solucos, ULONGLONG maior, ULONGLONG somaMs)
    {
        PROCESS_MEMORY_COUNTERS_EX pm = {};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm));
        DWORD handles = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles);
        Line("VIVO %llu s  privada %llu MB  ws %llu MB  pico %llu MB  handles %lu  solucos/min %u (maior %llu ms, total %llu ms)",
             (now - g_start) / 1000, static_cast<unsigned long long>(pm.PrivateUsage >> 20),
             static_cast<unsigned long long>(pm.WorkingSetSize >> 20), static_cast<unsigned long long>(pm.PeakWorkingSetSize >> 20),
             handles, solucos, maior, somaMs);
    }

    // Batida por quadro: CSimpleTop::OnLayerRender (0x495810, varias vezes por quadro, tambem na tela de carga e
    // no glue). A janela continua respondendo mensagens durante carga/logout (bombeia por dentro), entao o WM_NULL
    // nao via a trava que o testador sente (bateria 2b, 22:06: "trava ao entrar e ao sair, 0 solucos") -- o que
    // trava e o desenho. Mede o intervalo entre quadros desenhados.
    std::atomic<ULONGLONG> g_lastFrame{0};

    void OnFrame()
    {
        g_lastFrame.store(GetTickCount64(), std::memory_order_relaxed);
        if (!g_mainThread)
        {
            g_mainThread = GetCurrentThreadId();
            // Sem isto o estouro de pilha deixa so ~a pagina de guarda para o despacho + as VEH (esta e a do
            // crash_log do exe, que faz SymInitialize/SymFromAddr): elas estouram de novo, sem guarda, e o kernel
            // mata o processo calado -- sem EXC, sem SAIDA, sem WER (rodada 31/10 22:06, criacao de personagem).
            ULONG reserve = 64 * 1024;
            if (!SetThreadStackGuarantee(&reserve))
                Line("SetThreadStackGuarantee falhou (%lu)", GetLastError());
        }
    }

    // A cada 50 ms: ultimo quadro ha >= 150 ms -> captura a pilha (segura) e acompanha ate voltar:
    //   SOLUCO <ms>   ficou entre 200 ms e 20 s sem desenhar (pilha aos ~150 ms; ate 300 detalhados por execucao)
    //   TRAVADO       passou de 20 s (nova amostra), RECUPEROU quando volta
    // Janela minimizada nao conta (o cliente para de desenhar de proposito).
    DWORD WINAPI Watchdog(void*)
    {
        ULONGLONG lastAlive = GetTickCount64();
        unsigned solucos = 0, detalhados = 0;
        ULONGLONG maior = 0, somaMs = 0;
        for (;;)
        {
            Sleep(50);
            ULONGLONG now = GetTickCount64();
            if (now - lastAlive >= 60000)
            {
                Vivo(now, solucos, maior, somaMs);
                lastAlive = now;
                solucos = 0;
                maior = somaMs = 0;
            }
            if (!g_wnd || !IsWindow(g_wnd) || (g_mainThread && GetWindowThreadProcessId(g_wnd, nullptr) != g_mainThread))
            {
                g_wnd = nullptr;
                EnumWindows(&FindWnd, 0);
            }
            const ULONGLONG last = g_lastFrame.load(std::memory_order_relaxed);
            if (!last || (g_wnd && IsIconic(g_wnd)) || now - last < 150)
                continue;
            // sem desenhar ha >= 150 ms: captura agora, acompanha ate o proximo quadro (mais amostras se demorar)
            const bool detalhar = detalhados < 300;
            int amostras = 0;
            if (detalhar && Capture(g_samples[1]))
            {
                g_samples[1].at = now - last;
                amostras = 1;
            }
            bool travado = false;
            while (g_lastFrame.load(std::memory_order_relaxed) == last)
            {
                Sleep(25);
                if (g_wnd && IsIconic(g_wnd))
                    break;
                const ULONGLONG d = GetTickCount64() - last;
                if (detalhar && amostras > 0 && amostras < 4 && d >= kSampleAt[amostras] &&
                    Capture(g_samples[1 + amostras]))
                {
                    g_samples[1 + amostras].at = d;
                    ++amostras;
                }
                if (!travado && d >= 20000)
                {
                    travado = true;
                    Line("TRAVADO: %llu s sem desenhar um quadro; pilha da thread principal (t%lu):", d / 1000, g_mainThread);
                    MainStack("travado", 40);
                }
            }
            const ULONGLONG fim = g_lastFrame.load(std::memory_order_relaxed);
            if (fim == last)
                continue;   // minimizou no meio
            const ULONGLONG d = fim - last;
            if (travado)
                Line("RECUPEROU depois de %llu s sem desenhar", d / 1000);
            if (d >= 200)
            {
                ++solucos;
                somaMs += d;
                if (d > maior)
                    maior = d;
                if (amostras > 0 && !travado)
                {
                    ++detalhados;
                    Line("SOLUCO %llu ms sem desenhar (%d amostra(s) da pilha):", d, amostras);
                    for (int i = 0; i < amostras; ++i)
                    {
                        // tag fixa "soluco": tools/bateria.py le "  soluco #NN"/"  soluco rip"
                        Line("  amostra %d aos %llu ms:", i + 1, g_samples[1 + i].at);
                        // nas longas o chamador fica fundo na pilha: mais quadros (48 nas amostras de >= 1 s: com 24 o
                        // inflate do MPQ ainda nao chegava em quem pediu o arquivo -- bateria 01/11 12:52:25)
                        Report(g_samples[1 + i], "soluco", g_samples[1 + i].at >= 1000 ? 48 : d >= 1000 ? 24 : 10);
                    }
                }
            }
        }
    }

    bool WerOn()
    {
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\WowClient64.exe", 0,
                          KEY_READ, &k) != ERROR_SUCCESS)
            return false;
        RegCloseKey(k);
        return true;
    }

    DWORD Stamp(uintptr_t base)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        return reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
    }

    void Init()
    {
        InitializeCriticalSection(&g_lock);
        g_start = GetTickCount64();
        FillMod(g_exe, GetModuleHandleA(nullptr));
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Init), &self);
        FillMod(g_dll, self);

        GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
        if (char* s = strrchr(g_dir, '\\'))
            *s = 0;
        strcat_s(g_dir, "\\Crashes");
        CreateDirectoryA(g_dir, nullptr);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char path[MAX_PATH];
        sprintf_s(path, "%s\\bateria_%lu_%04u%02u%02u-%02u%02u%02u.log", g_dir, GetCurrentProcessId(), t.wYear, t.wMonth, t.wDay,
                  t.wHour, t.wMinute, t.wSecond);
        g_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                             FILE_FLAG_WRITE_THROUGH, nullptr);
        if (g_file == INVALID_HANDLE_VALUE)
        {
            AscLog::Printf("bateria: nao abriu %s (%lu)", path, GetLastError());
            return;
        }
        Line("INICIO pid %lu  %04u-%02u-%02u  exe %s base %llX ts %08lX  dll %s base %llX ts %08lX  WER LocalDumps %s",
             GetCurrentProcessId(), t.wYear, t.wMonth, t.wDay, g_exe.name, static_cast<unsigned long long>(g_exe.base),
             Stamp(g_exe.base), g_dll.name, static_cast<unsigned long long>(g_dll.base), Stamp(g_dll.base),
             WerOn() ? "ligado" : "DESLIGADO (fastfail sem dump fora do procdump)");

        g_soEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        g_soDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (HANDLE th = g_soEvent && g_soDone ? CreateThread(nullptr, 256 * 1024, &SoHelper, nullptr, 0, nullptr) : nullptr)
            CloseHandle(th);
        else
            g_soEvent = nullptr;   // sem a thread auxiliar a VEH volta ao caminho normal
        AddVectoredExceptionHandler(1, &Veh);

        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
            Line("MinHook: %d", static_cast<int>(s));
        void* exitFn = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlExitUserProcess"));
        void* termFn = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernelbase.dll"), "TerminateProcess"));
        if (!termFn)
            termFn = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "TerminateProcess"));
        if (exitFn && MH_CreateHook(exitFn, reinterpret_cast<void*>(&ExitDetour), reinterpret_cast<void**>(&g_exitOrig)) == MH_OK)
            MH_EnableHook(exitFn);
        else
            Line("hook RtlExitUserProcess falhou");
        if (termFn && MH_CreateHook(termFn, reinterpret_cast<void*>(&TermDetour), reinterpret_cast<void**>(&g_termOrig)) == MH_OK)
            MH_EnableHook(termFn);
        else
            Line("hook TerminateProcess falhou");

        AscRuntime::OnAfter495810(&OnFrame);
        if (HANDLE th = CreateThread(nullptr, 64 * 1024, &Watchdog, nullptr, 0, nullptr))
            CloseHandle(th);
        AscLog::Printf("bateria: %s", path);
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
#endif
