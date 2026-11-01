// The client's spell-description formatter, extended (installer FUN_10a11840):
//
//   0x608390  after it: its third argument's 16 bytes become the description context (0x10D3C268; the
//             first 8 are the unit the aura amounts are read for)
//   0x5782D0  the $-token evaluator (token, float stack, spell, ...): Ascension's tokens push one value
//             on the stack (+0x80 is its index, counting down); the rest go to the client
//   0x5797F0  token names: C... is refused; P<n> is whether the player's equipped items or item-set
//             bonuses carry spell n
//   0x57AAF0  before it: "$sc" in the text becomes the spell's school name (+0x284 school mask)
//   0x6238A0  while it runs for a spell with SpellCustomAttr +0x14 0x40, the branch at 0x62442C is a jmp
//             (0x10a12ee0; the stock jne is written back afterwards in every case)
#include <Ascension/AscLog.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscCAMgr.hpp>
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscScript.hpp>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

bool AscAuraAmounts_LastForSpell(uint64_t unit, uint32_t spell, int32_t out[3]);   // AscAuraAmounts.cpp
extern "C" char __cdecl CustomAttr40(uint32_t spell, uint32_t mask);                // below

namespace
{
    uint8_t g_context[16] = {};    // 0x10D3C268
    uint32_t g_formatSpell = 0;    // 0x10D3C27C

