// The AddOn-taint module (installer FUN_10312150): around protected client functions, an addon the
// server lists as secure (SMSG 0x94E, AscGlobalsJ) runs as if it were Blizzard code.
//
// "Secure" is (the running addon 0xD4139C is set and listed secure) or SecureAddonMgr +0x10, which
// nothing writes (always 0). The client's taint source 0xD4139C is written through FUN_10313b70
// (NtProtectVirtualMemory around the store).
//
//   gate     0x5191C0, 0x488540  secure -> allowed (1) without asking the client
//   push     0x816DA0            secure -> pushes 1 and returns 1 (a Lua function)
//   cleared  0x49F210 0x510430 0x510450 0x510A00 0x51A7A0 0x51AC90 0x51FB80 0x51FF20 0x5252D0
//            0x527F00 0x52B470 0x52B4E0: secure -> taint cleared for the call; restored after either way
//   0x525FC0 inside 0x510B30 (AscGlobalsJ's flag) -> 0x513530(0, 2) and 0 instead of the client
//   0x5F7E90 addon load: realms without addons (realm +0x48) refuse addons the server does not list;
//            called from 0x5F8071 / 0x5F80FA with loadUnknownAddOns 0, the same
// Target changes run with the taint cleared the same way (lists 0x10BE2950 / 0x10BE2970).
#include <Ascension/AscAddons.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/RealmInfo.hpp>
#include <Client/CVar.hpp>
#include <Windows.h>
#include <intrin.h>
#include <cstdint>

namespace
{
    const bool kAllSecure = false;   // SecureAddonMgr +0x10 (0x10BE3054): never written

    const char* Running() { return *reinterpret_cast<const char* const*>(ASC_PTR(0xD4139C)); }

    void SetRunning(const char* name)   // FUN_10313b70
    {
        DWORD old;
        const BOOL ok = VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0xD4139C)), 4, PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<const char**>(ASC_PTR(0xD4139C)) = name;
        if (ok)
            VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0xD4139C)), 4, old, &old);
    }

    bool Secure(const char* running) { return (running && AscAddons::IsSecure(running)) || kAllSecure; }

    template <class F, class... A> int Cleared(F original, A... args)
    {
        const char* saved = Running();
        if (Secure(saved))
            SetRunning(nullptr);
        const int r = original(args...);
        SetRunning(saved);
        return r;
    }

    typedef int(__cdecl* Fn1_t)(uint32_t);
    typedef int(__cdecl* FnL_t)(uintptr_t);   // lua_State*
    FnL_t g_816DA0;
    typedef int(__cdecl* Fn2_t)(uint32_t, uint32_t);
    typedef int(__cdecl* Fn3_t)(uint32_t, uint32_t, uint32_t);
    typedef int(__cdecl* Fn5_t)(const char*, uint32_t, uintptr_t, uintptr_t, uintptr_t);   // IsAddOnLoadable: bool, Reason&, Reason&, const char*

    Fn1_t g_5191C0;
    FnL_t g_49F210, g_51A7A0, g_527F00, g_52B470, g_52B4E0, g_525FC0, g_51AC90;   // Script_*(lua_State*)
    Fn3_t g_51FB80;
#if ASC_X64
    typedef void (*FocusX64_t)(const void*);                 // CGGameUI::SetFocusTarget(const WOWGUID&)
    typedef void (*DirectionX64_t)(float, float, int32_t);   // CGGameUI::TargetDirection(float, float, int): floats em xmm
    FocusX64_t g_51FF20;
    DirectionX64_t g_5252D0;
#else
    Fn2_t g_51FF20;
    Fn3_t g_5252D0;
#endif
    Fn5_t g_5F7E90;

    // Functions whose first bytes hold a relative call: the call is re-made, then the rest runs.
#if !ASC_X64
    __declspec(naked) int __fastcall Original488540(void*)
    {
        __asm
        {
            add ecx, 0x20
            mov eax, 0x489BB0
            call eax
            push 0x488548
            ret
        }
    }
#else
    // x64: o trampolim do MinHook (AscRuntime::Detour) faz o papel do prologo refeito a mao.
    typedef bool (*Fn488540_t)(void*);   // CScriptRegion::ProtectedFunctionsAllowed (__thiscall, bool)
    Fn488540_t g_488540 = nullptr;
    int Original488540(void* self) { return g_488540(self) ? 1 : 0; }
#endif
#if !ASC_X64
    __declspec(naked) int __cdecl Original510430(uint32_t)
    {
        __asm
        {
            push 0
            push 0
            mov eax, 0x6B0970
            call eax
            push 0x510439
            ret
        }
    }
#else
    int (*g_510430)(uintptr_t) = nullptr;   // int(lua_State*), trampolim do MinHook
    int Original510430(uintptr_t L) { return g_510430(L); }
