#pragma once
// The original Extensions.dll's own scripting substrate, transcribed rather than approximated.
//
// Every binding in ghidra-ext/out/bindings reads its arguments through the same few helpers, and
// sends through the same CDataStore sequence. Getting these exactly right once is what lets each
// binding be a line-for-line transcription of its decompile.
//
//   ValidateInput   FUN_10309e50  "Script::ValidateInput" -- 40 bindings directly, and every one of
//                                 the single-argument readers below. Logs and returns false on a
//                                 mismatch; never raises a Lua error.
//   ReadNumber      FUN_1008ba00  {number}          -> truncated u32               ~60 bindings
//   ReadString      FUN_100b8970  {string}          -> luaL_checklstring(L, 1)     93 bindings
//   ReadInt2        FUN_100b88c0  {number, number}  -> two truncated ints          42 bindings
//   ReadBool        FUN_100b8aa0  {boolean}         -> lua_toboolean(L, 1)         20 bindings
//
// AscArgs predates this and approximates the same contract with lua_is*; new transcriptions use
// this header so the accepted argument set is the original's, including its quirks (a boolean slot
// accepts nil and numbers; ReadInt2 validates two arguments even when a caller only uses one).
#include <Ascension/AscScript.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscLua.hpp>
#include <Client/CDataStore.hpp>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace AscScript
{
    // Type tags as the validator switches on them (the stock Lua 5.1 LUA_T* values).
    enum Arg : int
    {
        NIL = 0, BOOLEAN = 1, LIGHTUSERDATA = 2, NUMBER = 3, STRING = 4,
        TABLE = 5, FUNCTION = 6, USERDATA = 7, THREAD = 8,
    };

    bool ValidateInput(lua_State* L, std::initializer_list<int> types);
    bool ValidateInput(lua_State* L, const int* types, size_t count);   // a list built at run time

    // The client's lauxlib, at the addresses the DLL calls directly.
    double CheckNumber(lua_State* L, int idx);         // luaL_checknumber   0x84FAB0
    const char* CheckString(lua_State* L, int idx);    // luaL_checklstring  0x84F9F0

    // FUN_10ae6370 / FUN_10ae6780: the CRT's double -> int64 truncation; every binding keeps the low
    // 32 bits.
    inline int32_t ToInt(double d) { return static_cast<int32_t>(static_cast<int64_t>(d)); }

    bool ReadNumber(lua_State* L, uint32_t& out);
    bool ReadString(lua_State* L, std::string& out);
    bool ReadInt2(lua_State* L, int32_t& a, int32_t& b);
    bool ReadBool(lua_State* L, bool& out);

    // The packet sequence every sender in the DLL inlines: CDataStore ctor (0x401050), Put(u32 opcode)
    // (0x47B0A0), PutData for each field (0x47B1C0), Finalize (0x401130), ClientServices::Send via the
    // instance check (0x6B0970 -> 0x632B50), dtor (0x403880).
    class Packet
    {
    public:
        explicit Packet(uint32_t opcode);
        ~Packet();
        Packet(const Packet&) = delete;
        Packet& operator=(const Packet&) = delete;

        Packet& Data(const void* p, uint32_t n);
        Packet& U8(uint8_t v)   { return Data(&v, 1); }
        Packet& U16(uint16_t v) { return Data(&v, 2); }
        Packet& U32(uint32_t v) { return Data(&v, 4); }
        Packet& U64(uint64_t v) { return Data(&v, 8); }
        Packet& F32(float v)    { return Data(&v, 4); }
        Packet& Str(const char* s);                 // PutString 0x47B300: strlen+1, NUL included
        Packet& PackedGuid(uint64_t guid);          // FUN_100de790
        void Send();
        CDataStore* Store() { return &m_store; }

    private:
        CDataStore m_store{};
    };

    // CompressedDataStore (FUN_10195a60): the rest of the packet is u32 inflated size + zlib data,
    // inflated with the client's 0x778180 (stdcall dest, &destLen, src, &srcLen; 0 = ok). The packet
    // is consumed either way.
    bool InflatePacket(CDataStore* p, std::vector<uint8_t>& out);

    // Object manager, as the bindings call it.
    uint64_t ActivePlayerGuid();                            // 0x4D3790
    uint8_t* ActivePlayer();                                // 0x4038F0 (object, or nullptr)
    void* ObjectPtr(uint64_t guid, uint32_t typeMask);

    // CVar::m_stringValue.m_str: x86 +0x28, x64 +0x48 (PDB: TSHashObject 0x30 + category/flags, RCString.m_str +0x10).
    inline const char* CVarString(const void* cvar)
    {
#if ASC_X64
        constexpr size_t kValue = 0x48;
#else
        constexpr size_t kValue = 0x28;
#endif
        return cvar ? *reinterpret_cast<const char* const*>(static_cast<const uint8_t*>(cvar) + kValue) : nullptr;
    }

    // TSList do cliente (x86: linkoffset 0, terminator {prev 4, next 8}; x64: 0, {8, 0x10}); no = TSLink {prev, next}
    // seguido do dado. Percorre e devolve o 1o no em que pred(no + tamanho do link) for verdadeiro.
    template <class Pred>
    const uint8_t* ListFind(uint32_t listAddr, Pred pred)
    {
        constexpr size_t P = sizeof(void*);
        uintptr_t node = *reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(ASC_PTR(listAddr)) + 2 * P);
        while (node && !(node & 1))
        {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(node) + 2 * P;
            if (pred(data))
                return data;
            node = *reinterpret_cast<const uintptr_t*>(node + P);
        }
        return nullptr;
    }
    // s_completedAchievements (0xACFDB0): CLIENT_ACHIEVEMENTS.m_id logo apos o link
    inline bool AchievementDone(uint32_t id)
    {
        return ListFind(0xACFDB0, [id](const uint8_t* d) { return *reinterpret_cast<const uint32_t*>(d) == id; }) != nullptr;
    }
    // CGEquipmentManager::s_equipmentSets (0xACFC00): EQUIPMENT_SET* apos o link; EQUIPMENT_SET igual nos dois
    inline const uint8_t* EquipmentSetByGuid(uint64_t guid)
    {
        const uint8_t* d = ListFind(0xACFC00, [guid](const uint8_t* d) {
            const uint8_t* set = *reinterpret_cast<const uint8_t* const*>(d);
            return set && *reinterpret_cast<const uint64_t*>(set + 8) == guid;
        });
        return d ? *reinterpret_cast<const uint8_t* const*>(d) : nullptr;
    }

    // GUIDToHexString 0x74D0D0: x86 recebe o GUID em duas metades, x64 um WOWGUID.
    inline void GuidToHex(uint32_t lo, uint32_t hi, char* buf)
    {
#if ASC_X64
        reinterpret_cast<void (*)(uint64_t, char*)>(ASC_PTR(0x74D0D0))(AscX64::Guid(lo, hi), buf);
#else
        reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t, char*)>(ASC_PTR(0x74D0D0))(lo, hi, buf);