    uint32_t U32(const void* p, uint32_t off) { return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(p) + off); }
    int32_t I32(const void* p, uint32_t off) { return *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(p) + off); }
    float F32(const void* p, uint32_t off) { return *reinterpret_cast<const float*>(static_cast<const uint8_t*>(p) + off); }
    const uint8_t* Descriptor(const uint8_t* object) { return *reinterpret_cast<uint8_t* const*>(object + ASC_OFF(8, 0x10)); }

    typedef int(__cdecl* Fn3_t)(uintptr_t, uint32_t, const uint8_t*);   // Script_GetAuraInfo(lua_State*, int, ScriptAuraInfo*)
    // 0x5782D0 is __fastcall: ecx / edx carry the client's own state, then eight stack arguments (ret 0x20).
    typedef void(__fastcall* Token_t)(void* ASC_EDX_P(void*), uint32_t, uint8_t*, const uint8_t*, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t);
    typedef char(__cdecl* Name_t)(const char**, const char*);   // SpellParserParseConditional(cursor, end)
    // SpellParserParseSubText(rec, text, size, 5 x int, const char*, const char**): os 2 ultimos sao ponteiros.
    typedef int(__cdecl* Format_t)(const uint8_t*, char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uintptr_t, uintptr_t);
    Fn3_t g_608390 = nullptr;
    Token_t g_5782D0 = nullptr;
    Name_t g_5797F0 = nullptr;
    Format_t g_57AAF0 = nullptr;

    int __cdecl Hook608390(uintptr_t a, uint32_t b, const uint8_t* context)   // FUN_10a12940
    {
        const int r = g_608390(a, b, context);
        memcpy(g_context, context, sizeof(g_context));
        return r != 0;
    }

    // (level - base) of the player, capped at the spell's max level, times a per-level value.
    bool PerLevel(const uint8_t* rec, uint32_t perLevelOff, float& out)
    {
        const uint8_t* player = AscScript::ActivePlayer();
        if (!player)
            return false;
        const uint32_t base = U32(rec, 0x98), level = U32(Descriptor(player), 0xD8), max = U32(rec, 0x94);
        if (base > level)
            return false;
        const uint32_t levels = (max == 0 || level <= max) ? level - base : max - base;
        out = static_cast<float>(static_cast<double>(levels)) * F32(rec, perLevelOff);
        return true;
    }

    void __fastcall Hook5782D0(void* ecx ASC_EDX_P(void* edx), uint32_t token, uint8_t* stack, const uint8_t* rec, uint32_t a,
                               uint32_t b, uint32_t c, uint32_t d, uint32_t e)   // FUN_10a10c30
    {
        float v = 0.0f;
        bool known = true;
        switch (token)
        {
        case 0x52:
        case 0x53:
        {
            int32_t value = I32(rec, ASC_OFF(0x250, 0x260));   // SpellRec do cliente: m_maxTargets
#if ASC_X64
            reinterpret_cast<bool (*)(const uint8_t*, int32_t*, int32_t)>(ASC_PTR(0x7FDB50))(rec, &value, 0x22);   // (spell, value&, modifier)
#else
            reinterpret_cast<void(__cdecl*)(const uint8_t*, int, int32_t*)>(ASC_PTR(0x7FDB50))(rec, 0x22, &value);
#endif
            v = static_cast<float>(value);
            break;
        }
        case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
        {
            uint64_t unit;
            memcpy(&unit, g_context, 8);
            int32_t amount[3];
            if (AscAuraAmounts_LastForSpell(unit, U32(rec, 0), amount))
            {
                if (token == 0xA2 || token == 0xA5)
                    v = static_cast<float>(amount[0]);
                else if (token == 0xA3 || token == 0xA6)
                    v = static_cast<float>(amount[1]);
                else
                    v = static_cast<float>(amount[2]);
            }
            break;
        }
        case 0xA8: case 0xAB: PerLevel(rec, 0x134, v); break;
        case 0xA9: case 0xAC: PerLevel(rec, 0x138, v); break;
        case 0xAA: case 0xAD: PerLevel(rec, 0x13C, v); break;
        case 0xAE: case 0xAF: case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4:
            if (const uint8_t* player = AscScript::ActivePlayer())
                v = static_cast<float>(static_cast<double>(U32(Descriptor(player), token * 4 - 0x260)));
            break;
        case 0xB5: case 0xB6: case 0xB7: case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            if (const uint8_t* player = AscScript::ActivePlayer())
                v = static_cast<float>(static_cast<double>(U32(Descriptor(player), token * 4 - 0x25C)));
            break;
        case 0xBC:
        case 0xBD:
            if (const uint8_t* player = AscScript::ActivePlayer())
                v = F32(Descriptor(player), 0x1000);
            break;
        case 0xBE: v = static_cast<float>(I32(rec, 0x1C4)); break;
        case 0xBF: v = static_cast<float>(I32(rec, 0x1C8)); break;
        case 0xC0: v = static_cast<float>(I32(rec, 0x1CC)); break;
        case 0xC1:
            if (const uint8_t* player = AscScript::ActivePlayer())
                v = static_cast<float>(static_cast<double>(U32(Descriptor(player), 0x60)));
            break;
        case 0xC2:
            if (const uint8_t* player = AscScript::ActivePlayer())
                v = static_cast<float>(static_cast<double>(U32(Descriptor(player), 0x80)));
            break;
        default:
            known = false;
        }
        if (!known)
        {
            g_5782D0(ecx ASC_EDX_A(edx), token, stack, rec, a, b, c, d, e);
            return;
        }
        int32_t& top = *reinterpret_cast<int32_t*>(stack + 0x80);
        --top;
        *reinterpret_cast<float*>(stack + top * 4) = v;
    }

    // FUN_1008d9c0: how many of an item set's 17 items (+8..+0x48) the player has equipped (19 slots).
    uint32_t EquippedFromSet(const uint8_t* player, const uint8_t* set)
    {
        uint32_t n = 0;
        for (uint32_t off = 8; off < 0x4C; off += 4)
        {
            const uint32_t item = U32(set, off);
            for (uint32_t slot = 0; slot < 0x13; ++slot)
            {
                const uint8_t* d = Descriptor(player);
                const uint8_t* o = static_cast<const uint8_t*>(AscScript::ObjectPtr(AscX64::Guid(U32(d, 0x510 + slot * 8), U32(d, 0x514 + slot * 8)), 2));
                if (o && U32(Descriptor(o), 0xC) == item)
                {
                    ++n;
                    break;
                }
            }
        }
        return n;
    }

    // FUN_10a12260: an equipped item with an on-equip (trigger 1) spell, or an item-set bonus the player
    // has enough pieces for, is `spell`.
    bool PlayerHasSpellBonus(const uint8_t* player, uint32_t spell)
    {
        std::unordered_set<uint32_t> sets;
        for (uint32_t slot = 0; slot < 0x13; ++slot)
        {
            const uint8_t* d = Descriptor(player);
            const uint8_t* item = static_cast<const uint8_t*>(AscScript::ObjectPtr(AscX64::Guid(U32(d, 0x510 + slot * 8), U32(d, 0x514 + slot * 8)), 2));
            if (!item)
                continue;
            // 0x67CA30 is __thiscall on the item cache 0xC5D828 (Ghidra hides the ecx load).
            const uint8_t* proto = reinterpret_cast<const uint8_t*(__thiscall*)(void*, uint32_t, void*, void*, void*, int)>(ASC_PTR(0x67CA30))(
                reinterpret_cast<void*>(ASC_PTR(0xC5D828)), U32(Descriptor(item), 0xC), ASC_NOGUID, nullptr, nullptr, 0);
            if (!proto)
                continue;
            // ItemStats_C: m_itemSet +0x1A8 / x64 +0x1B0, m_spellId[5] +0x100 / +0x104, m_spellTrigger[5] +0x114 / +0x118.
            const uint32_t itemSet = U32(proto, ASC_OFF(0x1A8, 0x1B0));
            if (itemSet)
                sets.insert(itemSet);
            for (uint32_t j = 0; j < 5; ++j)
            {
                const uint32_t s = U32(proto, ASC_OFF(0x100, 0x104) + j * 4);
                if (!s)
                    break;
                if (U32(proto, ASC_OFF(0x114, 0x118) + j * 4) == 1 && s == spell)
                    return true;
            }
        }
        for (uint32_t id : sets)
        {
            const uint8_t* set = AscClientDbc::Row(0xAD3EFC, id);   // ItemSet.dbc (FUN_100b19c0)
            if (!set)
                continue;
            const uint32_t count = EquippedFromSet(player, set);
            if (!count)
                continue;
            for (uint32_t k = 0; k < 8; ++k)
            {
                const uint32_t s = U32(set, 0x4C + k * 4);
                if (!s)
                    break;
                if (U32(set, 0x6C + k * 4) <= count && s == spell)
                    return true;
            }
        }
        return false;
    }

    // FUN_10a136c0. The original copies the digits into a 12-byte stack buffer without terminating it;
    // it is terminated here (IMPROVEMENTS.md).
    char __cdecl Hook5797F0(const char** name, const char* b)
    {
        const char* s = *name;
        switch (*s)
        {
        case 'C':
        case 'c':
            return 0;
        case 'P':
        case 'p':
        {
            char digits[12] = {};
            const size_t len = strlen(s);
            for (uint32_t i = 0; i <= len && i < sizeof(digits) - 1 && s[i + 1] >= '0' && s[i + 1] <= '9'; ++i)
                digits[i] = s[i + 1];
            const int32_t spell = reinterpret_cast<int32_t(__cdecl*)(const char*)>(ASC_PTR(0x76F0D0))(digits);
            const uint8_t* player = AscScript::ActivePlayer();   // the original dereferences it unchecked
            return player && PlayerHasSpellBonus(player, static_cast<uint32_t>(spell)) ? 1 : 0;
        }
        default:
            return g_5797F0(name, b);
        }
    }

    // FUN_10a11310 / FUN_10a11290: the school mask's name, the global string when the client has it.
    std::string SchoolName(uint32_t mask)
    {
        struct School { uint32_t mask; const char* key; const char* text; };
        static const School kSchools[] = {
            {1, "STRING_SCHOOL_PHYSICAL", "Physical"}, {2, "STRING_SCHOOL_HOLY", "Holy"},
            {3, "STRING_SCHOOL_HOLYSTRIKE", "Holystrike"}, {4, "STRING_SCHOOL_FIRE", "Fire"},
            {5, "STRING_SCHOOL_FLAMESTRIKE", "Flamestrike"}, {6, "STRING_SCHOOL_HOLYFIRE", "Holyfire"},
            {8, "STRING_SCHOOL_NATURE", "Nature"}, {9, "STRING_SCHOOL_STORMSTRIKE", "Stormstrike"},
            {10, "STRING_SCHOOL_HOLYSTORM", "Holystorm"}, {0xC, "STRING_SCHOOL_FIRESTORM", "Firestorm"},
            {0x10, "STRING_SCHOOL_FROST", "Frost"}, {0x11, "STRING_SCHOOL_FROSTSTRIKE", "Froststrike"},
            {0x12, "STRING_SCHOOL_HOLYFROST", "Holyfrost"}, {0x14, "STRING_SCHOOL_FROSTFIRE", "Frostfire"},
            {0x18, "STRING_SCHOOL_FROSTSTORM", "Froststorm"}, {0x1C, "STRING_SCHOOL_ELEMENTAL", "Elemental"},
            {0x20, "STRING_SCHOOL_SHADOW", "Shadow"}, {0x21, "STRING_SCHOOL_SHADOWSTRIKE", "Shadowstrike"},
            {0x22, "STRING_SCHOOL_SHADOWLIGHT", "Shadowlight"}, {0x24, "STRING_SCHOOL_SHADOWFLAME", "Shadowflame"},
            {0x28, "STRING_SCHOOL_SHADOWSTORM", "Shadowstorm"}, {0x30, "STRING_SCHOOL_SHADOWFROST", "Shadowfrost"},
            {0x40, "STRING_SCHOOL_ARCANE", "Arcane"}, {0x41, "STRING_SCHOOL_SPELLSTRIKE", "Spellstrike"},
            {0x42, "STRING_SCHOOL_DIVINE", "Divine"}, {0x44, "STRING_SCHOOL_SPELLFIRE", "Spellfire"},
            {0x48, "STRING_SCHOOL_SPELLSTORM", "Spellstorm"}, {0x50, "STRING_SCHOOL_SPELLFROST", "Spellfrost"},
            {0x60, "STRING_SCHOOL_SPELLSHADOW", "Spellshadow"}, {0x7C, "STRING_SCHOOL_CHROMATIC", "Chromatic"},
            {0x7E, "STRING_SCHOOL_MAGIC", "Magic"}, {0x7F, "STRING_SCHOOL_CHAOS", "Chaos"},
        };
        if (mask == 0)
            return std::string();
        const char* key = "STRING_SCHOOL_UNKNOWN";
        const char* text = "Unknown";
        for (const School& sc : kSchools)
            if (sc.mask == mask)
            {
                key = sc.key;
                text = sc.text;
                break;
            }
        const char* global = reinterpret_cast<const char*(__cdecl*)(const char*, int, int)>(ASC_PTR(0x819D40))(key, -1, 0);
        return global ? global : text;
    }

    void ReplaceSchool(char* text, uint32_t size, const uint8_t* rec)   // FUN_10a12780
    {
        if (!text || !rec)
            return;
        std::string s(text);
        bool replaced = false;
        size_t from = 0;
        for (;;)
        {
            const size_t at = s.size() < 3 || s.size() - 3 < from ? std::string::npos : s.find("$sc", from);
            if (at == std::string::npos)
                break;
            const std::string name = SchoolName(U32(rec, ASC_OFF(0x284, 0x294)));   // SpellRec do cliente: m_schoolMask
            s.replace(at, 3, name);
            from = at + name.size();
            replaced = true;
        }
        if (replaced)
            _snprintf_s(text, size, _TRUNCATE, "%s", s.c_str());
    }

    int __cdecl Hook57AAF0(const uint8_t* rec, char* text, uint32_t size, uint32_t d, uint32_t e, uint32_t f,
                           uint32_t g, uint32_t h, uintptr_t i, uintptr_t j)   // FUN_10a137c0
    {
        ReplaceSchool(text, size, rec);
        return g_57AAF0(rec, text, size, d, e, f, g, h, i, j);
    }

    void Patch62442C(bool jump)   // FUN_10a12ee0: jmp +0x215 (E9 15 02 00 00) or the stock jne +0x214
    {
        static const uint8_t kJump[] = {0xE9, 0x15, 0x02, 0x00, 0x00};
        static const uint8_t kStock[] = {0x0F, 0x85, 0x14, 0x02, 0x00, 0x00};
        const uint8_t* bytes = jump ? kJump : kStock;
        const size_t n = jump ? sizeof(kJump) : sizeof(kStock);
#if ASC_X64
        // CGTooltip::SetSpell+0x431: jne (75 32) -> jmp (EB 32); pula o bloco inteiro do tempo de cast.
        static const uint8_t kJumpX64[2] = {0xEB, 0x32}, kStockX64[2] = {0x75, 0x32};
        (void)bytes; (void)n;
        AscX64::WriteCodeX64(0x62442C, jump ? kJumpX64 : kStockX64, 2, jump ? kStockX64 : kJumpX64);
        return;
#endif
        DWORD old;
        const BOOL ok = VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0x62442C)), n, PAGE_EXECUTE_READWRITE, &old);
        memcpy(reinterpret_cast<void*>(ASC_PTR(0x62442C)), bytes, n);
        if (ok)
            VirtualProtect(reinterpret_cast<void*>(ASC_PTR(0x62442C)), n, old, &old);
    }

    // 0x6238A0 begins push ebp / mov ebp,esp / mov eax,0x2088 / call 0x40BB50 (the stack probe): the
    // call is re-made with its return address pushed by hand, then 0x6238AD.
