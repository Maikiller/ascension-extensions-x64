// More GLOBAL bindings, each from its decompile (ghidra-ext/out/bindings/<Name>.c): CVar listing,
// runtime interface events, screen projection, and nearest-object helpers.
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscScript.hpp>
#include <Client/FrameScript.hpp>
#include <Misc/DataContainer.hpp>
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace AscScript;

namespace
{
    // handler_ListCVars: the client's CVar hash (0xCA19FC): list head +0x0C (low bit = end), link
    // offset +0x04; node +0x14 name, +0x28 value string. { [name] = value }.
    int ListCVars(lua_State* L)
    {
        AscLua::lua_createtable(L, 0, 0);
        const uint8_t* map = reinterpret_cast<const uint8_t*>(ASC_PTR(0xCA19FC));
#if ASC_X64
        // TSHashTable x64 (PDB): m_fulllist +8 { m_linkoffset +8, m_terminator.m_next +0x18 }; CVar: m_key +0x28,
        // m_stringValue.m_str +0x48; TSLink.m_next em +8 do link.
        const intptr_t link = *reinterpret_cast<const intptr_t*>(map + 0x8);
        uintptr_t node = *reinterpret_cast<const uintptr_t*>(map + 0x18);
        constexpr uint32_t kName = 0x28, kValue = 0x48, kNext = 8;
#else
        const uint32_t link = *reinterpret_cast<const uint32_t*>(map + 4);
        uint32_t node = *reinterpret_cast<const uint32_t*>(map + 0x0C);
        constexpr uint32_t kName = 0x14, kValue = 0x28, kNext = 4;
#endif
        if (node & 1)
            node = 0;
        while (node)
        {
            PushStr(L, *reinterpret_cast<const char* const*>(node + kName));
            PushStr(L, *reinterpret_cast<const char* const*>(node + kValue));
            AscLua::lua_settable(L, -3);
            const auto n = *reinterpret_cast<const decltype(node)*>(node + kNext + link);
            node = (n & 1) ? 0 : n;
        }
        return 1;
    }

