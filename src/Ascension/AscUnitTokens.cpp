// Two hooks of installer FUN_100a05d0.
//
//   0x60BB70  after the client lists a unit's tokens (__cdecl(guid*, count*), returning the buffer array),
//             "nameplate<N>" is appended when the unit has a nameplate (AscNamePlates) and input is live
//             (0xBD0792)                                                      (FUN_100a09e0)
//   0x84E350  lua_pushstring: a NULL string becomes "" when the return address is one of 0x81AC90,
//             0x81B530, 0x615020, 0x6143F0, 0x81AA00 -- function entry points, which a return address
//             never is, so it never does (IMPROVEMENTS.md)                    (FUN_100a0880)
#include <Ascension/AscScript.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscRuntime.hpp>
#include <intrin.h>
#include <cstdint>
#include <cstdio>

uint32_t AscNamePlates_IdForUnit(const void* unit);   // AscNamePlates.cpp (FUN_102bf3c0)

namespace
{
    typedef char** (__cdecl* Tokens_t)(const uint32_t*, int32_t*);
    typedef int(__cdecl* PushString_t)(void*, const char*);
    Tokens_t g_60BB70 = nullptr;
    PushString_t g_84E350 = nullptr;

    char** __cdecl Hook60BB70(const uint32_t* guid, int32_t* count)
    {
        char** lines = g_60BB70(guid, count);
        if (!*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)))
            return lines;
        const void* unit = static_cast<const void*>(AscScript::ObjectPtr(AscX64::Guid(guid[0], guid[1]), 0x18));
        if (!unit)
            return lines;
        if (const uint32_t id = AscNamePlates_IdForUnit(unit))
        {
            sprintf(lines[*count], "nameplate%d", id);   // FUN_100a4f80, unbounded as in the original
            ++*count;
        }
        return lines;
    }

#if !ASC_X64
    int __cdecl Hook84E350(void* L, const char* s)
    {
        if (!s)
        {
            const uint32_t caller = reinterpret_cast<uint32_t>(_ReturnAddress());
            if (caller == 0x81AC90 || caller == 0x81B530 || caller == 0x615020 || caller == 0x6143F0 || caller == 0x81AA00)
                s = "";
        }
        return g_84E350(L, s);
    }
#endif

    void Init()   // FUN_100a05d0
    {
#if !ASC_X64   // x64: os "retornos" sao entradas de funcao -- nunca casam (ver topo); o detour nao faz nada
        g_84E350 = reinterpret_cast<PushString_t>(AscRuntime::Detour(0x84E350, 6, reinterpret_cast<void*>(&Hook84E350)));
#endif
        g_60BB70 = reinterpret_cast<Tokens_t>(AscRuntime::Detour(0x60BB70, 7, reinterpret_cast<void*>(&Hook60BB70)));
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
