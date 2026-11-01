// The attach-time client code patches of FUN_10a3d840 (its 27 unconditional sub-writers 0x10A60240 ..
// 0x10A5E020 and its own), byte for byte and in the original's order, from AscAttachPatches.generated.inc
// (tools/gen_attach_patches.py). Found missing by tools/live_patch_check.py, which compares the running
// client with the original's writes.
//
// Among them: mov cl, 3 for the player class byte at 0x57E9A8 / 0x57EB27..; the camera cave at 0x86A62C ->
// 0x9296A2 with the rewritten 0x869DB0 (a 50-pixel clamp); 0x80 -> 0x7D0 at 0x4910DB / 0x4910E2;
// 0x100 -> 0x200 at 0x5663D4; the AFK logout jmp at 0x52B27A (SetAFKLogoutTimer changes it later) and the
// rune-UI class 0 at 0x728AA3 (AscCAMgr changes it later).
//
// Not in the generated table, because their bytes hold an Extensions.dll address: the race tables
// (FUN_10a5e020, RaceTables below), the fade flag 0x744A55 (AscAttachFixes), the vtable slot
// and 0x9F3A88. FUN_10a5b790's two 8-entry level tables and uiScale's default string are written here.
#include <Ascension/AscBindings.hpp>
#include <Ascension/AscLog.hpp>
#include <Ascension/AscX64.hpp>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <windows.h>

namespace
{
    struct Patch
    {
        uint32_t address;
        uint32_t size;
        uint8_t bytes[80];
    };
    const Patch kPatches[] = {
#include <Ascension/AscAttachPatches.generated.inc>
    };

    // The uiScale CVar's default ("1.0", pushed at 0x5238AD) becomes the original's own "0.85"
    // (0x10B610DC); written in table order, between 0x4D17F8 and 0x5238D1 (useUiScale "0" -> "1").
    const char kUiScaleDefault[] = "0.85";

    // FUN_10a5b790: the client's 6-entry level tables at 0xA41E04 (ints 14..4) and 0xA41E1C (floats
    // 150..25) become the original's 8-entry ones (0x10BCD230 / 0x10BCD250); the loop count 5 -> 7 at
    // 0x7F3AFD / 0x7F3B03 comes right after in the table.
    int32_t g_levels[8] = {18, 16, 14, 12, 10, 9, 7, 4};
    float g_distances[8] = {210.0f, 180.0f, 150.0f, 120.0f, 90.0f, 60.0f, 40.0f, 25.0f};

    // FUN_10a5f0e0: the class colour table read at 0x98EE88 (`mov ecx, [class*4 + table]`), 0x84 bytes on
    // the heap -- the client's 12 colours (0xB2DA20), class 10 (Hero) recoloured, then the 21 CoA
    // classes 12..32. 0x98EE8B points at it, written in table order just before 0x707BE2.
    uint32_t* g_classColours = nullptr;
    const uint32_t kCoAColours[21] = {
        0xFF8A3303, 0xFF6EFF00, 0xFFA330C9, 0xFFABD473, 0xFF0070DE, 0xFFD64747, 0xFFC79C6E,
        0xFF00FF99, 0xFFCC9900, 0xFFFFF569, 0xFFF2E699, 0xFF8787ED, 0xFFFF300F, 0xFFE0C7FF,
        0xFFB5FFFF, 0xFFF58CBA, 0xFFA3A3A3, 0xFFFF7D0A, 0xFFC41F3B, 0xFF0D2ED6, 0xFF40C7EB,
    };

    // FUN_10a5e020, the installer's last sub-writer: the character-creation race tables. Eight sites
    // (0x4E157D ..) point at a 64-entry table (0x10D3D830, zeroed); 0x4CDA43 at a 32-entry one (0x10D3D930)
    // holding the client's first 22 entries of 0xB24180 (0x58 bytes, movups) and ten pointers to the DLL's
    // empty string (0x10B1B240). Upstream's CHARCREATIONRACE_FIX, which used to cover these sites, copied
    // only 0x30 bytes and zero-filled the rest.
    uint32_t g_raceTable[64] = {};
    const char* g_raceMemoryTable[32] = {};
    const char kEmpty[] = "";

