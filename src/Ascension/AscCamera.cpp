// Action camera, camera target focus and camera M2 collision (module init FUN_1019c510).
//
// CVars (queued like the attach init's, see AscClientOptions; category 4) -- callbacks compare with "1":
//   ActionCam "0" (FUN_10199420)            "1": 0x5FFD34 = 7A
//   cameraTargetFocusInteractEnable "0"
//   cameraTargetFocusEnemyEnable "0" (FUN_1019a9e0)   jne/jmp at 0x72E870 / 0x72E5FE / 0x72E6AE / 0x5FBC80,
//                                           and the call at 0x7569B6 NOPed while on
//   cameraActionDist "1.25", cameraActionAngle "4.0", cameraActionZ "0.05"
//   cameraActionHeadBobs "0" (FUN_10199da0)  0x5FFD34 = EB on / 7A off
//   cameraTargetFocusTurnSpeed "3.15" (FUN_1019bcb0)  the client's own writers of 0xCA11DC NOPed (0x727520,
//                                           0x731720); 0xCA11DC = clamp(atof(value), 0.5, 16.5)
//   cameraM2Collision "1", flags 0x21 (FUN_1019a190)  the store at 0x791E60 and flag 0x100171 at 0x605E13
//                                           (0x170 without the M2 bit) follow the value
//   cameraM2CollisionAlpha "0.5", flags 0x21
//   cameraZoomFactor "1.0"
//   autoTarget "1" (FUN_10199650)           jne at 0x75697C / 0x806AA2 on, jmp off
// Hooks:
//   0x601D60 -> sub_101992c0  camera target position: + {cos, sin} (unit facing + angle) * dist, + Z
//   0x605D60 -> sub_10199050  camera collision: M2s between the player's head and the camera fade to
//                             cameraM2CollisionAlpha; with ActionCam the camera also shifts sideways
//   0x7A25F0 -> FUN_1019c2f0  REPLACED: the collision callback records each hit M2 and its alpha
//   0x401480 / 0x5FF950 / 0x5FFA60 -> the hit M2s' alphas are restored first (0x401480) / pass-through
//   0x5241B0 -> sub_101993b0  focus the player's camera on the target (0x7272C0) when the target clears
// Callbacks: after a target change (AscRuntime, FUN_10278330) FUN_1019c420 turns the camera to a hostile
// (enemy focus) or talkative (interact focus) target; before 0x6ECF80 (FUN_10278660) FUN_10198e60
// restores the faded M2s. The Lua natives InteractNearest / SetCursorPosition / GetScreenPosition
// (FUN_1019cfb0) live in AscGlobalsI / AscGlobalsF.
#include <Ascension/AscScript.hpp>
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscX64.hpp>
#include <Ascension/AscClientOptions.hpp>
#include <Ascension/AscRuntime.hpp>
#include <Client/CVar.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>
#include <windows.h>

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
    void WriteByte(uint32_t at, uint8_t v) { WriteCode(at, &v, 1); }
    bool IsOne(const char* value) { return value && strcmp(value, "1") == 0; }
#if ASC_X64
    // x64: troca os bytes de um site entre dois estados (bytes do WowClient64; o expect e o outro estado).
    void Toggle(uint32_t at, const uint8_t* want, const uint8_t* other, size_t n)
    {
        AscX64::WriteCodeX64(at, want, n, other);
    }
