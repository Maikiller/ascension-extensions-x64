// The original's render module: installer FUN_10265d20 (SetRenderOcclusion's registrar FUN_1026c7f0 and
// C_Sun's FUN_1026c850 are its world-state registrations). Nineteen render CVars whose callbacks rewrite
// client code, the lighting / sky / cloud / far-clip detours they feed, and the occlusion volumes: the
// DLL's OcclusionVolume(Points).dbc turned into the client's antiportal table, with a debug overlay.
//
//   Attach     0x9E8CFC <- 1400.0f.
//   Detours    0x834A40 -> sub_1025d020 (light created: attenuation, tracked)
//              0x834AB0 -> sub_1025d0c0 (light destroyed: untracked)
//              0x7EB210 -> FUN_10264820 (ambient colours x ambientShade)
//              0x7EB180 -> sub_102648c0 (sky/light params: SMSG 0x650 override, x ambientGlow)
//              0x7F1CD0 -> sub_102647c0, 0x7F1010 -> sub_102647f0 (cloud animation)
//              0x780770 -> FUN_1026ad40 (far-clip clamp; replaces the original)
//              0x6815C0, 0x681580 -> sub_10265d10 (both replaced by "return 0")
//   Lifecycle  sub_1025b020 on every second 0x6DF050; sub_1025b0c0 after 0x5204C0; sub_1025b0f0 and
//              the antiportal build FUN_10264d10 on world entry; the overlay FUN_10266560 after 0x4F6F90.
//   Packets    SMSG 0x650 (FUN_10266530); SMSG_PATCH_OCCLUSION_VOLUME 0x9D2 (FUN_101e2810) and
//              _POINTS 0x9D3 (FUN_101e2990) patch the tables and rebuild.
//
// The CVar callbacks' byte strings were taken by running each original callback under Unicorn for
// several values (tools/emulate_dll_fn.py), then checked against its decompile for the conditions.
// The CVars themselves are queued in CVar.cpp from AscGraphics::CVars().
#include <Ascension/AscClientDbc.hpp>
#include <Ascension/AscClientOptions.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscCrashContext.hpp>
#include <Ascension/AscGraphics.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscScript.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Ascension/AscDbc.hpp>
#include <Ascension/AscLua.hpp>
#include <Ascension/AscLog.hpp>
#include <Client/CDataStore.hpp>
#include <Client/CNetClient.hpp>
#include <Client/CVar.hpp>
#include <Misc/DataContainer.hpp>
#include <Windows.h>
#include <mapbox/earcut.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace AscScript;

namespace
{
    // ---- client addresses the DLL keeps pointers to (0x10BCB45C..0x10BCB4FC) ----------------------
    const uint32_t kMapId = 0xBD088C;            // DAT_10bcb45c
    const uint32_t kInWorld = 0xBD0792;          // DAT_10bcb460
    const uint32_t kTerrainFlags = 0xCD774C;     // DAT_10bcb468
    const uint32_t kCurrentMap = 0xADFBC4;       // DAT_10bcb46c
    const uint32_t kAntiportalReady = 0xD2DCD4;  // DAT_10bcb474
    const uint32_t kGxFlag = 0xC5DF84;           // DAT_10bcb4fc
    const uint32_t kClampedCVar = 0xCD85DC;      // the CVar* sub_1025b020 caps at 3 in maps 907 / 909

    // ---- DLL globals ----------------------------------------------------------------------------
    bool g_occlusionOverlay = false;             // DAT_10be270d, SetRenderOcclusion
    bool g_cloudsDirty = false;                  // DAT_10bcb4b8
    bool g_capApplied = false;                   // DAT_10be270c
    int32_t g_capSaved = -1;                     // DAT_10bcb478
    float g_dayProgress = 0.0f;                  // DAT_10be271c
    float g_mistDensity = 0.0f;                  // DAT_10bcb4d4
    uint32_t g_skyOverride = 0;                  // DAT_10be2720, SMSG 0x650
    uint32_t g_skyParams[9] = {};                // DAT_10be2724
    std::vector<uint8_t*> g_lights;              // DAT_10be2710

    struct Vec3 { float x, y, z; };
    struct Occluder                              // 0x28 bytes, the client's antiportal record
    {
        uint32_t mapId;                          // OcclusionVolume +0x08
        uint32_t flags;                          // OcclusionVolume +0x0C
        float min[3];
        float max[3];
        Vec3* points;
        uint32_t count;
    };
    std::vector<Occluder> g_occluders;           // DAT_10be2748
    std::vector<std::vector<Vec3>> g_points;     // DAT_10be2754

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
#if ASC_X64
    // x64: troca um site entre dois estados com os bytes do WowClient64 (o expect e o outro estado).
    template <size_t N> void ToggleX64(uint32_t addr, bool first, const uint8_t (&a)[N], const uint8_t (&b)[N])
    {
        AscX64::WriteCodeX64(addr, first ? a : b, N, first ? b : a);
    }
    // DayNight::g_dnInfo (0xD38B00): os campos de neblina nao tem endereco proprio no x64 -- base + layout.
    float* DnInfoFloat(uint32_t off64) { return reinterpret_cast<float*>(static_cast<uint8_t*>(ASC_PTR(0xD38B00)) + off64); }
#endif

    const uint8_t* Var(const char* name) { return reinterpret_cast<const uint8_t*>(CVar::Lookup(name)); }
    float VarF(const uint8_t* v) { return *reinterpret_cast<const float*>(v + ASC_OFF(0x2C, 0x50)); }
    int32_t VarI(const uint8_t* v) { return *reinterpret_cast<const int32_t*>(v + ASC_OFF(0x30, 0x54)); }
    const char* VarS(const uint8_t* v) { return AscClientDbc::Str(v, ASC_OFF(0x28, 0x48)); }   // m_stringValue (x64 +0x28 e o m_key)
    // FUN_10114c40 -> 0x7668C0: CVar::Set(value, 1, 0, 0, 0).
    void SetVar(const uint8_t* v, const char* value)
    {
        reinterpret_cast<void(__thiscall*)(const void*, const char*, int, int, int, int)>(ASC_PTR(0x7668C0))(v, value, 1, 0, 0, 0);
    }
    bool IsOne(const char* value) { return strcmp(value, "1") == 0; }