    void RaceTables();

#if ASC_X64
    // (b) operandos: os bytes x86 da tabela nao valem no codigo x64. Versao propria de cada site, conferida no
    // disasm 32/64 (out/operands-x64-parcial.md): {endereco 12340, delta x64, n, bytes novos, bytes stock x64}.
    // O endereco x64 vem da linha do extra-addr; delta != 0 so no 0x632163 (o x64 duplicou o cmp).
    struct PatchX64
    {
        uint32_t address;
        int32_t delta;
        uint32_t size;
        uint8_t bytes[20];
        uint8_t expect[20];
    };
    const PatchX64 kPatchesX64[] = {
        {0x464C4F,  0,  5, {0x80, 0x7C, 0x24, 0x70, 0x80}, {0x80, 0x7C, 0x24, 0x70, 0x0A}},   // HandleCharEnum: cmp byte [n],0xA -> 0x80
        {0x5663D4,  0,  6, {0x81, 0xFB, 0x00, 0x02, 0x00, 0x00}, {0x81, 0xFB, 0x00, 0x01, 0x00, 0x00}},   // SetMacroBody: limite 0x100 -> 0x200
        {0x56E499,  0,  5, {0x05, 0x01, 0x00, 0x00, 0x00}, {0x05, 0x60, 0xEA, 0x00, 0x00}},   // Script_CheckInbox: add 60000 -> 1
        {0x574A5F,  0,  7, {0x83, 0xB8, 0xC0, 0x00, 0x00, 0x00, 0x00}, {0x83, 0xB8, 0xC0, 0x00, 0x00, 0x00, 0x0A}},   // Script_ConvertToRaid: cmp [..+0xC0],0xA -> 0
        {0x611717,  0,  5, {0x80, 0xF9, 0x0A, 0x7C, 0x36}, {0x80, 0xF9, 0x0B, 0x75, 0x36}},   // Script_GetComboPoints: cmp 0xB;jne -> cmp 0xA;jl
        {0x7FF2A5,  0,  6, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x81, 0xC3, 0xF4, 0x01, 0x00, 0x00}},   // SpellRec::GetCastTime: NOP no add ebx,500
        {0x5AE56B,  0,  3, {0x83, 0xFB, 0x14}, {0x83, 0xFB, 0x0A}},   // GetNextFreeSetID: 10 -> 20 conjuntos
        {0x5AF142,  0,  3, {0x83, 0xF8, 0x14}, {0x83, 0xF8, 0x09}},   // Script_GetEquipmentSetInfo: 9 -> 0x14
        {0x5AF485,  0,  3, {0x83, 0xFB, 0x14}, {0x83, 0xFB, 0x0A}},   // CanCreateSet: 10 -> 20
        {0x5AF548,  0,  4, {0x41, 0x83, 0xFE, 0x14}, {0x41, 0x83, 0xFE, 0x0A}},   // SMSG_EQUIPMENT_SET_LIST: 10 -> 20
        {0x5AF809,  0,  3, {0x83, 0xFB, 0x14}, {0x83, 0xFB, 0x0A}},   // CreateEquipmentSet: 10 -> 20
        {0x5AFB64,  0,  6, {0x41, 0xB8, 0x14, 0x00, 0x00, 0x00}, {0x41, 0xB8, 0x0A, 0x00, 0x00, 0x00}},   // Script_SaveEquipmentSet: texto "%d sets" 10 -> 20
        {0x632163,  0,  3, {0x83, 0xFA, 0x08}, {0x83, 0xFA, 0x10}},   // GetNetStats: historico de ping 16 -> 8
        {0x632163, 10,  3, {0x83, 0xFA, 0x08}, {0x83, 0xFA, 0x10}},   // GetNetStats: 2a copia do cmp 0x10
        {0x63264A,  0,  3, {0x83, 0xF8, 0x08}, {0x83, 0xF8, 0x10}},   // HandlePong: 16 -> 8
        {0x63266A,  0,  3, {0x83, 0xF8, 0x08}, {0x83, 0xF8, 0x10}},   // HandlePong: 16 -> 8
        {0x632DE5,  0,  5, {0xBA, 0x78, 0xEC, 0xFF, 0xFF}, {0xBA, 0xD0, 0x8A, 0xFF, 0xFF}},   // HandleIdle: -30000 -> -5000 ms
        {0x54CFD4,  0,  6, {0x8D, 0x83, 0xF4, 0x01, 0x00, 0x00}, {0x8D, 0x83, 0x88, 0x13, 0x00, 0x00}},   // RequestPlayerPositions: +5000 -> +500 ms
        {0x4910DB,  0,  5, {0xBA, 0xD0, 0x07, 0x00, 0x00}, {0xBA, 0x80, 0x00, 0x00, 0x00}},   // SetFrameLevel: teto 0x80 -> 2000
        {0x802221,  0,  2, {0x90, 0x90}, {0x77, 0x3F}},   // Script_CancelItemTempEnchantment: o x86 grava 02 SOBRE o opcode do ja
        {0x59DC63,  0,  2, {0xEB, 0x24}, {0x74, 0x24}},   // Script_PlaceAuctionBid: je -> jmp
        {0x5A5163,  0,  2, {0xEB, 0x16}, {0x74, 0x16}},   // GuildBank SwapItems: je
        {0x5A522F,  0,  2, {0xEB, 0x14}, {0x75, 0x14}},   // GuildBank SwapItems: jne
        {0x5A52E4,  0,  2, {0xEB, 0x0F}, {0x75, 0x0F}},   // GuildBank SwapItems: jne
        {0x5A5472,  0,  2, {0xEB, 0x0F}, {0x75, 0x0F}},   // GuildBank SwapBankItems: jne
        {0x5A6506,  0,  2, {0xEB, 0x0F}, {0x75, 0x0F}},   // Script_AutoStoreGuildBankItem: jne
        {0x5A6956,  0,  2, {0x90, 0x90}, {0x74, 0x0F}},   // Script_GetGuildBankTabInfo: je
        {0x5AC94D,  0,  6, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x0F, 0x84, 0x25, 0x02, 0x00, 0x00}},   // CGGMTicketInfo::CreateTicket: je
        {0x5CF2F6,  0,  2, {0xEB, 0x0F}, {0x75, 0x0F}},   // Script_OfferPetition: jne
        {0x5AD0B0,  0,  4, {0x90, 0x90, 0x90, 0x90}, {0x85, 0xC0, 0x74, 0x4E}},   // Script_NewGMTicket: test eax,eax; je
        {0x5D6E9C,  0,  2, {0xEB, 0x07}, {0x75, 0x07}},   // SendRequestPetInfo: sempre envia
        {0x52ABD9,  0,  2, {0xEB, 0x47}, {0x8B, 0xC8}},   // InitializeFrameXML: pula o switch da assinatura do FrameXML
        {0x52ADCC,  0,  5, {0x90, 0x90, 0x90, 0x90, 0x90}, {0xE8, 0x3A, 0xB4, 0x05, 0x00}},   // InitializeFrameXML: NOP no call CGGameUI::Initialize
        {0x4E4BC2,  0,  6, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x0F, 0x82, 0x8D, 0x02, 0x00, 0x00}},   // CGlueMgr::SurveyExecute: jb
        {0x4E57FE,  0,  2, {0x90, 0x90}, {0x72, 0x5B}},   // CGlueMgr::PatchDownloadApply: autenticacao < 5 segue p/ o prepatch
        {0x707BA3,  0,  2, {0xEB, 0x3C}, {0x7E, 0x3C}},   // OnUpdateDurability: jle -> jmp
        {0x707BE2,  0,  5, {0x90, 0x90, 0x90, 0x90, 0x90}, {0xE8, 0x32, 0x86, 0x03, 0x00}},   // OnUpdateDurability: NOP no call CGPlayer_C::EquipItem
        {0x708328,  0,  6, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x0F, 0x85, 0x86, 0x03, 0x00, 0x00}},   // CGItem_C::CanGoInSlot slot 17: x86 je->jmp
        {0x634B07,  0,  2, {0xEB, 0x17}, {0x76, 0x17}},   // InitResistances: jbe -> jmp
        {0x74135D,  0,  2, {0xEB, 0x73}, {0x74, 0x73}},   // CGUnit_C::Dismount: je
        {0x57E9A8,  0,  4, {0xB1, 0x03, 0x90, 0x90}, {0x0F, 0xB6, 0x49, 0x45}},   // CGMinimapFrame::CountAvailableTrackingTypes: classe fixa 3
        {0x57EB27,  0, 13, {0x41, 0xB8, 0x01, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x49, 0x45, 0x41, 0xD3, 0xE0}},   // GetTrackingTypeInfo: o resultado final do x86
        {0x731480,  0,  2, {0xEB, 0x17}, {0x7D, 0x17}},   // CGUnit_C::OnRightClick: jge -> jmp
        {0x70ED92,  0,  2, {0x90, 0x90}, {0x75, 0x2E}},   // CGGameObject_C::Reenable: NOP no jne
        {0x71C2C0,  0, 20, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x48, 0x8B, 0x05, 0x31, 0x8B, 0x85, 0x00, 0x83, 0x78, 0x54, 0x00, 0x74, 0x07, 0x8B, 0x81, 0x1C, 0x18, 0x00, 0x00, 0xC3}},   // CGUnit_C::GetPredictedHealth: NOP no ramo predictedHealth
        {0x783310,  0,  2, {0xEB, 0x57}, {0x76, 0x57}},   // CWorld::Update: jne -> jmp
        {0x819A74,  0,  2, {0xEB, 0x07}, {0x75, 0x07}},   // Script_debuglocals: jne -> jmp
        {0x634E28,  0, 10, {0xC7, 0x05, 0xD0, 0xC4, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00}, {0xC7, 0x05, 0xD0, 0xC4, 0x7C, 0x00, 0x01, 0x00, 0x00, 0x00}},   // ClientDBInitialize: flag dbCompress = 0
        {0x5CBA74,  0,  6, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0x88, 0x05, 0x9C, 0xEF, 0x6C, 0x00}},   // Packet_MSG_GUILD_PERMISSIONS: NOP no store do rank
        {0x81C9A8,  0,  2, {0xB2, 0x01}, {0x33, 0xD2}},   // CM2Scene::SetTime: GarbageCollect(force 0->1). x64 xor edx,edx -> mov 
        {0x81C9D9,  0,  2, {0xB2, 0x01}, {0x33, 0xD2}},   // CM2Scene::AdvanceTime: idem
        {0x5FD886,  0,  2, {0x90, 0x90}, {0x72, 0x07}},   // ValidateCameraLinearSpeed: sem teto
        {0x517EE8,  0,  2, {0xEB, 0x37}, {0x45, 0x8B}},   // PlacedFrameCallback: nao grava "FrameLevel: %d"
        {0x5184E1,  0,  2, {0xEB, 0x41}, {0x48, 0x8D}},   // LoadPlacedFrames: nao le "FrameLevel: "
        // operario 31/10 (disasm 32/64; extra-addr em out/x64-operario-attachpatches-animcount-extra-addr.txt):
        {0x7F29B9,  0, 10, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, {0xC7, 0x05, 0x1E, 0x2A, 0x66, 0x00, 0x9A, 0x99, 0xD9, 0x3F}},   // DayNight::LoadMap: x86 NOP12 no fld 1.7/fstp g_planets.m_moon2.m_period; x64 mov [rip],0x3FD9999A (10 B)
        {0x76A67A,  0,  4, {0x48, 0x8B, 0xFD, 0x90}, {0x48, 0x0F, 0x45, 0xFD}},   // RegisterGxCVars: gxWindow sempre "1" (x86 jne->jmp; x64 cmovne rdi,rbp -> mov rdi,rbp; rbp = "1")
    };

    // Sites da tabela x86 que o x64 resolve acima, ou que nao se aplicam: NA (sem efeito / o fonte ja faz /
    // DISABLE_SIGNATURE_CHECKS), DUP (coberto por outro site x64) e 0x728AA3 (o AscCAMgr grava o cmp em
    // 0x728AA1 no init e confere os bytes; o 00 daqui faria o expect dele recusar).
    bool CoveredX64(uint32_t address)
    {
        for (uint32_t at : {0x464C4Fu, 0x4910DBu, 0x4910E2u, 0x4DA8EBu, 0x4E4BC2u, 0x4E57FEu, 0x517EE8u, 0x5184E1u,
                            0x52ABD9u, 0x52AD43u, 0x52AD45u, 0x52ADCCu, 0x54CFD4u, 0x5663D4u, 0x56E499u, 0x574A5Fu,
                            0x57E9A8u, 0x57EB27u, 0x57EB2Du, 0x57EB32u, 0x59DC63u, 0x5A5163u, 0x5A522Fu, 0x5A52E4u,
                            0x5A5472u, 0x5A6506u, 0x5A6956u, 0x5AC94Du, 0x5AD0B0u, 0x5AE56Bu, 0x5AF142u, 0x5AF485u,
                            0x5AF548u, 0x5AF809u, 0x5AFB64u, 0x5CBA74u, 0x5CF2F6u, 0x5D6E9Cu, 0x5D6EA6u, 0x5F83ECu,
                            0x5FD886u, 0x611717u, 0x632163u, 0x63264Au, 0x63266Au, 0x632DE5u, 0x634B07u, 0x634E28u,
                            0x6DBE41u, 0x6E2867u, 0x707BA3u, 0x707BE2u, 0x708328u, 0x70ED92u, 0x71C2C0u, 0x728AA3u,
                            0x731480u, 0x74135Du, 0x783310u, 0x7FF2A5u, 0x802221u, 0x819A74u, 0x81C9A8u, 0x81C9D9u,
                            // operario 31/10 -- kPatchesX64: 0x7F29B9, 0x76A67A; RetargetLeaX64: 0x8C51F0;
                            // fonte do cliente (out/x64-operario-cliente.diff): 0xA0F830 s_maxMacros, 0x7F3AFD/0x7F3B03
                            // MinimapSetZoom; Engine/Source/Os/Win64/Input.cpp ja faz a camera (0x86A62C.., 0x869DB0..);
                            // 0x52B27A = 05-afk-timers.diff (variavel, gravada abaixo);
                            // defaults de CVar = detour em CVar::Register (kCVarDefaults; 0x51E396 grava o proprio "0").
                            0x7F29B9u, 0x76A67Au, 0x8C51F0u, 0xA0F830u, 0x7F3AFDu, 0x7F3B03u, 0x86A62Cu, 0x86A62Fu,
                            0x9296A2u, 0x869DB0u, 0x869D83u, 0x52B27Au, 0x401E14u, 0x401E3Cu, 0x401C07u, 0x401C2Cu,
                            0x51E0C5u, 0x51E062u, 0x51F0E7u, 0x4D17F8u, 0x5238D1u, 0x4D16B7u, 0x51E396u, 0x7E617Eu})
            if (at == address)
                return true;
        return false;
    }

    // Defaults de CVar da tabela x86 (push "0"/"1" trocado no registro) + o uiScale "0.85" (0x5238AE). No x64 o
    // valor e lea r9,[rip+..] ou um registrador reaproveitado (predictedHealth: mov r9,rsi) -- nao ha byte-patch
    // uniforme. O Extensions64 carrega antes do main() (CoaBridgeInit), entao um detour em CVar::Register troca o
    // default no proprio registro. So quando o valor recebido e o default de fabrica = igual ao patch do site.
    struct CVarDefault
    {
        const char* name;
        const char* stock;
        const char* value;
    };
    const CVarDefault kCVarDefaults[] = {
        {"readTOS", "0", "1"},   // x86 401E14
        {"readEULA", "0", "1"},   // x86 401E3C
        {"movie", "1", "0"},   // x86 401C07
        {"expansionMovie", "1", "0"},   // x86 401C2C
        {"trackerSorting", "0", "1"},   // x86 51E0C5
        {"advancedWorldMap", "0", "1"},   // x86 51E062
        {"predictedHealth", "1", "0"},   // x86 51F0E7
        {"Sound_ZoneMusicNoDelay", "0", "1"},   // x86 4D17F8
        {"useUiScale", "0", "1"},   // x86 5238D1
        {"Sound_EnableErrorSpeech", "1", "0"},   // x86 4D16B7
        {"UnitNameNPC", "0", "1"},   // x86 7E617E
        {"uiScale", "1.0", kUiScaleDefault},   // x86 5238AE
    };
    typedef void*(__cdecl* CVarRegister_t)(const char*, const char*, uint32_t, const char*, void*, uint32_t, bool, void*, bool);
    CVarRegister_t g_cvarRegister = nullptr;
    void* __cdecl CVarRegisterDetour(const char* name, const char* help, uint32_t flags, const char* value, void* callback,
                                     uint32_t category, bool setCommand, void* arg, bool unk)
    {
        if (name && value)
            for (const CVarDefault& d : kCVarDefaults)
                if (_stricmp(name, d.name) == 0 && strcmp(value, d.stock) == 0)
                {
                    value = d.value;
                    break;
                }
        return g_cvarRegister(name, help, flags, value, callback, category, setCommand, arg, unk);
    }

    // lea reg,[rip+disp32] (7 bytes, site = endereco 12340 do push do x86) passa a apontar para outra string do
    // exe; as duas strings vem do extra-addr (`0xADDR @0xRVA`). Confere opcode e o alvo antigo antes de gravar.
    void RetargetLeaX64(uint32_t site, uint32_t from, uint32_t to)
    {
        uint8_t* p = static_cast<uint8_t*>(ASC_PTR(site));
        const uint8_t* oldStr = static_cast<const uint8_t*>(ASC_PTR(from));
        const uint8_t* newStr = static_cast<const uint8_t*>(ASC_PTR(to));
        if (!p || !oldStr || !newStr)
            return;   // sem linha no extra-addr (WowAddr ja logou)
        uint8_t expect[7], bytes[7];
        memcpy(expect, p, 3);   // REX + 8D + modrm (rip-relativo)
        if ((expect[0] & 0xF0) != 0x40 || expect[1] != 0x8D || (expect[2] & 0xC7) != 0x05)
        {
            AscLog::Printf("x64: RetargetLeaX64 0x%06X (%p): nao e lea rip-relativo [%02X %02X %02X] -- nao escrito", site, p,
                           expect[0], expect[1], expect[2]);
            return;
        }
        const int32_t oldDisp = static_cast<int32_t>(oldStr - (p + 7));
        const int32_t newDisp = static_cast<int32_t>(newStr - (p + 7));
        memcpy(bytes, expect, 3);
        memcpy(expect + 3, &oldDisp, 4);
        memcpy(bytes + 3, &newDisp, 4);
        AscX64::WriteCodeX64(site, bytes, 7, expect);
    }
