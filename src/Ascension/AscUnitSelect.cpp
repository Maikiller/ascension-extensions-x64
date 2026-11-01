// The unit-select module (installer FUN_102daaf0): AoE radius indicators, quest-unit circles, the
// player's own selection circle and auto-interact.
//
// Done here:
//   CVars (FUN_10114540, flags 1): aoeRadiusIndicatorSelf / Enemy / Friendly "0" (category 3),
//     showQuestUnitCircles "1" and ObjectSelectionCircleSelf "0" (category 4), AutoInteractTurnSpeed "3.15"
//     and AutoInteractCursorTime "0" (category 3).
//   World entry (FUN_102da1c0): ConeSplat_Static_30d_30yd.blp / _90d_30yd.blp into 0x10BE3858 / 0x10BE385C.
//   0x7460C0 (FUN_102da2b0): after it, UnitSelectTexture.blp / TrackingTexture.blp /
//     UnitSelectTexture-Quest.blp into 0x10BE384C / 850 / 854.
//   0x72B7E0 (a `jmp 0x7275C0` thunk; FUN_102da6c0): skipped entirely once AutoInteractCursorTime (ms, when
//     set) has passed since 0x10BE386C; otherwise run with the texture operand at 0x744EB1 pointing at the
//     tracking texture (&0x10BE3850), then the stock operand (0xCA12C4) back.
//   0x727400 (click-to-move, __fastcall + 4 stack arguments, ret 0x10; FUN_102da340): a call whose return
//     address lies past the exe image (> 0xDFCBFF) first sends CMSG 0x51F with three strings, "Ascension",
//     "CTM", "Click-to-Move called by bot." (XOR-decoded at run time in the original). After the original,
//     AutoInteractTurnSpeed's value (+0x2C) is copied into 0xCA11DC and 0x10BE386C = 0x86AE20().
//   World render (after 0x4F6F90; FUN_102dbf60): every visible dynamic object (FUN_102db9c0) and unit
//     (FUN_102db140) gets its AoE indicator; with showQuestUnitCircles the quest-entry cache 0x10BE3860 is
//     cleared and every unit gets a quest circle (FUN_102dbfe0); with ObjectSelectionCircleSelf == 1 the
//     active player's vtable +0x6C (its own selection circle) runs.
//   Each drawer points the texture operand at 0x744EB1 at its texture, draws the box through 0x7E4370
//     between 0x409670 / 0x685FB0 on the GX device (*0xC5DF88), then puts the stock operand back.
//   Every other 0x6DF050 call (FUN_102db0d0): outside a dungeon / raid (Map.dbc +8 not 1 / 2) the float
//     operand at 0x82080B points at 10.0 (0x10BCBF80); inside one, with any aoeRadiusIndicator* == 1, it is
//     put back to 0xA4040C (FUN_102dad10). The installer applies the 10.0 once at start-up.
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/AscCAMgr.hpp>
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscClientOptions.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscScript.hpp>
#include <Client/CVar.hpp>
#include <Windows.h>
#include <intrin.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

bool AscQuest_CreatureRelevant(const uint8_t* player, uint32_t entry);   // AscAttachFixes.cpp, FUN_1008de00
void AscSpellShadow_RotateBy(float* m, float angle);                       // AscSpellShadow.cpp, FUN_102bd3f0

namespace
{
    void* g_unitSelect = nullptr;   // 0x10BE384C
    void* g_tracking = nullptr;     // 0x10BE3850
    void* g_questSelect = nullptr;  // 0x10BE3854
    void* g_cone30 = nullptr;       // 0x10BE3858
    void* g_cone90 = nullptr;       // 0x10BE385C
    uint32_t g_lastInteract = 0;    // 0x10BE386C

    typedef void*(__cdecl* Load_t)(const char*, uint32_t, void*, int);
    void* Load(const char* path, void* status) { return reinterpret_cast<Load_t>(ASC_PTR(0x4B9760))(path, 0x201, status, 1); }

