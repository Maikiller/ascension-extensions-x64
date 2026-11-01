# Patches propostos para o fonte do cliente (branch coa) — itens de out/x64-todo-cliente.md

Autor: projects-2c (ajudante), 30/10. NADA aplicado em src/ nem compilado. Todos os diffs (01..17) passam em `git apply --check -p1` em src/ @ 73df46c, cada um sozinho.

| # | item | resultado | arquivo |
|---|---|---|---|
| 1 | 0x7F3B60 MinimapGetZoomLevels: 8 só quando o chamador é 0x57BF59 | **patch no cliente** | 01-zoomlevels-0x7F3B60.diff |
| 2 | 0x744150 BuildSelectionRotMatrix: seta gira com a unidade | **patch no cliente** | 02-selection-arrow-0x744150.diff |
| 3 | AddLink 0x4EAA70 x ItemDisplayInfo | sem patch no cliente (fix no ext) | — |
| 4 | POI 0xA41E00 SetPOIMaxRange | **patch no cliente** | 04-poi-maxrange-0xA41E00.diff |
| 5 | cursor 0xB7A9DC | sem patch no cliente (endereço errado no ext) | — |
| 6 | 0xAF487C+0x14 | sem patch no cliente (é s_iconPositions[1].x) | — |
| 7 | GetTaxiNodeID 0xC0DC38+slot*0x30 | sem patch no cliente (fix no ext) | — |
| 8 | mid-function / operandos (out/midpatch-x64.md, tarefa 2) | **patch no cliente**, 13 diffs | 05..17-*.diff |

## 1. 0x7F3B60 — zoom levels
- 0x57BF59 é o retorno do `call 0x7F3B60` dentro de `Script_GetZoomLevels` (OFFSET 0x57BF50, MinimapFrame_Scripts.cpp:186), o único chamador filtrado.
- Patch: `Script_GetZoomLevels` empilha 8. `MinimapGetZoomLevels()` continua devolvendo 6 para os outros chamadores (igual ao x86).
- Ext: no x64 o `Detour7F3B60` fica desligado ou é removido (o patch já cobre).

## 2. 0x744150 — seta do círculo de seleção
- O x86 lia o objeto do `ecx` que o chamador deixava. Conferido no disasm dos 4 call sites: 0x7063CF (`mov ecx, esi` = this do CGCorpse_C::RenderTargetSelection), 0x725A46 (CGUnit_C::RenderTargetSelection), 0x725D8C (RenderPetTargetSelection) e 0x7276F1 (RenderAutoTrackCursor). Nos três últimos o ecx também é o this.
- vtable +0x94 = slot 37 = `GetRenderFacing() const`. `unit+8` = `m_obj` (layout.py: x64 +0x10).
- Patch: nova `CGObject_C::CoaBuildSelectionRotMatrix(object, matrix, pos)`. Com `ObjectSelectionCircleTexture` 2/3 ela monta identidade, depois `RotateAroundZ(-GetRenderFacing())` e `RotateAroundZ(pi)`. É o mesmo `Rz*m` do `Rotate` do DLL; os callers usam `C44Matrix()` = identidade, igual ao x86. Senão chama a original. Os 4 callers passam `this`.
- A `BuildSelectionRotMatrix` original fica intacta (OFFSET/protos iguais). Ext: no x64 o `CircleDetour` fica desligado.

## 3. AddLink 0x4EAA70 x ItemDisplayInfo — fix no ext, sem patch no cliente
- `CCharacterComponent::AddLink` (CharacterComponent.cpp:2037) só lê `displayRec->m_particleColorID`. A cópia x86 de 0x64/124 bytes tem outro layout no x64 (o registro tem `char*`).
- Ext x64: passar `AscClientDbc::Native(0xAD3DDC, id)` (registro do próprio cliente) em vez de `display`. Pontos: AscItemCollections 214/361, AscGlobalsJ 1639, AscCharacterCreate 907, AscModelMethods 460.
- Atenção: o 1º argumento é lido como `uint32_t` (`*(uint32_t*)(unit+0xB4)` e `*(uint32_t*)(*(player+0xB4C)+0x38)`). É um `CM2Model*`, que trunca no x64, e os offsets precisam passar pelo layout.py.

## 4. 0xA41E00 — SetPOIMaxRange
- 0xA41E00 (694.444) é lido só em `UpdatePointsOfInterest` (0x7F4666) = `static const float MAX_POI_DISTANCE` (MinimapFrame.cpp:65). No x64 o valor fica embutido no código, sem dado para escrever.
- Patch: vira `float MAX_POI_DISTANCE` com `// STATIC: 0x00A41E00` e ligação externa, para não virar constante (sem /GL no CMake). Depois do build, o mapaddr põe 0xA41E00 na tabela.
- Ext x64: tirar o `return 0` do TODO. O VirtualProtect fica inofensivo (.data).