#endif

    // The CVars (the original's slots 0x10BDECF0 ..), found after registration.
    CVar* Var(const char* name)
    {
        return CVar::Lookup(name);
    }
    int32_t Int(const char* name)
    {
        CVar* v = Var(name);
        return v ? *reinterpret_cast<const int32_t*>(reinterpret_cast<uint8_t*>(v) + ASC_OFF(0x30, 0x54)) : 0;   // CVar::m_intValue
    }
    float Float(const char* name)
    {
        CVar* v = Var(name);
        return v ? *reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(v) + ASC_OFF(0x2C, 0x50)) : 0.0f;   // CVar::m_floatValue
    }
    bool ActionCam() { return Int("ActionCam") != 0; }

    uint8_t* ActivePlayer() { return reinterpret_cast<uint8_t*(__cdecl*)()>(ASC_PTR(0x4038F0))(); }
    uint8_t* ObjectByGuid(uint32_t lo, uint32_t hi, uint32_t mask)
    {
        return static_cast<uint8_t*>(AscScript::ObjectPtr(AscX64::Guid(lo, hi), mask));
    }
    float Facing(uint8_t* unit)   // vtable +0x34
    {
        return (*reinterpret_cast<float(__thiscall**)(void*)>(*reinterpret_cast<uint8_t**>(unit) + ASC_OFF(0x34, 0x68)))(unit);
    }

    // ---- CVar callbacks ------------------------------------------------------------------------------------
    int __cdecl OnActionCam(void*, const char*, const char* value, void*)   // FUN_10199420
    {
#if ASC_X64
        // CGCamera::IsBobbingAllowed+0x21: o x64 sai pelo ja (77 7D); o jp->jmp do x86 vira NOP.
        static const uint8_t kJa[2] = {0x77, 0x7D}, kNop2[2] = {0x90, 0x90};
        if (IsOne(value))
            Toggle(0x5FFD34, kJa, kNop2, 2);
#else
        if (IsOne(value))
            WriteByte(0x5FFD34, 0x7A);
#endif
        return 1;
    }
    int __cdecl OnHeadBobs(void*, const char*, const char* value, void*)   // FUN_10199da0
    {
#if ASC_X64
        static const uint8_t kJa[2] = {0x77, 0x7D}, kNop2[2] = {0x90, 0x90};
        Toggle(0x5FFD34, IsOne(value) ? kNop2 : kJa, IsOne(value) ? kJa : kNop2, 2);
#else
        WriteByte(0x5FFD34, IsOne(value) ? 0xEB : 0x7A);
#endif
        return 1;
    }
    int __cdecl OnEnemyFocus(void*, const char*, const char* value, void*)   // FUN_1019a9e0
    {
        const bool on = IsOne(value);
#if ASC_X64
        // Os 4 jne antes de ClearTrackingTarget (OnTurn/OnMove/OnStrafeStartLocal, UpdatePlayer): so o opcode muda.
        const uint8_t want = on ? 0xEB : 0x75, other = on ? 0x75 : 0xEB;
        for (uint32_t at : {0x72E870u, 0x72E5FEu, 0x72E6AEu, 0x5FBC80u})
            Toggle(at, &want, &other, 1);
#else
        for (uint32_t at : {0x72E870u, 0x72E5FEu, 0x72E6AEu, 0x5FBC80u})
            WriteByte(at, on ? 0xEB : 0x75);
#endif
        static const uint8_t kCall[5] = {0xE8, 0x05, 0x09, 0xFD, 0xFF}, kNop5[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
#if ASC_X64
        static const uint8_t kCallX64[5] = {0xE8, 0xD5, 0x83, 0xF6, 0xFF};   // Packet_Group_29+0x216: call ClearTrackingTarget
        AscX64::WriteCodeX64(0x7569B6, on ? kNop5 : kCallX64, 5, on ? kCallX64 : kNop5);
#else
        WriteCode(0x7569B6, on ? kNop5 : kCall, 5);
#endif
        return 1;
    }
    int __cdecl OnTurnSpeed(void*, const char*, const char* value, void*)   // FUN_1019bcb0
    {
        uint8_t nops[32];
        memset(nops, 0x90, sizeof(nops));
#if ASC_X64
        // O x86 tira o fld..fstp para s_trackingTurnRate (0xCA11DC); no x64 basta o movss que grava.
        // CGPlayer_C::ClickToMove+0x15B e CGUnit_C::HandleTrackingFacing+0xF7. (O NOP do x86 em 0x731720 deixava
        // o valor na pilha do x87 para o fcom seguinte; no SSE o compare usa xmm1 e nao muda.)
        // `movss [rip+disp32], xmm0` com o disp calculado para o s_trackingTurnRate do proprio exe: bytes fixos
        // quebravam a cada rebuild do cliente (o rip-relativo muda) e o WriteCodeX64 recusava.
        for (uint32_t site : {0x727520u, 0x731720u})
        {
            const uint8_t* p = static_cast<const uint8_t*>(ASC_PTR(site));
            const uint8_t* var = static_cast<const uint8_t*>(ASC_PTR(0xCA11DC));
            if (!p || !var)
                continue;   // sem linha no .addr (WowAddr ja logou)
            uint8_t store[8] = {0xF3, 0x0F, 0x11, 0x05};
            const int32_t disp = static_cast<int32_t>(var - (p + 8));
            memcpy(store + 4, &disp, 4);
            Toggle(site, nops, store, 8);
        }
#else
        WriteCode(0x727520, nops, 32);
        WriteCode(0x731720, nops, 6);
#endif
        const float v = static_cast<float>(atof(value));
        float r = v < 0.5f ? 0.5f : v;   // cmovbe pair: NaN passes through, as in the original
        if (v > 16.5f)
            r = 16.5f;
        *reinterpret_cast<float*>(ASC_PTR(0xCA11DC)) = r;
        return 1;
    }
    int __cdecl OnM2Collision(void*, const char*, const char* value, void*)   // FUN_1019a190
    {
        static const uint8_t kStore[6] = {0xD9, 0x98, 0x78, 0x01, 0x00, 0x00}, kNop6[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        const bool on = IsOne(value);
#if ASC_X64
        // CWorldScene::AddDoodadDefModelToModelScene+0x182: movss [rax+0x208],xmm6 (o campo 0x178 do CM2Model).
        static const uint8_t kStoreX64[8] = {0xF3, 0x0F, 0x11, 0xB0, 0x08, 0x02, 0x00, 0x00};
        static const uint8_t kNop8[8] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        (void)kStore; (void)kNop6;
        Toggle(0x791E60, on ? kStoreX64 : kNop8, on ? kNop8 : kStoreX64, 8);
#else
        WriteCode(0x791E60, on ? kStore : kNop6, 6);
#endif
#if ASC_X64
        // Cliente com o patch 14-camera-collide: flags = (agua ? 0x20000 : 0) + g_coaCameraCollideFlags (0x605E13).
        *static_cast<uint32_t*>(ASC_PTR(0x605E13)) = on ? 0x100171u : 0x170u;
#else
        const uint32_t flags = on ? 0x100171u : 0x170u;
        WriteCode(0x605E13, &flags, 4);
#endif
        return 1;
    }
    int __cdecl OnAutoTarget(void*, const char*, const char* value, void*)   // FUN_10199650
    {
        const uint8_t b = IsOne(value) ? 0x75 : 0xEB;
#if ASC_X64
        // Packet_Group_29+0x1D7 (o x64 compara o GUID de 64 bits num jne so) e PendingSpellCast::OnSpellStart+0x376.
        const uint8_t other = IsOne(value) ? 0xEB : 0x75;
        Toggle(0x75697C, &b, &other, 1);
        Toggle(0x806AA2, &b, &other, 1);
#else
        WriteByte(0x75697C, b);
        WriteByte(0x806AA2, b);
#endif
        return 1;
    }

    // ---- the faded M2s (0x10BDED20 .. 0x10BDED28): {model, alpha before the fade} ---------------------------
    std::vector<std::pair<uint8_t*, float>> g_hits;
    bool InWorld() { return *reinterpret_cast<const uint8_t*>(ASC_PTR(0xBD0792)) != 0; }   // DAT_10BCA098

    void RestoreAlphas()   // FUN_1019c3d0
    {
        if (InWorld())
            for (auto& h : g_hits)
                *reinterpret_cast<float*>(h.first + ASC_OFF(0x17C, 0x20C)) = h.second;   // CM2Model::m_baseAlpha
        g_hits.clear();
    }

    int __cdecl RecordHit(uintptr_t, uint32_t, uint8_t* model)   // FUN_1019c2f0, replaces 0x7A25F0
    {
        const float alpha = *reinterpret_cast<const float*>(model + ASC_OFF(0x17C, 0x20C));
        g_hits.emplace_back(model, alpha);
        int bits;
        memcpy(&bits, &alpha, 4);
        return bits;
    }

    // ---- hooks ---------------------------------------------------------------------------------------------
    typedef int(__cdecl* Fn401480_t)(uintptr_t, uint32_t, uint32_t, uintptr_t);
    Fn401480_t g_401480 = nullptr;
    int __cdecl Detour401480(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)   // sub_1019c970
    {
        RestoreAlphas();
        g_401480(a, b, c, d);
        return 1;
    }

    // CGCamera::ZoomIn/ZoomOut(float distance, uint32_t time, float duration)
    typedef int(__fastcall* This3_t)(void* ASC_EDX_P(void*), float, uint32_t, float);
    This3_t g_5FF950 = nullptr, g_5FFA60 = nullptr;
    int __fastcall Detour5FF950(void* s ASC_EDX_P(void* e), float a, uint32_t b, float c) { return g_5FF950(s ASC_EDX_A(e), a, b, c); }   // sub_10198ff0
    int __fastcall Detour5FFA60(void* s ASC_EDX_P(void* e), float a, uint32_t b, float c) { return g_5FFA60(s ASC_EDX_A(e), a, b, c); }   // sub_10199020

#if ASC_X64
    typedef int (*Fn5241B0_t)(uint64_t, int32_t);   // CGGameUI::ClearTarget(WOWGUID, int)
    Fn5241B0_t g_5241B0 = nullptr;
    int Detour5241B0(uint64_t guid, int32_t c)
    {
        const uint32_t lo = static_cast<uint32_t>(guid), hi = static_cast<uint32_t>(guid >> 32);
#else
    typedef int(__cdecl* Fn5241B0_t)(uint32_t, uint32_t, uint32_t);
    Fn5241B0_t g_5241B0 = nullptr;
    int __cdecl Detour5241B0(uint32_t lo, uint32_t hi, uint32_t c)   // sub_101993b0
    {
#endif
        const uint32_t* target = reinterpret_cast<const uint32_t*>(ASC_PTR(0xBD07B0));   // DAT_10BCA094
        if (ActionCam() && Int("cameraTargetFocusEnemyEnable") != 0 && lo == target[0] && hi == target[1])
            if (uint8_t* player = ActivePlayer())
                reinterpret_cast<void(__thiscall*)(void*, int, int)>(ASC_PTR(0x7272C0))(player, 0, 1);
#if ASC_X64
        return g_5241B0(guid, c);
#else
        return g_5241B0(lo, hi, c);
#endif
    }

    // C3Vector CGCamera::GetCameraPosition(const C3Vector& target, float distance, const C3Vector& offset): o 1o
    // argumento e o ponteiro oculto do retorno.
    typedef float*(__fastcall* Fn601D60_t)(void* ASC_EDX_P(void*), float*, const float*, float, const float*);
    Fn601D60_t g_601D60 = nullptr;
    float* __fastcall Detour601D60(void* self ASC_EDX_P(void* edx), float* a, const float* b, float c, const float* d)   // sub_101992c0
    {
        float* r = g_601D60(self ASC_EDX_A(edx), a, b, c, d);
        if (ActionCam())
        {
            const uint8_t* cam = static_cast<uint8_t*>(self);
            if (uint8_t* unit = ObjectByGuid(*reinterpret_cast<const uint32_t*>(cam + ASC_OFF(0x88, 0xA0)), *reinterpret_cast<const uint32_t*>(cam + ASC_OFF(0x8C, 0xA4)), 8))
            {
                const float angleVar = Float("cameraActionAngle");
                const double angle = static_cast<double>(static_cast<long double>(Facing(unit)) + angleVar);
                r[0] = static_cast<float>(cos(angle)) * Float("cameraActionDist") + r[0];
                r[1] = static_cast<float>(sin(angle)) * Float("cameraActionDist") + r[1];
                r[2] = Float("cameraActionZ") + r[2];
            }
        }
        return r;
    }

    // CollideCameraWithWorld(target&, distance&, height&, offset&, float liquid, extent&)
    typedef uint32_t(__fastcall* Fn605D60_t)(void* ASC_EDX_P(void*), float*, float*, float*, const float*, float, float*);
    Fn605D60_t g_605D60 = nullptr;
    uint32_t __fastcall Detour605D60(void* self ASC_EDX_P(void* edx), float* pos, float* dist, float* a3, const float* a4, float a5, float* a6)   // sub_10199050
    {
        uint8_t* cam = static_cast<uint8_t*>(self);
        RestoreAlphas();
        if (Int("cameraM2Collision") == 0)
            if (uint8_t* player = ActivePlayer())
            {
                float from[3] = {pos[0], pos[1], pos[2]};
                float zero[3] = {0.0f, 0.0f, 0.0f};
                float one = 1.0f;
                uint8_t scratch[ASC_OFF(0x780, 0x908)];   // World::HitTestDebug (x64 sizeof 0x908)
                const float height = (*reinterpret_cast<float(__thiscall**)(void*)>(*reinterpret_cast<uint8_t**>(player) + ASC_OFF(0x3C, 0x78)))(player);
                from[2] = static_cast<float>(height * 1.5L + from[2]);
                if (reinterpret_cast<bool(__cdecl*)(void*, float*, float*, float*, uint32_t, void*)>(ASC_PTR(0x77F310))(cam + ASC_OFF(8, 0x10), from, zero, &one, 0x100171, scratch))
                {
                    const float alpha = Float("cameraM2CollisionAlpha");
                    for (auto& h : g_hits)
                    {
                        uint32_t& flags = *reinterpret_cast<uint32_t*>(h.first + ASC_OFF(0x10, 0x18));   // m_fFlags
                        const bool shared = *reinterpret_cast<void* const*>(h.first + ASC_OFF(0x48, 0x80)) != nullptr;   // m_attachParent
                        flags |= shared ? 0x80u : 8u;
                        flags |= shared ? 0x20000u : 0x10000u;
                        *reinterpret_cast<float*>(h.first + ASC_OFF(0x17C, 0x20C)) = alpha;
                    }
                }
            }
        const uint32_t r = g_605D60(self ASC_EDX_A(edx), pos, dist, a3, a4, a5, a6);
        if ((r & 0x10000) && ActionCam())
            if (uint8_t* unit = ObjectByGuid(*reinterpret_cast<const uint32_t*>(cam + ASC_OFF(0x88, 0xA0)), *reinterpret_cast<const uint32_t*>(cam + ASC_OFF(0x8C, 0xA4)), 8))
            {
                const float angleVar = Float("cameraActionAngle");
                const float facing = static_cast<float>(Facing(unit) + static_cast<long double>(angleVar));
                const float d = Float("cameraActionDist");
                float shift = d - (dist[0] - 0.11111111f);
                if (shift > d)
                    shift = d;
                shift = 0.0f > shift ? 0.0f : shift;
                pos[0] -= static_cast<float>(cos(static_cast<double>(facing))) * shift;
                pos[1] -= static_cast<float>(sin(static_cast<double>(facing))) * shift;
            }
        return r;
    }

    // ---- lifecycle callbacks ---------------------------------------------------------------------------------
    void AfterTargetChange()   // FUN_1019c420
    {
        uint32_t guid[2];   // 0xBD07B0 = WOWGUID (8 bytes nos dois); 0xBD07B4 nao tem linha propria na tabela x64
        memcpy(guid, ASC_PTR(0xBD07B0), sizeof(guid));
        uint8_t* player = ActivePlayer();
        if (!player)
            return;
        uint8_t* unit = ObjectByGuid(guid[0], guid[1], 8);
        if (!unit || *reinterpret_cast<const uint32_t*>(unit + ASC_OFF(0x14, 0x24)) == 4)
            return;
        bool focus = false;
        if (reinterpret_cast<bool(__thiscall*)(void*, void*)>(ASC_PTR(0x729A70))(player, unit) && ActionCam() &&
            Int("cameraTargetFocusEnemyEnable") != 0)   // FUN_1019c940
            focus = true;
        else if (reinterpret_cast<int(__thiscall*)(void*, void*)>(ASC_PTR(0x7251C0))(player, unit) > 3 && ActionCam() &&
                 Int("cameraTargetFocusInteractEnable") != 0)
            focus = true;
        if (focus)
            reinterpret_cast<void(__thiscall*)(void*, uint32_t*, int)>(ASC_PTR(0x72B4A0))(player, guid, 1);
    }

    // sub_10198e60: `mov ecx, [esp+4]; call 0x4D43C0` -- a __thiscall on the list's argument (the object's
    // +0x144). Transcribed as a no-argument __cdecl until 2026-09-27, which left ECX to chance and crashed
    // on release-to-graveyard (0x4D43C3 reading [null]).
    void Before6ECF80(uintptr_t object)
    {
        // CGUnit_C::IsActiveMover devolve int32_t (no x64 so o EAX vale; ler bool pegava so o AL, ok, mas int e o certo)
        if (reinterpret_cast<int32_t(__thiscall*)(void*)>(ASC_PTR(0x4D43C0))(reinterpret_cast<void*>(object)))
            RestoreAlphas();
    }

    void Init()
    {
        AscRuntime::OnAfterTargetChange(&AfterTargetChange);
        AscRuntime::OnBefore6ECF80(&Before6ECF80);
        g_601D60 = reinterpret_cast<Fn601D60_t>(AscRuntime::Detour(0x601D60, 6, reinterpret_cast<void*>(&Detour601D60)));
        g_605D60 = reinterpret_cast<Fn605D60_t>(AscRuntime::Detour(0x605D60, 6, reinterpret_cast<void*>(&Detour605D60)));
        g_5241B0 = reinterpret_cast<Fn5241B0_t>(AscRuntime::Detour(0x5241B0, 6, reinterpret_cast<void*>(&Detour5241B0)));
        AscRuntime::ReplaceFunction(0x7A25F0, reinterpret_cast<void*>(&RecordHit));
        g_5FF950 = reinterpret_cast<This3_t>(AscRuntime::Detour(0x5FF950, 6, reinterpret_cast<void*>(&Detour5FF950)));
        g_5FFA60 = reinterpret_cast<This3_t>(AscRuntime::Detour(0x5FFA60, 6, reinterpret_cast<void*>(&Detour5FFA60)));
        g_401480 = reinterpret_cast<Fn401480_t>(AscRuntime::Detour(0x401480, 5, reinterpret_cast<void*>(&Detour401480)));
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}

namespace AscCamera
{
    // FUN_1019c510's FUN_10114540 queue entries, in its order.
    const AscClientOptions::CVarSpec kCVars[] = {
        {"ActionCam", "0", 1, 4, reinterpret_cast<void*>(&OnActionCam)},
        {"cameraTargetFocusInteractEnable", "0", 1, 4, nullptr},
        {"cameraTargetFocusEnemyEnable", "0", 1, 4, reinterpret_cast<void*>(&OnEnemyFocus)},
        {"cameraActionDist", "1.25", 1, 4, nullptr},
        {"cameraActionAngle", "4.0", 1, 4, nullptr},
        {"cameraActionZ", "0.05", 1, 4, nullptr},
        {"cameraActionHeadBobs", "0", 1, 4, reinterpret_cast<void*>(&OnHeadBobs)},
        {"cameraTargetFocusTurnSpeed", "3.15", 1, 4, reinterpret_cast<void*>(&OnTurnSpeed)},
        {"cameraM2Collision", "1", 0x21, 4, reinterpret_cast<void*>(&OnM2Collision)},
        {"cameraM2CollisionAlpha", "0.5", 0x21, 4, nullptr},
        {"cameraZoomFactor", "1.0", 1, 4, nullptr},
        {"autoTarget", "1", 1, 4, reinterpret_cast<void*>(&OnAutoTarget)},
    };
    const AscClientOptions::CVarSpec* CVars(size_t& count)
    {
        count = sizeof(kCVars) / sizeof(kCVars[0]);
        return kCVars;
    }
}
