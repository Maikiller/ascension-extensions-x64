// Two more of the attach init's hook groups (installers at 0x10A6271A and FUN_10a60d20), and FUN_10a60d20's
// 32 client byte patches (AscAttachLate.generated.inc).
//
//   0x4DE450  REPLACED: always 1
//   0x52ED60  0 unless the three client pointers 0xBD19AC / 0xBD19B0 / 0xBD19B4 are all set
//   0x5DEA60  the original, then its result replaced by 0x5DEC10's
//   0x6DAC10  called from 0x587450 while a gossip (0xBEB030), merchant (0xBFA3E8) or 0xC016F0 GUID is
//             set: 1 without asking the client
//   0x726160  REPLACED: whether the player knows a higher rank along SkillLineAbility's supercededBy
//             chain (0x71A670 / 0x7260E0), refusing a spell that supersedes itself with a fatal error
//   0x54A370  REPLACED: rested flag (0xBEA549) and three experience values scaled by the realm's rates
//             (realm object +0x10 / +0x14), then 0
//   0x5E4B70  the argument scaled by the realm's first rate, as the only result
//   0x86B5A0  REPLACED by the DLL's empty stub (0x1008D890): the client function does nothing
#include <Ascension/AscLog.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscCrashContext.hpp>
#include <Ascension/AscLua.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/RealmInfo.hpp>
#include <intrin.h>
#include <Windows.h>
#include <cstring>
#include <cstdio>

using namespace AscLua;

namespace
{
    double ToNumber(lua_State* L, int i) { return reinterpret_cast<double(__cdecl*)(lua_State*, int)>(ASC_PTR(0x84E030))(L, i); }
    void SetTop(lua_State* L, int i) { reinterpret_cast<void(__cdecl*)(lua_State*, int)>(ASC_PTR(0x84DBF0))(L, i); }
    void PushNumber(lua_State* L, double v) { reinterpret_cast<void(__cdecl*)(lua_State*, double)>(ASC_PTR(0x84E2A0))(L, v); }
    void PushInteger(lua_State* L, int32_t v) { reinterpret_cast<void(__cdecl*)(lua_State*, ptrdiff_t)>(ASC_PTR(0x84E2D0))(L, v); }
    void PushBoolean(lua_State* L, int v) { reinterpret_cast<void(__cdecl*)(lua_State*, int)>(ASC_PTR(0x84E4D0))(L, v); }
    // FUN_10ae6220 (float -> 64-bit, low half) read back as unsigned.
    double AsUnsigned(float f) { return static_cast<double>(static_cast<uint32_t>(static_cast<int64_t>(f))); }
    uint64_t Guid(uint32_t at) { return *reinterpret_cast<const uint64_t*>(ASC_PTR(at)); }

    typedef int(__cdecl* Arg_t)(uint32_t);
    typedef int(__fastcall* This_t)(void* ASC_EDX_P(void*));
    typedef int(__cdecl* Lua_t)(lua_State*);
    Arg_t g_5DEA60 = nullptr;
    This_t g_6DAC10 = nullptr;
    Lua_t g_5E4B70 = nullptr;

    int __cdecl Replace4DE450(lua_State* L)   // 0x10A77130
    {
        PushNumber(L, 1.0);
        return 1;
    }

    // 0x52ED60 begins push ebp / mov ebp,esp / call 0x52EBA0: the call is re-made, then 0x52ED68.
#if !ASC_X64
    __declspec(naked) int __cdecl Original52ED60(uint32_t)
    {
        __asm
        {
            push ebp
            mov ebp, esp
            mov eax, 0x52EBA0
            call eax
            push 0x52ED68
            ret
        }
    }
#else
    // x64: trampolim do MinHook no lugar do prologo refeito a mao. int Script_GetBarberShopTotalCost(lua_State*)
    int (*g_52ED60)(uintptr_t) = nullptr;
    int Original52ED60(uintptr_t L) { return g_52ED60(L); }
#endif
    int __cdecl Hook52ED60(uintptr_t a)   // 0x10A6EB60
    {
        if (!*reinterpret_cast<const uint32_t*>(ASC_PTR(0xBD19AC)) || !*reinterpret_cast<const uint32_t*>(ASC_PTR(0xBD19B0)) ||
            !*reinterpret_cast<const uint32_t*>(ASC_PTR(0xBD19B4)))
            return 0;
        return Original52ED60(a);
    }

    int __cdecl Hook5DEA60(uint32_t a)   // 0x10A44AC0
    {
        g_5DEA60(a);
        return reinterpret_cast<int(__cdecl*)(uint32_t)>(ASC_PTR(0x5DEC10))(a);
    }

