#include <Client/CDataStore.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/AscX64.hpp>
#include <Client/CNetClient.hpp>
#include <Misc/DataContainer.hpp>
#include <Misc/Util.hpp>
#include <Ascension/AscProfiler.hpp>
#include <Ascension/AscRuntime.hpp>

#include <unordered_set>

// The packet dispatch of the original's installer FUN_102c3540, transcribed (2026-09-27; until then the upstream
// scaffold patched the call sites 0x6324CA / 0x714AFC / 0x716A79 / 0x6B0B9E with a 0x51F split instead):
//   0x631FA0 SetMessageHandler  -> FUN_102c4400: opcode > 0x55E into the DLL's maps, else the client's table
//   0x631FE0 ProcessMessage     -> FUN_102c3ab0: opcode < 0x55F to the client, else the DLL's map
//   0x63200D                       ProcessMessage's bound `cmp esi, 0x51F` -> 0x9D4
//   0x632A40 NetClient ctor     -> FUN_102c3a90: keeps the object (0x10BE3564) for the DLL registrar FUN_102c4590
// The DLL's own handlers (sDC's map) are all above 0x55E, as the original's are.
// (0x631FC0, the clear, is AscProfiler's.)

namespace
{
    void* g_netClient = nullptr;   // DAT_10be3564

    // DAT_10be34d8: the "Skipped packet" set FUN_102c3ab0 consults first. Only its static initialiser
    // (FUN_10078380) touches it -- it is always empty -- kept for the exact branch.
    std::unordered_set<uint32_t> g_skipped;

    typedef void(__fastcall* SetHandler_t)(void* ASC_EDX_P(void*), uint32_t, void*, void*);
    SetHandler_t g_setHandler = nullptr;
    typedef int(__fastcall* Process_t)(void* ASC_EDX_P(void*), uint32_t, CDataStore*, uint32_t);
    Process_t g_process = nullptr;
    typedef void*(__fastcall* Ctor_t)(void* ASC_EDX_P(void*));
    Ctor_t g_ctor = nullptr;

    void __fastcall SetMessageHandlerDetour(void* net ASC_EDX_P(void* edx), uint32_t opcode, void* handler, void* param)   // FUN_102c4400
    {
        if (static_cast<int32_t>(opcode) > 0x55E)
        {
            sDC.GetPacketHandlerMap().insert_or_assign(opcode, CNetClientCustomPacket(handler, param));
            return;
        }
        g_setHandler(net ASC_EDX_A(edx), opcode, handler, param);
    }

    int __fastcall ProcessMessageDetour(void* net ASC_EDX_P(void* edx), uint32_t a2, CDataStore* store, uint32_t a4)   // FUN_102c3ab0
    {
        const int16_t op16 = *reinterpret_cast<const int16_t*>(store->m_buffer + store->m_read);
        store->m_read += 2;
        const int32_t opcode = op16;
        const int64_t start = AscProfiler::Received(opcode, static_cast<uint32_t>(store->m_size));
        if (g_skipped.count(static_cast<uint32_t>(opcode)))
            return 1;   // "Skipped packet: %s (%u)"
        if (opcode < 0x55F)
        {
            store->m_read = 0;
            const int r = g_process(net ASC_EDX_A(edx), a2, store, a4);
            AscProfiler::Handled(opcode, start);
            return r;
        }
        auto& map = sDC.GetPacketHandlerMap();
        const auto it = map.find(static_cast<uint32_t>(opcode));
        if (it == map.end())
        {
            AscProfiler::Unhandled(opcode);   // "Unhandled packet: %s (%u)", and nothing read
            return 1;
        }
        const int r = reinterpret_cast<int(__cdecl*)(void*, uint32_t, uint32_t, CDataStore*)>(it->second.m_handler)(
            it->second.m_param, static_cast<uint32_t>(opcode), a2, store);
        AscProfiler::Handled(opcode, start);
        return r;
    }

    void* __fastcall NetClientCtorDetour(void* self ASC_EDX_P(void* edx))   // FUN_102c3a90
    {
        g_netClient = g_ctor(self ASC_EDX_A(edx));
        return g_netClient;
    }
}

#if ASC_X64
// x64 (WowClient64): mesmas regras do FUN_102c4400 / FUN_102c3ab0 / FUN_102c3a90, nas assinaturas x64 reais
// (this em rcx, sem o edx falso do __thiscall). O teto 0x51F do ProcessMessage do exe nao precisa de patch:
// todo opcode > 0x55E e despachado aqui antes de chegar ao original.
namespace
{
    typedef void (*SetHandler64_t)(void*, uint32_t, void*, void*);
    typedef int (*Process64_t)(void*, uint32_t, CDataStore*, int);
    typedef void* (*Ctor64_t)(void*);
    SetHandler64_t g_setHandler64 = nullptr;
    Process64_t g_process64 = nullptr;
    Ctor64_t g_ctor64 = nullptr;