    // handler_RegisterInterfaceEvent: in world only; a name the client does not know is appended to
    // the custom list and the 722 stock events (0xC24EB0) are registered again with it (0x81B5F0).
    int RegisterInterfaceEvent(lua_State* L)
    {
        if (!*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)))
        {
            AscLua::luaL_error(L, "RegisterInterfaceEvent can only be called once the player is in the world.");
            return 0;
        }
        const char* name = CheckString(L, 1);
        if (AscRuntime::EventId(name) != -1)
        {
            AscLua::lua_pushboolean(L, 0);
            return 1;
        }
        sDC.RegisterCustomEvent(_strdup(name));
        reinterpret_cast<void(__cdecl*)(const char**, size_t)>(ASC_PTR(0x81B5F0))(reinterpret_cast<const char**>(ASC_PTR(0xC24EB0)), 0x2D2);   // the detour appends the customs
        AscLua::lua_pushboolean(L, AscRuntime::EventId(name) != -1);
        return 1;
    }

    // FUN_1019ca10: a unit's position projected by the world frame (*0xB7436C, 0x4F6D20) -> x, y.
    int GetScreenPosition(lua_State* L)
    {
        uint64_t guid = 0;
        if (!UnitTokenGuid(CheckString(L, 1), guid))
            return 0;
        uint8_t* unit = static_cast<uint8_t*>(ObjectPtr(guid, 0x18));
        if (!unit)
            return 0;
        float pos[3] = {};
        reinterpret_cast<void(__thiscall*)(void*, float*)>((*reinterpret_cast<void***>(unit))[0x2C / 4])(unit, pos);
        float xy[3] = {};
        void* world = *reinterpret_cast<void**>(ASC_PTR(0xB7436C));
        reinterpret_cast<bool(__thiscall*)(void*, float*, float*, void*)>(ASC_PTR(0x4F6D20))(world, pos, xy, nullptr);
        AscLua::lua_pushnumber(L, xy[0]);
        AscLua::lua_pushnumber(L, xy[1]);
        return 2;
    }

    // ---- nearest objects (FUN_1019cc50 / FUN_103367a0) --------------------------------------------
    // Units from ClntObjMgrEnumVisibleObjects (0x4D4B30, callback LAB_1019c4c0: ObjectPtr mask 8).
    int __cdecl CollectUnit(uint32_t lo, uint32_t hi, void* ctx)
    {
        if (void* obj = static_cast<void*>(AscScript::ObjectPtr(AscX64::Guid(lo, hi), 8)))
            static_cast<std::vector<uint8_t*>*>(ctx)->push_back(static_cast<uint8_t*>(obj));
        return 1;
    }

    std::vector<uint8_t*> VisibleUnits()
    {
        std::vector<uint8_t*> v;
        AscScript::EnumObjects(&CollectUnit, &v);
        return v;
    }

    void Position(uint8_t* obj, float* out)
    {
        reinterpret_cast<void(__thiscall*)(void*, float*)>((*reinterpret_cast<void***>(obj))[0x2C / 4])(obj, out);
    }

    float Distance(const float* a, const float* b)
    {
        const float dz = static_cast<float>(std::pow(static_cast<double>(b[2] - a[2]), 2.0));
        const float dx = static_cast<float>(std::pow(static_cast<double>(b[0] - a[0]), 2.0));
        const float dy = static_cast<float>(std::pow(static_cast<double>(b[1] - a[1]), 2.0));
        return static_cast<float>(std::sqrt(static_cast<double>(dy + dx + dz)));
    }

    // The first of the closest (a later candidate only wins when strictly closer).
    uint8_t* Closest(const std::vector<std::pair<uint8_t*, float>>& c)
    {
        if (c.empty())
            return nullptr;
        size_t best = 0;
        for (size_t i = 1; i < c.size(); ++i)
            if (!(c[best].second < c[i].second || c[best].second == c[i].second))
                best = i;
        return c[best].first;
    }

    void TargetUnit(const uint8_t* obj)   // FUN_10111d00 -> 0x524BF0
    {
        const uint32_t* g = *reinterpret_cast<const uint32_t* const*>(obj + ASC_OFF(8, 0x10));
#if ASC_X64
        reinterpret_cast<void (*)(uint64_t)>(ASC_PTR(0x524BF0))(AscX64::Guid(g[0], g[1]));   // CGGameUI::Target(WOWGUID)
#else
        reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(ASC_PTR(0x524BF0))(g[0], g[1]);
#endif
    }

    // Within 5.5 * 5.5 of the player -- the original compares the distance, not its square, against
    // the squared range -- interact (vtable +0xB0) with the closest, and target it when it is a unit.
    int InteractNearest(lua_State*)
    {
        uint8_t* player = ActivePlayer();
        float p[3];
        Position(player, p);
        std::vector<std::pair<uint8_t*, float>> c;
        for (uint8_t* o : VisibleUnits())
        {
            float q[3];
            Position(o, q);
            const float d = Distance(p, q);
            if (d <= 5.5f * 5.5f)
                c.emplace_back(o, d);
        }
        if (uint8_t* best = Closest(c))
        {
            reinterpret_cast<void(__thiscall*)(void*)>((*reinterpret_cast<void***>(best))[0xB0 / 4])(best);
            if (*reinterpret_cast<const int32_t*>(best + ASC_OFF(0x14, 0x24)) == 3)
                TargetUnit(best);
        }
        return 0;
    }

    void WriteProtected(uint32_t addr, uint32_t v)   // FUN_10313b70: through NtProtectVirtualMemory
    {
#if ASC_X64
        AscX64::WriteMem(addr, &v, sizeof(v), __FUNCTION__);
        return;
#endif
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), 4, PAGE_EXECUTE_READWRITE, &old))
            return;
        *reinterpret_cast<uint32_t*>(addr) = v;
        VirtualProtect(reinterpret_cast<void*>(addr), 4, old, &old);
    }

    // The closest unit the player can attack (0x729A70 on the player) becomes the target when it is
    // a creature. 0xD4139C is cleared for the scan and restored only when a candidate was found.
    int TargetClosestEnemy(lua_State*)
    {
#if ASC_X64
        // lua_tainted (0xD4139C) e const char*: 8 bytes no x64 -- a gravacao de 4 deixaria a metade alta
        uintptr_t* taint = reinterpret_cast<uintptr_t*>(ASC_PTR(0xD4139C));
        const uintptr_t saved = *taint;
        *taint = 0;
#else
        const uint32_t saved = *reinterpret_cast<const uint32_t*>(ASC_PTR(0xD4139C));
        WriteProtected(0xD4139C, 0);
#endif
        uint8_t* player = ActivePlayer();
        float p[3];
        Position(player, p);
        std::vector<std::pair<uint8_t*, float>> c;
        for (uint8_t* o : VisibleUnits())
        {
            float q[3];
            Position(o, q);
            if (reinterpret_cast<bool(__thiscall*)(void*, void*)>(ASC_PTR(0x729A70))(player, o))
                c.emplace_back(o, Distance(p, q));
        }
        if (uint8_t* best = Closest(c))
        {
            if (*reinterpret_cast<const int32_t*>(best + ASC_OFF(0x14, 0x24)) == 3)
                TargetUnit(best);
#if ASC_X64
            *taint = saved;
#else
            WriteProtected(0xD4139C, saved);
#endif
        }
        return 0;
    }

    const AscBindings::Binding kBindings[] = {
        {nullptr, "GetScreenPosition", GetScreenPosition},
        {nullptr, "InteractNearest", InteractNearest},
        {nullptr, "ListCVars", ListCVars},
        {nullptr, "RegisterInterfaceEvent", RegisterInterfaceEvent},
        {nullptr, "TargetClosestEnemy", TargetClosestEnemy},
    };
    AscBindings::Module s_module(kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}
