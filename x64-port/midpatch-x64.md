# Patches no meio de funcao: equivalentes x64 (ALTA + MEDIA de out/missing-classif.md)

projects-2c, 30/10. Cada linha foi conferida pelo disasm dos dois exes: destino e queda do salto (primeiro call mapeado) e/ou ancoras. Bytes x64 lidos do out/WowClient64.exe.
8 linhas tinham candidato errado na classificacao: 0x793A45, 0x7B3A8E, 0x80C8C1, 0x62442C (CORRIGIDO na nota) e 0x68E4AD, 0x73A0D0, 0x80D6EF, 0x8540D7 (o x64 tem outra forma; endereco novo na coluna). "restaurar" = gravar os bytes x64 originais da coluna, nunca os bytes x86 do ext.

| ext arquivo:linha | x86 | efeito no x86 | x64 (funcao+off) | bytes x64 originais | bytes x64 propostos | risco | nota |
|---|---|---|---|---|---|---|---|
| AscRealmData.cpp:390 | 0x4041BD | call DBCache_Initialize (E8 5E 13 23 00) -> NOP5 (sempre) | 0x140034F74 (InitializeClient+0x94) | E8 F7 0A 19 00 `call 0x1401c5a70` | 90 90 90 90 90 | baixo | alvo do call confere (DBCache_Initialize nos dois) |
| AscSuperTrack.cpp:425/430 | 0x49DDBF | 0x1C bytes (aspect*1024, fdivr, NDCToDDCWidth do offsetX) <-> NOP | **fonte** | - | - | - | FONTE: x87 inline, no x64 o calculo e SSE intercalado -> 06-setpoint-raw-offsets.diff |
| AscAccount.cpp:122 | 0x50D2B7 | jne +0x16 (75 16) -> jmp (EB) | 0x1404C4DBA (Script_SendChatMessage+0x18A) | 75 0F `jne 0x1404c4dcb` | EB 0F | baixo | alvo/queda conferem (lua_isstring / DisplayError 0x224) |
| AscAccount.cpp:121 | 0x50D641 | je +0x52 (74 52) -> jmp (EB) | 0x1404C50BA (Script_SendChatMessage+0x48A) | 74 29 `je 0x1404c50e5` | EB 29 | baixo | alvo/queda conferem (FlagTutorial / DisplayError) |
| AscGlobalsG.cpp:54/59 | 0x52B27A | jne near -> jmp (E9 D3 FD FF; volta 0F 85 D2 FD FF FF) | 0x1403DD887 (CGGameUI::Idle+0x3F7) | 75 65 `jne 0x1403dd8ee` | EB 65 | medio | byte-patch possivel (75 65 -> EB 65), mas o par 0x52B266 (imm do tempo) e operando -> preferir 05-afk-timers.diff (variaveis) |
| AscModelMethods.cpp:375 | 0x59714D | je (74) <-> op (SetFrameJumps) | 0x1404A55B9 (CGCharacterModelBase::RotateModel+0x69) | 74 53 `je 0x1404a560e` | 1o byte = op (74/75/EB) do ext | baixo | escrever o MESMO opcode (74/EB/75...) no 1o byte: forma curta igual |
| AscModelMethods.cpp:374 | 0x5971C1 | jne (75) <-> op (SetFrameJumps) | 0x1404A5935 (CGCharacterModelBase::UpdateModel+0x15) | 75 71 `jne 0x1404a59a8` | 1o byte = op (74/75/EB) do ext | baixo | idem: 1o byte = op |
| AscCAMgr.cpp:477/479 | 0x5A8C70 | jne (75) -> jmp (EB) durante 0x5AAB90, depois volta 75 | 0x14042B3F6 (CGActionBar::GetSpell+0x56) | 75 17 `jne 0x14042b40f` | EB 17 | baixo | alvo/queda conferem (IsItem / HasMacro) |
| AscGlobalsJ.cpp:1351 | 0x5B01C8 | je near (0F 84 F9 00 00 00) <-> NOP6 (SetCurrencyShow) | 0x14045EF8D (CGCurrencyTypes::BuildDisplayList+0x1DD) | 0F 84 ED 00 00 00 `je 0x14045f080` | 90 90 90 90 90 90 | baixo | queda confere (GetActivePlayer, ItemStats GetRecord); restaurar com os bytes x64 originais, nao kJe x86 |
| AscClientOptions.cpp:285 | 0x60FAB2 | jle (7E 0B) <-> NOP2 | 0x14040FC04 (Script_UnitLevel+0xF4) | 7E 0C `jle 0x14040fc12` | 90 90 | baixo | queda confere (IsBossMob) |
| AscGlobalsJ.cpp:1206 | 0x614AF8 | je near (0F 84 8D..) <-> NOP6 (UnitAuraAllowHidden) | 0x1403EA314 (PushUnitAura+0x344) | 0F 84 8F 00 00 00 `je 0x1403ea3a9` | 90 90 90 90 90 90 | baixo | queda confere (FindRaidBuff); alvo = dtor dobrado |
| AscGlobalsJ.cpp:1600/1618 | 0x61935B | je (74) <-> jmp (EB) (mascara de retrato) | 0x1403B941C (SetPortraitTexture+0x4C) | 74 11 `je 0x1403b942f` | EB 11 | medio | byte ok, mas o resto do recurso (0x6194ED.. 0x6177B2) e operando -> 07-portrait-mask.diff; com o diff este byte-patch nao e necessario |
| AscSpellTokens.cpp:310 | 0x62442C | jne near (0F 85 14 02..) <-> jmp (E9 15 02 00 00) | 0x1403B24F1 (CGTooltip::SetSpell+0x431) | 75 32 `jne 0x1403b2525` | EB 32 | medio | x64 tem 2 jne (o x86 1o testa esi): 0x1403B24F1 vai p/ 0x1403B2525 que cai em 0x1403B252C = fim do bloco de cast time; jmp no 1o pula o bloco inteiro |
| AscTooltipScaling.cpp:139 | 0x631760 | je near -> jmp (E9 DF 00 00 00 90) | 0x1403ABFD9 (CGItemStatsSummary::GetItemComparison+0x1E9) | 0F 84 F6 01 00 00 `je 0x1403ac1d5` | E9 F7 01 00 00 90 | baixo | 1a CreateFromTooltip (a 2a e 0x1403ABFEE) |
| AscRealmData.cpp:396 | 0x667EF9 | meio de GetCacheDirectory -> jmp CachePathHook (Cache/WDB/<locale>/<realm>) | **fonte** | - | - | - | FONTE: 08-cache-path-realm.diff |
| AscClientGuards.cpp:36 | 0x68E4AD | 11 NOPs: pula CGxDevice::DeviceSetBaseMipLevel | 0x140106C78 (CGxDeviceD3d::DeviceSetBaseMipLevel+0x28) | E9 23 3B FE FF `jmp 0x1400ea7a0` | C3 90 90 90 90 | baixo | x64 faz tail-jmp (E9) apos o epilogo -> trocar o jmp por ret |
| AscFontRender.cpp:357 | 0x6BE290 | call 0x990650 (FT_Add_Default_Modules) -> NOP5 | 0x140118E0B (GxuFontInitialize+0x8B) | E8 00 CB 57 00 `call 0x140695910` | 90 90 90 90 90 | baixo | alvo x64 = FT_Add_Default_Modules (ancoras) |
| AscFontRender.cpp:355 | 0x6CA067 | setne al (0F 95 C0) -> mov al,1; nop (B0 01 90) | 0x1401243F4 (TEXTURECACHE::CreateTexture+0x24) | 0F 95 C0 `setne al` | B0 01 90 | baixo | mesmos bytes no x64 |
| AscNamePlates.cpp:644 | 0x71557D | jne (75 28) -> jmp (EB) | 0x1402D4A01 (CGUnit_C::ObjectNameVisibilityChanged+0x21) | 75 18 `jne 0x1402d4a1b` | EB 18 | baixo | alvo confere (SetBoneSequence) |
| AscNamePlates.cpp:645 | 0x715586 | jne (75 1F) -> jmp (EB) | 0x1402D4A0B (CGUnit_C::ObjectNameVisibilityChanged+0x2B) | 75 0E `jne 0x1402d4a1b` | EB 0E | baixo | cmp [+0xC38] -> x64 cmp qword [rbx+0x1470] |
| AscCAMgr.cpp:307 | 0x728AA1 | cmp ebx,6 (83 FB 06) <-> cmp ebx,ebx; nop (39 DB 90) | 0x1402C7B70 (CGUnit_C::DoPowerRegen+0x90) | 40 80 FF 06 `cmp dil, 6` | 40 38 FF 90 | baixo | x64: cmp dil,6 (40 80 FF 06) -> cmp dil,dil; nop (40 38 FF 90) |
| AscClientOptions.cpp:302 | 0x729D3D | je (74 42) <-> NOP2 | 0x1402E5582 (CGUnit_C::ShouldRenderObjectName+0xD2) | 74 3B `je 0x1402e55bf` | 90 90 | baixo | queda confere (CanCooperate) |
| AscNamePlates.cpp:584 | 0x72B07A | jle (7E 5F) <-> NOP2 | 0x1402E579C (CGUnit_C::ShouldShowNamePlate+0x2C) | 0F 8E 07 03 00 00 `jle 0x1402e5aa9` | 90 90 90 90 90 90 | medio | x64 e jle NEAR (6 bytes) -> NOP6; restaurar com os 6 bytes originais |
| AscSpellMods.cpp:477/514 | 0x7370AF | je (74) <-> jmp (EB) | 0x1402E3FED (CGUnit_C::SetSheatheState+0x1AD) | 74 07 `je 0x1402e3ff6` | EB 07 | baixo | alvo/queda conferem (SheatheStateAnimate/Instant) |
| AscSpellMods.cpp:103 | 0x73A0D0 | 0x20 NOPs: pula SpellFailed(0x33)+return false | 0x1402C10C2 (CGUnit_C::CheckAndReportSpellInhibitFlags+0xA52) | C6 44 24 28 00 `mov byte ptr [rsp + 0x28], 0` | EB 29 | baixo | x64: no inicio do bloco (mov byte [rsp+0x28],0) escrever jmp 0x1402C10ED (EB 29) = o caminho seguinte |
| AscCamera.cpp:101 | 0x7569B6 | call ClearTrackingTarget (E8..) <-> NOP5 | 0x14035A866 (Packet_Group_29+0x216) | E8 F5 83 F6 FF `call 0x1402c2c60` | 90 90 90 90 90 | baixo | alvo x64 = ClearTrackingTarget |
| AscGraphics.cpp:164 | 0x792AF2 | je near (0F 84 C6..) <-> NOP6 | 0x14051F084 (CWorldScene::AddMapObjDefGroup+0x24) | 0F 84 FA 00 00 00 `je 0x14051f184` | 90 90 90 90 90 90 | baixo | test eax,0x10008 igual |
| AscGraphics.cpp:165 | 0x792AFF | je (74 11) <-> NOP2 | 0x14051F091 (CWorldScene::AddMapObjDefGroup+0x31) | 74 19 `je 0x14051f0ac` | 90 90 | baixo | test [+0xC],0x400 -> x64 [rdi+0x10] |
| AscGraphics.cpp:123 | 0x793115 | je near <-> NOP6 | 0x140520F94 (CWorldScene::CullEntitys+0x94) | 0F 84 D7 00 00 00 `je 0x140521071` | 90 90 90 90 90 90 | baixo | alvo = LinkToTail (inline no x86) |
| AscGraphics.cpp:124 | 0x793126 | jne near <-> NOP6 | 0x140520FA5 (CWorldScene::CullEntitys+0xA5) | 0F 85 C6 00 00 00 `jne 0x140521071` | 90 90 90 90 90 90 | baixo | idem |
| AscGraphics.cpp:125 | 0x79313F | jne near <-> NOP6 | 0x140520FBE (CWorldScene::CullEntitys+0xBE) | 0F 85 AD 00 00 00 `jne 0x140521071` | 90 90 90 90 90 90 | baixo | idem |
| AscGraphics.cpp:113 | 0x793A45 | je near (0F 84 B5..) <-> jmp (E9 B6 00 00 00 00) | 0x1405283DF (CWorldScene::RenderEntitys+0xBF) | 0F 84 D0 00 00 00 `je 0x1405284b5` | E9 D1 00 00 00 90 | baixo | CORRIGIDO: e o 2o "if (model...)" (linha 4413) = 0x1405283DF, nao 0x140528395 da classificacao |
| AscGraphics.cpp:132 | 0x7AD2EE | je (74 44) <-> NOP2 | 0x140558463 (CMapObj::RenderInterior+0x153) | 74 48 `je 0x1405584ad` | 90 90 | baixo | queda confere (TransformAABox/FrustumCull) |
| AscGraphics.cpp:167 | 0x7B3A8E | je (74 1D) <-> NOP2 | 0x140553D28 (CMapObjDef::CullMapObjDefGroupFromExterior+0x88) | 73 11 `jae 0x140553d3b` | 90 90 | baixo | CORRIGIDO: x64 = bt edi,0x10; jae (73 11) em 0x140553D28 |
| AscGraphics.cpp:166 | 0x7B3AAF | je (74 5B) <-> NOP2 | 0x140553D3F (CMapObjDef::CullMapObjDefGroupFromExterior+0x9F) | 74 51 `je 0x140553d92` | 90 90 | baixo | test dil,8; je |
| AscGraphics.cpp:218 | 0x7EC15A | fstp [esi+0x48] (D9 5E 48) <-> NOP3 | 0x1405323F9 (DayNight::LoadLightingBands+0x1F9) | F3 0F 11 43 48 `movss dword ptr [rbx + 0x48], xmm0` | 90 90 90 90 90 | medio | x64 movss [rbx+0x48],xmm0 (5 bytes) -> NOP5; conferir o par 0x7ED982 (operando, ver B) |
| AscSpellMods.cpp:502/509 | 0x808323 | je (74) <-> jmp (EB) | 0x1402AA696 (Spell_C_SpellFailed+0x146) | 74 24 `je 0x1402aa6bc` | EB 24 | baixo | alvo/queda conferem |
| AscClientOptions.cpp:274 | 0x80C8C1 | je (74 6F) <-> NOP2 | 0x1402A21F2 (Spell_C_BeginCast+0x132) | 74 54 `je 0x1402a2248` | 90 90 | baixo | CORRIGIDO: test [r14+0x24],0x800; je = 0x1402A21F2 |
| AscSpellMods.cpp:501/508 | 0x80D6EF | 0x23 bytes (escolha do kit + call PlaySoundKit) <-> NOP | 0x1402A4165 (Spell_C_CastSpell+0x7E5) | E8 A6 67 0D 00 `call 0x14037a910` | 90 90 90 90 90 | baixo | x64: NOP5 so no call SI2::PlaySoundKit (o resto e preparo de argumento) |
| AscSpellMods.cpp:110 | 0x80DB37 | jne (75) <-> jmp (EB) | 0x1402A466F (Spell_C_ClickSpell+0xAF) | 75 0F `jne 0x1402a4680` | EB 0F | baixo | alvo confere (CancelAutoRepeat) |
| AscClientGuards.cpp:94 | 0x8540D7 | 19 NOPs: pula luaL_register(string) (o DLL registra a tabela dele) | 0x1400D01E7 (luaopen_string+0x17) | E8 74 9B FF FF `call 0x1400c9d60` | 90 90 90 90 90 | baixo | x64: NOP5 so no call luaL_register (mov rbx,rcx antes e usado depois) |
| AscAura137.cpp:33 | 0x989B97 | jne (75 12) <-> NOP2 | 0x140364391 (CMovementShared::SetRawFacing+0x31) | 75 11 `jne 0x1403643a4` | 90 90 | baixo | queda confere (UpdateAnchors) |

