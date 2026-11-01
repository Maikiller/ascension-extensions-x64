#include <Ascension/AscX64.hpp>
#include <Ascension/AscLog.hpp>
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#if ASC_X64
#include <MinHook.h>
#endif

namespace AscX64
{
namespace
{
    typedef void*(*WowAddr_t)(uint32_t);
    WowAddr_t g_wowAddr = nullptr;
    bool g_mhReady = false;

    // Modulos portados e verificados contra os prototipos x64 (tools/protos.py). Vazio no marco 2a.
    // Modulos (stem do .cpp) cujo init/metodos de widget rodam no x64.
    const char* const kPorted[] = {
        "AscPatchData",   // GlobalStrings.dbc nos estados glue/world + registro dos patches de DBC por opcode
        "AscWidget",      // detours dos registradores de metodos (Frame/Minimap/Model/MovieFrame)
        // grupo glue (auditados: sem chamada/offset do exe; so pacotes custom + Lua)
        "AscDbcPatch",    // patches de DBC por opcode -> tabelas do proprio DLL (callbacks pos-patch bloqueados no x64)
        "AscAccount",     // SMSG 0x5C0 / 0x9BB (GM, info da conta)
        "AscCharacterList", // SMSG 0x75E (lista de personagens do CoA)
        "AscRealmMerge",  // SMSG 0x5F5 / 0x5F7
        // lote 1 (30/10): so pacote custom + Lua, ou leituras do exe ja mapeadas/conferidas (checksig, objoffs,
        // rawaddr limpos); hooks via AscRuntime::On* ficam inertes enquanto InstallHooks x64 nao liga as listas
        "AscArgs", "AscChatCheck", "AscFriendsMetadata", "AscGameEvents", "AscQuestAddon", "AscUnitAddon",
        "AscCharacterAdvancement", "AscCollectorCache", "AscToken", "AscWildcardRapid", "AscSeasonal",
        "AscWildcardSets", "AscObjectAddon", "AscCallboard", "AscBuildDraft", "AscSpecs", "AscHighRisk",
        "AscAutoQuest", "AscTooltipInsertFix", "AscBugTracker", "AscSpellCharges", "AscConfig", "AscLogger",
        "AscQuestSpellHide", "AscPlayerPoll", "AscMythicPlus",
        // lote 2 (30/10, projects-6a): triagem out/x64-triagem-modulos.md sem pendencia; dependem das listas do
        // AscRuntime (x64-runtime-hooks-6a.diff). AscAttachInline fica fora: seus patches sao de codigo e ja
        // entram como WriteCodeX64 no operands-x64.diff.
        "AscGameObjectUse", "AscSkillCardSlots", "AscSkillCardCollection", "AscRecoveryService", "AscCustomStore",
        "AscGMTicket", "AscGroupFinder", "AscMysticEnchantPreset", "AscTrialCreator",
        "AscTemplates", "AscGlobalsH", "AscSpellActivationOverlay", "AscAlternatePower", "AscAuraAmounts",
        // lote 3 (30/10, projects-6a): revisados um a um (x64-lote3-6a.diff). Corrigidos: AscChallenge (tipo do
        // objeto ASC_OFF), AscAppearance (cabecalho do ItemSet.dbc via extra-addr), AscGlobalsI (lua_tainted
        // 8 bytes), AscLibrary (UIADDON flag +0x30), AscAttachMisc (log do va_list).
        "AscAppearance", "AscAttachMisc", "AscCAFilter", "AscCallboardCache", "AscChallenge", "AscDraftMode",
        "AscGlobalsC", "AscGlobalsI", "AscScrollOfFortune", "AscSmallApis", "AscCacheFiles", "AscLibrary",
        "AscExtraActionButton",
        // lote 3 (30/10, projects-2c) g1: Lua sobre DBC/DBCache do cliente (ASC_PTR/AscClientDbc conferidos).
        "AscWorldMap", "AscAssetQuery", "AscCanToggle",
        // g2: pacotes custom/Lua (CDataStore x64 conferido; CVar m_key +0x28, m_stringValue +0x48, int +0x54).
        "AscPacketApi", "AscHotPatch", "AscEventPackets", "AscRaidVideo",
        // g3: SpellText (SpellParserParseText com o SpellRec nativo), SuperTrack (LeaderBoardParams x64 +
        // Script_SetPoint x64). Os CVars de mundo deles dependem do detour 0x51D9B0 do AscClientOptions.
        "AscSpellText", "AscSuperTrack",
        // lote 5 (30/10, projects-2c) g1 -- chamados ao entrar no mundo. SpellApi/CAMgr: limites de DBC via
        // AscClientDbc; MysticEnchant: ItemStats_C m_buyPrice +0x24; Tutorial: CVar int +0x54.
        "AscSpellApi", "AscTutorial", "AscMysticEnchant", "AscCAMgr",
        // lote 5 g2: LootLockout/TemporalContracts (limites e linhas de DBC via AscClientDbc), LuaDbc (userdata 8
        // bytes); BuildCreator (z_stream natural x64), WildcardRolls e VanityCollection conferidos sem mudanca.
        "AscBuildCreator", "AscWildcardRolls", "AscTemporalContracts", "AscVanityCollection", "AscLootLockout",
        "AscLuaDbc",
        // medicao 30/10 (projects-2c): nativas que voltaram nil + pacotes 0x66D/0x76F. GlobalsB (limites de DBC via
        // AscClientDbc), Misc (m_nameDesc 8 bytes em +0x130, CVar m_key +0x28); D/E/F/Json/GameMode conferidos.
        "AscGlobalsB", "AscGlobalsD", "AscGlobalsE", "AscGlobalsF", "AscJson", "AscMisc", "AscGameMode",
        // lote 3 (projects-2c): textura do circulo por troca do valor em 0xCA12C4 (operando rip nao alcanca o
        // DLL); raio 10.0 pelos dois disp32 do DrawBatchProj (extra-addr 0x820809); CVar int/float em ASC_OFF.
        "AscUnitSelect",
        // 31/10: GlobalsJ -- CombatLogEntry/lua_Debug/ItemStats_C em layout x64 (PDB); SpellRec/ItemDisplayInfo
        // nativos ao exe (CanStealAura, AddLink); SetMaskTexture inerte (pendente).
        "AscGlobalsJ",
        // 31/10: GlobalsG -- GetTokenIDsFromGUID etc.; SetAFKTimer/SetAFKLogoutTimer (patch em CGGameUI::Idle) recusados no x64 ate o port.
        "AscGlobalsG",
        // 31/10 (operario): byte-patches de attach -- WriteCodeX64 conferidos, defaults de CVar por detour em CVar::Register,
        // tabelas (cor de classe/racas/minimapa/macros) no fonte do cliente (x64-operario-cliente.diff, ainda nao compilado).
        "AscAttachPatches",
        // 31/10: Graphics -- end. crus via ASC_PTR, CM2Light ctor void (luz = this), atenuacao +0x5C/+0x60; AmbientColors
        // (0x7EB210 inlinado no x64) sem hook; overrideDayProgress/mistDensity/antiportal do servidor inertes (rip-relativo).
        "AscGraphics",
        // 31/10 (operario lote 2): Camera (CVar/objetos em ASC_OFF, 0x605E13 por WriteCodeX64) e NamePlates (layout x64;
        // patches mid-funcao/offset vertical/distancia dependem do 09-nameplates + lote2-cliente -- sem eles so loga).
        // AscBridgeStore pronto no fonte, mas fica FORA ate AscMemoryBridge (MMgr64) e AscHdDbc subirem no x64.
        "AscCamera", "AscNamePlates",
        // 31/10 (operario lote 3): ponte MMgr64 (MMgr64.exe e x64 nativo; copiado p/ a pasta do jogo x64), HdDbc campo a campo
        // (AscClientDbc::Overwrite), BridgeStore com camada Wire. AnimationCount so com o cliente novo (CoaSetAnimCount).
        "AscMemoryBridge", "AscHdDbc", "AscBridgeStore",
        // lote 3 (projects-2c): s_iconPositions escrito a partir de 0xAF46AC (so o inicio e mapeado);
        // SpellRange.Flags via AscClientDbc::Field (Row e copia no x64).
        "AscManastorm",
        // lote 3 (projects-2c): CCharacterComponent m_data em +0x20 no x64 (TSLink 16 bytes); CM2Model
        // m_scene/m_shared/m_attach* pelo layout; ReplaceMonsterSkin e o 0x4EAA70 recebem linhas nativas.
        "AscCharacterCreate",
        // 31/10 base = cliente novo: lote 4a do operario + AscAttachInline (3 sites = 18-attach-inline.diff)
        "AscItemUse", "AscSceneTransform", "AscGeosets", "AscModelNames", "AscSpellVisuals", "AscAttachInline",
        // 31/10: TerrainScale (CMapRenderChunk/CMapChunk pelo layout), UnitTokens (sem o hook morto do lua_pushstring),
        // LargeAlloc (TextureCreate devolve ponteiro; M2/DBC fora), SpawnVisibility (TSList x64), DbcReaders (inerte),
        // WorldEntryTweaks (19-world-entry-tweaks.diff; DivxTac/kill switch fora), AlphaAmbience (Field).
        "AscTerrainScale", "AscUnitTokens", "AscLargeAlloc", "AscSpawnVisibility", "AscDbcReaders", "AscWorldEntryTweaks",
        "AscAlphaAmbience",
        // lote 4b do operario
        "AscItemStats", "AscItemCollections", "AscModelMethods", "AscMovementForce", "AscProfiler",
        "AscLoadingScreen", "AscGossipQuest",
        "AscAddonTaint", "AscAttachLua",   // Script_* com lua_State* (sem truncar); SetFocusTarget/TargetDirection x64
        "AscInitHooks",   // 23 detours do init; GetVisibleItem devolve ponteiro; retornos 0x62BAC8/0x57ABE4 por extra-addr; m_typeId +0x24
        "AscZoneLight",   // 2a lua = DNMoonGlare x64 (0xC0, vfptr copiado de g_moonGlare, m_planet +0xB8); DNPlanet m_scale +0x18/m_period +0x20
        "AscCursor", "AscClientGuards", "AscCVarHooks",   // CVar m_stringValue.m_str x64 +0x48; HOSFILE sem truncar
        "AscSmallHooks",   // operario 4c: 0x805F60 x64 3 args; CVar +0x54/+0x48; RealmEnum por variavel (patch 21)
        "AscPOI", "AscAura137", "AscRealmData", "AscClientOptions",   // lote5 da mestre: variaveis dos patches 02/04/08/10/11/17/20
        "AscBateria",        // bateria de testes: EXC 1a chance, SAIDA com pilha, TRAVADO, VIVO (Crashes\\bateria_*.log)
        "AscCrashContext",   // M2 +0x48/+0x74, lua_Debug x64, retornos ponteiro; 0x65C290 (GetRecord ICF) sem hook no x64
        "AscAttachFixes",   // retaddrs por funcao (AscX64::ReturnsInto/OutsideClient); cast jump x64 AddKnownSpell +0xB6D; quest items +0x84/+0x4C; 0x4181B0 sem hook
        "AscClientDbcPatch",   // SMSG_PATCH_*: escreve no registro x64 (PutField/InsertRecord), nao na sombra; 0x92A Spell via CompressRow x64
        "AscTooltipScaling",   // quest/leilao/cores por ASC_PTR; retorno 0x625231 por extra-addr (SetSpell +0x676); SetItem GUID2 ok
        "AscFontRender",   // GLYPHBITMAPDATA x64 0x40; CGxFont m_cellHeight/m_fontFlags/m_pixelSize 0x1DC/0x1E4/0x330
        "AscDiscord",   // SDK C ABI natural; x64 precisa do discord_game_sdk.dll x86_64 ao lado do exe (sem ele: desligado)
        "AscSpellMods", "AscSpellShadow", "AscSpellTokens",   // operario 4c: SpellRec nativo >=0x220 +0x10; sombra/tokens por variaveis do cliente (patches 22/23)
        "AscAttachLate",   // 32 byte-patches -> variaveis do cliente (patch 24, kX64Var/Covered/InSource); Hook6DAC10 retaddr por extra-addr
        nullptr };
}

#if ASC_X64
void* Ptr(uint32_t addr)
{
    if (!g_wowAddr)
    {
        g_wowAddr = reinterpret_cast<WowAddr_t>(GetProcAddress(GetModuleHandleA(nullptr), "WowAddr"));
        if (!g_wowAddr)
        {
            AscLog::Printf("AscX64: o exe nao exporta WowAddr (nao e o WowClient64 do CoA?)");
            return nullptr;
        }
    }
    void* p = g_wowAddr(addr);
    if (!p)
        AscLog::Printf("AscX64: 0x%06X sem equivalente no exe x64", addr);
    return p;
}
#endif

#if ASC_X64
static bool InitMH()
{
    if (!g_mhReady)
    {
        MH_STATUS s = MH_Initialize();
        g_mhReady = (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED);
        if (!g_mhReady)
            AscLog::Printf("AscX64: MH_Initialize falhou (%d)", static_cast<int>(s));
    }
    return g_mhReady;
}
#endif

void* Hook(uint32_t addr, void* detour, const char* what)
{
#if ASC_X64
    void* target = Ptr(addr);
    if (!target || !InitMH())
    {
        AscLog::Printf("AscX64: hook %s (0x%06X) NAO instalado", what ? what : "?", addr);
        return nullptr;
    }
    // Varios hooks no mesmo endereco (o x86 encadeia, ex. 0x766640: AscCVarHooks + AscRaidVideo): o MinHook so
    // aceita um por alvo, entao o novo detour se prende ao detour anterior (que ja chama o original). Ordem igual a
    // do x86: o mais novo roda primeiro e o trampolim dele leva ao anterior.
    // Tabela de TODOS os alvos (sao centenas de hooks; com 64 enchia antes de chegar no 0x7BD480/0x766640 e o
    // encadeamento nunca achava o anterior -- teste 2, 31/10).
    static void* s_top[2048][2];   // {alvo, detour mais recente}
    static size_t s_count = 0;
    void* hookAt = target;
    for (size_t i = 0; i < s_count; ++i)
        if (s_top[i][0] == target)
            hookAt = s_top[i][1];
    void* tramp = nullptr;
    MH_STATUS s = MH_CreateHook(hookAt, detour, &tramp);
    if (s != MH_OK || (s = MH_EnableHook(hookAt)) != MH_OK)
    {
        AscLog::Printf("AscX64: hook %s (0x%06X -> %p) falhou: %s", what ? what : "?", addr, hookAt, MH_StatusToString(s));
        return nullptr;
    }
    size_t slot = s_count;
    for (size_t i = 0; i < s_count; ++i)
        if (s_top[i][0] == target)
            slot = i;
    if (slot >= 2048)
        AscLog::Printf("AscX64: tabela de hooks cheia -- 0x%06X nao pode ser encadeado depois", addr);
    else
    {
        s_top[slot][0] = target;
        s_top[slot][1] = detour;
        if (slot == s_count)
            ++s_count;
    }
    if (hookAt != target)
        AscLog::Printf("AscX64: hook %s 0x%06X -> %p encadeado sobre o detour anterior %p", what ? what : "?", addr, target, hookAt);
    else
        AscLog::Printf("AscX64: hook %s 0x%06X -> %p", what ? what : "?", addr, target);
    return tramp;
#else
    (void)addr; (void)detour; (void)what;
    return nullptr;
#endif
}

bool Replace(uint32_t addr, void* fn, const char* what)
{
    return Hook(addr, fn, what) != nullptr;
}

void EnableAll()
{
#if ASC_X64
    if (g_mhReady)
        MH_EnableHook(MH_ALL_HOOKS);
#endif
}

void Unported(const char* what)
{
    AscLog::Printf("AscX64: rotina x86 NAO portada chamada: %s", what);
    RaiseException(0xE0C0A064, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    for (;;) {}
}

#if ASC_X64
bool WriteMem(uint32_t addr, const void* bytes, size_t n, const char* who)
{
    uint8_t* p = static_cast<uint8_t*>(Ptr(addr));
    if (!p)
        return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
    {
        AscLog::Printf("x64: patch de codigo 0x%06X (%u bytes, %s) recusado -- precisa versao x64", addr, static_cast<unsigned>(n), who);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(p, n, PAGE_READWRITE, &old))
        return false;
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    return true;
}
#else
bool WriteMem(uint32_t addr, const void* bytes, size_t n, const char*)
{
    DWORD old;
    void* p = reinterpret_cast<void*>(addr);
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return true;
}
#endif

#if ASC_X64
// ---- rebase em tempo de execucao ----------------------------------------------------------------------
// Os bytes x64 dos patches levam rel32 (call/jmp/jcc near) e disp32 RIP-relative que mudam a cada build do
// cliente. RelFields decodifica um subconjunto de x86-64 e devolve onde estao esses campos; RebaseRel copia
// para a referencia o valor vivo de cada campo cuja instrucao tem o MESMO formato na mesma posicao (bytes
// antes do campo iguais) e diferenca <= 1 MB. O resto dos bytes continua conferido exato.
namespace
{
    struct RelField { uint8_t insn, field; };

    size_t RelFields(const uint8_t* b, size_t n, RelField* out, size_t cap)
    {
        size_t i = 0, k = 0;
        while (i < n && k < cap)
        {
            const size_t start = i;
            bool op16 = false, rexW = false;
            while (i < n && (b[i] == 0x66 || b[i] == 0xF2 || b[i] == 0xF3 || b[i] == 0xF0 || b[i] == 0x2E || b[i] == 0x3E ||
                             b[i] == 0x26 || b[i] == 0x36 || b[i] == 0x64 || b[i] == 0x65))
                op16 |= b[i++] == 0x66;
            if (i < n && (b[i] & 0xF0) == 0x40)
                rexW = (b[i++] & 8) != 0;
            if (i >= n)
                break;
            const uint8_t op = b[i++];
            bool modrm = false;
            int imm = 0;          // bytes de imediato depois do modrm/disp
            int rel32 = -1;       // offset do rel32 (sem modrm)
            if (op == 0x0F)
            {
                if (i >= n) break;
                const uint8_t o2 = b[i++];
                if (o2 >= 0x80 && o2 <= 0x8F) rel32 = static_cast<int>(i);
                else if (o2 == 0x38) { if (i >= n) break; ++i; modrm = true; }
                else if (o2 == 0x3A) { if (i >= n) break; ++i; modrm = true; imm = 1; }
                else if ((o2 >= 0x10 && o2 <= 0x1F) || (o2 >= 0x28 && o2 <= 0x2F) || (o2 >= 0x40 && o2 <= 0x6F) ||
                         (o2 >= 0x74 && o2 <= 0x7F) || (o2 >= 0x90 && o2 <= 0x9F) || o2 == 0xA3 || o2 == 0xA5 || o2 == 0xAB ||
                         o2 == 0xAD || o2 == 0xAF || o2 == 0xB0 || o2 == 0xB1 || o2 == 0xB3 || (o2 >= 0xB6 && o2 <= 0xB7) ||
                         (o2 >= 0xBB && o2 <= 0xC1) || o2 == 0xC7 || (o2 >= 0xD0 && o2 <= 0xFE))
                    modrm = true;
                else if ((o2 >= 0x70 && o2 <= 0x73) || o2 == 0xA4 || o2 == 0xAC || o2 == 0xBA || o2 == 0xC2 ||
                         (o2 >= 0xC4 && o2 <= 0xC6))
                { modrm = true; imm = 1; }
                else break;
            }
            else if (op == 0xE8 || op == 0xE9) rel32 = static_cast<int>(i);
            else if (op == 0x90 || (op >= 0x50 && op <= 0x5F) || op == 0xC3 || op == 0xCC || op == 0x98 || op == 0x99 || op == 0xC9) {}
            else if ((op >= 0x70 && op <= 0x7F) || op == 0xEB || op == 0x6A || op == 0xA8 || (op >= 0xB0 && op <= 0xB7) ||
                     (op < 0x40 && (op & 7) == 4)) i += 1;
            else if (op == 0x68 || op == 0xA9 || (op < 0x40 && (op & 7) == 5)) i += op16 && op != 0x68 ? 2 : 4;
            else if (op >= 0xB8 && op <= 0xBF) i += rexW ? 8 : (op16 ? 2 : 4);
            else if ((op < 0x40 && (op & 7) < 4) || op == 0x63 || (op >= 0x84 && op <= 0x8B) || op == 0x8D || op == 0x8F ||
                     (op >= 0xD0 && op <= 0xD3) || (op >= 0xD8 && op <= 0xDF) || op == 0xFE || op == 0xFF)
                modrm = true;
            else if (op == 0x80 || op == 0x83 || op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0x6B) { modrm = true; imm = 1; }
            else if (op == 0x81 || op == 0x69 || op == 0xC7) { modrm = true; imm = op16 ? 2 : 4; }
            else if (op == 0xF6 || op == 0xF7)
            {
                if (i >= n) break;
                modrm = true;
                if (((b[i] >> 3) & 7) < 2) imm = op == 0xF6 ? 1 : (op16 ? 2 : 4);
            }
            else break;   // formato desconhecido: o resto fica estrito

            if (rel32 >= 0)
            {
                if (static_cast<size_t>(rel32) + 4 <= n) out[k++] = {static_cast<uint8_t>(start), static_cast<uint8_t>(rel32)};
                i = rel32 + 4;
                continue;
            }
            if (modrm)
            {
                if (i >= n) break;
                const uint8_t m = b[i++], mod = m >> 6, rm = m & 7;
                if (mod != 3 && rm == 4)
                {
                    if (i >= n) break;
                    const uint8_t sib = b[i++];
                    if (mod == 0 && (sib & 7) == 5) i += 4;
                }
                if (mod == 0 && rm == 5)
                {
                    if (i + 4 <= n) out[k++] = {static_cast<uint8_t>(start), static_cast<uint8_t>(i)};
                    i += 4;
                }
                else if (mod == 1) i += 1;
                else if (mod == 2) i += 4;
                i += imm;
            }
        }
        return k;
    }

    bool HasField(const RelField* f, size_t k, RelField x)
    {
        for (size_t j = 0; j < k; ++j)
            if (f[j].insn == x.insn && f[j].field == x.field) return true;
        return false;
    }
}

size_t RebaseRel(uint8_t* ref, const uint8_t* live, size_t n)
{
    if (n > 255) return 0;
    RelField fr[32], fl[32];
    const size_t kr = RelFields(ref, n, fr, 32), kl = RelFields(live, n, fl, 32);
    size_t fixed = 0;
    for (size_t j = 0; j < kr; ++j)
    {
        const RelField f = fr[j];
        if (!HasField(fl, kl, f) || memcmp(ref + f.insn, live + f.insn, f.field - f.insn) != 0)
            continue;
        int32_t a, b;
        memcpy(&a, ref + f.field, 4);
        memcpy(&b, live + f.field, 4);
        if (a == b) continue;
        const int64_t d = static_cast<int64_t>(b) - a;
        if (d > 0x100000 || d < -0x100000) continue;
        memcpy(ref + f.field, live + f.field, 4);
        ++fixed;
    }
    return fixed;
}

bool CodeMatches(const void* live, const void* ref, size_t n)
{
    if (!live || !ref || n > 255) return false;
    uint8_t tmp[255];
    memcpy(tmp, ref, n);
    RebaseRel(tmp, static_cast<const uint8_t*>(live), n);
    return memcmp(live, tmp, n) == 0;
}

namespace
{
    // Por site: bytes vivos do primeiro toque (o stock deste build) e o deslocamento aprendido de cada campo
    // rel32/disp32 (vivo - referencia). O deslocamento vem primeiro do `expect` (o estado que esta no exe) e
    // vale para o `bytes` na mesma posicao -- um jmp redirecionado anda junto com o codigo em vez de virar o
    // alvo do stock.
    struct Site { const uint8_t* p; size_t n; uint8_t b[64]; RelField f[8]; int32_t d[8]; size_t nf; };
    Site g_sites[256];
    size_t g_siteN = 0;
    SRWLOCK g_siteLock = SRWLOCK_INIT;

    Site* SiteFor(const uint8_t* p, size_t n)
    {
        if (n > sizeof(g_sites[0].b)) return nullptr;
        for (size_t i = 0; i < g_siteN; ++i)
            if (g_sites[i].p == p && g_sites[i].n == n) return &g_sites[i];
        if (g_siteN >= 256) return nullptr;
        Site& s = g_sites[g_siteN++];
        s.p = p; s.n = n; s.nf = 0; memcpy(s.b, p, n);
        return &s;
    }

    // Rebase de `arr` (copia local) no site: deslocamento aprendido, ou aprende copiando do stock vivo.
    size_t RebaseAt(Site& s, uint8_t* arr)
    {
        RelField fa[32], fo[32];
        const size_t ka = RelFields(arr, s.n, fa, 32), ko = RelFields(s.b, s.n, fo, 32);
        size_t fixed = 0;
        for (size_t j = 0; j < ka; ++j)
        {
            const RelField f = fa[j];
            int32_t v;
            memcpy(&v, arr + f.field, 4);
            size_t q = 0;
            while (q < s.nf && !(s.f[q].insn == f.insn && s.f[q].field == f.field)) ++q;
            if (q == s.nf)
            {
                if (!HasField(fo, ko, f) || memcmp(arr + f.insn, s.b + f.insn, f.field - f.insn) != 0)
                    continue;
                int32_t live;
                memcpy(&live, s.b + f.field, 4);
                const int64_t d = static_cast<int64_t>(live) - v;
                if (d > 0x100000 || d < -0x100000 || s.nf >= 8)
                    continue;
                s.f[s.nf] = f; s.d[s.nf] = static_cast<int32_t>(d); ++s.nf;
            }
            if (s.d[q] == 0) continue;
            v += s.d[q];
            memcpy(arr + f.field, &v, 4);
            ++fixed;
        }
        return fixed;
    }
}

bool WriteCodeX64(uint32_t addr, const void* bytesIn, size_t n, const void* expectIn, ptrdiff_t delta)
{
    uint8_t* p = static_cast<uint8_t*>(Ptr(addr));
    if (!p)
    {
        AscLog::Printf("x64: WriteCodeX64 0x%06X sem endereco x64 (falta a linha no extra-addr)", addr);
        return false;
    }
    p += delta;
    // rebase: rel32/disp32 dos dois estados vem do stock vivo deste build (capturado no 1o toque)
    uint8_t bytes[255], expectBuf[255];
    const uint8_t* expect = nullptr;
    if (n > sizeof(bytes))
        return false;
    memcpy(bytes, bytesIn, n);
    if (expectIn)
    {
        memcpy(expectBuf, expectIn, n);
        expect = expectBuf;
    }
    size_t fixed = 0;
    AcquireSRWLockExclusive(&g_siteLock);
    if (Site* site = SiteFor(p, n))
    {
        if (expect) fixed += RebaseAt(*site, expectBuf);   // 1o: o deslocamento sai do estado esperado
        fixed += RebaseAt(*site, bytes);
    }
    ReleaseSRWLockExclusive(&g_siteLock);
    if (fixed)
        AscLog::Printf("x64: WriteCodeX64 0x%06X: %zu campo(s) rel32/disp32 rebaseados", addr, fixed);
    if (memcmp(p, bytes, n) == 0)
        return true;   // ja aplicado
    if (expect && memcmp(p, expect, n) != 0)
    {
        char found[3 * 16 + 1] = {};
        for (size_t i = 0; i < n && i < 16; ++i)
            sprintf_s(found + i * 3, sizeof(found) - i * 3, "%02X ", p[i]);
        AscLog::Printf("x64: WriteCodeX64 0x%06X (%p): bytes nao conferem [%s] -- nao escrito", addr, p, found);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old))
    {
        AscLog::Printf("x64: WriteCodeX64 0x%06X: VirtualProtect falhou (%lu)", addr, GetLastError());
        return false;
    }
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return true;
}
#endif

bool ReturnsInto(const void* ret, uint32_t fn12340)
{
#if ASC_X64
    DWORD64 base = 0;
    const RUNTIME_FUNCTION* rf = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(ret), &base, nullptr);
    if (!rf)
        return false;
    // funcoes com unwind encadeado (UNW_FLAG_CHAININFO) tem varias entradas; a primeira e o inicio real
    const uint8_t* info = reinterpret_cast<const uint8_t*>(base + rf->UnwindData);
    while (info[0] >> 3 & 4)
    {
        const uint8_t count = info[2];
        rf = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + ((count + 1) & ~1) * 2);
        info = reinterpret_cast<const uint8_t*>(base + rf->UnwindData);
    }
    return reinterpret_cast<void*>(base + rf->BeginAddress) == Ptr(fn12340);
#else
    (void)ret, (void)fn12340;
    return false;   // x86: os chamadores comparam o endereco de retorno exato
#endif
}

bool OutsideClient(const void* ret)
{
#if ASC_X64
    static const uint8_t* lo = nullptr;
    static const uint8_t* hi = nullptr;
    if (!lo)
    {
        HMODULE exe = GetModuleHandleA(nullptr);
        MODULEINFO mi = {};
        if (K32GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof(mi)))
        {
            lo = static_cast<const uint8_t*>(mi.lpBaseOfDll);
            hi = lo + mi.SizeOfImage;
        }
    }
    const uint8_t* p = static_cast<const uint8_t*>(ret);
    return lo && (p < lo || p >= hi);
#else
    return reinterpret_cast<uintptr_t>(ret) > 0xDFCBFF;
#endif
}

bool ModulePorted(const char* name)
{
#if ASC_X64
    for (const char* const* p = kPorted; *p; ++p)
        if (strcmp(*p, name) == 0)
            return true;
    return false;
#else
    (void)name;
    return true;
#endif
}
}