#if !ASC_X64
    __declspec(naked) int __fastcall Original6238A0()
    {
        __asm
        {
            push ebp
            mov ebp, esp
            mov eax, 0x2088
            push 0x6238AD
            push 0x40BB50
            ret
        }
    }
#else
    // x64: o trampolim do MinHook substitui o prologo refeito (o x64 nao tem o probe 0x40BB50 no mesmo lugar).
    // int CGTooltip::SetSpell(int spell, int, unsigned, int, int, int, int, const TalentRec*, CGTalentGroup_C*,
    // int x6) (__thiscall), assinatura do protos.json.
    typedef int (*SetSpell_t)(void*, int, int, uint32_t, int, int, int, int, const void*, void*, int, int, int, int,
                              int, int);
    SetSpell_t g_6238A0 = nullptr;
#endif

    // FUN_10a11130: __fastcall with fifteen stack arguments (ret 0x3C); the first is the spell.
#if !ASC_X64
    __declspec(naked) int __fastcall Hook6238A0()
    {
        __asm
        {
            push ebx
            push esi
            push edi
            mov ebx, ecx
            mov edi, edx
            mov esi, dword ptr [esp + 0x10]      // the spell
            mov dword ptr [g_formatSpell], esi
            push 0x40
            push esi
            call CustomAttr40
            add esp, 8
            test al, al
            je nopatch
            push 1
            call Patch62442C
            add esp, 4
        nopatch:
            // re-push the fifteen arguments and call the original with ecx / edx intact
            mov ecx, 15
        again:
            push dword ptr [esp + 0x48]
            loop again
            mov ecx, ebx
            mov edx, edi
            call Original6238A0
            mov esi, eax
            push 0
            call Patch62442C
            add esp, 4
            mov dword ptr [g_formatSpell], 0
            mov eax, esi
            pop edi
            pop esi
            pop ebx
            ret 0x3C
        }
    }