    void LoadCones()   // FUN_102da1c0
    {
        typedef void(__cdecl* Release_t)(void*);
        if (g_cone30)
            reinterpret_cast<Release_t>(ASC_PTR(0x47BF30))(g_cone30);
        if (g_cone90)
            reinterpret_cast<Release_t>(ASC_PTR(0x47BF30))(g_cone90);
        void* status = reinterpret_cast<void*>(ASC_PTR(0xAF5718));
        g_cone30 = Load("Interface\\SpellShadow\\ConeSplat_Static_30d_30yd.blp", status);
        g_cone90 = Load("Interface\\SpellShadow\\ConeSplat_Static_90d_30yd.blp", status);
    }

    typedef int(__cdecl* Fn0_t)();
    Fn0_t g_7460C0 = nullptr;
    int __cdecl Hook7460C0()   // FUN_102da2b0
    {
        const int r = g_7460C0();
#if ASC_X64
        // the CStatus objects (vtable 0x9F4250) have no x64 address; the global status 0xAF5718 instead
        void* first = ASC_PTR(0xAF5718);
        void* second = first;
#else
        uint32_t first[2] = {0x9F4250, 8}, second[2] = {0x9F4250, 8};
#endif
        g_unitSelect = Load("Textures\\UnitSelectTexture.blp", first);
        g_tracking = Load("Textures\\TrackingTexture.blp", second);
        g_questSelect = Load("Textures\\UnitSelectTexture-Quest.blp", second);
        return r;
    }

    void WriteCode32(uint32_t at, uint32_t value)
    {
#if ASC_X64
        AscX64::WriteMem(at, &value, 4, __FUNCTION__);
        return;
#endif
        DWORD old;
        const BOOL ok = VirtualProtect(reinterpret_cast<void*>(at), 4, PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<uint32_t*>(at) = value;
        if (ok)
            VirtualProtect(reinterpret_cast<void*>(at), 4, old, &old);
    }

    // The circle texture SetCircleRenderStates (0x744EB0) binds. x86: its operand at 0x744EB1 is pointed at
    // `texture` and then back at the stock 0xCA12C4. x64: the operand is rip-relative and cannot reach the DLL,
    // so the value at 0xCA12C4 itself is swapped for the draw and put back.
#if ASC_X64
    void* g_stockTexture = nullptr;
#endif
    void UseTexture(void* const* texture)
    {
#if ASC_X64
        void** slot = static_cast<void**>(ASC_PTR(0xCA12C4));
        g_stockTexture = *slot;
        *slot = *texture;
#else
        WriteCode32(0x744EB1, reinterpret_cast<uint32_t>(texture));
#endif
    }
    void StockTexture()
    {
#if ASC_X64
        *static_cast<void**>(ASC_PTR(0xCA12C4)) = g_stockTexture;
#else
        StockTexture();
#endif
    }

    int __fastcall Hook72B7E0(void* self ASC_EDX_P(void* edx))   // FUN_102da6c0
    {
        if (const CVar* time = CVar::Lookup("AutoInteractCursorTime"))   // 0x10BE3874
        {
            const uint32_t ms = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(time) + ASC_OFF(0x30, 0x54));
            const uint32_t now = reinterpret_cast<uint32_t(__cdecl*)()>(ASC_PTR(0x86AE20))();
            if (ms && ms < now - g_lastInteract)
                return 0;
        }
        UseTexture(&g_tracking);
        const int r = reinterpret_cast<int(__fastcall*)(void* ASC_EDX_P(void*))>(ASC_PTR(0x7275C0))(self ASC_EDX_A(edx));   // the thunk's target
        StockTexture();
        return r;
    }

