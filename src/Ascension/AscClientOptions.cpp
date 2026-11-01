// The attach init's (FUN_10a66100) option CVars and the hooks that read them (installer FUN_10a66d40
// and the attach init itself). Callback byte strings come from running each original callback under
// tools/emulate_dll_fn.py, checked against its decompile.
//
//   0x56C500 -> sub_10a429a0   chat-bubble height over nameplates (float 0x10BCCB9C, chatBubblesNameplate)
//   0x744150 -> sub_10a44df0   selection circle turned with the unit (ObjectSelectionCircleTexture 2/3)
//   0x720330 -> sub_10a462f0   selection circle radius from both units' sizes (ObjectSelectionCircleMode)
//   0x71F990 -> FUN_10a468f0   "AbsorbGetHit" sound on hit type 10 (absorbSoundOnSpellHit)
//   0x722340 -> sub_10a468b0   melee combat-log lines off (CombatLogMelee)
//   0x606330 -> sub_10a42930   camera shake off (cameraShake)
//   0x5873E0 -> FUN_10a44c90   TRADE_REQUEST instead of the stock trade prompt (autoAcceptTrades 0)
#include <Ascension/AscClientOptions.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscScript.hpp>
#include <Client/CVar.hpp>
#include <Windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace AscScript;