#else
    // x64: mesma logica do naked: g_formatSpell e o salto 0x62442C valem so durante o original.
    int Hook6238A0(void* self, int spell, int a2, uint32_t a3, int a4, int a5, int a6, int a7, const void* talent,
                   void* group, int a10, int a11, int a12, int a13, int a14, int a15)
    {
        g_formatSpell = static_cast<uint32_t>(spell);
        if (CustomAttr40(static_cast<uint32_t>(spell), 0x40))
            Patch62442C(true);
        const int r = g_6238A0(self, spell, a2, a3, a4, a5, a6, a7, talent, group, a10, a11, a12, a13, a14, a15);
        Patch62442C(false);
        g_formatSpell = 0;
        return r;
    }
#endif

    template <class T> T Hook(uint32_t target, uint32_t prologue, void* fn)
    {
        return reinterpret_cast<T>(AscRuntime::Detour(target, prologue, fn));
    }

    // The variable-name table (0x10D3C2B0): the client's 0x8C names (0xACE8F8) then 36 more, which the
    // parser at 0x576B5x now searches (0x576B63 = table, 0x576B7C = count 0xB0). ppl1..3 appear twice
    // in the original; the second copies are unreachable.
    const char* g_varNames[0xB0] = {};
    const char* const kNewVarNames[0x24] = {
        "w1", "w2", "w3", "W1", "W2", "W3", "ppl1", "ppl2", "ppl3", "PPL1", "PPL2", "PPL3", "ppl1", "ppl2", "ppl3",
        "power1", "power2", "power3", "power4", "power5", "power6", "power7",
        "POWER1", "POWER2", "POWER3", "POWER4", "POWER5", "POWER6", "POWER7",
        "br", "BR", "r1", "r2", "r3", "health", "HEALTH",
    };
    const char kTokenChars[] = "sSaApP";   // 0x10B5DBC8, replacing "sSaA" at 0x579852