    int __fastcall Hook6DAC10(void* self ASC_EDX_P(void* edx))   // 0x10A44960
    {
        // Retorno do call virtual IsDeadOrGhost em CGTradeInfo::HandleTradeMessage (extra-addr no x64).
        if (_ReturnAddress() == ASC_PTR(0x587455) &&
            (Guid(0xBEB030) != 0 || Guid(0xBFA3E8) != 0 || Guid(0xC016F0) != 0))
            return 1;
        return g_6DAC10(self ASC_EDX_A(edx));
    }

    char __fastcall Replace726160(void* player ASC_EDX_P(void*), uint32_t spell)   // 0x10A45AA0
    {
        typedef const uint8_t*(__thiscall* Ability_t)(void*, uint32_t);
        typedef char(__thiscall* Knows_t)(void*, uint32_t);
        const Ability_t Ability = reinterpret_cast<Ability_t>(ASC_PTR(0x71A670));
        const Knows_t Knows = reinterpret_cast<Knows_t>(ASC_PTR(0x7260E0));
        const uint8_t* row = Ability(player, spell);
        if (!row)
            return 0;
        uint32_t next = *reinterpret_cast<const uint32_t*>(row + 0x20);
        if (next == spell)
        {
            char text[160];
            snprintf(text, sizeof(text),
                     "Spell: %u has recursive supercededBySpell in SkillLineAbility.dbc (dbc_skill_line_ability)", spell);
            AscCrashContext::Assert(text, nullptr, 0);
            return 0;
        }
        while (next)
        {
            if (Knows(player, next))
                return 1;
            row = Ability(player, next);
            if (!row)
                return 0;
            next = *reinterpret_cast<const uint32_t*>(row + 0x20);
        }
        return 0;
    }

    int __cdecl Replace54A370(lua_State* L)   // 0x10A6F720
    {
        const RealmInfo& realm = RealmInfoSvc::Get();
        PushBoolean(L, *reinterpret_cast<const uint8_t*>(ASC_PTR(0xBEA549)) != 0);
        PushNumber(L, AsUnsigned(static_cast<float>(*reinterpret_cast<const int32_t*>(ASC_PTR(0xBEA55C))) * realm.rate1));
        PushNumber(L, AsUnsigned(static_cast<float>(*reinterpret_cast<const int32_t*>(ASC_PTR(0xBEA560))) * realm.rate2));
        PushNumber(L, AsUnsigned(static_cast<float>(*reinterpret_cast<const int32_t*>(ASC_PTR(0xBEA558))) * realm.rate1));
        PushNumber(L, 0.0);
        return 5;
    }

    int __cdecl Hook5E4B70(lua_State* L)   // 0x10A6F6D0
    {
        g_5E4B70(L);
        const float value = static_cast<float>(ToNumber(L, 1));
        SetTop(L, 0);
        PushInteger(L, static_cast<int32_t>(value * RealmInfoSvc::Get().rate1));
        return 1;
    }

#if ASC_X64
    void Nothing(const void*) {}   // 0x1008D890 -- x64: OsValidateFunctionPointer(const void*)
#else
    void __cdecl Nothing() {}   // 0x1008D890
#endif

