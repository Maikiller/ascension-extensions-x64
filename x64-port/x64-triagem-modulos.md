# Triagem x64 dos 106 modulos fora do kPorted (projects-6a, 30/10, base 142b557)

Gerado por `ws-6a/scripts/triagem.py` sobre a saida de checksig/objoffs/rawaddr/missing + varredura do codigo
VIVO no x64 (fora de `#if !ASC_X64` / `#else` de `ASC_X64`). Colunas:
- **bloqueios** = patch de codigo x86 vivo (WriteCode/PatchBytes/... em endereco < 0xA00000, sem WriteCodeX64) +
  `Unported` + `__asm`/naked + raw addr + endereco sem par x64 + offset de objeto sem ASC_OFF (objoffs).
- **ptr->u32** / **ptr+0xNN a conferir** = suspeitas (heuristica; muitas sao DBC-copia/descriptor e estao certas).
- checksig geral: ok 939 / diverge 0 / sem_proto 9. ⚠ checksig NAO pega `uint32_t` onde o proto e ponteiro
  (ver o achado do 0x724820 abaixo).

## Revisao manual (6a) -- 31 modulos com 0 bloqueio e 0 suspeita
- **Prontos p/ kPorted** (contato com o exe so por DBC-copia, descriptor, Lua, pacote, chamadas com proto ok):
  AscAttachInline*, AscGameObjectUse, AscSkillCardSlots, AscSkillCardCollection (depois do fix RLE),
  AscRecoveryService, AscCustomStore, AscGMTicket, AscDbcReaders, AscGroupFinder, AscMysticEnchantPreset,
  AscTrialCreator, AscTemplates, AscGlobalsH, AscSpellActivationOverlay, AscAlternatePower, AscAuraAmounts.
  *AscAttachInline: todos os sites (menos 0xAA2D0E dado e 0x6DB8F9/0x67A18E PEND) ja estao no kPatchesX64 de
  out/operands-x64.diff -> no x64 o WriteMem dele so loga "recusado"; da p/ pular os de codigo.
- **Corrigidos em out/x64-fixes-6a-4.diff** (aplica em 142b557, syntax-check mingw x64/x86 ok):
  - AscAlphaAmbience: escrevia na COPIA-sombra de Row (perdido no x64) -> AscClientDbc::Field(0xAD464C, id, 4/8).
  - AscSpellVisuals: typeId `U32(object,0x14)` -> ASC_OFF(0x14,0x24) (4x); `_ReturnAddress()==0x80ED4E` -> no x64
    ASC_PTR(0x80ED4E) = retorno do `call r10` em OnSpellGo (**extra-addr: `0x80ED4E @0x29D601`**).
  - ⚠ AscRuntime Detour724820/71E930 + UnitSpellVeto: o 3o arg e `const SpellRec*` (AddAuraEffect/RemoveAuraEffect)
    e estava `uint32_t` -> truncava o ponteiro no x64. Agora `uintptr_t` (x86 = mesmo tipo) + os 7 callbacks
    (AscAura137 x2, AscSpellMods x2, AscSpellVisuals, AscAppearance, AscSpawnVisibility).
- **Precisam de trabalho**: AscDiscord (SDK x64 + static_assert), AscBridgeStore (CreatureRecord/VanityRecord com
  string, PENDENCIAS), AscBuildCreator (ZStream), AscMemoryBridge (Detour 0x4067F0 9 bytes = patch x86 cru),
  AscCursor (32 ASC_PTR + 4 hooks, nao revisado), AscCacheFiles/AscPacketApi/AscLuaDbc/AscJson/AscLibrary/
  AscHotPatch/AscGlobalsF/AscExtraActionButton (ASC_PTR 6-13 cada, nao revisados a fundo).

## Mais pesados (bloqueios)
AscGraphics 62, AscGlobalsJ 50, AscInitHooks 23, AscAttachFixes 19, AscCamera 16, AscCAMgr 13,
AscTooltipScaling 12, AscClientOptions 12, AscSpellMods 10, AscNamePlates 10.

