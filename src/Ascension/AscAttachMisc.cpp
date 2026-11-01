// The attach init's second hook installer (FUN_10a66950), seven hooks:
//
//   0x81AC90  FrameScript_SignalEvent(id, format, va): an id past the client's event table or with no
//             entry is refused and logged to the Fatal channel (FUN_10a79130)
//   0x7668C0  CVar::Set: scriptProfile can only be set to "0"; setting nameplateShowEnemies posts the
//             NAMEPLATES_MESSAGE_ALL_OFF / _ALL_ON_AUTO text as stock event 0xBC (FUN_10a4a780)
//   0x403DE0  before it: an empty CMSG 0x53B, the auto-quest pop-ups and the chat checks emptied
//             (FUN_10a4f080)
//   0x5B3020 / 0x5B32F0 / 0x5B3610  after these packet handlers: the challenge data stores are
//             rebuilt (FUN_10a4f020 / 0x10a4f050 / 0x10a4eff0); the hook returns 1
//   0x808200  a cast that fails with 0x55 for a spell with Attributes 0x404, AttributesEx 0x200 or
//             AttributesEx2 0x100000 stops the player's melee swing unless Auto Shot is on (FUN_10a771c0)
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscLogger.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscScript.hpp>
#include <Client/CDataStore.hpp>
#include <cstdio>
#include <cstring>
#include <string>

void AscAutoQuest_Clear();
void AscChatCheck_Clear();
void AscChallenge_ScheduleRebuild();

namespace
{
    typedef int(__cdecl* Signal_t)(int, const char*, void*);
    typedef int(__fastcall* CVarSet_t)(uint8_t* ASC_EDX_P(void*), const char*, uint32_t, uint32_t, uint32_t, uint32_t);
    // handlers de pacote (void* param, NETMESSAGE, unsigned time, CDataStore*) e Spell_C_SpellFailed(PendingSpellCast*,
    // const SpellRec*, int, int, int, bool): os ponteiros em uintptr_t (x86 = mesmo tipo) -- no x64 o original
    // recebia o CDataStore* / PendingSpellCast* truncado.
    typedef int(__cdecl* Fn4_t)(uintptr_t, uint32_t, uint32_t, uintptr_t);
    typedef int(__cdecl* Fn6_t)(uintptr_t, const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t);
    Signal_t g_81AC90 = nullptr;
    CVarSet_t g_7668C0 = nullptr;
    Fn4_t g_403DE0 = nullptr, g_5B3020 = nullptr, g_5B32F0 = nullptr, g_5B3610 = nullptr;
    Fn6_t g_808200 = nullptr;