    // FUN_10a60d20's 32 client byte patches (NOPs, jcc -> jmp, operand tweaks), recovered by emulating it after the
    // DLL's static constructors (tools/emulate_installer.py). Missing until 2026-09-27 (the live audit's step 4).
    struct BytePatch
    {
        uint32_t address;
        uint32_t size;
        uint8_t bytes[24];
    };
    const BytePatch kBytePatches[] = {
#include <Ascension/AscAttachLate.generated.inc>
    };

#if ASC_X64
    // x64: destino de cada byte-patch (patch de cliente "attach-late"):
    //   kX64Var      int32 do cliente com STATIC no endereco x86; grava 1 (= patch ligado).
    //   kX64Covered  a mesma variavel de um endereco de kX64Var (nada a gravar).
    //   kX64InSource o fonte do cliente ja faz o mesmo que o patch (WoWFix335); nada a gravar.
    // Fora das listas: so loga. Nao vai por WriteMem: se o endereco ganhar STATIC sem entrar aqui, os bytes x86
    // (ate 22) cairiam por cima de uma variavel de 4.
    const uint32_t kX64Var[] = {
        0x58CBEE,   // g_coaQuestItemCountAnyFlags: GetItemTypeCount(item, 8 -> 0) nas quests
        0x727431,   // g_coaClickToMoveNoFollowEnd: ClickToMove sem AUTOFOLLOW_END
        // patch de cliente 24-attach-late (mestre):
        0x7561BF,   // g_coaNoVictimFeedback: ClearAttackRound sem feedback de vitima
        0x79ABB3,   // g_coaIgnoreCamTarget: CWorldScene::Render ignora camTargetEntity
        0x725915,   // g_coaNamePlateBottom: placa (overlap) ancorada por BOTTOM
        0x615F8C,   // g_coaSmartRectBottom: SmartScreenRectPositionFrame center ? 14 : 7
        0x71F89E,   // g_coaCanAssistAny: CanAssist(corpse) >= HATED
        0x528D1E,   // g_coaKeepReputationOnLeave: LeaveWorld sem CGReputationInfo::LeaveWorld
        0x7FFB4C,   // g_coaEnchantNoLevelCheck: CheckEnchantItemLevel sem erros 48/36
        0x6D7856,   // g_coaMountNoCollisionCheck: UpdateMountedCollisionBox nao recusa
        0x737AE0,   // g_coaSheatheAlways: SheatheStateHandler sempre aplica
        0x5709E8,   // g_coaMailNoStationery: SendMail sem papel de carta
        0x409850,   // g_coaLoadingIgnoreTransport: IsStillLoading nao espera o transporte
        0x73D394,   // g_coaNoJumpLandRun: PlayFallLandAnimation sem JUMP_LAND_RUN
        0x767427,   // g_coaKeepCVarsOnDestroy: CVar::Destroy nao limpa s_registeredCVars
        0x50E260,   // g_coaNoYouChanged: OnChannelNotify sem "YOU_CHANGED"
        0x520658,   // g_coaZoneChatIgnoreFlag: NewZoneFeedback sem o teste 0x100000
        0x7FC3E2,   // g_coaShardNoGroundHit: BlizzardObject::Update ignora o Intersect
        0x72E59D,   // g_coaCastKeepLoot: ClearCastingSpell nao fecha o loot
        0x621EC1,   // g_coaQuestTooltipAlways: showQuestTrackingTooltips sem olhar o valor
        0x5247D3,   // g_coaRaidDifficultyFromUI: RAID_DIFFICULTY de CGGameUI::m_raidDifficulty
        0x62D779,   // g_coaSellPriceNoCharges: preco de venda sem ajuste por cargas
    };
    const uint32_t kX64Covered[] = {0x5E174C, 0x5E1770, 0x5E1AD8, 0x5E1071,   // = 0x58CBEE
                                    0x615F91,                                   // = 0x615F8C
                                    0x7FFB36,                                   // = 0x7FFB4C
                                    0x626ABF};                                  // = 0x621EC1
    const uint32_t kX64InSource[] = {
        0x401A94,   // DesktopGammaCallback (windowed-mode-gamma-fix)
        0x73E3C9,   // CGUnit_C::PreAnimate (npc-turn-attack-anim)
        0x73ECD6,   // CGUnit_C::OnChannelSpellChanged (missing-pre-cast-animation)
    };
    template <size_t N> bool In(const uint32_t (&list)[N], uint32_t a)
    {
        for (uint32_t x : list)
            if (x == a)
                return true;
        return false;
    }
#endif

    void WriteBytes(uint32_t address, const uint8_t* bytes, uint32_t size)
    {
#if ASC_X64
        if (In(kX64InSource, address) || In(kX64Covered, address))
            return;
        if (In(kX64Var, address))
        {
            const int32_t on = 1;
            AscX64::WriteMem(address, &on, sizeof(on), __FUNCTION__);
            return;
        }
        (void)bytes;
        AscLog::Printf("x64: AscAttachLate byte-patch 0x%06X (%u bytes) sem versao x64 -- ignorado", address, size);
        return;
#endif
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old))
            return;
        memcpy(reinterpret_cast<void*>(address), bytes, size);
        VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    }

    void Init()
    {
        for (const BytePatch& p : kBytePatches)
            WriteBytes(p.address, p.bytes, p.size);
        AscRuntime::ReplaceFunction(0x4DE450, reinterpret_cast<void*>(&Replace4DE450));
#if ASC_X64
        g_52ED60 = reinterpret_cast<int (*)(uintptr_t)>(AscRuntime::Detour(0x52ED60, 0, reinterpret_cast<void*>(&Hook52ED60)));
#else
        AscRuntime::ReplaceFunction(0x52ED60, reinterpret_cast<void*>(&Hook52ED60));
#endif
        g_5DEA60 = reinterpret_cast<Arg_t>(AscRuntime::Detour(0x5DEA60, 10, reinterpret_cast<void*>(&Hook5DEA60)));
        g_6DAC10 = reinterpret_cast<This_t>(AscRuntime::Detour(0x6DAC10, 6, reinterpret_cast<void*>(&Hook6DAC10)));
        AscRuntime::ReplaceFunction(0x726160, reinterpret_cast<void*>(&Replace726160));
        AscRuntime::ReplaceFunction(0x54A370, reinterpret_cast<void*>(&Replace54A370));
        g_5E4B70 = reinterpret_cast<Lua_t>(AscRuntime::Detour(0x5E4B70, 6, reinterpret_cast<void*>(&Hook5E4B70)));
        AscRuntime::ReplaceFunction(0x86B5A0, reinterpret_cast<void*>(&Nothing));
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