## 5. Cursor 0xB7A9DC — endereço errado na reconstrução
- Nenhuma instrução do 12340 referencia 0xB7A9DC (cai em `CGChat::m_chatHistory[3]`). O alvo de `CSimpleTop::SetCursor(MipBits*)` (0x493C80) é **`CGlueMgr::m_cursorMipBits` = 0xB6A9DC** (STATIC já marcado, CGlueMgr.cpp:148). Ele é usado por CGlueMgr::Suspend/Resume (0x4D8997, 0x4D89B0, 0x4DA6E4, 0x4DA6EF) e carregado com `Interface\Cursor\Point.blp`. A diferença é exatamente 0x10000 (um dígito trocado).
- Já está no .addr (0xB6A9DC → RVA 0xA1BE50). Ext: trocar 0xB7A9DC → 0xB6A9DC, no x64 com `ASC_PTR` e leitura de ponteiro de 8 bytes (`MipBits*`, não `uint32_t`). No x86 também é bug: hoje lê lixo do histórico de chat. Mexer ou não no x86 é decisão sua (o x86 deixa de ficar idêntico).

## 6. 0xAF487C+0x14 — é s_iconPositions[1].x
- WorldText.cpp: 0xAF487C = `s_shadowOffset` (C2Vector, 8 B) e 0xAF4884 = `s_iconPositions[4]` (C3Vector). 0xAF487C+0x14 = 0xAF4890 = `s_iconPositions[1].x`.
- Confere com `kTextOther` do OnWorldTextScale: os 11 floats a partir de 0xAF487C são {0.002, 0.002 | 0,1,0 | 1,1,0 | 1,0,0} = shadowOffset + iconPositions[0..2].
- No x64 as duas variáveis não são contíguas (s_shadowOffset → RVA 0xA1B100, s_iconPositions → RVA 0x965E28). WorldTextRenderBatch lê s_iconPositions da memória (movss rip+...), então escrever funciona.
- Proposta extra-addr (campos, C3Vector = 12 B nos dois):
```
0xAF4888 =0xAF487C+0x4
0xAF488C =0xAF4884+0x8
0xAF4890 =0xAF4884+0xC
0xAF4894 =0xAF4884+0x10
0xAF4898 =0xAF4884+0x14
0xAF489C =0xAF4884+0x18
0xAF48A0 =0xAF4884+0x1C
0xAF48A4 =0xAF4884+0x20
```
  e no ext trocar `0xAF487C + i*4` / `0xAF487C + 0x14` por `ASC_PTR(endereço)` (0xAF4884 já está na tabela).

## 7. GetTaxiNodeID — 0xC0DC38 é m_nodes.m_data, a expressão do DLL está errada
- `CGTaxiMap::m_nodes` (STATIC 0xC0DC30) é um `TSGrowableArray<TaxiNodeInfo>`: x86 {alloc 0, count 4, data 8, chunk C}. Então 0xC0DC38 = **m_data**, e `0xC0DC38 + slot*0x30` anda pelas statics vizinhas (s_nodeMask 0xC0DC40...) para slot ≥ 1.
- Intenção provável: `m_nodes.m_data[slot].m_rec->m_id` (TaxiNodeInfo.m_rec +0 = TaxiNodesRec*, m_id +0).
- x64 (pdbfields/layout.py): TSBaseArray.m_data em +0x8, sizeof(TaxiNodeInfo) = **0x38**, m_rec +0, TaxiNodesRec.m_id +0:
```cpp
uint8_t* data = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(AscX64::Ptr(0xC0DC30)) + 0x8);
const TaxiNodesRec* rec = *reinterpret_cast<const TaxiNodesRec* const*>(data + slot * 0x38);   // int32 m_id em +0
```
- Suspeita também em `GetCurrentTaxiNodeID`: 0xC0D7EC = `static TaxiNodesRec* s_currentNode`, ou seja, empilha o ponteiro e não o id. Vale conferir no original (FUN_10217ce0/d20) antes de mexer no x86.