## extra-addr correspondente (substitui as linhas mid-function de out/extra-addr-proposta.txt)

```
0x4041BD InitializeClient +0x94
0x50D2B7 Script_SendChatMessage +0x18A
0x50D641 Script_SendChatMessage +0x48A
0x52B27A CGGameUI::Idle +0x3F7
0x59714D CGCharacterModelBase::RotateModel +0x69
0x5971C1 CGCharacterModelBase::UpdateModel +0x15
0x5A8C70 CGActionBar::GetSpell +0x56
0x5B01C8 CGCurrencyTypes::BuildDisplayList +0x1DD
0x60FAB2 Script_UnitLevel +0xF4
0x614AF8 PushUnitAura +0x344
0x61935B SetPortraitTexture +0x4C
0x62442C CGTooltip::SetSpell +0x431
0x631760 CGItemStatsSummary::GetItemComparison +0x1E9
0x68E4AD CGxDeviceD3d::DeviceSetBaseMipLevel +0x28
0x6BE290 GxuFontInitialize +0x8B
0x6CA067 TEXTURECACHE::CreateTexture +0x24
0x71557D CGUnit_C::ObjectNameVisibilityChanged +0x21
0x715586 CGUnit_C::ObjectNameVisibilityChanged +0x2B
0x728AA1 CGUnit_C::DoPowerRegen +0x90
0x729D3D CGUnit_C::ShouldRenderObjectName +0xD2
0x72B07A CGUnit_C::ShouldShowNamePlate +0x2C
0x7370AF CGUnit_C::SetSheatheState +0x1AD
0x73A0D0 CGUnit_C::CheckAndReportSpellInhibitFlags +0xA52
0x7569B6 Packet_Group_29 +0x216
0x792AF2 CWorldScene::AddMapObjDefGroup +0x24
0x792AFF CWorldScene::AddMapObjDefGroup +0x31
0x793115 CWorldScene::CullEntitys +0x94
0x793126 CWorldScene::CullEntitys +0xA5
0x79313F CWorldScene::CullEntitys +0xBE
0x793A45 CWorldScene::RenderEntitys +0xBF
0x7AD2EE CMapObj::RenderInterior +0x153
0x7B3A8E CMapObjDef::CullMapObjDefGroupFromExterior +0x88
0x7B3AAF CMapObjDef::CullMapObjDefGroupFromExterior +0x9F
0x7EC15A DayNight::LoadLightingBands +0x1F9
0x808323 Spell_C_SpellFailed +0x146
0x80C8C1 Spell_C_BeginCast +0x132
0x80D6EF Spell_C_CastSpell +0x7E5
0x80DB37 Spell_C_ClickSpell +0xAF
0x8540D7 luaopen_string +0x17
0x989B97 CMovementShared::SetRawFacing +0x31
```

Obs.: `SetPortraitTexture` (0x61935B) tem 3 simbolos no PDB -> usar o diff 07 em vez do byte-patch.