    void SetMessageHandler64(void* net, uint32_t opcode, void* handler, void* param)
    {
        if (static_cast<int32_t>(opcode) > 0x55E)
        {
            sDC.GetPacketHandlerMap().insert_or_assign(opcode, CNetClientCustomPacket(handler, param));
            return;
        }
        g_setHandler64(net, opcode, handler, param);
    }

    int ProcessMessage64(void* net, uint32_t time, CDataStore* store, int a4)
    {
        const int16_t op16 = *reinterpret_cast<const int16_t*>(store->m_buffer + store->m_read);
        const int32_t opcode = op16;
        if (opcode < 0x55F)
        {
            // FUN_102c3ab0 carimba TODO pacote recebido (AscProfiler::Received -> g_lastReceive do vigia de 30 s).
            // Antes so os custom (>= 0x55F) carimbavam: parado num lugar sem pacote custom por 30 s, o vigia dava
            // "Packet watchdog timed out" e derrubava a conexao (DC 31/10 23:42, servidor normal).
            const int64_t start = AscProfiler::Received(opcode, static_cast<uint32_t>(store->m_size));
            const int r = g_process64(net, time, store, a4);   // o original le o opcode ele mesmo
            AscProfiler::Handled(opcode, start);
            return r;
        }
        store->m_read += 2;
        const int64_t start = AscProfiler::Received(opcode, static_cast<uint32_t>(store->m_size));
        auto& map = sDC.GetPacketHandlerMap();
        const auto it = map.find(static_cast<uint32_t>(opcode));
        static std::unordered_set<int32_t> seen;
        const bool first = seen.insert(opcode).second;
        if (it == map.end())
        {
            if (first)
                AscLog::Printf("x64 pacote custom 0x%X (%d bytes): SEM handler (modulo nao portado)", opcode, store->m_size);
            AscProfiler::Unhandled(opcode);
            return 1;
        }
        if (first)
            AscLog::Printf("x64 pacote custom 0x%X (%d bytes): tratado", opcode, store->m_size);
        const int r = reinterpret_cast<int (*)(void*, uint32_t, uint32_t, CDataStore*)>(it->second.m_handler)(
            it->second.m_param, static_cast<uint32_t>(opcode), time, store);
        AscProfiler::Handled(opcode, start);
        return r;
    }

    void* NetClientCtor64(void* self)
    {
        g_netClient = g_ctor64(self);
        return g_netClient;
    }
}
#endif

void CNetClient::ApplyPatches()
{
#if ASC_X64
    g_setHandler64 = reinterpret_cast<SetHandler64_t>(AscRuntime::Detour(0x631FA0, 6, reinterpret_cast<void*>(&SetMessageHandler64)));
    g_process64 = reinterpret_cast<Process64_t>(AscRuntime::Detour(0x631FE0, 10, reinterpret_cast<void*>(&ProcessMessage64)));
    g_ctor64 = reinterpret_cast<Ctor64_t>(AscRuntime::Detour(0x632A40, 5, reinterpret_cast<void*>(&NetClientCtor64)));
    return;
#endif
    g_setHandler = reinterpret_cast<SetHandler_t>(AscRuntime::Detour(0x631FA0, 6, reinterpret_cast<void*>(&SetMessageHandlerDetour)));
    g_process = reinterpret_cast<Process_t>(AscRuntime::Detour(0x631FE0, 10, reinterpret_cast<void*>(&ProcessMessageDetour)));
    const uint8_t bound[2] = {0xD4, 0x09};   // HOOKED 0x63200D: cmp esi, 0x51F -> 0x9D4
    Util::OverwriteBytesAtAddress(0x63200D, const_cast<uint8_t*>(bound), sizeof(bound));
    g_ctor = reinterpret_cast<Ctor_t>(AscRuntime::Detour(0x632A40, 5, reinterpret_cast<void*>(&NetClientCtorDetour)));
}

// The DLL registrar FUN_102c4590: FUN_102c4400 with the kept NetClient.
void CNetClient::RegisterHandler(uint32_t opcode, void* handler, void* param)
{
#if ASC_X64
    SetMessageHandler64(g_netClient, opcode, handler, param);
#else
    SetMessageHandlerDetour(g_netClient, nullptr, opcode, handler, param);
#endif
}

void CNetClient::Packet_MSG_SET_ACTION_BUTTON(uint32_t slotID, bool p1, bool p2)
{
    reinterpret_cast<void (__cdecl*)(uint32_t, bool, bool)>(ASC_PTR(0x5AA390))(slotID, p1, p2);
}