## 8. Tarefa 2 — patches no meio de função / em operando, levados para o fonte (05..17)
Convenção: tudo o que é novo fica dentro de `#if defined(_WIN64)`, e o `#else` mantém o código original, então o x86 não muda (VERIFY). Cada mudança vira uma variável global nova (ligação externa, sem `static`) com `// STATIC: 0x<endereço x86 do patch>`. O mapaddr leva a variável ao .addr e o ext x64 escreve nela via `ASC_PTR`, em vez de mexer nos bytes. Os valores iniciais reproduzem o original e os ponteiros de função começam nulos (nulo = chama o original).
Conferido: cada diff passa sozinho no `git apply --check` @ 73df46c, e o ramo x86 de cada arquivo (descartando o `#if _WIN64`) é igual ao original, tirando linhas em branco. No 16 entram macros (`COA_OCCLUSION_VOLUMES`/`_COUNT`) que no x86 expandem para `s_occlusionVolumes`/`62`, com tokens idênticos. Nada compilado.

### extra-addr (`tools/extra-addr.txt`) — nomes das variáveis (S_GDATA32 depois de compilar)
```
0x52B259 g_coaAutoSitIdleTime
0x52B266 g_coaAutoLogoutIdleTime
0x49DDBF g_coaSetPointRawOffsets
0x6194ED g_coaPortraitMaskSize
0x61773A g_coaPortraitMaskPath
0x7258CE g_coaNamePlates
0x4648B3 g_coaGameBuild
0x9F5200 g_coaGameBuildString
0x406851 g_coaConfigFile
0x52AB26 g_coaCreateScriptEvents
0x634E30 g_coaClientDbLoadAll
0x605E13 g_coaCameraCollideFlags
0x781426 g_coaZonesInitialize
0x781751 g_coaFindAndAddZoneLight
0x781730 g_coaFogModeMinMap
0x7CD8A0 g_coaOcclusionVolumes
0x7CD8A5 g_coaOcclusionVolumeCount
0x989B9A g_coaAura137
```
Atenção: se o mapaddr já resolve esses endereços pela instrução (fn+off), a linha do extra-addr precisa ter precedência. Senão o ext escreve no código.

### Quem passa a escrever (ext @ fb2a78b) — no x64 vira escrita de dado `*AscX64::Ptr<T>(addr) = v`
| diff | variável | ext arquivo:linha hoje | o que escrever no x64 |
|---|---|---|---|
| 05 | g_coaAutoSitIdleTime | src/Ascension/AscGlobalsG.cpp:47 (`PatchValue(0x52B259, -v)`) | `v` positivo em ms (0 = desligado). Sem o jmp em 0x52B25F |
| 05 | g_coaAutoLogoutIdleTime | AscGlobalsG.cpp:65 (0x52B266); AscAttachPatches.generated.inc:9 (0x52B27A jmp) | `v` positivo; o patch 0x52B27A sai do x64 |
| 06 | g_coaSetPointRawOffsets | AscSuperTrack.cpp:425-435 (NOP 0x49DDBF/0x49DDE5) | 1 = cru, 0 = original |
| 07 | g_coaPortraitMaskSize | AscGlobalsJ.cpp:1605 (set) / 1624 (clear) (+0x617721/0x61779F/0x619546/48, 0x61935B :1614/1632, 0x61770A :1615/1633) | tamanho / 0. Os outros patches saem do x64 |
| 07 | g_coaPortraitMaskPath | AscGlobalsJ.cpp:1610 / 1629 (+0x6177A6/0x6177B2) | `const char*` / nullptr |
| 08 | — | AscRealmData.cpp:400 (`ReplaceFunction(0x667EF9, CachePathHook)`) | nada: desligar no x64 |
| 09 | g_coaNamePlates | AscNamePlates.cpp:626 (0x7258CE) e os patches 0x72594B/0x98E9F9/0x98EA07 do mesmo Init | 1 |
| 10 | g_coaGameBuild | AscRealmData.cpp:239 (0x8CA5F0) e :240 (0x4648B3) | `build` (u32) — uma escrita cobre os dois |
| 10 | g_coaGameBuildString | AscRealmData.cpp:237 (ponteiro sobre 0x9F5200) | copiar os dígitos (`char[8]`), não ponteiro |
| 11 | g_coaConfigFile | AscMisc.cpp:457 (0x406851) | `const char*` estático com o nome |
| 12 | g_coaCreateScriptEvents | WotLKExtensions/src/Client/FrameScript.cpp:11 (rel32 0x52AB26) | `&RegisterEventEx` (assinatura `void(const char**, uint32_t)`) |
| 13 | g_coaClientDbLoadAll | WotLKExtensions/src/CDBCMgr/CDBCMgr.cpp:71 (rel32 0x634E30) | `&CDBCMgr::Register` (`void()`) |
| 14 | g_coaCameraCollideFlags | AscCamera.cpp:127 (0x605E13) | 0x100171 / 0x170 |
| 15 | g_coaZonesInitialize | WotLKExtensions/src/WorldData/ZoneLightData.cpp:12 (0x781426) | `&FillZoneLightData` |
| 15 | g_coaFogModeMinMap | ZoneLightData.cpp:14 (-1) e AscAttachPatches.generated.inc:14 ({00 00} → 0) | -1 (ou 0); qualquer um desliga o "map < 530" |
| 15 | g_coaFindAndAddZoneLight | ZoneLightData.cpp:15 (0x781751) | `&FindAndAddZoneLightEx` |
| 16 | g_coaOcclusionVolumes | AscGraphics.cpp:527 (`base + 4` em 0x7CD8A0) e :525 (`base + 8` em 0x7CCD30) | `base` (início da tabela, layout x64 de OcclusionVolume). Uma escrita cobre Init/Create/Render |
| 16 | g_coaOcclusionVolumeCount | AscGraphics.cpp:528 (0x7CD8A5) e :526 (0x7CCD35) | `count` |
| 17 | g_coaAura137 | AscAura137.cpp:39 (0x989B9A) e :35-37 (jne 0x988E48/0x989053/0x989B97) | 1/0; os 3 jne saem do x64 |