    void SendBotReport()   // CMSG 0x51F
    {
        alignas(8) uint8_t store[ASC_OFF(0x18, 0x20)];   // CDataStore: 0x18 x86, 0x20 x64
        reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x401050))(store);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(ASC_PTR(0x47B0A0))(store, 0x51F);
        typedef void(__thiscall* PutString_t)(void*, const char*);
        reinterpret_cast<PutString_t>(ASC_PTR(0x47B300))(store, "Ascension");
        reinterpret_cast<PutString_t>(ASC_PTR(0x47B300))(store, "CTM");
        reinterpret_cast<PutString_t>(ASC_PTR(0x47B300))(store, "Click-to-Move called by bot.");
        reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x401130))(store);
        void* connection = reinterpret_cast<void*(__cdecl*)()>(ASC_PTR(0x6B0970))();
        reinterpret_cast<void(__thiscall*)(void*, void*)>(ASC_PTR(0x632B50))(connection, store);
        reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x403880))(store);
    }

    typedef char(__fastcall* Fn727400_t)(void* ASC_EDX_P(void*), uint32_t, uintptr_t, uintptr_t, float);
    Fn727400_t g_727400 = nullptr;
    char __fastcall Hook727400(void* self ASC_EDX_P(void* edx), uint32_t a, uintptr_t b, uintptr_t c, float d)   // FUN_102da340
    {
#if ASC_X64
        const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const uintptr_t exeEnd = exe + reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew)->OptionalHeader.SizeOfImage;
        if (reinterpret_cast<uintptr_t>(_ReturnAddress()) >= exeEnd)   // past the exe image, as > 0xDFCBFF on x86
#else
        if (reinterpret_cast<uint32_t>(_ReturnAddress()) > 0xDFCBFF)