#if ASC_X64
    // x64: os 3 operandos viraram variaveis do cliente (patch de cliente "spell-tokens"): 0x579852
    // g_coaSpellConditionChars (const char*), 0x576B63 g_coaMathVariables (const char* const*), 0x576B7C
    // g_coaMathVariableCount (u32).
    template <class T> void WriteVar(uint32_t at, T value) { AscX64::WriteMem(at, &value, sizeof(value), __FUNCTION__); }
#else
    void WriteCode32(uint32_t at, uint32_t value)
    {
        DWORD old;
        const BOOL ok = VirtualProtect(reinterpret_cast<void*>(at), 4, PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<uint32_t*>(at) = value;
        if (ok)
            VirtualProtect(reinterpret_cast<void*>(at), 4, old, &old);
    }
#endif

    void Init()   // FUN_10a11840
    {
        memcpy(g_varNames, reinterpret_cast<const void*>(ASC_PTR(0xACE8F8)), 0x8C * sizeof(const char*));
        memcpy(g_varNames + 0x8C, kNewVarNames, sizeof(kNewVarNames));
#if ASC_X64
        WriteVar<const char*>(0x579852, kTokenChars);
        WriteVar<const char* const*>(0x576B63, g_varNames);   // a tabela antes do limite: nunca 0xB0 sobre a de 140
        WriteVar<uint32_t>(0x576B7C, 0xB0);
#else
        WriteCode32(0x579852, reinterpret_cast<uint32_t>(kTokenChars));
        WriteCode32(0x576B63, reinterpret_cast<uint32_t>(g_varNames));
        WriteCode32(0x576B7C, 0xB0);
#endif
        g_5782D0 =Hook<Token_t>(0x5782D0, 6, reinterpret_cast<void*>(&Hook5782D0));   // hooked 0x5782D0
        g_5797F0 = Hook<Name_t>(0x5797F0, 5, reinterpret_cast<void*>(&Hook5797F0));    // hooked 0x5797F0
        g_57AAF0 = Hook<Format_t>(0x57AAF0, 6, reinterpret_cast<void*>(&Hook57AAF0));  // hooked 0x57AAF0
        g_608390 = Hook<Fn3_t>(0x608390, 6, reinterpret_cast<void*>(&Hook608390));     // hooked 0x608390
#if ASC_X64
        g_6238A0 = Hook<SetSpell_t>(0x6238A0, 0, reinterpret_cast<void*>(&Hook6238A0));
#else
        AscRuntime::ReplaceFunction(0x6238A0, reinterpret_cast<void*>(&Hook6238A0));
#endif
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}

// FUN_10324a10 with 0x40, for the naked hook above.
extern "C" char __cdecl CustomAttr40(uint32_t spell, uint32_t mask)
{
    const uint8_t* row = AscCA::SpellCustomAttrRow(spell);
    return row && (*reinterpret_cast<const uint32_t*>(row + 0x14) & mask) ? 1 : 0;
}