#endif
#if !ASC_X64
    __declspec(naked) int __cdecl Original510450(uint32_t)
    {
        __asm
        {
            push 0
            push 1
            mov eax, 0x6B0970
            call eax
            push 0x510459
            ret
        }
    }
#else
    int (*g_510450)(uintptr_t) = nullptr;   // int(lua_State*), trampolim do MinHook
    int Original510450(uintptr_t L) { return g_510450(L); }
#endif
#if !ASC_X64
    __declspec(naked) int __cdecl Original510A00(uint32_t)
    {
        __asm
        {
            push 0
            mov eax, 0x4033C0
            call eax
            push 0x510A07
            ret
        }
    }
#else
    int (*g_510A00)(uintptr_t) = nullptr;   // int(lua_State*), trampolim do MinHook
    int Original510A00(uintptr_t L) { return g_510A00(L); }
#endif

    int __cdecl Hook5191C0(uint32_t action)   // FUN_103115b0
    {
        return Secure(Running()) ? 1 : g_5191C0(action);
    }

    int __fastcall Hook488540(void* self ASC_EDX_P(void*))   // FUN_103119c0
    {
        return Secure(Running()) ? 1 : Original488540(self);
    }

    int __cdecl Hook816DA0(uintptr_t L)   // FUN_10313a50
    {
        if (Secure(Running()))
        {
            reinterpret_cast<void(__cdecl*)(uintptr_t, double)>(ASC_PTR(0x84E2A0))(L, 1.0);   // lua_pushnumber
            return 1;
        }
        return g_816DA0(L);
    }

    int __cdecl Hook49F210(uintptr_t L) { return Cleared(g_49F210, L); }        // FUN_10311ac0
    int __cdecl Hook510430(uintptr_t a) { return Cleared(&Original510430, a); } // FUN_10313520
    int __cdecl Hook510450(uintptr_t a) { return Cleared(&Original510450, a); } // FUN_10313620
    int __cdecl Hook510A00(uintptr_t a) { return Cleared(&Original510A00, a); } // FUN_10313030
    int __cdecl Hook51A7A0(uintptr_t L) { return Cleared(g_51A7A0, L); }        // FUN_10313950
    int __cdecl Hook51AC90(uintptr_t L) { return Cleared(g_51AC90, L); }                     // FUN_10312f00
    int __cdecl Hook51FB80(uint32_t a, uint32_t b, uint32_t c) { return Cleared(g_51FB80, a, b, c); }   // FUN_103117b0
#if ASC_X64
    void Hook51FF20(const void* guid)   // FUN_103116b0
    {
        const char* saved = Running();
        if (Secure(saved))
            SetRunning(nullptr);
        g_51FF20(guid);
        SetRunning(saved);
    }
    void Hook5252D0(float a, float b, int32_t c)   // FUN_103118b0
    {
        const char* saved = Running();
        if (Secure(saved))
            SetRunning(nullptr);
        g_5252D0(a, b, c);
        SetRunning(saved);
    }
#else
    int __cdecl Hook51FF20(uint32_t a, uint32_t b) { return Cleared(g_51FF20, a, b); }                  // FUN_103116b0
    int __cdecl Hook5252D0(uint32_t a, uint32_t b, uint32_t c) { return Cleared(g_5252D0, a, b, c); }   // FUN_103118b0
