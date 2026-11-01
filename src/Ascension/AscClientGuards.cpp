// Two small client guards from the original's init.
//
//   FUN_10a4c0c0: NOPs the 11 bytes at 0x68E4AD (`mov eax,[ebp+8] / push eax / mov ecx,esi / call 0x682D00`).
//     Registered by FUN_10a60cd0 in the after-0x52A980 list (AscRuntime::OnAfter52A980), and applied by the
//     memory guard FUN_10a3abd0 (after every 0x403340, AscRuntime::OnAfter403340) once the process working
//     set exceeds 3200 MB -- which also sets the client byte 0xBD0791 (DAT_10BCC8EC), the one the
//     target-change hook checks.
//   0x8540D0 -> FUN_10296fd0 (hook object 0x10BCB928): the client's `string` library registration is
//     replaced by one with an extra function, string.toboolean (FUN_10297280); the client's own
//     luaL_register at 0x8540D7 is NOPed (19 bytes) before the original runs.
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscLua.hpp>
#include <Ascension/AscRuntime.hpp>
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <psapi.h>

namespace
{
    void WriteCode(uint32_t at, const void* bytes, size_t n)
    {
#if ASC_X64
        AscX64::WriteMem(at, bytes, n, __FUNCTION__);
        return;
#endif
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(at), n, PAGE_EXECUTE_READWRITE, &old))
            return;
        memcpy(reinterpret_cast<void*>(at), bytes, n);
        VirtualProtect(reinterpret_cast<void*>(at), n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n);
    }

    void SkipCall68E4AD()   // FUN_10a4c0c0
    {
#if ASC_X64
        // CGxDeviceD3d::DeviceSetBaseMipLevel+0x28: o x64 faz tail-jmp p/ CGxDevice::DeviceSetBaseMipLevel -> ret
        static const uint8_t kRet[5] = {0xC3, 0x90, 0x90, 0x90, 0x90}, kJmpX64[5] = {0xE9, 0x23, 0x3B, 0xFE, 0xFF};
        AscX64::WriteCodeX64(0x68E4AD, kRet, 5, kJmpX64);
#else
        uint8_t nops[11];
        memset(nops, 0x90, sizeof(nops));
        WriteCode(0x68E4AD, nops, sizeof(nops));   // HOOKED 0x68E4AD
#endif
    }

    void MemoryGuard()   // FUN_10a3abd0
    {
        PROCESS_MEMORY_COUNTERS pmc = {};
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, 0x28))
            return;
        if ((pmc.WorkingSetSize >> 20) > 0xC80)
        {
            SkipCall68E4AD();
            *reinterpret_cast<uint8_t*>(ASC_PTR(0xBD0791)) = 1;
        }
    }

    // FUN_10297280 string.toboolean(v)
    int ToBoolean(lua_State* L)
    {
        const int type = AscLua::lua_type(L, 1);
        if (type == 1)   // boolean
        {
            AscLua::lua_pushboolean(L, AscLua::lua_toboolean(L, 1) != 0);
            return 1;
        }
        if (type == 3)   // number, truncated
        {
            const double d = reinterpret_cast<double(__cdecl*)(lua_State*, int)>(ASC_PTR(0x84FAB0))(L, 1);
            AscLua::lua_pushboolean(L, static_cast<int32_t>(static_cast<int64_t>(d)) != 0);
            return 1;
        }
        const char* s = reinterpret_cast<const char*(__cdecl*)(lua_State*, int, size_t*)>(ASC_PTR(0x84F9F0))(L, 1, nullptr);
        const bool yes = *s && (strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "on") == 0 || strcmp(s, "t") == 0);
        AscLua::lua_pushboolean(L, yes);
        return 1;
    }

    struct LuaReg { const char* name; lua_CFunction fn; };
    LuaReg g_string[17];   // the client's 15 (0xA478E0) + toboolean + the terminator

    typedef int(__cdecl* Fn8540D0_t)(lua_State*);
    Fn8540D0_t g_8540D0 = nullptr;
    int __cdecl OpenString(lua_State* L)   // FUN_10296fd0
    {
#if ASC_X64
        // x64: a tabela luaL_Reg do cliente (0xA478E0) nao tem simbolo e o NOP em 0x8540D7 nao se aplica; abre a
        // biblioteca original e acrescenta string.toboolean na mesma tabela (resultado igual).
        const int r = g_8540D0(L);
        static const LuaReg kExtra[] = {{"toboolean", &ToBoolean}, {nullptr, nullptr}};
        reinterpret_cast<void(__cdecl*)(lua_State*, const char*, const LuaReg*)>(ASC_PTR(0x84FDD0))(L, "string", kExtra);
        AscLua::lua_settop(L, -2);
        return r;
#endif
        memcpy(g_string, reinterpret_cast<const void*>(ASC_PTR(0xA478E0)), 0x80);   // 16 entries incl. the terminator
        g_string[15] = {"toboolean", &ToBoolean};
        g_string[16] = {nullptr, nullptr};
        reinterpret_cast<void(__cdecl*)(lua_State*, const char*, const LuaReg*)>(ASC_PTR(0x84FDD0))(L, "string", g_string);
        uint8_t nops[19];
        memset(nops, 0x90, sizeof(nops));
        WriteCode(0x8540D7, nops, sizeof(nops));
        return g_8540D0(L);
    }

    void Init()
    {
        AscRuntime::OnAfter52A980(&SkipCall68E4AD);
        AscRuntime::OnAfter403340(&MemoryGuard);
        g_8540D0 = reinterpret_cast<Fn8540D0_t>(AscRuntime::Detour(0x8540D0, 7, reinterpret_cast<void*>(&OpenString)));
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}