Fora: 0x63200D (já no detour ProcessMessage64), 0x4181B0 (anti-tamper, N/A), 0x9E0360 (.rdata → nullsub; tratar no ext junto com o hook).

Riscos / decisões suas:
- 10: o original do Ascension já escreve um PONTEIRO sobre o texto "12340" em 0x9F5200 (a reconstrução imita isso, AscRealmData.cpp:33). No x64, copiar o texto. GetBuildInfo da Glue usa FrameScript_GetText e fica igual. SE2::Init também lê 0x9F5200 e não mudei.
- 13: o DLL precisa estar carregado antes de ClientDBInitialize (hoje é carregado no MainFiberProc). Conferir a ordem; se vier depois, o ponteiro nulo cai no original sem quebrar.
- 14: o imm 0x120171 (água) não é patchado no x86. Aqui fica 0x20000 + g_coaCameraCollideFlags, então com 0x170 a água vira 0x20170. Conferir se o x86 mantinha 0x120171.
- 16: OcclusionVolume tem ponteiro em m_data. A tabela do DLL x64 precisa do layout x64 (layout.py). O ext ainda marca s_occlusionVolumesInit=0 e chama InitOcclusionVolumes (AscGraphics.cpp:529-530).
- 17: o NOP do jne (queda) = `UpdateAnchors(1)` (force só pula o teste de queda em CalcDirection, MovementShared.cpp:178).
- 08: no x64 passa a ser sempre por reino (é o que o hook fazia no x86).

## 41 — crash_log: estouro de pilha calado (p27, 02/11)
- `FirstChanceHandler` não chama mais `LogStack` (SymInitialize/SymFromAddr) em `EXCEPTION_STACK_OVERFLOW`, porque na sobra da página de guarda ele estourava de novo e o kernel matava o processo sem log nem dump.
- Tira `SEM_NOGPFAULTERRORBOX` do `SetErrorMode`. Ainda não está confirmado que ele bloqueia o WER LocalDumps (0 dumps do WER em 22 rodadas); validar com um crash forçado sem o procdump.
- Complementa o ext b254c92 (SetThreadStackGuarantee + SoHelper no AscBateria). `git apply --check -p1` ok em src/ @ coa-cliente-20261031 (5bb308e). NÃO aplicado.

## 40 — SpellTableBuildSkillLineLinkInfo (0x812030): jogador buscado uma vez (b6, 02/11)
- Travada de ~9 s ao entrar no mundo (bateria 01/11 12:52:25, amostra aos 8 s): `CGSpellBook::AddKnownSpell` →
  `SpellTableLookupAbility` → build de `CharBaseInfo × SkillLineAbility` (~1000 × 41000 no CoA Custom, 62 no repack),
  com `ClntObjMgrObjectPtr` em cada volta dentro de `SpellTableAddSkillLineRec`.
- Patch (só `_WIN64`): jogador buscado uma vez; linha com raça E classe diferentes das do jogador pula o laço interno
  (o `AddSkillLineRec` já recusaria todas), exceto a 1ª linha válida, que monta `s_skillLineLinkInfo`. Resultado igual.
- `git apply --check -p1` OK em src/ @ 5bb308e (coa-cliente-20261031). Não aplicado nem compilado.