namespace
{
    void Patch(uint32_t addr, const void* bytes, size_t n)
    {
#if ASC_X64
        AscX64::WriteMem(addr, bytes, n, __FUNCTION__);
        return;
#endif
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), n, PAGE_EXECUTE_READWRITE, &old))
            return;
        memcpy(reinterpret_cast<void*>(addr), bytes, n);
        VirtualProtect(reinterpret_cast<void*>(addr), n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), n);
    }
    template <typename T> void PatchValue(uint32_t addr, T v) { Patch(addr, &v, sizeof(T)); }
    template <size_t N> void PatchBytes(uint32_t addr, const uint8_t (&b)[N]) { Patch(addr, b, N); }
    bool IsOne(const char* v) { return strcmp(v, "1") == 0; }

    const uint8_t* Var(const char* name) { return reinterpret_cast<const uint8_t*>(CVar::Lookup(name)); }
    float VarF(const uint8_t* v) { return *reinterpret_cast<const float*>(v + ASC_OFF(0x2C, 0x50)); }
    int32_t VarI(const uint8_t* v) { return *reinterpret_cast<const int32_t*>(v + ASC_OFF(0x30, 0x54)); }

    // ---- chat bubbles over nameplates --------------------------------------------------------------
    float g_bubbleHeight = 0.7f;        // DAT_10bccb9c, read by the client through 0x56C76B

    // FUN_10a48c10: "1" lets bubbles follow nameplates (0x720020 NOPed, 0x725737 jmp); the height read at
    // 0x56C76B always points at our float.
    int __cdecl OnChatBubblesNameplate(void*, const char*, const char* value, void*)
    {
        static const uint8_t kJne[] = {0x0F, 0x85, 0x01, 0x01, 0x00, 0x00}, kNop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        // CGUnit_C::ChatBubbleShow: jne [+0x1470] (0F 85 3C 01 00 00); NamePlateAddOrUpdate: je (74) <-> jmp
        static const uint8_t kJneX64[] = {0x0F, 0x85, 0x3C, 0x01, 0x00, 0x00}, kJe[] = {0x74}, kJmp[] = {0xEB};
        AscX64::WriteCodeX64(0x720020, on ? kNop : kJneX64, 6, on ? kJneX64 : kNop);
        *static_cast<float*>(ASC_PTR(0x56C76B)) = g_bubbleHeight;   // patch 20: g_coaChatBubbleHeight (valor, nao ponteiro)
        AscX64::WriteCodeX64(0x725737, on ? kJmp : kJe, 1, on ? kJe : kJmp);
#else
        PatchBytes(0x720020, on ? kNop : kJne);
        PatchValue<float*>(0x56C76B, &g_bubbleHeight);
        PatchValue<uint8_t>(0x725737, on ? 0xEB : 0x74);
#endif
        return 1;
    }

    // sub_10a429a0 over 0x56C500 (__thiscall(this, a, unit), ret 8): the bubble height before the original.
    // The player's own bubble with a personal nameplate: 1.25 at position 1, else 0.7. Anyone else: the
    // nameplate offset + 1.0 while the unit has a plate, else 0.7. Option off: 0.7.
    typedef int(__fastcall* Bubble_t)(void* ASC_EDX_P(void*), uintptr_t, uint8_t*);
    Bubble_t g_bubble = nullptr;
    int __fastcall BubbleDetour(void* ecx ASC_EDX_P(void* edx), uintptr_t a, uint8_t* unit)
    {
        const uint8_t* cvar = Var("chatBubblesNameplate");
        if (cvar && VarI(cvar) == 1)
        {
            const uint8_t* personal = Var("nameplateShowPersonal");
            if (reinterpret_cast<bool(__thiscall*)(void*)>(ASC_PTR(0x4CEE50))(unit) && personal && VarI(personal) == 1)
            {
                const uint8_t* pos = Var("nameplatePersonalPosition");
                g_bubbleHeight = (pos && VarI(pos) == 1) ? 1.25f : 0.7f;
            }
            else
            {
                const uint8_t* offset = Var("nameplateVerticalOffset");
                const float o = offset ? VarF(offset) : 0.7f;
                g_bubbleHeight = *reinterpret_cast<const uintptr_t*>(unit + ASC_OFF(0xC38, 0x1470)) != 0 ? o + 1.0f : 0.7f;
            }
        }
        else
            g_bubbleHeight = 0.7f;
#if ASC_X64
        *static_cast<float*>(ASC_PTR(0x56C76B)) = g_bubbleHeight;   // g_coaChatBubbleHeight
#endif
        return g_bubble(ecx ASC_EDX_A(edx), a, unit);
    }

    // ---- selection circle ----------------------------------------------------------------------------
    // FUN_10a46fa0: the circle texture at 0x746147 ("1" no fill, "2" arrow, "3" arrow without fill, else
    // the stock texture), then reloaded through 0x7460C0.
    const char* g_circleTexture = "Textures\\UnitSelectTexture.blp";   // PTR_DAT_10bccc20
    int __cdecl OnCircleTexture(void*, const char*, const char* value, void*)
    {
        if (IsOne(value))
            g_circleTexture = "Textures\\UnitSelectTextureNoFill.blp";
        else if (strcmp(value, "2") == 0)
            g_circleTexture = "Textures\\UnitSelectTextureArrow.blp";
        else if (strcmp(value, "3") == 0)
            g_circleTexture = "Textures\\UnitSelectTextureArrowNoFill.blp";
        else
            g_circleTexture = "Textures\\UnitSelectTexture.blp";
#if ASC_X64
        *static_cast<const char**>(ASC_PTR(0x746147)) = g_circleTexture;   // patch 20: g_coaSelectionTexture
#else
        PatchValue<const char*>(0x746147, g_circleTexture);
#endif
        reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x7460C0))();
        return 1;
    }

    // FUN_102bd3f0: m = Rz(angle) * m (row-major 4x4, FUN_102bcea0).
    void Rotate(float* m, float angle)
    {
        const float c = static_cast<float>(cos(static_cast<double>(angle)));
        const float s = static_cast<float>(sin(static_cast<double>(angle)));
        const float r[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float out[16];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[i * 4 + j] = r[i * 4 + 1] * m[4 + j] + r[i * 4] * m[j] + m[8 + j] * r[i * 4 + 2] + m[12 + j] * r[i * 4 + 3];
        memcpy(m, out, sizeof(out));
    }

    // sub_10a44df0 over 0x744150 (__thiscall(unit, matrix, b), ret 8): with an arrow texture (2 or 3) the
    // matrix is turned by the unit's facing (vtable +0x94) and a half turn, in place of the original.
    // x86: __thiscall(unit, matrix, pos); x64: static __stdcall BuildSelectionRotMatrix(C44Matrix*, const C3Vector*) -- sem unit
#if ASC_X64
    typedef int (*Circle_t)(float*, const void*);
    Circle_t g_circle = nullptr;
    // TODO x64 (cliente): a seta que gira com a unidade precisa da unidade, que o x64 nao passa -- patch no fonte.
    int CircleDetour(float* m, const void* b) { return g_circle(m, b); }
#else
    typedef int(__fastcall* Circle_t)(void* ASC_EDX_P(void*), float*, const void*);
    Circle_t g_circle = nullptr;
    int __fastcall CircleDetour(void* ecx ASC_EDX_P(void* edx), float* m, const void* b)
    {
        uint8_t* unit = static_cast<uint8_t*>(ecx);
        const uint8_t* cvar = Var("ObjectSelectionCircleTexture");
        if (*reinterpret_cast<const uint32_t*>(unit + 8) != 0 && cvar && (VarI(cvar) == 2 || VarI(cvar) == 3))
        {
            void** vt = *reinterpret_cast<void***>(unit);
            const float facing = reinterpret_cast<float(__thiscall*)(void*)>(vt[0x94 / 4])(unit);
            Rotate(m, -facing);
            Rotate(m, 3.14159274f);
            return 0;
        }
        return g_circle(ecx ASC_EDX_A(edx), m, b);
    }
#endif

    // sub_10a462f0 over 0x720330 (__thiscall(unit)): unless the option is 0, the circle radius (+0xB0C) is
    // both units' sizes (descriptor +0x108) + 4/3, at least 5, in place of the original.
    typedef void(__fastcall* Radius_t)(void* ASC_EDX_P(void*));
    Radius_t g_radius = nullptr;
    void __fastcall RadiusDetour(void* ecx ASC_EDX_P(void* edx))
    {
        const uint8_t* cvar = Var("ObjectSelectionCircleMode");
        if (cvar && VarI(cvar) == 0)
        {
            g_radius(ecx ASC_EDX_A(edx));
            return;
        }
        uint8_t* unit = static_cast<uint8_t*>(ecx);
        float r = 5.0f;
        if (uint8_t* player = ActivePlayer())
        {
            const float sum = *reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(player + ASC_OFF(8, 0x10)) + 0x108) +
                              *reinterpret_cast<const float*>(*reinterpret_cast<uint8_t* const*>(unit + ASC_OFF(8, 0x10)) + 0x108) + 1.33333337f;
            r = sum > 5.0f ? sum : 5.0f;
        }
        *reinterpret_cast<float*>(unit + ASC_OFF(0xB0C, 0x1324)) = r;
    }
    // sub_10a3ab20: every unit's radius recomputed through the (hooked) 0x720330.
    int __cdecl RecomputeRadius(uint32_t lo, uint32_t hi, void*)
    {
        if (void* unit = ObjectPtr((static_cast<uint64_t>(hi) << 32) | lo, 8))
            reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x720330))(unit);
        return 1;
    }
    // sub_10a46f60: in world, the value is stored first so the recompute sees it.
    int __cdecl OnCircleMode(uint8_t* cvar, const char*, const char* value, void*)
    {
        if (*reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)) != 0)
        {
            *reinterpret_cast<int32_t*>(cvar + ASC_OFF(0x30, 0x54)) = atoi(value);   // CVar::m_intValue
            AscScript::EnumObjects(&RecomputeRadius, nullptr);
        }
        return 1;
    }

    // ---- absorb sound (FUN_10a468f0 over 0x71F990, __thiscall(unit, a, type, c, d), ret 0x10) ----------
    typedef int(__fastcall* Hit_t)(void* ASC_EDX_P(void*), uintptr_t, uint32_t, uintptr_t, uint32_t);
    Hit_t g_hit = nullptr;
    int __fastcall HitDetour(void* ecx ASC_EDX_P(void* edx), uintptr_t a, uint32_t type, uintptr_t c, uint32_t d)
    {
        const int r = g_hit(ecx ASC_EDX_A(edx), a, type, c, d);
        const uint8_t* cvar = Var("absorbSoundOnSpellHit");
        if (type == 10 && cvar && VarI(cvar) == 1)
        {
            float pos[3];
            void** vt = *static_cast<void***>(ecx);
            reinterpret_cast<void(__thiscall*)(void*, float*)>(vt[0x2C / 4])(ecx, pos);
            pos[2] = static_cast<float>(static_cast<double>(pos[2]) + 2.0);
            reinterpret_cast<int(__cdecl*)(const char*, const float*, void*, void*)>(ASC_PTR(0x4C74A0))("(DONOTRENAME)AbsorbGetHit", pos, nullptr, nullptr);
        }
        return r;
    }

    // ---- melee combat log (sub_10a468b0 over 0x722340, __thiscall + 6 arguments, ret 0x18) --------------
    typedef int(__fastcall* Log_t)(void* ASC_EDX_P(void*), uintptr_t, uint32_t, uintptr_t, uint32_t, uint32_t, uint32_t);
    Log_t g_log = nullptr;
    int __fastcall LogDetour(void* ecx ASC_EDX_P(void* edx), uintptr_t a, uint32_t b, uintptr_t c, uint32_t d, uint32_t e, uint32_t f)
    {
        const uint8_t* cvar = Var("CombatLogMelee");
        if (c != 0 || (cvar && VarI(cvar) != 0))
            return g_log(ecx ASC_EDX_A(edx), a, b, c, d, e, f);
        return 0;
    }

    // ---- camera shake (sub_10a42930 over 0x606330, __thiscall + 8 arguments, ret 0x20) -------------------
    // AddShake(const C3Vector&, int, CGCameraDir, float x5)
    typedef int(__fastcall* Shake_t)(void* ASC_EDX_P(void*), const void*, uint32_t, uint32_t, float, float, float, float, float);
    Shake_t g_shake = nullptr;
    int __fastcall ShakeDetour(void* ecx ASC_EDX_P(void* edx), const void* a, uint32_t b, uint32_t c, float d, float e, float f, float g, float h)
    {
        const uint8_t* cvar = Var("cameraShake");
        if (!cvar || VarI(cvar) != 0)
            return g_shake(ecx ASC_EDX_A(edx), a, b, c, d, e, f, g, h);
        return 0;
    }

    // ---- trade prompt (FUN_10a44c90 over 0x5873E0, __cdecl(state, b, c, d)) ------------------------------
    // State 1 with autoAcceptTrades off: TRADE_REQUEST(name of the trader at 0xCA0FE8, vtable +0xD8)
    // replaces the stock prompt; no trader leaves it to the original.
    typedef int(__cdecl* Trade_t)(int32_t, uint32_t, uint32_t, uint32_t, uint32_t);   // HandleTradeMessage(status, a, b, c, uchar)
    Trade_t g_trade = nullptr;
    int __cdecl TradeDetour(int32_t state, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
    {
        const uint8_t* cvar = Var("autoAcceptTrades");
        if (state == 1 && cvar && VarI(cvar) != 1)
            if (void* trader = ObjectPtr(*reinterpret_cast<const uint64_t*>(ASC_PTR(0xCA0FE8)), 0x10))
            {
                void** vt = *static_cast<void***>(trader);
                const char* name = reinterpret_cast<const char*(__thiscall*)(void*)>(vt[0xD8 / 4])(trader);
                AscRuntime::Signal("TRADE_REQUEST", "%s", name);
                return 0;
            }
        return g_trade(state, b, c, d, e);
    }

    // ---- callback-only options -----------------------------------------------------------------------
    // FUN_10a47b70: combat-text sizes (11 styles at 0xAF4748, +0x10/+0x14 per 0x1C, and 0xAF487C) are the
    // stock values times the scale clamped to 0.25..3.
    const float kTextSize[11][2] = {
        {0.018333f, 0.018333f}, {0.018333f, 0.018333f}, {0.0f, 0.0275f}, {0.018333f, 0.018333f}, {0.018333f, 0.018333f},
        {0.018333f, 0.018333f}, {0.018333f, 0.018333f}, {0.0f, 0.0275f}, {0.0275f, 0.0275f}, {0.0275f, 0.0275f}, {0.018333f, 0.018333f}};
    const float kTextOther[11] = {0.002f, 0.002f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    int __cdecl OnWorldTextScale(void*, const char*, const char* value, void*)
    {
        const float x = static_cast<float>(atof(value));
        const float lo = 0.25f <= x ? x : 0.25f;
        const float s = x <= 3.0f ? lo : 3.0f;
        for (uint32_t i = 0; i < 11; ++i)
        {
#if ASC_X64
            // s_worldTextTypes (WORLDTEXTTYPE 0x1C, so campos de 4 bytes: mesmo layout); s_shadowOffset e
            // s_iconPositions nao sao contiguos no x64 -- cada endereco 0xAF487C..0xAF48A4 tem linha no extra-addr.
            float* type = reinterpret_cast<float*>(static_cast<uint8_t*>(ASC_PTR(0xAF4748)) + i * 0x1C + 0x10);
            type[0] = s * kTextSize[i][0];
            type[1] = s * kTextSize[i][1];
            *static_cast<float*>(ASC_PTR(0xAF487C + i * 4)) = s * kTextOther[i];
#else
            PatchValue<float>(0xAF4748 + 0x10 + i * 0x1C, s * kTextSize[i][0]);
            PatchValue<float>(0xAF4748 + 0x14 + i * 0x1C, s * kTextSize[i][1]);
            PatchValue<float>(0xAF487C + i * 4, s * kTextOther[i]);
#endif
        }
        return 1;
    }
    // FUN_10a47c30: "1" casts through assist targets. Quirk kept: other values write 0x89 at 0x80C91E where
    // the stock byte is 0x80.
    int __cdecl OnAutoAssistCast(void*, const char*, const char* value, void*)
    {
        static const uint8_t kA[] = {0x74, 0x6F}, kB[] = {0x74, 0x07}, kC[] = {0x0D, 0x89, 0x00, 0x00, 0x00};
        static const uint8_t kNop2[] = {0x90, 0x90}, kNop5[] = {0x90, 0x90, 0x90, 0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        static const uint8_t kAX64[2] = {0x74, 0x54};   // Spell_C_BeginCast+0x132: test [r14+0x24],0x800; je
        AscX64::WriteCodeX64(0x80C8C1, on ? kNop2 : kAX64, 2, on ? kAX64 : kNop2);
        // Spell_C_CastSpell+0x877: test [rsp+0x74],0x800; je (74 08).
        static const uint8_t kBX64[2] = {0x74, 0x08};
        AscX64::WriteCodeX64(0x80D7C1, on ? kNop2 : kBX64, 2, on ? kBX64 : kNop2);
        // Spell_C_BeginCast+0x17E: or dword [rip+..],0x80 (10 bytes, imm em +6). O quirk 0x89 do x86 e mantido; a
        // 1a escrita parte do stock (0x80), que nao e nenhum dos dois estados.
        static const uint8_t kCStockX64[10] = {0x81, 0x0D, 0x88, 0xBB, 0x77, 0x00, 0x80, 0x00, 0x00, 0x00};
        static const uint8_t kCX64[10] = {0x81, 0x0D, 0x88, 0xBB, 0x77, 0x00, 0x89, 0x00, 0x00, 0x00};
        static const uint8_t kNop10[10] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const uint8_t* cur = static_cast<const uint8_t*>(AscX64::Ptr(0x80C91D));
        const bool stock = cur && memcmp(cur, kCStockX64, 10) == 0;
        AscX64::WriteCodeX64(0x80C91D, on ? kNop10 : kCX64, 10, stock ? kCStockX64 : (on ? kCX64 : kNop10));   // o stock (0x80) vira candidato
#else
        PatchBytes(0x80C8C1, on ? kNop2 : kA);
        PatchBytes(0x80D7C1, on ? kNop2 : kB);
        PatchBytes(0x80C91D, on ? kNop5 : kC);
#endif
        return 1;
    }
    // FUN_10a49850: "1" keeps the stock "??" level for dangerous enemies.
    int __cdecl OnHideDangerousEnemyLevel(void*, const char*, const char* value, void*)
    {
        static const uint8_t kA[] = {0x7E, 0x0B}, kB[] = {0x0F, 0x8E, 0xDD, 0x00, 0x00, 0x00};
        static const uint8_t kNop2[] = {0x90, 0x90}, kNop6[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        static const uint8_t kAX64[2] = {0x7E, 0x0C};   // Script_UnitLevel+0xF4: jle
        AscX64::WriteCodeX64(0x60FAB2, on ? kAX64 : kNop2, 2, on ? kNop2 : kAX64);
        // CGTooltip::SetUnit+0x801: o x64 inverteu o teste (jg 7F 04 -> caminho do IsBossMob); NOP6 do x86 = jmp.
        static const uint8_t kBX64[2] = {0x7F, 0x04}, kJmpX64[2] = {0xEB, 0x04};
        AscX64::WriteCodeX64(0x621766, on ? kBX64 : kJmpX64, 2, on ? kJmpX64 : kBX64);
#else
        PatchBytes(0x60FAB2, on ? kA : kNop2);
        PatchBytes(0x621766, on ? kB : kNop6);
#endif
        return 1;
    }
    // FUN_10a4a200: frame 4 of the client's list (0x6F75B0) gets the scale clamped to 0..5 at +0x10.
    int __cdecl OnLootArtScale(void*, const char*, const char* value, void*)
    {
        const float x = static_cast<float>(atof(value));
        const float lo = 0.0f <= x ? x : 0.0f;
        const float s = x <= 5.0f ? lo : 5.0f;
        if (uint8_t* frame = reinterpret_cast<uint8_t*(__cdecl*)(int)>(ASC_PTR(0x6F75B0))(4))
            *reinterpret_cast<float*>(frame + ASC_OFF(0x10, 0x1C)) = s;   // SpellVisualEffectNameRec::m_scale
        return 1;
    }
    int __cdecl OnTargetName(void*, const char*, const char* value, void*)   // FUN_10a4a270
    {
        static const uint8_t kJe[] = {0x74, 0x42}, kNop[] = {0x90, 0x90};
#if ASC_X64
        static const uint8_t kJeX64[2] = {0x74, 0x3B};   // CGUnit_C::ShouldRenderObjectName+0xD2
        AscX64::WriteCodeX64(0x729D3D, IsOne(value) ? kJeX64 : kNop, 2, IsOne(value) ? kNop : kJeX64);
#else
        PatchBytes(0x729D3D, IsOne(value) ? kJe : kNop);
#endif
        return 1;
    }
    int __cdecl OnSoundEnableFall(void*, const char*, const char* value, void*)   // FUN_10a47780
    {
#if ASC_X64
        static const uint8_t kJle[] = {0x7E}, kJmp[] = {0xEB};   // CGUnit_C::OnCollideFallLand: jle <-> jmp
        AscX64::WriteCodeX64(0x73D46A, IsOne(value) ? kJle : kJmp, 1, IsOne(value) ? kJmp : kJle);
#else
        PatchValue<uint8_t>(0x73D46A, IsOne(value) ? 0x7E : 0xEB);
#endif
        return 1;
    }

    template <class F> void* Cb(F f) { return reinterpret_cast<void*>(f); }
    // In the original's registration order (FUN_10a66100's call sites 0x10A67607 .. 0x10A6B338).
    AscClientOptions::CVarSpec kCVars[] = {
        {"ObjectSelectionCircleTexture", "2", 1, 4, Cb(&OnCircleTexture)},
        {"ObjectSelectionCircleMode", "0", 1, 4, Cb(&OnCircleMode)},
        {"flashWindow", "0", 1, 4, nullptr},             // DAT_10D3D72C
        {"absorbSoundOnSpellHit", "0", 1, 3, nullptr},
        {"CombatLogMelee", "1", 0x21, 3, nullptr},
        {"chatBubblesNameplate", "1", 1, 4, Cb(&OnChatBubblesNameplate)},
        {"holdToCast", "0", 1, 3, nullptr},              // DAT_10D3D9FC
        {"hideDangerousEnemyLevel", "1", 0x21, 4, Cb(&OnHideDangerousEnemyLevel)},
        {"lootArtScale", "1.0", 1, 4, Cb(&OnLootArtScale)},
        {"targetName", "1", 1, 4, Cb(&OnTargetName)},
        {"WorldTextScale", "1.0", 1, 4, Cb(&OnWorldTextScale)},
        {"autoAssistCast", "0", 1, 3, Cb(&OnAutoAssistCast)},
        {"cameraShake", "1", 1, 4, nullptr},
        {"UnitNameGO", "0", 1, 4, nullptr},              // DAT_10D3DA24 (AscAttachFixes)
        {"UnitNameInteractiveNPC", "0", 1, 4, nullptr},  // DAT_10D3DA2C
        {"UnitNameQuestNPC", "1", 1, 4, nullptr},        // DAT_10D3DC30
        {"UnitNameHostileNPC", "1", 1, 4, nullptr},      // DAT_10D3DC34
        {"UnitNameAllNPC", "0", 1, 4, nullptr},          // DAT_10D3DC38
        {"Sound_EnableFall", "1", 1, 7, Cb(&OnSoundEnableFall)},
        {"autoAcceptTrades", "1", 1, 4, nullptr},
    };

    // The client's world CVar registration (0x51D9B0, push ebp / mov ebp,esp / sub esp,0x80: nine bytes);
    // the original drains its queue after it (0x10114090, hook object 0x10BC9900), as here.
    typedef void(__cdecl* WorldCVars_t)();
    WorldCVars_t g_worldCVars = nullptr;
    void __cdecl WorldCVarsDetour()
    {
        g_worldCVars();
        CVar::RegisterWorldQueue();
    }

    // FUN_10a6b890 (after the glue natives, list 0x10BE284C): the client's own UI-scale registration 0x5238A0
    // (uiScale, default patched to "0.85" by AscAttachPatches, and useUiScale "0") runs at glue too.
    void RegisterUiScaleCVars() { reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x5238A0))(); }

    void Init()
    {
        AscBindings::OnGlueRegistered(&RegisterUiScaleCVars);
        typedef void* V;
        g_worldCVars = reinterpret_cast<WorldCVars_t>(AscRuntime::Detour(0x51D9B0, 9, reinterpret_cast<V>(&WorldCVarsDetour)));
        g_bubble = reinterpret_cast<Bubble_t>(AscRuntime::Detour(0x56C500, 6, reinterpret_cast<V>(&BubbleDetour)));
#if ASC_X64
        (void)&CircleDetour;   // patch 02-selection-arrow: CoaBuildSelectionRotMatrix no cliente le a CVar e gira a seta
#else
        g_circle = reinterpret_cast<Circle_t>(AscRuntime::Detour(0x744150, 6, reinterpret_cast<V>(&CircleDetour)));
#endif
        g_radius = reinterpret_cast<Radius_t>(AscRuntime::Detour(0x720330, 6, reinterpret_cast<V>(&RadiusDetour)));
        g_hit = reinterpret_cast<Hit_t>(AscRuntime::Detour(0x71F990, 7, reinterpret_cast<V>(&HitDetour)));
        g_log = reinterpret_cast<Log_t>(AscRuntime::Detour(0x722340, 6, reinterpret_cast<V>(&LogDetour)));
        g_shake = reinterpret_cast<Shake_t>(AscRuntime::Detour(0x606330, 5, reinterpret_cast<V>(&ShakeDetour)));
        g_trade = reinterpret_cast<Trade_t>(AscRuntime::Detour(0x5873E0, 6, reinterpret_cast<V>(&TradeDetour)));
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}

const AscClientOptions::CVarSpec* AscClientOptions::CVars(size_t& count)
{
    count = sizeof(kCVars) / sizeof(kCVars[0]);
    return kCVars;
}

namespace
{
    std::vector<AscClientOptions::CVarSpec>& Queued() { static std::vector<AscClientOptions::CVarSpec> v; return v; }
}

void AscClientOptions::QueueWorldCVar(const CVarSpec& spec) { Queued().push_back(spec); }

void AscClientOptions::SetOutSlot(const char* name, void** slot)
{
    for (CVarSpec& c : kCVars)
        if (strcmp(c.name, name) == 0)
            c.out = slot;
}

const AscClientOptions::CVarSpec* AscClientOptions::QueuedWorldCVars(size_t& count)
{
    count = Queued().size();
    return Queued().data();
}