## Tabela
| modulo | linhas | patch codigo x86 vivo | Unported | asm | raw addr | sem addr x64 | obj offset | bloqueios | ptr->u32 | ptr+0xNN a conferir |
|---|---|---|---|---|---|---|---|---|---|---|
| AscAlphaAmbience | 46 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscAlternatePower | 120 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscAttachInline | 42 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscAuraAmounts | 283 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscBridgeStore | 1433 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscBuildCreator | 2596 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscCacheFiles | 147 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscCursor | 232 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscCustomStore | 555 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscDbcReaders | 133 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscDiscord | 367 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscExtraActionButton | 236 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscGMTicket | 1840 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscGameObjectUse | 23 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscGlobalsF | 154 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscGlobalsH | 138 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscGroupFinder | 630 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscHotPatch | 745 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscJson | 298 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscLibrary | 98 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscLuaDbc | 361 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscMemoryBridge | 890 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscMysticEnchantPreset | 308 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscPacketApi | 215 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscRecoveryService | 708 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscSkillCardCollection | 1230 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscSkillCardSlots | 542 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscSpellActivationOverlay | 230 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscSpellVisuals | 304 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscTemplates | 242 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscTrialCreator | 914 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 0 |
| AscAttachMisc | 131 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 1 | 0 |
| AscCAFilter | 1372 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscCallboardCache | 155 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscChallenge | 2295 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscDraftMode | 666 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscGlobalsI | 214 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscSceneTransform | 76 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 1 | 0 |
| AscScrollOfFortune | 462 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscSmallApis | 603 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscWildcardRolls | 599 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 1 |
| AscGlobalsC | 196 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 2 |
| AscTerrainScale | 74 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 2 |
| AscTutorial | 1316 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 2 |
| AscAppearance | 2078 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 3 |
| AscFontRender | 392 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 0 | 3 |
| AscGeosets | 204 | 0 | 0 | 0 | 0 | 0 | 0 | **0** | 2 | 4 |
| AscGameMode | 300 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 0 |
| AscItemUse | 85 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 0 |
| AscLoadingScreen | 101 | 0 | 1 | 0 | 0 | 0 | 0 | **1** | 0 | 0 |
| AscPOI | 134 | 0 | 0 | 0 | 0 | 1 | 0 | **1** | 0 | 0 |
| AscProfiler | 484 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 0 |
| AscZoneLight | 256 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 0 |
| AscAttachLate | 185 | 0 | 1 | 0 | 0 | 0 | 0 | **1** | 1 | 0 |
| AscSmallHooks | 225 | 0 | 1 | 0 | 0 | 0 | 0 | **1** | 0 | 1 |
| AscUnitTokens | 61 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 1 | 0 |
| AscGossipQuest | 162 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 1 | 1 |
| AscCanToggle | 183 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 3 |
| AscSpellApi | 411 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 4 |
| AscCharacterCreate | 1580 | 0 | 0 | 0 | 1 | 0 | 0 | **1** | 0 | 6 |
| AscCrashContext | 383 | 0 | 0 | 0 | 0 | 1 | 0 | **1** | 0 | 7 |
| AscAttachLua | 338 | 0 | 0 | 0 | 0 | 2 | 0 | **2** | 0 | 0 |
| AscClientGuards | 116 | 1 | 0 | 0 | 0 | 1 | 0 | **2** | 0 | 0 |
| AscModelMethods | 544 | 0 | 0 | 0 | 2 | 0 | 0 | **2** | 0 | 0 |
| AscLargeAlloc | 82 | 0 | 0 | 0 | 0 | 2 | 0 | **2** | 0 | 1 |
| AscSpawnVisibility | 119 | 0 | 0 | 0 | 2 | 0 | 0 | **2** | 0 | 1 |
| AscWorldMap | 139 | 0 | 0 | 0 | 2 | 0 | 0 | **2** | 0 | 2 |
| AscSpellTokens | 443 | 0 | 2 | 0 | 0 | 0 | 0 | **2** | 2 | 1 |
| AscMisc | 524 | 0 | 0 | 0 | 0 | 1 | 1 | **2** | 0 | 8 |
| AscAura137 | 75 | 0 | 0 | 0 | 0 | 3 | 0 | **3** | 0 | 0 |
| AscCVarHooks | 183 | 0 | 1 | 0 | 2 | 0 | 0 | **3** | 0 | 0 |
| AscMovementForce | 714 | 0 | 1 | 0 | 2 | 0 | 0 | **3** | 0 | 0 |
| AscGlobalsG | 210 | 2 | 0 | 0 | 0 | 1 | 0 | **3** | 0 | 1 |
| AscItemStats | 255 | 0 | 0 | 0 | 3 | 0 | 0 | **3** | 0 | 1 |
| AscEventPackets | 505 | 0 | 0 | 0 | 3 | 0 | 0 | **3** | 1 | 1 |
| AscLootLockout | 401 | 0 | 0 | 0 | 3 | 0 | 0 | **3** | 0 | 3 |
| AscMysticEnchant | 3070 | 0 | 0 | 0 | 3 | 0 | 0 | **3** | 0 | 3 |
| AscItemCollections | 501 | 0 | 0 | 0 | 3 | 0 | 0 | **3** | 3 | 1 |
| AscGlobalsE | 176 | 0 | 0 | 0 | 4 | 0 | 0 | **4** | 0 | 0 |
| AscModelNames | 138 | 0 | 0 | 0 | 3 | 0 | 1 | **4** | 0 | 0 |
| AscSpellText | 765 | 0 | 0 | 0 | 4 | 0 | 0 | **4** | 0 | 2 |
| AscAddonTaint | 233 | 0 | 4 | 0 | 1 | 0 | 0 | **5** | 1 | 0 |
| AscGlobalsD | 354 | 0 | 0 | 0 | 4 | 0 | 1 | **5** | 0 | 1 |
| AscClientDbcPatch | 367 | 0 | 0 | 0 | 5 | 0 | 0 | **5** | 3 | 0 |
| AscGlobalsB | 414 | 0 | 0 | 0 | 5 | 0 | 0 | **5** | 1 | 3 |
| AscSpellShadow | 182 | 0 | 0 | 0 | 5 | 0 | 0 | **5** | 4 | 2 |
| AscUnitSelect | 426 | 0 | 0 | 0 | 2 | 0 | 3 | **5** | 7 | 2 |
| AscAssetQuery | 180 | 0 | 0 | 0 | 6 | 0 | 0 | **6** | 0 | 0 |
| AscManastorm | 1217 | 0 | 0 | 0 | 6 | 0 | 0 | **6** | 0 | 0 |
| AscSuperTrack | 578 | 0 | 0 | 0 | 4 | 2 | 0 | **6** | 0 | 0 |
| AscTemporalContracts | 681 | 0 | 0 | 0 | 6 | 0 | 0 | **6** | 0 | 0 |
| AscWorldEntryTweaks | 155 | 3 | 0 | 0 | 3 | 0 | 0 | **6** | 0 | 1 |
| AscVanityCollection | 721 | 0 | 0 | 0 | 7 | 0 | 0 | **7** | 0 | 0 |
| AscRaidVideo | 163 | 0 | 0 | 0 | 8 | 0 | 0 | **8** | 1 | 0 |
| AscRealmData | 435 | 3 | 1 | 0 | 1 | 4 | 0 | **9** | 0 | 0 |
| AscAttachPatches | 123 | 3 | 0 | 0 | 6 | 0 | 0 | **9** | 6 | 0 |
| AscSpellMods | 595 | 0 | 0 | 0 | 10 | 0 | 0 | **10** | 0 | 1 |
| AscNamePlates | 690 | 1 | 0 | 0 | 5 | 4 | 0 | **10** | 0 | 4 |
| AscClientOptions | 411 | 8 | 0 | 0 | 0 | 4 | 0 | **12** | 0 | 2 |
| AscTooltipScaling | 1271 | 2 | 0 | 0 | 7 | 2 | 1 | **12** | 0 | 16 |
| AscCAMgr | 5599 | 0 | 0 | 0 | 13 | 0 | 0 | **13** | 0 | 1 |
| AscCamera | 339 | 8 | 0 | 0 | 4 | 4 | 0 | **16** | 0 | 6 |
| AscAttachFixes | 1113 | 7 | 0 | 0 | 6 | 6 | 0 | **19** | 0 | 7 |
| AscInitHooks | 359 | 8 | 0 | 0 | 8 | 0 | 7 | **23** | 1 | 5 |
| AscGlobalsJ | 1769 | 20 | 3 | 0 | 13 | 10 | 4 | **50** | 1 | 6 |
| AscGraphics | 761 | 19 | 0 | 0 | 32 | 11 | 0 | **62** | 3 | 2 |