#endif
    }
    // ClntObjMgrEnumVisibleObjects (0x4D4B30) com callback no formato x86 (guid lo, guid hi, param). No x64 o cliente
    // chama o callback com (WOWGUID, param): uma ponte reparte o GUID.
    template <class F, class Q>
    int EnumObjects(F fn, Q param)
    {
#if ASC_X64
        struct Ctx { F fn; Q param; } ctx{fn, param};
        auto bridge = [](uint64_t guid, void* c) -> int {
            Ctx* x = static_cast<Ctx*>(c);
            return x->fn(static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32), x->param);
        };
        return reinterpret_cast<int (*)(int (*)(uint64_t, void*), void*)>(ASC_PTR(0x4D4B30))(bridge, &ctx);
#else
        return reinterpret_cast<int(__cdecl*)(void*, void*)>(ASC_PTR(0x4D4B30))((void*)fn, (void*)param);
#endif
    }
      // 0x4D4DB0
    bool UnitTokenGuid(const char* token, uint64_t& guid);  // 0x60ABF0 ("player", "target", ...)

    // Lua pushes as the original writes them: a NULL string is pushed as "" (DAT_10b1b240).
    inline void PushStr(lua_State* L, const char* s) { AscLua::lua_pushstring(L, s ? s : ""); }
    inline void PushInt(lua_State* L, int32_t v)      { AscLua::lua_pushinteger(L, v); }
    inline void PushNum(lua_State* L, double v)       { AscLua::lua_pushnumber(L, v); }
    inline void PushBool(lua_State* L, bool v)        { AscLua::lua_pushboolean(L, v ? 1 : 0); }
    inline int PushNils(lua_State* L, int n)          { for (int i = 0; i < n; ++i) AscLua::lua_pushnil(L); return n; }
    inline void SetStrInt(lua_State* L, const char* k, int32_t v)
    {
        AscLua::lua_pushstring(L, k);
        AscLua::lua_pushinteger(L, v);
        AscLua::lua_settable(L, -3);
    }

    // FUN_100b2cb0: a copy of the Spell.dbc record (0x2A8 bytes) via 0x4CFD20 on container 0xAD49D0.
    bool FetchSpell(uint32_t spellId, uint8_t* rec);
    // __thiscall 0x7282A0(unit, spellId): does the unit have an aura from this spell.
    bool UnitHasAuraSpell(void* unit, uint32_t spellId);

    // FUN_100cd740: the value after "-name" / "--name" / "/name" on the command line, or nullptr. The
    // original scans argv[1..argc-2] (the last argument can only ever be a value).
    const char* CommandLineArg(const char* name);

    // A client DBC container (the exe's own WowClientDB): rows at +0x20, max +0x0C, min +0x10.
    const uint8_t* ClientDbcRow(uint32_t container, uint32_t id);
    const uint32_t kItemDbc = 0xAD3D4C;     // FUN_100b1870
    const uint32_t kItemSetDbc = 0xAD3EFC;  // FUN_100b19c0
}