## 42 — TalentGroupTab_C: cópia toma a posse dos tiers (c1, 02/11) — D16
- Pilha D16 (bateria 01/11 12:41:18, C0000374): `SMemFree` ← `~TSFixedArray<TSExplicitList<TalentGroupTierItem_C>>`
  (slots do hash de um tier) ← `~TalentGroupTab_C` ← `CGTalentGroup_C::Reset` ← `CGTalentInfo_Inspect::Reset` ←
  `UpdateTalentData` ← `SMSG_INSPECT_TALENT`.
- Causa: o `SMemReAlloc` reconstruído (SMem.cpp) devolve `nullptr` SEMPRE que há `SMEM_FLAG_NOREALLOC`; então
  `TSFixedArray<T>::ReallocData` sempre copia (`new T(old)`) e destrói o antigo. `TalentGroupTab_C` não tinha cópia própria:
  a implícita duplica os ponteiros de `m_tiers` e o `~TalentGroupTab_C` do antigo dá `delete` nos tiers → a aba nova fica
  com ponteiros soltos e o próximo `Reset` libera de novo (double free).
- Gatilho: `SetInspectTarget` → `UpdateTalentGroups` SEM `Reset` → `UpdateTabs` → `m_tabs.SetCount(n)` com `n` ≠ nº de
  abas do alvo anterior (≠ 0). No 3.3.5 puro toda classe tem 3 abas (latente); no CoA a contagem varia por classe.
  O Storm original tenta no lugar (encolher sempre dá), por isso o x86 não mostra.
- Patch (só `_WIN64`, TalentInfo.h): construtor de cópia que copia `m_tiers` e esvazia o da origem (`const_cast` +
  `Clear`); `operator=` deletado (não é instanciado). Muda código → RVAs andam (rebase normal).
- `git apply --check -p1` OK em src/ @ fe04e56 (coa-cliente-20261031). Branch src `x64-c1-d16` (05276be). NÃO aplicado na
  branch do mestre.
- Fica de fora (avaliar): outros `TSFixedArray<T>` com T dono de ponteiro sofrem do mesmo; correção geral seria o
  `SMemReAlloc` tentar `_expand` antes de devolver nulo, mas crescer ainda copiaria.

## 43 — ChatChannel: cópia move os m_pendingUsers (c1, 02/11) — proposta, NÃO aplicado
- Mesmo mecanismo do 42: `TSGrowableArray<ChatChannel>::ReallocData` sempre copia+destrói (SMemReAlloc NOREALLOC → nullptr).
  `s_channels` cresce com m_count ≠ 0 em `ChatFrame.cpp` (`SetCount(number)` com N > m_count; `SetCount(m_count + 1)` sem slot livre).
- O copy ctor do `TSList` só copia o offset: a lista nova nasce vazia, o `~TSList` do antigo só faz UnlinkAll → os nós
  `PENDINGUSERLIST` (NewNode do SMSG_CHANNEL_LIST, nomes ainda fora do name cache) vazam e a listagem pendente do canal
  some (o callback do name cache não acha mais ninguém). Sem corrupção de heap; efeito funcional + vazamento.
- Patch (só `_WIN64`, ChatFrame.h): `ChatPendingUserList` = `TSExplicitList<PENDINGUSERLIST, 0>` + cópia que MOVE os nós
  (`Combine(&src, 2, nullptr)`, o mesmo idioma de ChatFrame.cpp ao imprimir os pendentes); `operator=` deletado
  (não há atribuição de ChatChannel). Layout igual (sem campo novo). Muda código → RVAs andam.
- Teste: estar em ≥ 2 canais; com o name cache frio (bots recém-vistos, sem /who), `/chatlist 1` (canal com muitos
  desconhecidos) e no MESMO instante `/join <canal novo>` (sem slot livre → s_channels cresce). Antes: a lista do canal 1
  nunca aparece (ou sai incompleta). Depois: aparece quando os nomes chegam. Repetir 20× sem crash e sem crescer a memória.
- `git apply --check -p1` OK em src/ @ 05276be. Branch src `x64-c1-43`.
- Levantamento PDB (out/cliente-05276be): 253 T não-ponteiro com `TSFixedArray<T>::ReallocData`; os 139 sem <T> no OFFSET
  são 112 `TSExplicitList<…>` (slots de TSHashTable; o grow esvazia as listas antes do SetCount → seguro) e 27 POD/deep-copy
  (EditHistory tem cópia própria com SStrDupA). Nenhum dono de memória novo.