#endif
    int __cdecl Hook527F00(uintptr_t L) { return Cleared(g_527F00, L); }        // FUN_10313130
    int __cdecl Hook52B470(uintptr_t L) { return Cleared(g_52B470, L); }        // FUN_10313720
    int __cdecl Hook52B4E0(uintptr_t L) { return Cleared(g_52B4E0, L); }        // FUN_10313820

    int __cdecl Hook525FC0(uintptr_t a)   // LAB_10313000
    {
        if (AscAddons::InsideProtectedCall())
        {
            reinterpret_cast<void(__cdecl*)(void*, uint32_t)>(ASC_PTR(0x513530))(nullptr, 2);
            return 0;
        }
        return g_525FC0(a);
    }

    // FUN_10312630. Refusals return with only the low byte cleared in the original (the rest is left
    // over from a destructor); 0 here.
    int __cdecl Hook5F7E90(const char* name, uint32_t b, uintptr_t c, uintptr_t d, uintptr_t e)
    {
#if ASC_X64
        // x64: retornos dos calls em LoadAddOnFileCount (2o call) e LoadAddOn, via extra-addr (@0x38A884, @0x38A170).
        void* const caller = _ReturnAddress();
        if (!RealmInfoSvc::Get().trailingFlag && !AscAddons::IsKnown(name))
            return 0;
        if (caller == AscX64::Ptr(0x5F8076) || caller == AscX64::Ptr(0x5F80FF))
#else
        const uint32_t caller = reinterpret_cast<uint32_t>(_ReturnAddress());
        if (!RealmInfoSvc::Get().trailingFlag && !AscAddons::IsKnown(name))
            return 0;
        if (caller == 0x5F8076 || caller == 0x5F80FF)
#endif
            if (CVar* cvar = CVar::Lookup("loadUnknownAddOns"))
                if (*reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(cvar) + ASC_OFF(0x30, 0x54)) == 0 &&
                    !AscAddons::IsKnown(name))
                    return 0;
        return g_5F7E90(name, b, c, d, e);
    }

    // FUN_10313d90 / LAB_10313d70: a target change runs with a secure addon's taint cleared.
    const char* g_targetChangeRunning = nullptr;   // 0x10BE3F80
    void BeforeTargetChange()
    {
        g_targetChangeRunning = Running();
        if (Secure(g_targetChangeRunning))
            SetRunning(nullptr);
    }
    void AfterTargetChange()
    {
        SetRunning(g_targetChangeRunning);
        g_targetChangeRunning = nullptr;
    }

    template <class T> T Hook(uint32_t target, uint32_t prologue, void* fn)
    {
        return reinterpret_cast<T>(AscRuntime::Detour(target, prologue, fn));
    }

    void Init()   // FUN_10312150 (SMSG 0x94E and the bindings live in AscGlobalsJ)
    {
        g_5191C0 = Hook<Fn1_t>(0x5191C0, 10, reinterpret_cast<void*>(&Hook5191C0));   // hooked 0x5191C0
        AscRuntime::OnBeforeTargetChange(BeforeTargetChange);
        AscRuntime::OnAfterTargetChange(AfterTargetChange);
        g_816DA0 = Hook<FnL_t>(0x816DA0, 10, reinterpret_cast<void*>(&Hook816DA0));   // hooked 0x816DA0
#if ASC_X64
        g_510430 = Hook<int (*)(uintptr_t)>(0x510430, 0, reinterpret_cast<void*>(&Hook510430));
#else
        AscRuntime::ReplaceFunction(0x510430, reinterpret_cast<void*>(&Hook510430));
#endif
        g_51AC90 = Hook<FnL_t>(0x51AC90, 7, reinterpret_cast<void*>(&Hook51AC90));   // hooked 0x51AC90
        g_51A7A0 = Hook<FnL_t>(0x51A7A0, 9, reinterpret_cast<void*>(&Hook51A7A0));   // hooked 0x51A7A0
        g_527F00 = Hook<FnL_t>(0x527F00, 7, reinterpret_cast<void*>(&Hook527F00));   // hooked 0x527F00
        g_52B470 = Hook<FnL_t>(0x52B470, 10, reinterpret_cast<void*>(&Hook52B470));   // hooked 0x52B470
        g_52B4E0 = Hook<FnL_t>(0x52B4E0, 10, reinterpret_cast<void*>(&Hook52B4E0));   // hooked 0x52B4E0
#if ASC_X64
        g_510450 = Hook<int (*)(uintptr_t)>(0x510450, 0, reinterpret_cast<void*>(&Hook510450));
#else
        AscRuntime::ReplaceFunction(0x510450, reinterpret_cast<void*>(&Hook510450));
#endif
#if ASC_X64
        g_510A00 = Hook<int (*)(uintptr_t)>(0x510A00, 0, reinterpret_cast<void*>(&Hook510A00));
#else
        AscRuntime::ReplaceFunction(0x510A00, reinterpret_cast<void*>(&Hook510A00));
#endif
#if ASC_X64
        g_488540 = Hook<Fn488540_t>(0x488540, 0, reinterpret_cast<void*>(&Hook488540));
#else
        AscRuntime::ReplaceFunction(0x488540, reinterpret_cast<void*>(&Hook488540));
#endif
        g_49F210 = Hook<FnL_t>(0x49F210, 8, reinterpret_cast<void*>(&Hook49F210));   // hooked 0x49F210
#if ASC_X64
        g_51FF20 = Hook<FocusX64_t>(0x51FF20, 8, reinterpret_cast<void*>(&Hook51FF20));
        g_5252D0 = Hook<DirectionX64_t>(0x5252D0, 9, reinterpret_cast<void*>(&Hook5252D0));
#else
        g_51FF20 = Hook<Fn2_t>(0x51FF20, 8, reinterpret_cast<void*>(&Hook51FF20));   // hooked 0x51FF20
        g_5252D0 = Hook<Fn3_t>(0x5252D0, 9, reinterpret_cast<void*>(&Hook5252D0));   // hooked 0x5252D0
#endif
        g_51FB80 = Hook<Fn3_t>(0x51FB80, 8, reinterpret_cast<void*>(&Hook51FB80));   // hooked 0x51FB80
        g_525FC0 = Hook<FnL_t>(0x525FC0, 8, reinterpret_cast<void*>(&Hook525FC0));   // hooked 0x525FC0
        g_5F7E90 = Hook<Fn5_t>(0x5F7E90, 5, reinterpret_cast<void*>(&Hook5F7E90));   // hooked 0x5F7E90
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