    // ---- CVar callbacks (all __cdecl (CVar*, previous, value, arg), all return 1) -----------------
    // FUN_1025e290: "1" keeps entity shadows (stock), anything else skips them.
    int __cdecl OnEntityShadows(void*, const char*, const char* value, void*)
    {
        static const uint8_t kOn1[] = {0x0F, 0x84, 0xB5, 0x00, 0x00, 0x00}, kOn2[] = {0x75, 0x07};
        static const uint8_t kOff1[] = {0xE9, 0xB6, 0x00, 0x00, 0x00, 0x00}, kOff2[] = {0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        // CWorldScene::RenderEntitys+0xBF (2o "if (model...)")
        static const uint8_t kOn1X64[6] = {0x0F, 0x84, 0xD0, 0x00, 0x00, 0x00}, kOff1X64[6] = {0xE9, 0xD1, 0x00, 0x00, 0x00, 0x90};
        AscX64::WriteCodeX64(0x793A45, on ? kOn1X64 : kOff1X64, 6, on ? kOff1X64 : kOn1X64);
#else
        PatchBytes(0x793A45, on ? kOn1 : kOff1);
#endif
#if ASC_X64
        // 0x7BBA22: o GatherShadowCasters reconstruido nao tem o teste de [0xAEEDD0]&1 -- o x64 ja se comporta
        // como o estado "off" (NOP); nao ha byte a trocar.
        (void)kOn2; (void)kOff2;
#else
        PatchBytes(0x7BBA22, on ? kOn2 : kOff2);
#endif
        return 1;
    }
    // FUN_1025d520
    int __cdecl OnEntityCulling(void*, const char*, const char* value, void*)
    {
        static const uint8_t kA[] = {0x0F, 0x84, 0xEA, 0x00, 0x00, 0x00}, kB[] = {0x0F, 0x85, 0xD9, 0x00, 0x00, 0x00},
                             kC[] = {0x0F, 0x85, 0xC0, 0x00, 0x00, 0x00}, kNop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        // CWorldScene::CullEntitys+0x94 / +0xA5 / +0xBE
        static const uint8_t kAX64[6] = {0x0F, 0x84, 0xD7, 0x00, 0x00, 0x00}, kBX64[6] = {0x0F, 0x85, 0xC6, 0x00, 0x00, 0x00}, kCX64[6] = {0x0F, 0x85, 0xAD, 0x00, 0x00, 0x00};
        AscX64::WriteCodeX64(0x793115, on ? kAX64 : kNop, 6, on ? kNop : kAX64);
        AscX64::WriteCodeX64(0x793126, on ? kBX64 : kNop, 6, on ? kNop : kBX64);
        AscX64::WriteCodeX64(0x79313F, on ? kCX64 : kNop, 6, on ? kNop : kCX64);
#else
        PatchBytes(0x793115, on ? kA : kNop);
        PatchBytes(0x793126, on ? kB : kNop);
        PatchBytes(0x79313F, on ? kC : kNop);
#endif
        return 1;
    }
    // FUN_102628b0
    int __cdecl OnPortalCulling(void*, const char*, const char* value, void*)
    {
        static const uint8_t kOn[] = {0x74, 0x44}, kOff[] = {0x90, 0x90};
#if ASC_X64
        static const uint8_t kOnX64[2] = {0x74, 0x48};   // CMapObj::RenderInterior+0x153
        AscX64::WriteCodeX64(0x7AD2EE, IsOne(value) ? kOnX64 : kOff, 2, IsOne(value) ? kOff : kOnX64);
#else
        PatchBytes(0x7AD2EE, IsOne(value) ? kOn : kOff);
#endif
        return 1;
    }
    // FUN_10262e10: also bit 0x20 of the terrain flags at 0xCD774C.
    int __cdecl OnTerrainCulling(void*, const char*, const char* value, void*)
    {
        static const uint8_t kOn[] = {0x7B, 0x1C}, kOff[] = {0x90, 0x90};
#if ASC_X64
        // CWorldScene::Render+0x275: o x64 inverteu o teste (jbe 76 0E -> CullSortTable); o NOP do jnp = jmp.
        static const uint8_t kOnX64[2] = {0x76, 0x0E}, kOffX64[2] = {0xEB, 0x0E};
        (void)kOn; (void)kOff;
        uint32_t* flags = static_cast<uint32_t*>(ASC_PTR(kTerrainFlags));
        if (IsOne(value))
            *flags |= 0x20;
        else
            *flags &= ~0x20u;
        ToggleX64(0x79AA42, IsOne(value), kOnX64, kOffX64);
#else
        uint32_t* flags = reinterpret_cast<uint32_t*>(kTerrainFlags);
        if (IsOne(value))
        {
            *flags |= 0x20;
            PatchBytes(0x79AA42, kOn);
        }
        else
        {
            *flags &= ~0x20u;
            PatchBytes(0x79AA42, kOff);
        }
#endif
        return 1;
    }
    // FUN_102613f0
#if ASC_X64
    // CM2Scene::SelectLights+0x166: je 74 0C (so o opcode muda).
    void Patch81E548(bool stock)
    {
        static const uint8_t kJe[1] = {0x74}, kJmp[1] = {0xEB};
        ToggleX64(0x81E548, stock, kJe, kJmp);
    }
#endif
    int __cdecl OnLightCulling(void*, const char*, const char* value, void*)
    {
#if ASC_X64
        Patch81E548(IsOne(value));
#else
        PatchValue<uint8_t>(0x81E548, IsOne(value) ? 0x74 : 0xEB);
#endif
        return 1;
    }
    // FUN_10263380
    int __cdecl OnWmoCulling(void*, const char*, const char* value, void*)
    {
        static const uint8_t kA[] = {0x0F, 0x84, 0xC6, 0x00, 0x00, 0x00}, kB[] = {0x74, 0x11}, kC[] = {0x74, 0x5B},
                             kD[] = {0x74, 0x1D}, kNop6[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, kNop2[] = {0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        // AddMapObjDefGroup+0x24 / +0x31; CullMapObjDefGroupFromExterior+0x9F / +0x88 (x64: bt edi,0x10; jae)
        static const uint8_t kAX64[6] = {0x0F, 0x84, 0xFA, 0x00, 0x00, 0x00}, kBX64[2] = {0x74, 0x19}, kCX64[2] = {0x74, 0x51}, kDX64[2] = {0x73, 0x11};
        AscX64::WriteCodeX64(0x792AF2, on ? kAX64 : kNop6, 6, on ? kNop6 : kAX64);
        AscX64::WriteCodeX64(0x792AFF, on ? kBX64 : kNop2, 2, on ? kNop2 : kBX64);
        AscX64::WriteCodeX64(0x7B3AAF, on ? kCX64 : kNop2, 2, on ? kNop2 : kCX64);
        AscX64::WriteCodeX64(0x7B3A8E, on ? kDX64 : kNop2, 2, on ? kNop2 : kDX64);
#else
        PatchBytes(0x792AF2, on ? kA : kNop6);
        PatchBytes(0x792AFF, on ? kB : kNop2);
        PatchBytes(0x7B3AAF, on ? kC : kNop2);
        PatchBytes(0x7B3A8E, on ? kD : kNop2);
#endif
        return 1;
    }
    // sub_10260f40: re-apply fogDistance and fogDensity with their current values.
    int __cdecl OnFogOverride(void*, const char*, const char*, void*)
    {
        if (const uint8_t* v = Var("fogDistance"))
            SetVar(v, VarS(v));
        if (const uint8_t* v = Var("fogDensity"))
            SetVar(v, VarS(v));
        return 1;
    }
    // FUN_1025ebf0: with fogOverride on and a value of at least 0.001, the density goes to 0xD38B98;
    // exactly 1.0 restores the client's five stores to it, anything else NOPs them.
    int __cdecl OnFogDensity(void*, const char*, const char* value, void*)
    {
        const uint8_t* over = Var("fogOverride");
        if (!over || VarI(over) == 0)
            return 1;
        const double d = atof(value);
        if (d < 0.001)
            return 1;
#if ASC_X64
        // g_dnInfo.m_fog.m_density (x86 +0x98, x64 +0xB0). Os 5 stores anulados no DayNight::UpdateFog, pelo
        // fonte: S1 m_fog = override/bands, S4 m_fog = density, S5 interior = density (else), S6/S7 os "* 2" do
        // fogMode 1. Ficam S2 (grupo/agua) e S3 (lerp interior), como no x86. movss [rip+..],xmm (8 bytes).
        *DnInfoFloat(0xB0) = static_cast<float>(d);
        static const uint8_t k7F1777[8] = {0xF3, 0x0F, 0x11, 0x05, 0x39, 0xE4, 0x65, 0x00};   // +0xBF  S1
        static const uint8_t k7F19F4[8] = {0xF3, 0x0F, 0x11, 0x35, 0x43, 0xE1, 0x65, 0x00};   // +0x3B5 S4
        static const uint8_t k7F1A49[8] = {0xF3, 0x0F, 0x11, 0x35, 0x21, 0xE1, 0x65, 0x00};   // +0x3EB S5
        static const uint8_t k7F1A13[8] = {0xF3, 0x0F, 0x11, 0x35, 0xEE, 0xE0, 0x65, 0x00};   // +0x40A S6
        static const uint8_t k7F1A1B[8] = {0xF3, 0x0F, 0x11, 0x35, 0xFA, 0xE0, 0x65, 0x00};   // +0x412 S7
        static const uint8_t kNop8[8] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool stock = d == 1.0;
        ToggleX64(0x7F1A13, stock, k7F1A13, kNop8);
        ToggleX64(0x7F1A49, stock, k7F1A49, kNop8);
        ToggleX64(0x7F1A1B, stock, k7F1A1B, kNop8);
        ToggleX64(0x7F1777, stock, k7F1777, kNop8);
        ToggleX64(0x7F19F4, stock, k7F19F4, kNop8);
#else
        PatchValue<float>(0xD38B98, static_cast<float>(d));
        static const uint8_t kA[] = {0xD9, 0x1D, 0x98, 0x8B, 0xD3, 0x00}, kB[] = {0xD9, 0x15, 0xAC, 0x8B, 0xD3, 0x00},
                             kC[] = {0xD9, 0x1D, 0xAC, 0x8B, 0xD3, 0x00}, kD[] = {0xD9, 0x15, 0x98, 0x8B, 0xD3, 0x00},
                             kNop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool stock = d == 1.0;
        PatchBytes(0x7F1A13, stock ? kA : kNop);
        PatchBytes(0x7F1A49, stock ? kB : kNop);
        PatchBytes(0x7F1A1B, stock ? kC : kNop);
        PatchBytes(0x7F1777, stock ? kA : kNop);
        PatchBytes(0x7F19F4, stock ? kD : kNop);
#endif
        return 1;
    }
    // FUN_10260400: with fogOverride on, the distance clamped to 498..1600 goes to 0xD38C1C; above 498
    // the client's two stores to it are NOPed, otherwise restored.
    int __cdecl OnFogDistance(void*, const char*, const char* value, void*)
    {
        const uint8_t* over = Var("fogOverride");
        if (!over || VarI(over) == 0)
            return 1;
        const float x = static_cast<float>(atof(value));
        const float lo = 498.0f <= x ? x : 498.0f;
        // Azeroth-Ascension: teto 50000 em vez de 1600 -- a neblina do patch-ZZ (LightFloatBand) vai a
        // 50000 jardas e o addon CoAVista acompanha o farclip.
        const float d = x <= 50000.0f ? lo : 50000.0f;
#if ASC_X64
        *DnInfoFloat(0x140) = d;   // g_dnInfo.m_bands.m_fogEnd (x86 +0x11C)
#else
        PatchValue<float>(0xD38C1C, d);
#endif
        static const uint8_t kA[] = {0xD9, 0x41, 0x48, 0xD9, 0x58, 0x48}, kB[] = {0xD9, 0x5E, 0x48};
        static const uint8_t kNop6[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, kNop3[] = {0x90, 0x90, 0x90};
        const bool fixed = 498.0f < d;
#if ASC_X64
        // DayNight::CurrentLight::operator=+0x6D: mov [rcx+0x48],eax (m_fogEnd; o x64 copia o campo como int).
        static const uint8_t kAX64[3] = {0x89, 0x41, 0x48}, kNop3X64[3] = {0x90, 0x90, 0x90};
        ToggleX64(0x7ED982, !fixed, kAX64, kNop3X64);
        (void)kA; (void)kNop6;
#else
        PatchBytes(0x7ED982, fixed ? kNop6 : kA);
#endif
#if ASC_X64
        // DayNight::LoadLightingBands+0x1F9: movss [rbx+0x48], xmm0 (5 bytes).
        static const uint8_t kBX64[5] = {0xF3, 0x0F, 0x11, 0x43, 0x48}, kNop5[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
        AscX64::WriteCodeX64(0x7EC15A, fixed ? kNop5 : kBX64, 5, fixed ? kBX64 : kNop5);
#else
        PatchBytes(0x7EC15A, fixed ? kNop3 : kB);
#endif
        return 1;
    }
    // FUN_1025d130
    int __cdecl OnAntiportal(void*, const char*, const char* value, void*)
    {
#if ASC_X64
        static const uint8_t kJne[1] = {0x75}, kJmp[1] = {0xEB};   // CMapObjGroup::Create+0x157: jne 75 08
        ToggleX64(0x7D8444, IsOne(value), kJne, kJmp);
#else
        PatchValue<uint8_t>(0x7D8444, IsOne(value) ? 0x75 : 0xEB);
#endif
        return 1;
    }
    // FUN_102617e0: "1" skips the client's light-count limit.
    int __cdecl OnLightLimitFix(void*, const char*, const char* value, void*)
    {
        static const uint8_t kFix[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xE9, 0xA6, 0x00, 0x00, 0x00};
        static const uint8_t kStock[] = {0xC7, 0x45, 0xE4, 0x03, 0x00, 0x00, 0x00, 0xEB, 0x02, 0xDD, 0xD8, 0x8B};
#if ASC_X64
        // CMapRenderChunk::SetVertexShader+0xB4: o x86 pula o laco dos 3 point lights; no x64 o laco esta
        // desenrolado -- o 1o call GetPointLight vira jmp para o fim (0x14053FBCB, = o 0x7D0171 do x86), depois que
        // xmm6/ebp ja foram zerados. CONFERIR em jogo.
        static const uint8_t kStockX64[5] = {0xE8, 0x17, 0xBB, 0xBF, 0xFF}, kFixX64[5] = {0xE9, 0x32, 0x02, 0x00, 0x00};
        (void)kFix; (void)kStock;
        ToggleX64(0x7D00BF, IsOne(value), kFixX64, kStockX64);
#else
        PatchBytes(0x7D00BF, IsOne(value) ? kFix : kStock);
#endif
        return 1;
    }
    // Attenuation of a tracked light (sub_1025d020 / FUN_10260f90): linear x 0.7, quadratic x 0.03,
    // each CVar (at +0x2C) or 1.0 when missing.
    void Attenuate(uint8_t* light)
    {
        const uint8_t* lin = Var("lightLinearAttenuation");
        const uint8_t* quad = Var("lightQuadraticAttenuation");
        *reinterpret_cast<float*>(light + ASC_OFF(0x58, 0x5C)) = (lin ? VarF(lin) : 1.0f) * 0.7f;
        *reinterpret_cast<float*>(light + ASC_OFF(0x5C, 0x60)) = (quad ? VarF(quad) : 1.0f) * 0.03f;
    }
    // FUN_10260f90 (both attenuation CVars): anything but 1.0 also forces the light-culling jump, then
    // every tracked light is updated. Called before the CVar stores the new value, as in the original.
    int __cdecl OnLightAttenuation(void*, const char*, const char* value, void*)
    {
#if ASC_X64
        Patch81E548(!(static_cast<float>(atof(value)) != 1.0f));
#else
        PatchValue<uint8_t>(0x81E548, static_cast<float>(atof(value)) != 1.0f ? 0xEB : 0x74);
#endif
        for (uint8_t* light : g_lights)
            Attenuate(light);
        return 1;
    }
    // sub_10262850 -> FUN_10266c30: from 0.1 up, the client's 24 day-progress reads point at our float.
    const uint32_t kDayProgressSites[] = {
        0x7F3389, 0x7F33EB, 0x7F3310, 0x7F3483, 0x7F38CE, 0x7F38ED, 0x7EEB5E, 0x7EEB7C,
        0x7EEFF0, 0x7EF00C, 0x7EF18B, 0x7EF1BB, 0x7EF1E2, 0x7EF35D, 0x7EF3C3, 0x7EF3EA,
        0x7EF5FF, 0x7EE51B, 0x7EE571, 0x7EF7E3, 0x7EFAEE, 0x7EFCAF, 0x7F0538, 0x7EE0F7,
    };
    int __cdecl OnOverrideDayProgress(void*, const char*, const char* value, void*)
    {
        const float v = static_cast<float>(atof(value));
#if ASC_X64
        // x64: as 24 leituras sao rip-relativo; o cliente (patch 36) le g_coaDayProgressOverride (STATIC 0x7F3389) e
        // usa o g_dnInfo.m_dayProgression quando o valor e < 0.1. O valor vai sempre; o cliente decide.
        if (!(v < 0.1f))
            g_dayProgress = v;
        AscX64::WriteMem(0x7F3389, &v, sizeof(v), "overrideDayProgress (g_coaDayProgressOverride)");
        return 1;
#endif
        uint32_t target = 0xD38B04;
        if (!(v < 0.1f))
        {
            g_dayProgress = v;
            target = reinterpret_cast<uint32_t>(&g_dayProgress);
        }
        for (uint32_t site : kDayProgressSites)
            PatchValue<uint32_t>(site, target);
        return 1;
    }
    int __cdecl OnAnimateClouds(void*, const char*, const char*, void*)   // sub_1025d120
    {
        g_cloudsDirty = false;
        return 1;
    }
    // FUN_10261d60
    int __cdecl OnLodObjectCull(void*, const char*, const char* value, void*)
    {
        static const uint8_t kOn[] = {0x0F, 0x85, 0xBC, 0x00, 0x00, 0x00}, kOff[] = {0xE9, 0xBD, 0x00, 0x00, 0x00, 0x00};
#if ASC_X64
        // CWorldScene::AddDoodadDefModelToModelScene+0xA2: bt eax,0xB; jb (0F 82 A6 00 00 00) -> jmp mesmo alvo.
        static const uint8_t kOnX64[6] = {0x0F, 0x82, 0xA6, 0x00, 0x00, 0x00}, kOffX64[6] = {0xE9, 0xA7, 0x00, 0x00, 0x00, 0x90};
        (void)kOn; (void)kOff;
        ToggleX64(0x791D6A, IsOne(value), kOnX64, kOffX64);
#else
        PatchBytes(0x791D6A, IsOne(value) ? kOn : kOff);
#endif
        return 1;
    }
    // FUN_102622d0: the three mist-density reads point at our float, whatever the value.
    int __cdecl OnMistDensity(void*, const char*, const char* value, void*)
    {
        g_mistDensity = static_cast<float>(atof(value));
#if ASC_X64
        // x64: Rain/Snow/Sand leem g_coaMistDensity (patch 36, STATIC 0x78CB05) ao criar o clima.
        AscX64::WriteMem(0x78CB05, &g_mistDensity, sizeof(g_mistDensity), "mistDensity (g_coaMistDensity)");
        return 1;
#endif
        for (uint32_t site : {0x78CB05u, 0x78CDA5u, 0x78CF95u})
            PatchValue<uint32_t>(site, reinterpret_cast<uint32_t>(&g_mistDensity));
        return 1;
    }

    template <class F> void* Cb(F f) { return reinterpret_cast<void*>(f); }
    const AscGraphics::CVarSpec kCVars[] = {
        {"entityShadows", "1", Cb(&OnEntityShadows), false, false},
        {"entityCulling", "1", Cb(&OnEntityCulling), false, true},
        {"portalCulling", "1", Cb(&OnPortalCulling), false, true},
        {"terrainCulling", "1", Cb(&OnTerrainCulling), false, true},
        {"lightCulling", "1", Cb(&OnLightCulling), false, true},
        {"wmoCulling", "1", Cb(&OnWmoCulling), false, true},
        {"fogOverride", "0", Cb(&OnFogOverride), true, false},
        {"fogDensity", "1", Cb(&OnFogDensity), false, true},
        {"fogDistance", "498", Cb(&OnFogDistance), false, false},
        {"antiportal", "1", Cb(&OnAntiportal), false, true},
        {"lightLimitFix", "0", Cb(&OnLightLimitFix), false, true},
        {"ambientGlow", "1", nullptr, false, true},
        {"ambientShade", "1", nullptr, false, true},
        {"lightLinearAttenuation", "1", Cb(&OnLightAttenuation), false, true},
        {"lightQuadraticAttenuation", "1", Cb(&OnLightAttenuation), false, true},
        {"overrideDayProgress", "0", Cb(&OnOverrideDayProgress), false, true},
        {"animateClouds", "1", Cb(&OnAnimateClouds), false, false},
        {"lodObjectCull", "1", Cb(&OnLodObjectCull), false, false},
        {"mistDensity", "12.0", Cb(&OnMistDensity), false, true},
    };

    // ---- detours --------------------------------------------------------------------------------
    // sub_1025d020 over 0x834A40 (__thiscall, returns the light): in world, attenuate and track it.
    typedef uint8_t*(__fastcall* LightNew_t)(void* ASC_EDX_P(void*));
    LightNew_t g_lightNew = nullptr;
    uint8_t* __fastcall LightNewDetour(void* ecx ASC_EDX_P(void* edx))
    {
        if (*static_cast<const uint8_t*>(ASC_PTR(kInWorld)) == 0)
            return g_lightNew(ecx ASC_EDX_A(edx));
#if ASC_X64
        g_lightNew(ecx);   // CM2Light::CM2Light no x64 devolve void (rax lixo): a luz e o proprio this
        uint8_t* light = static_cast<uint8_t*>(ecx);
#else
        uint8_t* light = g_lightNew(ecx ASC_EDX_A(edx));
#endif
        Attenuate(light);
        g_lights.push_back(light);
        return light;
    }
    // sub_1025d0c0 over 0x834AB0 (__thiscall): untrack, then the original.
    typedef void(__fastcall* LightFree_t)(void* ASC_EDX_P(void*));
    LightFree_t g_lightFree = nullptr;
    void __fastcall LightFreeDetour(void* ecx ASC_EDX_P(void* edx))
    {
        g_lights.erase(std::remove(g_lights.begin(), g_lights.end(), static_cast<uint8_t*>(ecx)), g_lights.end());
        g_lightFree(ecx ASC_EDX_A(edx));
    }
    // FUN_10264820 over 0x7EB210 (__thiscall(this, a, params), ret 8): the 16 ambient colours at
    // params +0x48 scaled by ambientShade.
#if ASC_X64
    // x64: 0x7EB210 e a instancia dobrada (COMDAT) de WowClientDB<LightIntBandRec/LightFloatBandRec>::GetRecord(id,
    // rec*) -- sem funcao propria no WowClient64. O efeito pretendido (cores da LightIntBand x ambientShade) vai no
    // chamador DayNight::LoadLightIntBand 0x7EBF30: escalar a cor ja interpolada = escalar as 16 cores e interpolar
    // (diferenca <= 1 por canal, do arredondamento). O x86 tambem escalava, por causa da dobra, os bytes dos floats
    // da LightFloatBand (0x7EBF90) -- esse efeito colateral nao e reproduzido.
    // CImVector tem construtor: o x64 devolve por ponteiro oculto (rcx = saida, retorna rcx); time edx, params r8, band r9d.
    typedef uint8_t* (*LightIntBand_t)(uint8_t*, int32_t, const void*, int32_t);
    LightIntBand_t g_lightIntBand = nullptr;
    uint8_t* LightIntBandDetour(uint8_t* out, int32_t time, const void* params, int32_t band)
    {
        uint8_t* color = g_lightIntBand(out, time, params, band);   // (b, g, r, a)
        const uint8_t* shade = Var("ambientShade");
        if (shade && color)
            for (uint32_t c = 0; c < 3; ++c)
                color[c] = static_cast<uint8_t>(static_cast<int>(static_cast<float>(color[c]) * VarF(shade)));
        return color;
    }
#endif
    typedef int(__fastcall* AmbientColors_t)(void* ASC_EDX_P(void*), void*, uint8_t*);
    AmbientColors_t g_ambientColors = nullptr;
    int __fastcall AmbientColorsDetour(void* ecx ASC_EDX_P(void* edx), void* a, uint8_t* params)
    {
        const int r = g_ambientColors(ecx ASC_EDX_A(edx), a, params);
        const uint8_t* shade = Var("ambientShade");
        if (shade && params)
            for (uint32_t i = 0; i < 0x10; ++i)
                for (uint32_t c = 0; c < 3; ++c)
                {
                    uint8_t& b = params[0x48 + i * 4 + c];
                    b = static_cast<uint8_t>(static_cast<int>(static_cast<float>(b) * VarF(shade)));
                }
        return r;
    }
    // sub_102648c0 over 0x7EB180 (__cdecl(a, b), returns a 9-dword record): copied to DAT_10be2724;
    // with an SMSG 0x650 override the copy is returned with [1] = 1 and [2] = the override. ambientGlow
    // then scales [4] of whichever record is returned -- the client's own when there is no override.
#if ASC_X64
    typedef uint32_t* (*SkyParams_t)(void*, int);   // DayNight::GetLightParams(const LightRec*, int)
    SkyParams_t g_skyParamsFn = nullptr;
    uint32_t* SkyParamsDetour(void* a, int b)
#else
    typedef uint32_t*(__cdecl* SkyParams_t)(void*, void*);
    SkyParams_t g_skyParamsFn = nullptr;
    uint32_t* __cdecl SkyParamsDetour(void* a, void* b)
#endif
    {
        uint32_t* p = g_skyParamsFn(a, b);
        if (!p)
            return nullptr;
        memcpy(g_skyParams, p, sizeof(g_skyParams));
        if (g_skyOverride != 0)
        {
            g_skyParams[1] = 1;
            p = g_skyParams;
            g_skyParams[2] = g_skyOverride;
        }
        if (const uint8_t* glow = Var("ambientGlow"))
            reinterpret_cast<float*>(p)[4] = VarF(glow) * reinterpret_cast<float*>(p)[4];
        return p;
    }
    // sub_102647c0 over 0x7F1CD0 (__cdecl, four arguments): marks the clouds for an update.
    typedef void(__cdecl* CloudsSet_t)(void*, void*, void*, void*);
    CloudsSet_t g_cloudsSet = nullptr;
    void __cdecl CloudsSetDetour(void* a, void* b, void* c, void* d)
    {
        g_cloudsDirty = true;
        g_cloudsSet(a, b, c, d);
    }
    // sub_102647f0 over 0x7F1010: runs only when marked, or when animateClouds is off (0).
    typedef void(__cdecl* CloudsTick_t)();
    CloudsTick_t g_cloudsTick = nullptr;
    void __cdecl CloudsTickDetour()
    {
        const uint8_t* anim = Var("animateClouds");
        if (!g_cloudsDirty && (!anim || VarI(anim) != 0))
            return;
        g_cloudsDirty = false;
        g_cloudsTick();
    }
    // FUN_1026ad40 in place of 0x780770 (__cdecl(float farClip, uint mapId) -> float): clamped to
    // 183.33 .. 791.66 on maps 0, 1, 530 and 571, .. 700 on 909, .. 1583.33 elsewhere.
    // Azeroth-Ascension: o teto sobe para 4000 em todo mapa (o do 909 fica), como o patch binario que o
    // nosso DLL original levava (791 -> 4000). Acima disso o processo 32 bits estoura a memoria
    // (Texture.cpp:2057, medido em 27/09 pedindo 10000).
    // x64 (01/11): sem o teto de 4 GB do processo 32 bits, o teto vai a 50000 -- o mesmo da neblina
    // (fogDistance/LightFloatBand do patch-ZZ). Quem escolhe o alcance e' o CoAVista (/vista).
    float __cdecl FarClipClamp(float farClip, uint32_t mapId)
    {
#if ASC_X64
        float cap = 50000.0f;
#else
        float cap = 4000.0f;
#endif
        if (mapId == 909)
            cap = 700.0f;
        const float lo = 183.33f <= farClip ? farClip : 183.33f;
        return farClip <= cap ? lo : cap;
    }
    // sub_10265d10 in place of 0x6815C0 and 0x681580.
#if ASC_X64
    int ReturnZero(void*) { return 0; }   // GxTexReusable devolve int no x64 (uint8_t deixaria lixo na parte alta de eax)
#else
    uint8_t __cdecl ReturnZero() { return 0; }
#endif

    // ---- lifecycle ------------------------------------------------------------------------------
    // sub_1025b020 (every second 0x6DF050): clouds marked; in maps 907 / 909 the client CVar at
    // 0xCD85DC is capped at 3 through 0x874210, and restored on leaving.
    void MapCap()
    {
        g_cloudsDirty = true;
        const uint32_t map = *static_cast<const uint32_t*>(ASC_PTR(kMapId));
        const uint8_t* cvar = *static_cast<uint8_t* const*>(ASC_PTR(kClampedCVar));
        typedef void(__cdecl* SetLevel_t)(int32_t);
        if (map == 0x38B || map == 0x38D)
        {
            if (!g_capApplied && cvar && (g_capSaved = VarI(cvar), 3 < g_capSaved))
            {
                reinterpret_cast<SetLevel_t>(ASC_PTR(0x874210))(3);
                g_capApplied = true;
            }
        }
        else if (g_capApplied && cvar)
        {
            reinterpret_cast<SetLevel_t>(ASC_PTR(0x874210))(g_capSaved);
            g_capApplied = false;
            g_capSaved = -1;
        }
    }
    // sub_1025b0c0 (after 0x5204C0): the far clip re-applied through 0x780800.
    void ReapplyFarClip(void*)
    {
        if (const uint8_t* v = Var("farClip"))
            reinterpret_cast<void(__cdecl*)(float)>(ASC_PTR(0x780800))(VarF(v));
    }
    // sub_1025b0f0 (world entry): the five culling CVars are switched back on if off.
    void CullingOn()
    {
        for (const char* name : {"entityCulling", "portalCulling", "terrainCulling", "lightCulling", "wmoCulling"})
            if (const uint8_t* v = Var(name))
                if (VarI(v) == 0)
                    SetVar(v, "1");
    }

    // ---- occlusion volumes (FUN_10264d10) ---------------------------------------------------------
    AscDbc::Table& Volumes() { return AscDbc::Get("DBFilesClient\\OcclusionVolume.dbc"); }        // {id, name, map, flags}
    AscDbc::Table& VolumePoints() { return AscDbc::Get("DBFilesClient\\OcclusionVolumePoints.dbc"); }  // {id, volume, x, y, z}

    bool ById(const uint8_t* a, const uint8_t* b) { return AscDbc::Table::I32(a, 0) < AscDbc::Table::I32(b, 0); }

    // Every volume with at least three points becomes an antiportal record (points in id order, with
    // their bounds); the client's two antiportal loops are pointed at the records and its table rebuilt
    // (0xD2DCD4 = 0, 0x7CCD20). With no records the client keeps whatever it had.
    void BuildOccluders()
    {
        g_occluders.clear();
        g_points.clear();
        std::vector<const uint8_t*> volumes;
        AscDbc::Table& vt = Volumes();
        if (vt.Loaded())
            for (uint32_t id = vt.MinId(); id <= vt.MaxId() && id >= vt.MinId(); ++id)
            {
                if (const uint8_t* row = vt.Row(id))
                    volumes.push_back(row);
                if (id == 0xFFFFFFFF)
                    break;
            }
        std::sort(volumes.begin(), volumes.end(), ById);

        std::unordered_map<uint32_t, std::vector<const uint8_t*>> byVolume;
        AscDbc::Table& pt = VolumePoints();
        if (pt.Loaded())
            for (uint32_t id = pt.MinId(); id <= pt.MaxId() && id >= pt.MinId(); ++id)
            {
                if (const uint8_t* row = pt.Row(id))
                    byVolume[AscDbc::Table::U32(row, 4)].push_back(row);
                if (id == 0xFFFFFFFF)
                    break;
            }

        g_occluders.reserve(volumes.size());
        g_points.reserve(volumes.size());
        for (const uint8_t* volume : volumes)
        {
            auto it = byVolume.find(AscDbc::Table::U32(volume, 0));
            if (it == byVolume.end() || it->second.size() <= 2)
                continue;
            std::vector<const uint8_t*>& rows = it->second;
            std::sort(rows.begin(), rows.end(), ById);
            g_points.emplace_back();
            std::vector<Vec3>& pts = g_points.back();
            pts.reserve(rows.size());
            Occluder o;
            o.min[0] = o.min[1] = o.min[2] = 3.4028235e38f;
            o.max[0] = o.max[1] = o.max[2] = -3.4028235e38f;
            for (const uint8_t* row : rows)
            {
                const Vec3 p = {AscDbc::Table::F32(row, 8), AscDbc::Table::F32(row, 0xC), AscDbc::Table::F32(row, 0x10)};
                pts.push_back(p);
                const float c[3] = {p.x, p.y, p.z};
                for (int k = 0; k < 3; ++k)
                {
                    o.min[k] = o.min[k] <= c[k] ? o.min[k] : c[k];
                    o.max[k] = c[k] <= o.max[k] ? o.max[k] : c[k];
                }
            }
            o.mapId = AscDbc::Table::U32(volume, 8);
            o.flags = AscDbc::Table::U32(volume, 0xC);
            o.points = pts.data();
            o.count = static_cast<uint32_t>(pts.size());
            g_occluders.push_back(o);
        }
        if (g_occluders.empty())
            return;
#if ASC_X64
        // Cliente com o patch 16-occlusion: g_coaOcclusionVolumes (0x7CD8A0) = inicio da tabela e
        // g_coaOcclusionVolumeCount (0x7CD8A5). Occluder tem o layout x64 de OcclusionVolume (0x30 bytes).
        static_assert(sizeof(Occluder) == 0x30 && offsetof(Occluder, points) == 0x20, "layout x64 de OcclusionVolume");
        *static_cast<void**>(ASC_PTR(0x7CD8A0)) = g_occluders.data();
        *static_cast<uint32_t*>(ASC_PTR(0x7CD8A5)) = static_cast<uint32_t>(g_occluders.size());
        *static_cast<int32_t*>(ASC_PTR(kAntiportalReady)) = 0;
        reinterpret_cast<void (*)()>(ASC_PTR(0x7CCD20))();
        return;
#endif
        const uint32_t base = reinterpret_cast<uint32_t>(g_occluders.data());
        const uint32_t count = static_cast<uint32_t>(g_occluders.size());
        PatchValue<uint32_t>(0x7CCD30, base + 8);
        PatchValue<uint32_t>(0x7CCD35, count);
        PatchValue<uint32_t>(0x7CD8A0, base + 4);
        PatchValue<uint32_t>(0x7CD8A5, count);
        *reinterpret_cast<uint32_t*>(kAntiportalReady) = 0;
        reinterpret_cast<void(__cdecl*)()>(ASC_PTR(0x7CCD20))();
    }

    // SMSG_PATCH_OCCLUSION_VOLUME (0x9D2, FUN_101e2810): {u32 id, u32 unused, u32 map, u32 flags, name}.
    void __cdecl OnPatchVolume(void*, uint32_t, uint32_t, CDataStore* p)
    {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p->m_buffer + p->m_read);
        uint32_t row[4];
        memcpy(row, b, 16);
        const char* name = reinterpret_cast<const char*>(b + 16);
        p->m_read += 17 + static_cast<uint32_t>(strlen(name));
        row[1] = Volumes().AddString(name);
        Volumes().Upsert(row[0], std::vector<uint8_t>(reinterpret_cast<uint8_t*>(row), reinterpret_cast<uint8_t*>(row) + 16));
        BuildOccluders();
    }
    // SMSG_PATCH_OCCLUSION_VOLUME_POINTS (0x9D3, FUN_101e2990): {u32 id, u32 volume, float x, y, z}.
    void __cdecl OnPatchVolumePoint(void*, uint32_t, uint32_t, CDataStore* p)
    {
        uint32_t row[5];
        memcpy(row, p->m_buffer + p->m_read, 20);
        p->m_read += 20;
        VolumePoints().Upsert(row[0], std::vector<uint8_t>(reinterpret_cast<uint8_t*>(row), reinterpret_cast<uint8_t*>(row) + 20));
        BuildOccluders();
    }
    // SMSG 0x650 (FUN_10266530): the sky override for sub_102648c0.
    void __cdecl OnSkyOverride(void*, uint32_t, uint32_t, CDataStore* p)
    {
        memcpy(&g_skyOverride, p->m_buffer + p->m_read, 4);
        p->m_read += 4;
    }

    // ---- the overlay (FUN_10266560, after 0x4F6F90) -----------------------------------------------
    // FUN_10a3aac0: world position relative to the active camera (0x4F5960, position at +8). Without a
    // camera the original raises the client's fatal error (FUN_10112f70 -> 0x8C51D0).
    Vec3 ToDrawPosition(const Vec3& p)
    {
        const uint8_t* camera = reinterpret_cast<const uint8_t*(__cdecl*)()>(ASC_PTR(0x4F5960))();
        if (!camera)
        {
            AscCrashContext::Assert("WorldRender::ToDrawPosition requires an active camera", nullptr, 0);
            return Vec3{0.0f, 0.0f, 0.0f};
        }
        const float* c = reinterpret_cast<const float*>(camera + 8);
        return Vec3{p.x - c[0], p.y - c[1], p.z - c[2]};
    }

    // The GX device (DAT_10bcb464 / DAT_10bcc8c4 -> 0xC5DF88): 0x409670 pushes its render state and
    // 0x685FB0 pops it, both __thiscall on the device (the decompile drops the ecx load).
    void* Device() { return *reinterpret_cast<void* const*>(ASC_PTR(0xC5DF88)); }
    void PopState() { reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x685FB0))(Device()); }

    // The render state both draws set (push, then 0x408240 / 0x408BF0 pairs); `stateD` is the value
    // of state 0xD, 0 for the fill and 1 for the outline.
    void OverlayState(int stateD)
    {
        reinterpret_cast<void(__thiscall*)(void*)>(ASC_PTR(0x409670))(Device());
        typedef void(__cdecl* Rs_t)(int, intptr_t);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408240))(0x4D, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408240))(0x4E, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408240))(0x15, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(0x11, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(0xB, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(0xC, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(6, 2);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(7, 0);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(0xD, stateD);
        reinterpret_cast<Rs_t>(ASC_PTR(0x408BF0))(0xF, 0);
    }
    typedef void(__cdecl* PrimLock_t)(uint32_t, const void*, uint32_t, intptr_t, int, const uint32_t*, int, intptr_t, int, intptr_t, int, intptr_t, int);
    typedef void(__cdecl* PrimDraw_t)(int, uint32_t, const uint16_t*);

    // FUN_10a3a900: one outline segment.
    void DrawLine(const Vec3& a, const Vec3& b, uint32_t color)
    {
        const Vec3 v[2] = {ToDrawPosition(a), ToDrawPosition(b)};
        const uint16_t idx[2] = {0, 1};
        OverlayState(1);
        reinterpret_cast<PrimLock_t>(ASC_PTR(0x6828C0))(2, v, 0xC, 0, 0, &color, 0, 0, 0, 0, 0, 0, 0);
        reinterpret_cast<PrimDraw_t>(ASC_PTR(0x682340))(1, 2, idx);
        *static_cast<uint32_t*>(ASC_PTR(kGxFlag)) = 0;
        PopState();
    }

    // FUN_10264930: the polygon projected on the plane its Newell normal is largest across (x/y when
    // degenerate), triangulated by mapbox earcut (the original links the same library).
    std::vector<uint32_t> Triangulate(const std::vector<Vec3>& poly)
    {
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        const size_t n = poly.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Vec3& a = poly[i];
            const Vec3& b = poly[(i + 1) % n];
            nx = nx + (a.y - b.y) * (b.z + a.z);
            ny = (a.z - b.z) * (a.x + b.x) + ny;
            nz = (b.y + a.y) * (a.x - b.x) + nz;
        }
        nx = std::fabs(nx);
        ny = std::fabs(ny);
        nz = std::fabs(nz);
        int drop;
        if (1e-06f < nx || 1e-06f < ny || 1e-06f < nz)
            drop = (nx < ny || nx < nz) ? (nz <= ny ? 1 : 0) : 2;
        else
            drop = 1;
        std::vector<std::vector<std::array<float, 2>>> rings(1);
        rings[0].reserve(n);
        for (const Vec3& p : poly)
        {
            if (drop == 0)
                rings[0].push_back({p.x, p.y});
            else if (drop == 2)
                rings[0].push_back({p.y, p.z});
            else
                rings[0].push_back({p.x, p.z});
        }
        return mapbox::earcut<uint32_t>(rings);
    }

    // Each volume of the current map drawn filled (0x4040FF20) and outlined (0xB040FF20).
    void DrawOcclusion()
    {
        if (!g_occlusionOverlay)
            return;
        for (const Occluder& o : g_occluders)
        {
            if (o.mapId != *static_cast<const uint32_t*>(ASC_PTR(kCurrentMap)) || !o.points || o.count <= 2)
                continue;
            std::vector<Vec3> draw;
            draw.reserve(o.count);
            for (uint32_t i = 0; i < o.count; ++i)
                draw.push_back(ToDrawPosition(o.points[i]));
            const std::vector<Vec3> world(o.points, o.points + o.count);
            const std::vector<uint32_t> tri = Triangulate(world);
            std::vector<uint16_t> idx;
            idx.reserve(tri.size());
            for (uint32_t t : tri)
                idx.push_back(static_cast<uint16_t>(t));
            OverlayState(0);
            const uint32_t fill = 0x4040FF20;
            reinterpret_cast<PrimLock_t>(ASC_PTR(0x6828C0))(static_cast<uint32_t>(draw.size()), draw.data(), 0xC, 0, 0, &fill, 0, 0, 0, 0, 0, 0, 0);
            reinterpret_cast<PrimDraw_t>(ASC_PTR(0x682340))(3, static_cast<uint32_t>(idx.size()), idx.data());
            *static_cast<uint32_t*>(ASC_PTR(kGxFlag)) = 0;
            PopState();
            for (uint32_t i = 0; i < o.count; ++i)
                DrawLine(o.points[i], o.points[(i + 1) % o.count], 0xB040FF20);
        }
    }

    // handler_SetRenderOcclusion (FUN_1026a510): exactly one argument, else ignored; returns nothing.
    int SetRenderOcclusion(lua_State* L)
    {
        if (AscLua::lua_gettop(L) == 1)
            g_occlusionOverlay = AscLua::lua_toboolean(L, 1) != 0;
        return 0;
    }

    void Init()
    {
        PatchValue<float>(0x9E8CFC, 1400.0f);

        // FUN_10265d20's FUN_10114540 half, in its order: no help, flags 1, category 1.
        for (const AscGraphics::CVarSpec& c : kCVars)
            if (c.worldList)
                AscClientOptions::QueueWorldCVar({c.name, c.defaultValue, 1, 1, c.callback, nullptr, c.a7});

        typedef void* V;
        g_lightNew = reinterpret_cast<LightNew_t>(AscRuntime::Detour(0x834A40, 7, reinterpret_cast<V>(&LightNewDetour)));
        g_lightFree = reinterpret_cast<LightFree_t>(AscRuntime::Detour(0x834AB0, 5, reinterpret_cast<V>(&LightFreeDetour)));
#if ASC_X64
        (void)&AmbientColorsDetour;
        g_lightIntBand = reinterpret_cast<LightIntBand_t>(AscRuntime::Detour(0x7EBF30, 0, reinterpret_cast<V>(&LightIntBandDetour)));
#else
        g_ambientColors = reinterpret_cast<AmbientColors_t>(AscRuntime::Detour(0x7EB210, 6, reinterpret_cast<V>(&AmbientColorsDetour)));
#endif
        g_skyParamsFn = reinterpret_cast<SkyParams_t>(AscRuntime::Detour(0x7EB180, 6, reinterpret_cast<V>(&SkyParamsDetour)));
        sDC.AddPacketHandler(0x650, CNetClientCustomPacket((void*)&OnSkyOverride, nullptr));
        sDC.AddPacketHandler(0x9D2, CNetClientCustomPacket((void*)&OnPatchVolume, nullptr));
        sDC.AddPacketHandler(0x9D3, CNetClientCustomPacket((void*)&OnPatchVolumePoint, nullptr));
        g_cloudsSet = reinterpret_cast<CloudsSet_t>(AscRuntime::Detour(0x7F1CD0, 6, reinterpret_cast<V>(&CloudsSetDetour)));
        g_cloudsTick = reinterpret_cast<CloudsTick_t>(AscRuntime::Detour(0x7F1010, 5, reinterpret_cast<V>(&CloudsTickDetour)));
        AscRuntime::ReplaceFunction(0x780770, reinterpret_cast<V>(&FarClipClamp));
        AscRuntime::OnAlternate6DF050(&MapCap);
        AscRuntime::OnAfter5204C0(&ReapplyFarClip);
        AscRuntime::OnEnterWorld(&CullingOn);
        AscRuntime::OnEnterWorld(&BuildOccluders);
        AscRuntime::OnAfter4F6F90(&DrawOcclusion);
        AscRuntime::ReplaceFunction(0x6815C0, reinterpret_cast<V>(&ReturnZero));
        AscRuntime::ReplaceFunction(0x681580, reinterpret_cast<V>(&ReturnZero));
    }

    const AscBindings::Binding kBindings[] = {
        {nullptr, "SetRenderOcclusion", SetRenderOcclusion},
    };
    AscBindings::Module s_module(kBindings, sizeof(kBindings) / sizeof(kBindings[0]), &Init);
}

const AscGraphics::CVarSpec* AscGraphics::CVars(size_t& count)
{
    count = sizeof(kCVars) / sizeof(kCVars[0]);
    return kCVars;
}