limpos (0 bloqueio, 0 suspeita): 31: AscAlphaAmbience, AscAlternatePower, AscAttachInline, AscAuraAmounts, AscBridgeStore, AscBuildCreator, AscCacheFiles, AscCursor, AscCustomStore, AscDbcReaders, AscDiscord, AscExtraActionButton, AscGMTicket, AscGameObjectUse, AscGlobalsF, AscGlobalsH, AscGroupFinder, AscHotPatch, AscJson, AscLibrary, AscLuaDbc, AscMemoryBridge, AscMysticEnchantPreset, AscPacketApi, AscRecoveryService, AscSkillCardCollection, AscSkillCardSlots, AscSpellActivationOverlay, AscSpellVisuals, AscTemplates, AscTrialCreator
0 bloqueio mas com suspeitas a conferir: 16: AscAttachMisc, AscCAFilter, AscCallboardCache, AscChallenge, AscDraftMode, AscGlobalsI, AscSceneTransform, AscScrollOfFortune, AscSmallApis, AscWildcardRolls, AscGlobalsC, AscTerrainScale, AscTutorial, AscAppearance, AscFontRender, AscGeosets

zero pendencia automatica: 31 de 106: AscAlphaAmbience, AscAlternatePower, AscAttachInline, AscAuraAmounts, AscBridgeStore, AscBuildCreator, AscCacheFiles, AscCursor, AscCustomStore, AscDbcReaders, AscDiscord, AscExtraActionButton, AscGMTicket, AscGameObjectUse, AscGlobalsF, AscGlobalsH, AscGroupFinder, AscHotPatch, AscJson, AscLibrary, AscLuaDbc, AscMemoryBridge, AscMysticEnchantPreset, AscPacketApi, AscRecoveryService, AscSkillCardCollection, AscSkillCardSlots, AscSpellActivationOverlay, AscSpellVisuals, AscTemplates, AscTrialCreator