#endif

    void Write(uint32_t address, const void* bytes, uint32_t size)
    {
#if ASC_X64
        AscX64::WriteMem(address, bytes, size, __FUNCTION__);
        return;
#endif
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old))
            return;
        memcpy(reinterpret_cast<void*>(address), bytes, size);
        VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    }

    void Init()
    {
        for (const Patch& p : kPatches)
        {
#if !ASC_X64
            // x64: as tabelas, as cores e o default do uiScale moram no fonte do cliente / no detour do CVar::Register.
            if (p.address == 0x7F3AFD)
            {
                const uint32_t distances = reinterpret_cast<uint32_t>(g_distances);
                const uint32_t levels = reinterpret_cast<uint32_t>(g_levels);
                for (uint32_t at : {0x7F3BA5u, 0x7F4272u, 0x7F5D8Eu, 0x7F62A7u})   // HOOKED 0x7F3BA5
                    Write(at, &distances, 4);
                for (uint32_t at : {0x7F3BB6u, 0x7F3BBDu, 0x7F4281u, 0x7F4288u, 0x7F5A9Du})
                    Write(at, &levels, 4);
            }
            if (p.address == 0x707BE2)
            {
                g_classColours = static_cast<uint32_t*>(operator new(0x84));
                memcpy(g_classColours, reinterpret_cast<const void*>(ASC_PTR(0xB2DA20)), 12 * 4);
                g_classColours[10] = 0xFFFFD624;
                memcpy(g_classColours + 12, kCoAColours, sizeof(kCoAColours));
                const uint32_t table = reinterpret_cast<uint32_t>(g_classColours);
                Write(0x98EE8B, &table, 4);   // HOOKED 0x98EE8B
            }
            if (p.address == 0x5238D1)
            {
                const uint32_t text = reinterpret_cast<uint32_t>(kUiScaleDefault);
                Write(0x5238AE, &text, 4);   // HOOKED 0x5238AE
            }
#endif
#if ASC_X64
            if (CoveredX64(p.address))
                continue;
#endif
            Write(p.address, p.bytes, p.size);
        }
#if ASC_X64
        for (const PatchX64& p : kPatchesX64)
            AscX64::WriteCodeX64(p.address, p.bytes, p.size, p.expect, p.delta);
        // BattlenetAssert: "Internal Battle.net Error: %s" (0xA944A4) -> "    %s" (0x9FF5FC), o lea do 0x8C51F0.
        RetargetLeaX64(0x8C51F0, 0xA944A4, 0x9FF5FC);
        // 0x52B27A (jne -> jmp: sem logout por AFK, senta normal). x64: patch 05-afk-timers no cliente --
        // g_coaAutoLogoutIdleTime (0x52B266) = 0 desliga o logout.
        *static_cast<int32_t*>(ASC_PTR(0x52B266)) = 0;
        g_cvarRegister = reinterpret_cast<CVarRegister_t>(AscX64::Hook(0x767FC0, reinterpret_cast<void*>(&CVarRegisterDetour), "CVar::Register (defaults do CoA)"));
        // RaceTables: m_charPreferences[32][2] e s_raceFileNames[32] no fonte do cliente (x64-operario-cliente.diff).
        return;
#endif
        RaceTables();
    }

    void RaceTables()
    {
        const uint32_t race = reinterpret_cast<uint32_t>(g_raceTable);
        for (uint32_t at : {0x4E157Du, 0x4E16A3u, 0x4E15B5u, 0x4E20EEu, 0x4E222Au, 0x4E2127u, 0x4E1E94u, 0x4E1C3Au})
            Write(at, &race, 4);   // HOOKED 0x4E157D
        memcpy(g_raceMemoryTable, reinterpret_cast<const void*>(ASC_PTR(0xB24180)), 0x58);
        for (int i = 22; i < 32; ++i)
            g_raceMemoryTable[i] = kEmpty;
        const uint32_t memory = reinterpret_cast<uint32_t>(g_raceMemoryTable);
        Write(0x4CDA43, &memory, 4);   // HOOKED 0x4CDA43
    }
    AscBindings::Module s_module(nullptr, 0, &Init);
}