#endif
            SendBotReport();
        const char r = g_727400(self ASC_EDX_A(edx), a, b, c, d);
        const CVar* speed = CVar::Lookup("AutoInteractTurnSpeed");   // 0x10BE3870
        *reinterpret_cast<uint32_t*>(ASC_PTR(0xCA11DC)) = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(speed) + ASC_OFF(0x2C, 0x50));
        g_lastInteract = reinterpret_cast<uint32_t(__cdecl*)()>(ASC_PTR(0x86AE20))();
        return r;
    }

    bool CustomAttr14(uint32_t spell, uint32_t mask)   // FUN_10324a10
    {
        const uint8_t* row = AscCA::SpellCustomAttrRow(spell);
        return row && (*reinterpret_cast<const uint32_t*>(row + 0x14) & mask) != 0;
    }

    const float kTen = 10.0f;   // 0x10BCBF80
    void PatchRadius(bool on)   // FUN_102dad10
    {
#if ASC_X64
        // CM2SceneRender::DrawBatchProj: x86 widens the projected box in z by one constant (fld [0xA4040C], 2.0);
        // x64 does `addss xmm0,[rip -> -2.0]` then `addss xmm1,[rip -> 2.0]` (0x820809 = DrawBatchProj+0xC1, 8 bytes
        // each). Their disp32s are repointed at a -10.0 / 10.0 already in the exe's .rdata. Tudo calculado em tempo de
        // execucao (antes eram rvas fixas do cliente velho e o patch nunca conferia): o disp stock e lido na 1a chamada
        // e os dois floats sao procurados na .rdata do proprio exe.
        (void)kTen;
        static int32_t stock[2] = {0, 0}, ten[2] = {0, 0};
        static bool ready = false, ok = false;
        uint8_t* code = static_cast<uint8_t*>(AscX64::Ptr(0x820809));
        if (!ready)
        {
            ready = true;
            const uint8_t addss0[4] = {0xF3, 0x0F, 0x58, 0x05}, addss1[4] = {0xF3, 0x0F, 0x58, 0x0D};
            if (code && memcmp(code, addss0, 4) == 0 && memcmp(code + 8, addss1, 4) == 0)
            {
                memcpy(&stock[0], code + 4, 4);
                memcpy(&stock[1], code + 0xC, 4);
                const uint8_t* const end0 = code + 8;
                const uint8_t* const end1 = code + 0x10;
                const float* cur0 = reinterpret_cast<const float*>(end0 + stock[0]);
                const float* cur1 = reinterpret_cast<const float*>(end1 + stock[1]);
                const float* minusTen = nullptr;
                const float* plusTen = nullptr;
                if (*cur0 == -2.0f && *cur1 == 2.0f)
                {
                    uint8_t* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
                    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
                    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
                    for (WORD i = 0; i < nt->FileHeader.NumberOfSections && (!minusTen || !plusTen); ++i, ++sec)
                    {
                        if (memcmp(sec->Name, ".rdata", 6) != 0)
                            continue;
                        const float* f = reinterpret_cast<const float*>(base + sec->VirtualAddress);
                        const size_t n = sec->Misc.VirtualSize / sizeof(float);
                        for (size_t k = 0; k < n && (!minusTen || !plusTen); ++k)
                        {
                            if (!minusTen && f[k] == -10.0f)
                                minusTen = f + k;
                            else if (!plusTen && f[k] == 10.0f)
                                plusTen = f + k;
                        }
                    }
                }
                const intptr_t d0 = minusTen ? reinterpret_cast<const uint8_t*>(minusTen) - end0 : 0;
                const intptr_t d1 = plusTen ? reinterpret_cast<const uint8_t*>(plusTen) - end1 : 0;
                if (minusTen && plusTen && d0 == static_cast<int32_t>(d0) && d1 == static_cast<int32_t>(d1))
                {
                    ten[0] = static_cast<int32_t>(d0);
                    ten[1] = static_cast<int32_t>(d1);
                    ok = true;
                }
            }
            if (!ok)
                AscLog::Printf("x64: AscUnitSelect PatchRadius sem alvo (DrawBatchProj+0xC1 ou -10.0/10.0 na .rdata) -- ignorado");
        }
        if (!ok)
            return;
        const int32_t* now = on ? ten : stock;
        const int32_t* was = on ? stock : ten;
        AscX64::WriteCodeX64(0x820809, &now[0], 4, &was[0], 4);
        AscX64::WriteCodeX64(0x820809, &now[1], 4, &was[1], 0xC);
        return;
#endif
        WriteCode32(0x82080B, on ? reinterpret_cast<uint32_t>(&kTen) : 0xA4040C);
    }

    // Output slots the world registration refills each time (0x10114090).
    int32_t CVarValue(const void* cvar)
    {
        return cvar ? *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(cvar) + ASC_OFF(0x30, 0x54)) : 0;
    }
    const CVar* g_self = nullptr;       // 0x10BE3838
    const CVar* g_enemy = nullptr;      // 0x10BE383C
    const CVar* g_friendly = nullptr;   // 0x10BE3840
    const CVar* g_quest = nullptr;      // 0x10BE3844
    const CVar* g_circle = nullptr;     // 0x10BE3848

    void Tick()   // FUN_102db0d0
    {
        const uint8_t* map = AscClientDbc::Row(0xAD4160, *reinterpret_cast<const uint32_t*>(ASC_PTR(0xADFBC4)));
        if (!map)
            return;
        const uint32_t type = *reinterpret_cast<const uint32_t*>(map + 8);
        if (type != 1 && type != 2)
        {
            PatchRadius(true);
            return;
        }
        if (CVarValue(g_self) == 1 || CVarValue(g_enemy) == 1 ||
            CVarValue(g_friendly) == 1)
            PatchRadius(false);
    }

    typedef uint8_t*(__cdecl* ObjectPtr_t)(uint32_t, uint32_t, uint32_t);
    uint8_t* ObjectPtr(uint32_t lo, uint32_t hi, uint32_t mask) { return static_cast<uint8_t*>(AscScript::ObjectPtr(AscX64::Guid(lo, hi), mask)); }   // 0x4D4DB0
    const uint8_t* Fields(const uint8_t* object) { return *reinterpret_cast<const uint8_t* const*>(object + ASC_OFF(8, 0x10)); }
    bool SameGuid(const uint8_t* a, const uint8_t* b)
    {
        return reinterpret_cast<const uint32_t*>(a)[0] == reinterpret_cast<const uint32_t*>(b)[0] &&
               reinterpret_cast<const uint32_t*>(a)[1] == reinterpret_cast<const uint32_t*>(b)[1];
    }

    // The indicator colour for an effect of `unit` as the player sees it, or false when its CVar is off
    // (or the unit is neutral): self / enemy (reaction < 3) / friendly (reaction > 3).
    bool IndicatorColour(uint8_t* player, uint8_t* unit, uint32_t& colour)
    {
        colour = *reinterpret_cast<const uint32_t*>(ASC_PTR(0xAD2D3C));
        if (SameGuid(Fields(player), Fields(unit)))
            return CVarValue(g_self) != 0;
        typedef int32_t(__thiscall* Reaction_t)(void*, void*);
        if (reinterpret_cast<Reaction_t>(ASC_PTR(0x7251C0))(player, unit) < 3)
        {
            if (!CVarValue(g_enemy))
                return false;
            colour = *reinterpret_cast<const uint32_t*>(ASC_PTR(0xAD2D48));
            return true;
        }
        if (reinterpret_cast<Reaction_t>(ASC_PTR(0x7251C0))(player, unit) <= 3)
            return false;
        if (!CVarValue(g_friendly))
            return false;
        colour = *reinterpret_cast<const uint32_t*>(ASC_PTR(0xAD2D40));
        return true;
    }

    void Identity(float* m)   // FUN_100d2360
    {
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    void Position(uint8_t* object, float* out)
    {
        reinterpret_cast<void(__thiscall*)(uint8_t*, float*)>((*reinterpret_cast<void***>(object))[0x2C / 4])(object, out);
    }
    // {min x, min y, min z, max x, max y, max z}: r either side in x / y, 2r in z.
    void Box(const float* p, float r, float* box)
    {
        box[3] = p[0] + r;
        box[0] = p[0] - r;
        box[4] = p[1] + r;
        box[1] = p[1] - r;
        box[5] = r + r + p[2];
        box[2] = p[2] - (r + r);
    }
    void* Device() { return *reinterpret_cast<void**>(ASC_PTR(0xC5DF88)); }
    void BeginDraw() { reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x409670))(Device()); }
    void EndDraw() { reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x685FB0))(Device()); }
    void Draw(uint8_t* object, float* box, uint32_t* colour, float* matrix)
    {
#if ASC_X64
        (void)object;
        if (reinterpret_cast<int (*)()>(ASC_PTR(0x744EB0))())   // static CGObject_C::SetCircleRenderStates()
#else
        if (reinterpret_cast<int(__thiscall*)(uint8_t*)>(ASC_PTR(0x744EB0))(object))
#endif
            reinterpret_cast<void(__cdecl*)(float*, uint32_t*, float*, float, uint32_t, uint32_t, float)>(ASC_PTR(0x7E4370))(
                box, colour, matrix, 0.5f, 0x200122, 1, 0.4f);
    }

    // FUN_102db9c0: a dynamic object's AoE -- its radius (+0x28, + 0.5) around it, the unit-select texture.
    void DrawDynamicObject(uint8_t* object)
    {
        uint8_t* player = AscScript::ActivePlayer();
        if (!player)
            return;
        const uint8_t* fields = Fields(object);
        uint8_t* caster = ObjectPtr(*reinterpret_cast<const uint32_t*>(fields + 0x18), *reinterpret_cast<const uint32_t*>(fields + 0x1C), 8);
        if (!caster)
            return;
        const uint32_t spell = *reinterpret_cast<const uint32_t*>(Fields(object) + 0x24);
        if (!spell || CustomAttr14(spell, 0x800))
            return;
        const float r = *reinterpret_cast<const float*>(Fields(object) + 0x28) + 0.5f;
        uint32_t colour;
        if (!IndicatorColour(player, caster, colour))
            return;
        if (*reinterpret_cast<const uint32_t*>(ASC_PTR(0xAC80A8)) == 0)
            return;
        float matrix[16];
        Identity(matrix);
        float p[3], box[6];
        Position(object, p);
        Box(p, r, box);
        UseTexture(&g_unitSelect);
        BeginDraw();
        Draw(object, box, &colour, matrix);
        EndDraw();
        StockTexture();
    }

    // FUN_102db140: a unit casting a cone (an effect whose implicit target A is 24 / 54 / 104 / 59) -- the
    // 90-degree cone texture, 0.725 x the radius, turned to the unit's facing and pushed 0.725 x that ahead.
    void DrawUnit(uint8_t* unit)
    {
        uint8_t* player = AscScript::ActivePlayer();
        if (!player)
            return;
        const uint32_t spell = *reinterpret_cast<const uint32_t*>(unit + ASC_OFF(0xA6C, 0x1268));
        if (!spell)
            return;
        uint8_t rec[0x2A8];
        if (!AscScript::FetchSpell(spell, rec) || CustomAttr14(spell, 0x800))
            return;
        const uint32_t* targetA = reinterpret_cast<const uint32_t*>(rec + 0x158);
        uint32_t i = 0;
        while (targetA[i] != 0x18 && targetA[i] != 0x36 && targetA[i] != 0x68 && targetA[i] != 0x3B)
            if (++i >= 3)
                return;
        void* texture = g_cone90;
        float radius[2] = {0.0f, 0.0f};
        for (uint32_t k = 0; k < 2; ++k)
        {
            // The original reads EffectRadiusIndex[0] (+0x170) both times.
            const uint32_t index = *reinterpret_cast<const uint32_t*>(rec + 0x170);
            const uint8_t* row = index ? AscClientDbc::Row(0xAD4964, index) : nullptr;
            if (!row)
                continue;
            const float level = static_cast<float>(static_cast<double>(*reinterpret_cast<const uint32_t*>(Fields(unit) + 0xD8)));
            const float max = *reinterpret_cast<const float*>(row + 0xC);
            radius[k] = level * *reinterpret_cast<const float*>(row + 8) + *reinterpret_cast<const float*>(row + 4);
            if (radius[k] > max)
                radius[k] = max;
        }
        float r = radius[0] > radius[1] ? radius[0] : radius[1];
        uint32_t colour;
        if (!IndicatorColour(player, unit, colour))
            return;
        // The original tests the pointer 0x10BCBF6C itself here (always set), not *0xAC80A8.
        float matrix[16];
        Identity(matrix);
        float p[3], box[6];
        Position(unit, p);
        r = r * 0.725f;
        Box(p, r, box);
        reinterpret_cast<void(__thiscall*)(float*)>(ASC_PTR(0x407F40))(matrix);
        const float facing = reinterpret_cast<float(__thiscall*)(uint8_t*)>((*reinterpret_cast<void***>(unit))[0x34 / 4])(unit);
        AscSpellShadow_RotateBy(matrix, -facing);
        r = r * 0.725f;
        const float dx = static_cast<float>(cos(static_cast<double>(facing))) * r;
        const float dy = static_cast<float>(sin(static_cast<double>(facing))) * r;
        box[5] = box[5] + 0.0f;
        box[3] = dx + box[3];
        box[0] = dx + box[0];
        box[4] = dy + box[4];
        box[1] = dy + box[1];
        box[2] = box[2] + 0.0f;
        BeginDraw();
        UseTexture(&texture);
        Draw(unit, box, &colour, matrix);
        StockTexture();
        EndDraw();
    }

    std::vector<uint32_t> g_questEntries;   // 0x10BE3860: entries found quest-relevant this frame

    // FUN_102dbfe0: a living, non-pet unit whose entry an unfinished quest still needs -- a circle of its
    // +0xB0C radius in 0x32FFD200 with the quest texture.
    void DrawQuestUnit(uint8_t* unit)
    {
        uint8_t* player = AscScript::ActivePlayer();
        if (!player)
            return;
        const uint8_t* fields = Fields(unit);
        if (*reinterpret_cast<const uint32_t*>(fields + 0x60) == 0 || *reinterpret_cast<const uint32_t*>(fields + 0x12C) != 0)
            return;
        const uint32_t entry = *reinterpret_cast<const uint32_t*>(fields + 0xC);
        if (std::find(g_questEntries.begin(), g_questEntries.end(), entry) == g_questEntries.end() &&
            !AscQuest_CreatureRelevant(player, entry))
            return;
        g_questEntries.push_back(entry);   // pushed again even when already cached
        uint32_t colour = 0x32FFD200;
        const float r = *reinterpret_cast<const float*>(unit + ASC_OFF(0xB0C, 0x1324));
        if (*reinterpret_cast<const uint32_t*>(ASC_PTR(0xAC80A8)) == 0)
            return;
        float matrix[16];
        Identity(matrix);
        float p[3], box[6];
        Position(unit, p);
        Box(p, r, box);
        UseTexture(&g_questSelect);
        BeginDraw();
        Draw(unit, box, &colour, matrix);
        EndDraw();
        StockTexture();
    }

    int __cdecl EnumDynamic(uint32_t lo, uint32_t hi, void*)   // FUN_102da220
    {
        if (uint8_t* object = ObjectPtr(lo, hi, 0x40))
            DrawDynamicObject(object);
        return 1;
    }
    int __cdecl EnumUnit(uint32_t lo, uint32_t hi, void*)   // FUN_102da250
    {
        if (uint8_t* unit = ObjectPtr(lo, hi, 8))
            DrawUnit(unit);
        return 1;
    }
    int __cdecl EnumQuest(uint32_t lo, uint32_t hi, void*)   // FUN_102da280
    {
        if (uint8_t* unit = ObjectPtr(lo, hi, 8))
            DrawQuestUnit(unit);
        return 1;
    }

    void Render()   // FUN_102dbf60
    {
        auto enumerate = [](int(__cdecl* fn)(uint32_t, uint32_t, void*), void* p) { AscScript::EnumObjects(fn, p); };   // 0x4D4B30
        enumerate(&EnumDynamic, nullptr);
        enumerate(&EnumUnit, nullptr);
        if (CVarValue(g_quest) != 0)
        {
            g_questEntries.clear();
            enumerate(&EnumQuest, nullptr);
        }
        if (CVarValue(g_circle) == 1)
        {
            uint8_t* player = AscScript::ActivePlayer();
            if (player && SameGuid(Fields(player), reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD07B0))))
                reinterpret_cast<void(__thiscall*)(uint8_t*)>((*reinterpret_cast<void***>(player))[0x6C / 4])(player);
        }
    }

    void Init()   // FUN_102daaf0
    {
        AscRuntime::OnEnterWorld(&LoadCones);
        AscClientOptions::QueueWorldCVar({"aoeRadiusIndicatorSelf", "0", 1, 3, nullptr, nullptr, false, reinterpret_cast<void**>(const_cast<CVar**>(&g_self))});
        AscClientOptions::QueueWorldCVar({"aoeRadiusIndicatorEnemy", "0", 1, 3, nullptr, nullptr, false, reinterpret_cast<void**>(const_cast<CVar**>(&g_enemy))});
        AscClientOptions::QueueWorldCVar({"aoeRadiusIndicatorFriendly", "0", 1, 3, nullptr, nullptr, false, reinterpret_cast<void**>(const_cast<CVar**>(&g_friendly))});
        AscRuntime::OnAfter4F6F90(&Render);
        AscRuntime::OnAlternate6DF050(&Tick);
        g_7460C0 = reinterpret_cast<Fn0_t>(AscRuntime::Detour(0x7460C0, 6, reinterpret_cast<void*>(&Hook7460C0)));
        AscClientOptions::QueueWorldCVar({"showQuestUnitCircles", "1", 1, 4, nullptr, nullptr, false, reinterpret_cast<void**>(const_cast<CVar**>(&g_quest))});
        AscClientOptions::QueueWorldCVar({"ObjectSelectionCircleSelf", "0", 1, 4, nullptr, nullptr, false, reinterpret_cast<void**>(const_cast<CVar**>(&g_circle))});
        AscRuntime::ReplaceFunction(0x72B7E0, reinterpret_cast<void*>(&Hook72B7E0));
        g_727400 = reinterpret_cast<Fn727400_t>(AscRuntime::Detour(0x727400, 6, reinterpret_cast<void*>(&Hook727400)));
        AscClientOptions::QueueWorldCVar({"AutoInteractTurnSpeed", "3.15", 1, 3, nullptr});
        AscClientOptions::QueueWorldCVar({"AutoInteractCursorTime", "0", 1, 3, nullptr});
        PatchRadius(true);
    }

    AscBindings::Module s_module(nullptr, 0, &Init);
}