    int __cdecl Hook81AC90(int id, const char* format, void* va)
    {
        const uint8_t* table = reinterpret_cast<const uint8_t*>(ASC_PTR(0xD3F7D0));
        const uint32_t count = *reinterpret_cast<const uint32_t*>(table + 4);
        const uint8_t* const* entries = *reinterpret_cast<const uint8_t* const* const*>(table + 8);
        if (entries && id >= 0 && static_cast<uint32_t>(id) < count && entries[id])
            return g_81AC90(id, format, va);
        char line[512];
        snprintf(line, sizeof(line),
                 "Blocked invalid FrameScript signal event at 0x0081AC90: eventId=%d, registeredEventCount=%u, "
                 "format=%s, vaList=0x%X, hasEventData=%s, hasEventObject=false",
                 id, count, format ? format : "", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(va)), entries ? "true" : "false");
        AscLogger::Write(5, line);
        return 0;
    }

    int __fastcall Hook7668C0(uint8_t* cvar ASC_EDX_P(void* edx), const char* value, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
    {
#if ASC_X64
        // CVar : TSHashObject<CVar, HASHKEY_STRI> -- m_key.m_str em +0x28 no x64 (PDB), +0x14 no x86
        const char* name = *reinterpret_cast<const char* const*>(cvar + 0x28);
#else
        const char* name = AscClientDbc::Str(cvar, 0x14);
#endif
        const char* v = strcmp(name, "scriptProfile") != 0 ? value : "0";
        if (strcmp(name, "nameplateShowEnemies") == 0)
        {
            const char* key = strcmp(v, "0") == 0 ? "NAMEPLATES_MESSAGE_ALL_OFF" : "NAMEPLATES_MESSAGE_ALL_ON_AUTO";
            if (const char* text = reinterpret_cast<const char*(__cdecl*)(const char*, int, int)>(ASC_PTR(0x819D40))(key, -1, 0))
                reinterpret_cast<int(__cdecl*)(int, const char*, ...)>(ASC_PTR(0x81B530))(0xBC, "%s", text);
        }
        return g_7668C0(cvar ASC_EDX_A(edx), v, b, c, d, e);
    }

    int __cdecl Hook403DE0(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)
    {
        AscScript::Packet(0x53B).Send();   // FUN_100e08b0
        AscAutoQuest_Clear();
        AscChatCheck_Clear();
        return g_403DE0(a, b, c, d);
    }

    int __cdecl Hook5B3020(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)
    {
        g_5B3020(a, b, c, d);
        AscChallenge_ScheduleRebuild();
        return 1;
    }
    int __cdecl Hook5B32F0(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)
    {
        g_5B32F0(a, b, c, d);
        AscChallenge_ScheduleRebuild();
        return 1;
    }
    int __cdecl Hook5B3610(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)
    {
        g_5B3610(a, b, c, d);
        AscChallenge_ScheduleRebuild();
        return 1;
    }

    int __cdecl Hook808200(uintptr_t a, const uint8_t* rec, uint32_t result, uint32_t d, uint32_t e, uint32_t f)
    {
        const uint32_t attr = *reinterpret_cast<const uint32_t*>(rec + 0x10);
        const uint32_t attrEx = *reinterpret_cast<const uint32_t*>(rec + 0x14);
        const uint32_t attrEx2 = *reinterpret_cast<const uint32_t*>(rec + 0x18);
        if (((attr & 0x404) || ((attrEx >> 9) & 1) || ((attrEx2 >> 0x14) & 1)) && result == 0x55)
            if (uint8_t* player = AscScript::ActivePlayer())
                if (!reinterpret_cast<char(__thiscall*)(uint8_t*)>(ASC_PTR(0x71AF90))(player))
                    {
#if ASC_X64
                    reinterpret_cast<int (*)(uint8_t*, uint64_t, int)>(ASC_PTR(0x72C2B0))(player, 0, 0);   // OnAttackIconPressed(WOWGUID, int)
#else
                    reinterpret_cast<void(__thiscall*)(uint8_t*, int, int, int)>(ASC_PTR(0x72C2B0))(player, 0, 0, 0);
#endif
                }
        return g_808200(a, rec, result, d, e, f);
    }

    template <class T> T Hook(uint32_t target, uint32_t prologue, void* fn)
    {
        return reinterpret_cast<T>(AscRuntime::Detour(target, prologue, fn));
    }

    void Init()
    {
        g_403DE0 = Hook<Fn4_t>(0x403DE0, 6, reinterpret_cast<void*>(&Hook403DE0));        // hooked 0x403DE0
        g_5B3020 = Hook<Fn4_t>(0x5B3020, 6, reinterpret_cast<void*>(&Hook5B3020));        // hooked 0x5B3020
        g_5B32F0 = Hook<Fn4_t>(0x5B32F0, 6, reinterpret_cast<void*>(&Hook5B32F0));        // hooked 0x5B32F0
        g_5B3610 = Hook<Fn4_t>(0x5B3610, 7, reinterpret_cast<void*>(&Hook5B3610));        // hooked 0x5B3610
        g_7668C0 = Hook<CVarSet_t>(0x7668C0, 7, reinterpret_cast<void*>(&Hook7668C0));    // hooked 0x7668C0
        g_808200 = Hook<Fn6_t>(0x808200, 9, reinterpret_cast<void*>(&Hook808200));        // hooked 0x808200
        g_81AC90 = Hook<Signal_t>(0x81AC90, 6, reinterpret_cast<void*>(&Hook81AC90));     // hooked 0x81AC90
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
