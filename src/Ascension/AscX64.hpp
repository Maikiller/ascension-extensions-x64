#pragma once
// CoA x64: o DLL roda dentro do WowClient64.exe (cliente recompilado do fonte), nao do Ascension.exe 12340.
// Cada endereco 12340 do codigo vira um ponteiro do exe x64 via WowAddr() (tabela WowClient64.addr gerada
// do PDB). Hooks usam MinHook. Modulos so sao instalados quando portados (AscX64::ModulePorted).
#include <cstddef>
#include <cstdint>

#if defined(_WIN64)
#define ASC_X64 1
#else
#define ASC_X64 0
#endif

namespace AscX64
{
    // GUID que o codigo x86 carrega em duas metades (lo, hi) -> o WOWGUID de 64 bits do x64.
    inline uint64_t Guid(uint32_t lo, uint32_t hi) { return (static_cast<uint64_t>(hi) << 32) | lo; }
    // Endereco 12340 -> ponteiro no exe atual (x86: o proprio endereco). nullptr se nao ha equivalente.
#if ASC_X64
    void* Ptr(uint32_t addr12340);
#else
    inline void* Ptr(uint32_t addr12340) { return reinterpret_cast<void*>(static_cast<uintptr_t>(addr12340)); }
#endif
    template <class T> inline T Fn(uint32_t addr12340) { return reinterpret_cast<T>(Ptr(addr12340)); }

    // Hook x64 (MinHook). Devolve o trampolim para chamar o original, ou nullptr (e loga) se nao instalou.
    void* Hook(uint32_t addr12340, void* detour, const char* what);
    bool Replace(uint32_t addr12340, void* fn, const char* what);
    void EnableAll();

    // Stub de rotina x86 (naked/asm) ainda nao reescrita: loga e derruba (nunca deve ser alcancado).
    [[noreturn]] void Unported(const char* what);

    // Escrita num endereco 12340 (dado ou patch). x64: resolve pela tabela e RECUSA (loga) se cair em secao
    // executavel -- bytes de patch do x86 nao valem no codigo x64; esses patches precisam de versao propria.
    bool WriteMem(uint32_t addr12340, const void* bytes, size_t n, const char* who);

#if ASC_X64
    // Patch de codigo no exe x64 (out/midpatch-x64.md): resolve addr12340 pela tabela (as linhas "fn +off" do
    // extra-addr), confere que os n bytes atuais sao `expect` (ou ja sao `bytes`), e so entao escreve com
    // VirtualProtect + FlushInstructionCache. Se nao bater, loga o que achou e nao escreve. `delta` desloca o
    // destino a partir do endereco x64 resolvido (sites que o x64 duplicou, ex. as 2 copias do 0x632163).
    bool WriteCodeX64(uint32_t addr12340, const void* bytes, size_t n, const void* expect, ptrdiff_t delta = 0);
    // Rebase dos bytes conferidos: copia para `ref` os rel32 (E8/E9/0F 8x) e disp32 RIP-relative de `live` cuja
    // instrucao tem o mesmo formato na mesma posicao (diferenca <= 1 MB). Devolve quantos campos trocou.
    // WriteCodeX64 ja faz isso nos dois estados; CodeMatches e a comparacao equivalente para quem le direto.
    size_t RebaseRel(uint8_t* ref, const uint8_t* live, size_t n);
    bool CodeMatches(const void* live, const void* ref, size_t n);
#endif

    // Filtros por endereco de retorno. x86: compara com os enderecos 12340 exatos. x64: o RVA de retorno muda
    // a cada build do cliente, entao compara a FUNCAO que contem o retorno (inicio pela tabela de unwind .pdata)
    // com a funcao 12340 `fn12340` -- equivalente quando todas as chamadas daquela funcao estao na lista x86.
    bool ReturnsInto(const void* ret, uint32_t fn12340);
    // true se `ret` esta fora do exe do cliente (x86: acima de 0xDFCBFF, o fim da imagem 12340).
    bool OutsideClient(const void* ret);

    // Lista branca dos modulos ja portados para x64 (nome do modulo em AscBindings).
    bool ModulePorted(const char* name);
}

#define ASC_PTR(a) (::AscX64::Ptr(a))

// __fastcall(this, edx, ...) emula __thiscall no x86; no x64 o 1o argumento real vem logo apos o this (rdx).
#if ASC_X64
#define ASC_EDX_P(decl)
#define ASC_EDX_A(name)
#else
#define ASC_EDX_P(decl) , decl
#define ASC_EDX_A(name) , name
#endif

// Offset de campo de objeto/struct do cliente: x86 (a) ou x64 (b), conferido em tools/layout.py / pdbfields.py.
#if ASC_X64
#define ASC_OFF(a, b) (b)
#else
#define ASC_OFF(a, b) (a)
#endif

// GUID que o x86 recebe em duas metades (lo, hi) e o x64 como um WOWGUID (um registrador). Para hooks que so repassam.
#if ASC_X64
#define ASC_GUID2_T uint64_t
#define ASC_GUID2_P(lo, hi) uint64_t lo
#define ASC_GUID2_A(lo, hi) lo
#else
#define ASC_GUID2_T uint32_t, uint32_t
#define ASC_GUID2_P(lo, hi) uint32_t lo, uint32_t hi
#define ASC_GUID2_A(lo, hi) lo, hi
#endif

// DBCache<T,...>::GetRecord(id, const WOWGUID& requester, callback, param, bool): no x64 o 2o argumento e
// referencia -- o GetRecord copia o guid, e nullptr derrubava (crash do teste 3, GetCreatureTemplate). No x86 o
// original passa 0 nas duas metades. Uso: GetRecord(cache, id, ASC_NOGUID, nullptr, nullptr, 0).
#if ASC_X64
namespace AscX64 { static const uint64_t kNoGuid = 0; }
#define ASC_NOGUID (const_cast<void*>(static_cast<const void*>(&::AscX64::kNoGuid)))
#else
#define ASC_NOGUID nullptr
#endif
