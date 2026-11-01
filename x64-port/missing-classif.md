# Classificacao dos enderecos de codigo de out/missing.txt (< 0xA00000)

Gerado por projects-2c (ajudante) em 30/10. Fonte: tags.json (OFFSET, bisect), wow.exe 12340 (capstone), WowClient64.exe/.pdb/.addr.
Metodo mid-function: ancora = calls vizinhos cujo alvo esta no .addr; candidato x64 = mesma mnemonica perto da ancora correspondente. `anc` = distancia (bytes) entre o candidato e a posicao prevista pela ancora.

**Confianca**: ALTA = ancora <= 8 bytes e unica; MEDIA = ancora <= 25 (conferir no disasm antes de usar); BAIXA/NENHUMA = precisa patch no fonte. DOBRADA = x86 tem 1 corpo dobrado (ICF) usado por varios templates, x64 tem 1 por tipo.
**Atencao bytes**: o DLL grava opcodes x86. Jcc curto (7x/EB) e near (0F 8x/E9) tem a mesma codificacao no x64, MAS so vale se a forma (curta/near) do x64 for a mesma -- coluna `tam`. Patches que gravam ponteiro/imm32/rel32 nao sao portaveis por byte.

## Resumo

| tipo | confianca | qtd |
|---|---|---|
| byte-patch em OPERANDO | - | 27 |
| dado | - | 3 |
| entrada de funcao sem OFFSET | ALTA | 15 |
| entrada de funcao sem OFFSET | DOBRADA | 3 |
| entrada de funcao sem OFFSET | N/A | 3 |
| mid-function patch | ALTA | 19 |
| mid-function patch | BAIXA | 21 |
| mid-function patch | MEDIA | 23 |
| mid-function patch | NENHUMA | 11 |

## Mid-function patches (74)

| end. 12340 | funcao (fonte) +off | instr x86 | x64 proposto | tam x86/x64 | conf | uso no DLL | nota |
|---|---|---|---|---|---|---|---|
| 0x4041BD | static void InitializeClient +0x8D | `e85e132300 call 0x635520` | 0x140034F74 (InitializeClient+0x94) `e8f70a1900 call 0x1401c5a70` | 5/5 | MEDIA | AscRealmData.cpp:390 | extra-addr: 0x4041BD InitializeClient +0x94 |
| 0x466BF1 | void WowConnection::SetEncryptionKey +0x1 | `8bec mov ebp, esp` | 0x14058D060 (WowConnection::SetEncryptionKey+0x10) `488b05191d4100 mov rax, qword ptr [rip + 0x411d19]` | 2/7 **difere** | BAIXA | AscMain.cpp:119 | precisa patch no fonte (sem ancora confiavel) |
| 0x49DDBF | static int32_t Script_SetPoint +0x2AF | `e81ce2fdff call 0x47bfe0` | 0x1400B5A07 (Script_SetPoint+0x2E7) `e814b0f8ff call 0x140040a20` | 5/5 | MEDIA | AscSuperTrack.cpp:425 / AscSuperTrack.cpp:430 | extra-addr: 0x49DDBF Script_SetPoint +0x2E7 |
| 0x49DDE5 | static int32_t Script_SetPoint +0x2D5 | `d95df8 fstp dword ptr [ebp - 8]` | - | 3/- | NENHUMA | AscSuperTrack.cpp:426 / AscSuperTrack.cpp:431 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x50D2B7 | static int32_t Script_SendChatMessage +0x147 | `7516 jne 0x50d2cf` | 0x1404C4DBA (Script_SendChatMessage+0x18A) `750f jne 0x1404c4dcb` | 2/2 | MEDIA | AscAccount.cpp:122 | extra-addr: 0x50D2B7 Script_SendChatMessage +0x18A |
| 0x50D641 | static int32_t Script_SendChatMessage +0x4D1 | `7452 je 0x50d695` | 0x1404C50BA (Script_SendChatMessage+0x48A) `7429 je 0x1404c50e5` | 2/2 | ALTA | AscAccount.cpp:121 | extra-addr: 0x50D641 Script_SendChatMessage +0x48A |
| 0x52B25F | int32_t CGGameUI::Idle +0x31F | `0f8cedfdffff jl 0x52b052` | - | 6/- | NENHUMA | AscGlobalsG.cpp:36 / AscGlobalsG.cpp:41 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x52B27A | int32_t CGGameUI::Idle +0x33A | `0f85d2fdffff jne 0x52b052` | 0x1403DD887 (CGGameUI::Idle+0x3F7) `7565 jne 0x1403dd8ee` | 6/2 **difere** | ALTA | AscGlobalsG.cpp:54 / AscGlobalsG.cpp:59 | extra-addr: 0x52B27A CGGameUI::Idle +0x3F7 |
| 0x59714D | void CGCharacterModelBase::RotateModel +0x5D | `742d je 0x59717c` | 0x1404A55B9 (CGCharacterModelBase::RotateModel+0x69) `7453 je 0x1404a560e` | 2/2 | ALTA | AscModelMethods.cpp:375 | extra-addr: 0x59714D CGCharacterModelBase::RotateModel +0x69 |
| 0x5971C1 | void CGCharacterModelBase::UpdateModel +0x11 | `753f jne 0x597202` | 0x1404A5935 (CGCharacterModelBase::UpdateModel+0x15) `7571 jne 0x1404a59a8` | 2/2 | ALTA | AscModelMethods.cpp:374 | extra-addr: 0x5971C1 CGCharacterModelBase::UpdateModel +0x15 |
| 0x5A8C70 | int32_t CGActionBar::GetSpell +0x40 | `751d jne 0x5a8c8f` | 0x14042B3F6 (CGActionBar::GetSpell+0x56) `7517 jne 0x14042b40f` | 2/2 | ALTA | AscCAMgr.cpp:477 / AscCAMgr.cpp:479 | extra-addr: 0x5A8C70 CGActionBar::GetSpell +0x56 |
| 0x5B01C8 | void CGCurrencyTypes::BuildDisplayList +0x198 | `0f84f9000000 je 0x5b02c7` | 0x14045EF8D (CGCurrencyTypes::BuildDisplayList+0x1DD) `0f84ed000000 je 0x14045f080` | 6/6 | ALTA | AscGlobalsJ.cpp:1351 | extra-addr: 0x5B01C8 CGCurrencyTypes::BuildDisplayList +0x1DD |
| 0x60FAB2 | static int32_t Script_UnitLevel +0xD2 | `7e0b jle 0x60fabf` | 0x14040FC04 (Script_UnitLevel+0xF4) `7e0c jle 0x14040fc12` | 2/2 | ALTA | AscClientOptions.cpp:285 | extra-addr: 0x60FAB2 Script_UnitLevel +0xF4 |
| 0x614AF8 | int32_t PushUnitAura +0x338 | `0f848d000000 je 0x614b8b` | 0x1403EA314 (PushUnitAura+0x344) `0f848f000000 je 0x1403ea3a9` | 6/6 | MEDIA | AscGlobalsJ.cpp:1206 | extra-addr: 0x614AF8 PushUnitAura +0x344 |
| 0x61770A | static TSFixedArray<uint8_t>* GetAlphaMask +0x6A | `0f853f010000 jne 0x61784f` | 0x1403B5F7D (GetAlphaMask+0x6D) `0f8586010000 jne 0x1403b6109` | 6/6 | BAIXA | AscGlobalsJ.cpp:1619 | precisa patch no fonte (sem ancora confiavel) |
| 0x61935B | void SetPortraitTexture +0x2B | `7411 je 0x61936e` | 0x1403B941C (SetPortraitTexture+0x4C) `7411 je 0x1403b942f` | 2/2 | ALTA | AscGlobalsJ.cpp:1600 / AscGlobalsJ.cpp:1618 | extra-addr: 0x61935B SetPortraitTexture +0x4C |
| 0x621766 | int32_t CGTooltip::SetUnit +0x6F6 | `0f8edd000000 jle 0x621849` | 0x1403B3D0B (CGTooltip::SetUnit+0x85B) `0f8ea9000000 jle 0x1403b3dba` | 6/6 | BAIXA | AscClientOptions.cpp:286 | precisa patch no fonte (sem ancora confiavel) |
| 0x62442C | int32_t CGTooltip::SetSpell +0xB8C | `0f8514020000 jne 0x624646` | 0x1403B24FD (CGTooltip::SetSpell+0x43D) `752d jne 0x1403b252c` | 6/2 **difere** | MEDIA | AscSpellTokens.cpp:310 / AscSpellTokens.cpp:311 / AscSpellTokens.cpp:313 | extra-addr: 0x62442C CGTooltip::SetSpell +0x43D |
| 0x627980 | int32_t CGTooltip::SetItem +0x190 | `0f8597000000 jne 0x627a1d` | 0x1403B0166 (CGTooltip::SetItem+0x76) `7505 jne 0x1403b016d` | 6/2 **difere** | BAIXA | AscTooltipScaling.cpp:134 | precisa patch no fonte (sem ancora confiavel) |
| 0x62B98F | int32_t CGTooltip::SetItem +0x419F | `0f84c0010000 je 0x62bb55` | 0x1403B01BD (CGTooltip::SetItem+0xCD) `741e je 0x1403b01dd` | 6/2 **difere** | BAIXA | AscTooltipScaling.cpp:129 | precisa patch no fonte (sem ancora confiavel) |
| 0x631760 | int32_t CGItemStatsSummary::GetItemComparison +0x1D0 | `0f84de000000 je 0x631844` | 0x1403ABFD9 (CGItemStatsSummary::GetItemComparison+0x1E9) `0f84f6010000 je 0x1403ac1d5` | 6/6 | MEDIA | AscTooltipScaling.cpp:139 | extra-addr: 0x631760 CGItemStatsSummary::GetItemComparison +0x1E9 |
| 0x667EF9 | void GetCacheDirectory +0x9 | `6888dec500 push 0xc5de88` | 0x140248B90 (GetCacheDirectory+0x0) `4053 push rbx` | 5/2 **difere** | MEDIA | AscRealmData.cpp:396 | extra-addr: 0x667EF9 GetCacheDirectory +0x0 |
| 0x68E4AD | void CGxDeviceD3d::DeviceSetBaseMipLevel +0xD | `8b4508 mov eax, dword ptr [ebp + 8]` | 0x140106C69 (CGxDeviceD3d::DeviceSetBaseMipLevel+0x19) `8bd3 mov edx, ebx` | 3/2 **difere** | MEDIA | AscClientGuards.cpp:36 | extra-addr: 0x68E4AD CGxDeviceD3d::DeviceSetBaseMipLevel +0x19 |
| 0x6BE290 | void GxuFontInitialize +0x60 | `e8bb232d00 call 0x990650` | 0x140118E0B (GxuFontInitialize+0x8B) `e800cb5700 call 0x140695910` | 5/5 | MEDIA | AscFontRender.cpp:357 | extra-addr: 0x6BE290 GxuFontInitialize +0x8B |
| 0x6CA067 | void TEXTURECACHE::CreateTexture +0x17 | `0f95c0 setne al` | 0x1401243F4 (TEXTURECACHE::CreateTexture+0x24) `0f95c0 setne al` | 3/3 | MEDIA | AscFontRender.cpp:355 | extra-addr: 0x6CA067 TEXTURECACHE::CreateTexture +0x24 |
| 0x6DC41D | int32_t CGPlayer_C::CanUseItem +0x2D | `8b4e38 mov ecx, dword ptr [esi + 0x38]` | 0x14032BBCD (CGPlayer_C::CanUseItem+0x2D) `488b5c2438 mov rbx, qword ptr [rsp + 0x38]` | 3/5 **difere** | BAIXA | AscAttachFixes.cpp:1006 / AscAttachFixes.cpp:1008 | precisa patch no fonte (sem ancora confiavel) |
| 0x71557D | void CGUnit_C::ObjectNameVisibilityChanged +0x1D | `7528 jne 0x7155a7` | 0x1402D4A01 (CGUnit_C::ObjectNameVisibilityChanged+0x21) `7518 jne 0x1402d4a1b` | 2/2 | MEDIA | AscNamePlates.cpp:644 | extra-addr: 0x71557D CGUnit_C::ObjectNameVisibilityChanged +0x21 |
| 0x715586 | void CGUnit_C::ObjectNameVisibilityChanged +0x26 | `751f jne 0x7155a7` | 0x1402D4A0B (CGUnit_C::ObjectNameVisibilityChanged+0x2B) `750e jne 0x1402d4a1b` | 2/2 | ALTA | AscNamePlates.cpp:645 | extra-addr: 0x715586 CGUnit_C::ObjectNameVisibilityChanged +0x2B |
| 0x720020 | void CGUnit_C::ChatBubbleShow +0x10 | `0f8501010000 jne 0x720127` | 0x1402C04E8 (CGUnit_C::ChatBubbleShow+0x18) `0f853c010000 jne 0x1402c062a` | 6/6 | BAIXA | AscClientOptions.cpp:54 | precisa patch no fonte (sem ancora confiavel) |
| 0x7258CE | void CGUnit_C::UnitNameplatesReposition +0x3E | `7468 je 0x725938` | 0x1402E7C8F (CGUnit_C::UnitNameplatesReposition+0x1F) `0f84ed000000 je 0x1402e7d82` | 2/6 **difere** | BAIXA | AscNamePlates.cpp:622 | precisa patch no fonte (sem ancora confiavel) |
| 0x727520 | bool CGPlayer_C::ClickToMove +0x120 | `8b83d8000000 mov eax, dword ptr [ebx + 0xd8]` | 0x1402C2E80 (CGPlayer_C::ClickToMove+0x0) `48895c2408 mov qword ptr [rsp + 8], rbx` | 6/5 **difere** | BAIXA | AscCamera.cpp:108 | precisa patch no fonte (sem ancora confiavel) |
| 0x728AA1 | void CGUnit_C::DoPowerRegen +0x81 | `83fb06 cmp ebx, 6` | 0x1402C7B70 (CGUnit_C::DoPowerRegen+0x90) `4080ff06 cmp dil, 6` | 3/4 **difere** | MEDIA | AscCAMgr.cpp:307 | extra-addr: 0x728AA1 CGUnit_C::DoPowerRegen +0x90 |
| 0x729D3D | int32_t CGUnit_C::ShouldRenderObjectName +0xCD | `7442 je 0x729d81` | 0x1402E5582 (CGUnit_C::ShouldRenderObjectName+0xD2) `743b je 0x1402e55bf` | 2/2 | MEDIA | AscClientOptions.cpp:302 | extra-addr: 0x729D3D CGUnit_C::ShouldRenderObjectName +0xD2 |
| 0x72B07A | int32_t CGUnit_C::ShouldShowNamePlate +0x1A | `7e5f jle 0x72b0db` | 0x1402E579C (CGUnit_C::ShouldShowNamePlate+0x2C) `0f8e07030000 jle 0x1402e5aa9` | 2/6 **difere** | MEDIA | AscNamePlates.cpp:584 | extra-addr: 0x72B07A CGUnit_C::ShouldShowNamePlate +0x2C |
| 0x731720 | bool CGUnit_C::HandleTrackingFacing +0xD0 | `d91ddc11ca00 fstp dword ptr [0xca11dc]` | - | 6/- | NENHUMA | AscCamera.cpp:109 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7370AF | void CGUnit_C::SetSheatheState +0x37F | `740d je 0x7370be` | 0x1402E3FED (CGUnit_C::SetSheatheState+0x1AD) `7407 je 0x1402e3ff6` | 2/2 | ALTA | AscSpellMods.cpp:477 / AscSpellMods.cpp:514 | extra-addr: 0x7370AF CGUnit_C::SetSheatheState +0x1AD |
| 0x73A0D0 | bool CGUnit_C::CheckAndReportSpellInhibitFlags +0xA80 | `8b4508 mov eax, dword ptr [ebp + 8]` | 0x1402C06FC (CGUnit_C::CheckAndReportSpellInhibitFlags+0x8C) `41b897000000 mov r8d, 0x97` | 3/6 **difere** | MEDIA | AscSpellMods.cpp:103 | extra-addr: 0x73A0D0 CGUnit_C::CheckAndReportSpellInhibitFlags +0x8C |
| 0x744A55 | void CGObject_C::PostInit +0x5 | `8b90e4000000 mov edx, dword ptr [eax + 0xe4]` | 0x1402B9890 (CGObject_C::PostInit+0x0) `48895c2408 mov qword ptr [rsp + 8], rbx` | 6/5 **difere** | BAIXA | AscAttachFixes.cpp:1065 | precisa patch no fonte (sem ancora confiavel) |
| 0x7569B6 | int32_t Packet_Group_29 +0x1B6 | `e80509fdff call 0x7272c0` | 0x14035A866 (Packet_Group_29+0x216) `e8f583f6ff call 0x1402c2c60` | 5/5 | MEDIA | AscCamera.cpp:101 | extra-addr: 0x7569B6 Packet_Group_29 +0x216 |
| 0x791D6A | void CWorldScene::AddDoodadDefModelToModelScene +0xBA | `0f85bc000000 jne 0x791e2c` | 0x14051EBCA (CWorldScene::AddDoodadDefModelToModelScene+0x1A) `0f8579010000 jne 0x14051ed49` | 6/6 | BAIXA | AscGraphics.cpp:281 | precisa patch no fonte (sem ancora confiavel) |
| 0x791E60 | void CWorldScene::AddDoodadDefModelToModelScene +0x1B0 | `d99878010000 fstp dword ptr [eax + 0x178]` | - | 6/- | NENHUMA | AscCamera.cpp:121 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x792AF2 | void CWorldScene::AddMapObjDefGroup +0x22 | `0f84c6000000 je 0x792bbe` | 0x14051F084 (CWorldScene::AddMapObjDefGroup+0x24) `0f84fa000000 je 0x14051f184` | 6/6 | ALTA | AscGraphics.cpp:164 | extra-addr: 0x792AF2 CWorldScene::AddMapObjDefGroup +0x24 |
| 0x792AFF | void CWorldScene::AddMapObjDefGroup +0x2F | `7411 je 0x792b12` | 0x14051F091 (CWorldScene::AddMapObjDefGroup+0x31) `7419 je 0x14051f0ac` | 2/2 | ALTA | AscGraphics.cpp:165 | extra-addr: 0x792AFF CWorldScene::AddMapObjDefGroup +0x31 |
| 0x793115 | void CWorldScene::CullEntitys +0xB5 | `0f84ea000000 je 0x793205` | 0x140520F94 (CWorldScene::CullEntitys+0x94) `0f84d7000000 je 0x140521071` | 6/6 | ALTA | AscGraphics.cpp:123 | extra-addr: 0x793115 CWorldScene::CullEntitys +0x94 |
| 0x793126 | void CWorldScene::CullEntitys +0xC6 | `0f85d9000000 jne 0x793205` | 0x140520FA5 (CWorldScene::CullEntitys+0xA5) `0f85c6000000 jne 0x140521071` | 6/6 | ALTA | AscGraphics.cpp:124 | extra-addr: 0x793126 CWorldScene::CullEntitys +0xA5 |
| 0x79313F | void CWorldScene::CullEntitys +0xDF | `0f85c0000000 jne 0x793205` | 0x140520FBE (CWorldScene::CullEntitys+0xBE) `0f85ad000000 jne 0x140521071` | 6/6 | ALTA | AscGraphics.cpp:125 | extra-addr: 0x79313F CWorldScene::CullEntitys +0xBE |
| 0x793A45 | void CWorldScene::RenderEntitys +0xC5 | `0f84b5000000 je 0x793b00` | 0x140528395 (CWorldScene::RenderEntitys+0x75) `7441 je 0x1405283d8` | 6/2 **difere** | MEDIA | AscGraphics.cpp:113 | extra-addr: 0x793A45 CWorldScene::RenderEntitys +0x75 |
| 0x79AA42 | void CWorldScene::Render +0x1D2 | `7b1c jnp 0x79aa60` | - | 2/- | NENHUMA | AscGraphics.cpp:143 / AscGraphics.cpp:148 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7AD2EE | void CMapObj::RenderInterior +0xFE | `7444 je 0x7ad334` | 0x140558463 (CMapObj::RenderInterior+0x153) `7448 je 0x1405584ad` | 2/2 | ALTA | AscGraphics.cpp:132 | extra-addr: 0x7AD2EE CMapObj::RenderInterior +0x153 |
| 0x7B3A8E | void CMapObjDef::CullMapObjDefGroupFromExterior +0x7E | `741d je 0x7b3aad` | 0x140553D3F (CMapObjDef::CullMapObjDefGroupFromExterior+0x9F) `7451 je 0x140553d92` | 2/2 | MEDIA | AscGraphics.cpp:167 | extra-addr: 0x7B3A8E CMapObjDef::CullMapObjDefGroupFromExterior +0x9F |
| 0x7B3AAF | void CMapObjDef::CullMapObjDefGroupFromExterior +0x9F | `745b je 0x7b3b0c` | 0x140553D3F (CMapObjDef::CullMapObjDefGroupFromExterior+0x9F) `7451 je 0x140553d92` | 2/2 | MEDIA | AscGraphics.cpp:166 | extra-addr: 0x7B3AAF CMapObjDef::CullMapObjDefGroupFromExterior +0x9F |
| 0x7BBA22 | void CShadowQuery::GatherShadowCasters +0x52 | `7507 jne 0x7bba2b` | 0x1405761B1 (CShadowQuery::GatherShadowCasters+0x31) `7508 jne 0x1405761bb` | 2/2 | BAIXA | AscGraphics.cpp:114 | precisa patch no fonte (sem ancora confiavel) |
| 0x7BD4AF | int32_t CMap::SafeOpen +0x2F | `56 push esi` | 0x140514C3A (CMap::SafeOpen+0xA) `57 push rdi` | 1/1 | BAIXA | AscAttachLua.cpp:325 / AscAttachLua.cpp:326 / AscAttachLua.cpp:328 | precisa patch no fonte (sem ancora confiavel) |
| 0x7D00BF | void CMapRenderChunk::SetVertexShader +0x6F | `c745e403000000 mov dword ptr [ebp - 0x1c], 3` | 0x14053FC86 (CMapRenderChunk::SetVertexShader+0x3A6) `488b9c24c8010000 mov rbx, qword ptr [rsp + 0x1c8]` | 7/8 **difere** | BAIXA | AscGraphics.cpp:232 | precisa patch no fonte (sem ancora confiavel) |
| 0x7EC15A | void DayNight::LoadLightingBands +0x16A | `d95e48 fstp dword ptr [esi + 0x48]` | 0x1405323F9 (DayNight::LoadLightingBands+0x1F9) `f30f114348 movss dword ptr [rbx + 0x48], xmm0` | 3/5 **difere** | ALTA | AscGraphics.cpp:218 | extra-addr: 0x7EC15A DayNight::LoadLightingBands +0x1F9 |
| 0x7ED982 | DayNight::CurrentLight& DayNight::CurrentLight::operator= +0x72 | `d94148 fld dword ptr [ecx + 0x48]` | 0x14052E78A (DayNight::CurrentLight::operator=+0x6A) `8b4248 mov eax, dword ptr [rdx + 0x48]` | 3/3 | BAIXA | AscGraphics.cpp:217 | precisa patch no fonte (sem ancora confiavel) |
| 0x7F1777 | void DayNight::UpdateFog +0x87 | `d91d988bd300 fstp dword ptr [0xd38b98]` | - | 6/- | NENHUMA | AscGraphics.cpp:197 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7F19F4 | void DayNight::UpdateFog +0x304 | `d915988bd300 fst dword ptr [0xd38b98]` | - | 6/- | NENHUMA | AscGraphics.cpp:198 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7F1A13 | void DayNight::UpdateFog +0x323 | `d91d988bd300 fstp dword ptr [0xd38b98]` | - | 6/- | NENHUMA | AscGraphics.cpp:194 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7F1A1B | void DayNight::UpdateFog +0x32B | `d91dac8bd300 fstp dword ptr [0xd38bac]` | - | 6/- | NENHUMA | AscGraphics.cpp:196 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x7F1A49 | void DayNight::UpdateFog +0x359 | `d915ac8bd300 fst dword ptr [0xd38bac]` | - | 6/- | NENHUMA | AscGraphics.cpp:195 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x805DDD | void Spell_C_StartSpellCooldown +0x6D | `7561 jne 0x805e40` | 0x1402AB427 (Spell_C_StartSpellCooldown+0x17) `750c jne 0x1402ab435` | 2/2 | BAIXA | AscAttachFixes.cpp:1026 / AscAttachFixes.cpp:1031 | precisa patch no fonte (sem ancora confiavel) |
| 0x805E2D | void Spell_C_StartSpellCooldown +0xBD | `7611 jbe 0x805e40` | 0x1402AB4AE (Spell_C_StartSpellCooldown+0x9E) `764b jbe 0x1402ab4fb` | 2/2 | BAIXA | AscAttachFixes.cpp:1021 / AscAttachFixes.cpp:1030 | precisa patch no fonte (sem ancora confiavel) |
| 0x808323 | void Spell_C_SpellFailed +0x123 | `7412 je 0x808337` | 0x1402AA696 (Spell_C_SpellFailed+0x146) `7424 je 0x1402aa6bc` | 2/2 | MEDIA | AscSpellMods.cpp:502 / AscSpellMods.cpp:509 | extra-addr: 0x808323 Spell_C_SpellFailed +0x146 |
| 0x80C8C1 | static bool Spell_C_BeginCast +0x131 | `746f je 0x80c932` | 0x1402A24EF (Spell_C_BeginCast+0x42F) `7453 je 0x1402a2544` | 2/2 | MEDIA | AscClientOptions.cpp:274 | extra-addr: 0x80C8C1 Spell_C_BeginCast +0x42F |
| 0x80C91D | static bool Spell_C_BeginCast +0x18D | `0d80000000 or eax, 0x80` | 0x1402A223E (Spell_C_BeginCast+0x17E) `810d20a9770080000000 or dword ptr [rip + 0x77a920], 0x80` | 5/10 **difere** | BAIXA | AscClientOptions.cpp:276 | precisa patch no fonte (sem ancora confiavel) |
| 0x80D6EF | bool Spell_C_CastSpell +0xA0F | `f68558fdffff10 test byte ptr [ebp - 0x2a8], 0x10` | 0x1402A4150 (Spell_C_CastSpell+0x7D0) `f644246010 test byte ptr [rsp + 0x60], 0x10` | 7/5 **difere** | ALTA | AscSpellMods.cpp:501 / AscSpellMods.cpp:508 | extra-addr: 0x80D6EF Spell_C_CastSpell +0x7D0 |
| 0x80D7C1 | bool Spell_C_CastSpell +0xAE1 | `7407 je 0x80d7ca` | 0x1402A39EC (Spell_C_CastSpell+0x6C) `0f849b0a0000 je 0x1402a448d` | 2/6 **difere** | BAIXA | AscClientOptions.cpp:275 | precisa patch no fonte (sem ancora confiavel) |
| 0x80DB37 | void Spell_C_ClickSpell +0xB7 | `751e jne 0x80db57` | 0x1402A466F (Spell_C_ClickSpell+0xAF) `750f jne 0x1402a4680` | 2/2 | MEDIA | AscSpellMods.cpp:110 | extra-addr: 0x80DB37 Spell_C_ClickSpell +0xAF |
| 0x8540D7 | LUALIB_API int luaopen_string  +0x7 | `68e078a400 push 0xa478e0` | 0x1400D01D0 (luaopen_string+0x0) `4053 push rbx` | 5/2 **difere** | MEDIA | AscClientGuards.cpp:94 | extra-addr: 0x8540D7 luaopen_string +0x0 |
| 0x988E48 | void CMovementShared::StartTurn +0x58 | `0f85aa010000 jne 0x988ff8` | - | 6/- | NENHUMA | AscAura137.cpp:31 | instrucao nao existe igual no x64 (x87/inline) -> precisa patch no fonte |
| 0x989053 | int32_t CMovementShared::StopTurn +0x43 | `0f85aa010000 jne 0x989203` | 0x140364AA9 (CMovementShared::StopTurn+0x9) `7507 jne 0x140364ab2` | 6/2 **difere** | BAIXA | AscAura137.cpp:32 | precisa patch no fonte (sem ancora confiavel) |
| 0x989B97 | void CMovementShared::SetRawFacing +0x27 | `7512 jne 0x989bab` | 0x140364391 (CMovementShared::SetRawFacing+0x31) `7511 jne 0x1403643a4` | 2/2 | ALTA | AscAura137.cpp:33 | extra-addr: 0x989B97 CMovementShared::SetRawFacing +0x31 |
| 0x98E9F9 | void CGNamePlateFrame::OnLayerUpdate +0x9 | `8b86a8020000 mov eax, dword ptr [esi + 0x2a8]` | 0x14044CED8 (CGNamePlateFrame::OnLayerUpdate+0x8) `488b0521ed6c00 mov rax, qword ptr [rip + 0x6ced21]` | 6/7 **difere** | BAIXA | AscNamePlates.cpp:631 | precisa patch no fonte (sem ancora confiavel) |

## Entradas de funcao sem OFFSET (21)

| end. 12340 | funcao (fonte) +off | instr/contexto x86 | x64 | conf | uso no DLL | nota |
|---|---|---|---|---|---|---|
| 0x4181B0 | int WINAPI WinMain +0xC9C0 | `` | - | N/A | AscAttachFixes.cpp:1084 | CRT/anti-tamper: so reporta "External call to FrameScript_RegisterFunction" quando o retorno e de fora do exe; comentario do proprio DLL diz que nada nosso dispara -> deixar desligado no x64 |
| 0x4CFD20 | int32_t SI2::PlayFoleySound +0x110 | `55 push ebp` | 0x140299250 WowClientDB_RLE<SpellRec>::GetRecord | ALTA | DBClient.cpp:14 | extra-addr: 0x4CFD20 WowClientDB_RLE<SpellRec>::GetRecord |
| 0x55F4D0 | void CGUIBindings::AdjustCommandKeyIndices +0xA0 | `55 push ebp` | 0x14038BF20 TSHashTable<UIADDON,HASHKEY_STRI>::Ptr | ALTA | AscLibrary.cpp:66 | dobrado no x86, mas o unico uso (AscLibrary:66, tabela s_addons 0xC2491C) e UIADDON -> extra-addr: 0x55F4D0 TSHashTable<UIADDON,HASHKEY_STRI>::Ptr |
| 0x6348B0 | static void StaticDBLoadAll +0x10E0 | `8b01 mov eax, dword ptr [ecx]` | - | N/A | AscLargeAlloc.cpp:77 | thunk vcall `mov eax,[ecx]; jmp [eax+4]` (WowClientDB_Base::Load); o detour so repassa (LargeAlloc nunca preenche a lista) -> dispensavel no x64 |
| 0x65C290 | void RealmResponse::HandleCharacterLoginFailed +0x8880 | `55 push ebp` | x64: 1 instancia por tabela (WowClientDB<AreaTableRec/SpellIconRec/ChrClassesRec/MapRec/...>::GetRecord) | DOBRADA | AscCrashContext.cpp:355 / DBClient.cpp:19 | x86 e o WowClientDB<T>::GetRecord dobrado (ICF). DBClient.cpp:19 -> trocar por acesso direto (recordsById x64 = +0x30, ver extra-addr WowClientDB_Base) ou pela instancia do tipo; AscCrashContext:355 (detour p/ crash context) -> nao da p/ 1 detour, dispensavel ou patch no fonte |
| 0x67CA30 | template <class T, class TKey, class THashKey> +0x2570 | `55 push ebp` | 0x1402990C0 DBCache<ItemStats_C,int,HASHKEY_INT>::GetRecord | ALTA | AscAttachFixes.cpp:802 / AscGlobalsB.cpp:38 / AscGlobalsC.cpp:156 / AscGlobalsJ.cpp:829 | extra-addr: 0x67CA30 DBCache<ItemStats_C,int,HASHKEY_INT>::GetRecord |
| 0x67D770 | template <class T, class TKey, class THashKey> +0x32B0 | `55 push ebp` | 0x140278C40 DBCache<NameCache,unsigned __int64,CHashKeyGUID>::GetRecord | ALTA | DBNameCache.cpp:6 | extra-addr: 0x67D770 DBCache<NameCache,unsigned __int64,CHashKeyGUID>::GetRecord |
| 0x67DE90 | template <class T, class TKey, class THashKey> +0x39D0 | `55 push ebp` | 0x140330050 DBCache<QuestCache,int,HASHKEY_INT>::GetRecord | ALTA | AscAttachFixes.cpp:877 / AscGlobalsB.cpp:43 / AscSmallApis.cpp:264 / AscTemporalContracts.cpp:66 | extra-addr: 0x67DE90 DBCache<QuestCache,int,HASHKEY_INT>::GetRecord |
| 0x67EF70 | template <class T, class TKey, class THashKey> +0x4AB0 | `55 push ebp` | 0x14032FEC0 DBCache<CGPetition,int,HASHKEY_INT>::GetRecord | ALTA | DBPetitionCache.cpp:6 | extra-addr: 0x67EF70 DBCache<CGPetition,int,HASHKEY_INT>::GetRecord |
| 0x6F6020 | CObjectEffectManager::CObjectEffectManager +0x720 | `55 push ebp` | 0x1403B7C70 TSHashTable<UNITPORTRAIT,HASHKEY_DWORD>::Ptr (s_unitPortraits 0xC5CE30, AscRuntime 240/591) / 0x140438260 TSHashTable<MACRONODE,HASHKEY_NONE>::Ptr (s_macros 0xBEAF68, AscAttachLua 146) | DOBRADA | AscAttachLua.cpp:146 / AscRuntime.cpp:240 / AscRuntime.cpp:591 | TSHashTable<T,K>::Ptr dobrado: 1 endereco x86 -> 2 funcoes x64; extra-addr nao expressa -> o ext precisa de 2 ids (sugestao: chave sintetica em extra-addr, p.ex. 0x6F6021/0x6F6022, se o mapaddr aceitar) |
| 0x7EB210 | template <class T> +0x60 | `55 push ebp` | 0x140274CA0 WowClientDB_RLE<LightIntBandRec>::GetRecord / 0x1405317A0 WowClientDB_RLE<LightFloatBandRec>::GetRecord | DOBRADA | AscGraphics.cpp:695 | unicos chamadores: DayNight::LoadLightIntBand e CalcIndividualColorFromDBRec; AmbientColorsDetour deve ir no IntBand (cores) -> conferir qual dos 2 o detour trata; 2 detours se precisar |
| 0x83CF00 | int32_t CM2Shared::Initialize +0x280 | `53 push ebx` | - | N/A | AscLargeAlloc.cpp:76 | M2 load do LargeAlloc: so alimenta o crash report e o contador nunca soma -> dispensavel no x64 (unica ref: jmp em 0x83D35E) |
| 0x991050 | const LiquidTypeRec* GetLiquidTypeRecForArea +0xA90 | `55 push ebp` | 0x140692210 FT_Get_Kerning | ALTA | AscFontRender.cpp:370 | extra-addr: 0x991050 FT_Get_Kerning |
| 0x9911A0 | const LiquidTypeRec* GetLiquidTypeRecForArea +0xBE0 | `55 push ebp` | 0x140692080 FT_Get_Char_Index | ALTA | AscFontRender.cpp:369 | extra-addr: 0x9911A0 FT_Get_Char_Index |
| 0x991320 | const LiquidTypeRec* GetLiquidTypeRecForArea +0xD60 | `55 push ebp` | 0x140693370 FT_New_Library | ALTA | AscFontRender.cpp:364 | extra-addr: 0x991320 FT_New_Library |
| 0x992610 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x2050 | `55 push ebp` | 0x140691CB0 FT_Done_Face | ALTA | AscFontRender.cpp:366 | chamador exato ~FACEDATA; no x86 o mesmo corpo tambem e chamado por GetLiquidTypeRecForArea (ICF) -- irrelevante no x64 -> extra-addr: 0x992610 FT_Done_Face |
| 0x992780 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x21C0 | `55 push ebp` | 0x140694D60 FT_Set_Pixel_Sizes | ALTA | AscFontRender.cpp:368 | extra-addr: 0x992780 FT_Set_Pixel_Sizes |
| 0x992B60 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x25A0 | `55 push ebp` | 0x1406949A0 FT_Render_Glyph | ALTA | AscFontRender.cpp:372 | extra-addr: 0x992B60 FT_Render_Glyph |
| 0x992CB0 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x26F0 | `55 push ebp` | 0x140691DD0 FT_Done_Library | ALTA | AscFontRender.cpp:371 | unico chamador GxuFontShutdown -> extra-addr: 0x992CB0 FT_Done_Library |
| 0x992DA0 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x27E0 | `55 push ebp` | 0x140692E00 FT_Load_Glyph | ALTA | AscFontRender.cpp:367 | extra-addr: 0x992DA0 FT_Load_Glyph |
| 0x993370 | const LiquidTypeRec* GetLiquidTypeRecForArea +0x2DB0 | `55 push ebp` | 0x140693420 FT_New_Memory_Face | ALTA | AscFontRender.cpp:365 | extra-addr: 0x993370 FT_New_Memory_Face |

## Byte-patches em operando (imm/rel32 no meio da instrucao) (27)

| end. 12340 | funcao (fonte) +off | instr/contexto x86 | x64 | conf | uso no DLL | nota |
|---|---|---|---|---|---|---|
| 0x406851 | bool InitializeGlobal +0x61 | `406846: call 0x6b0190 / 40684B: call 0x7691c0 / 406850: push 0x9e29e4 / 406855: call 0x768340 / 40685A: add esp, 4 / 406` | - | - | AscMisc.cpp:451 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x4648B3 | void RealmConnection::HandleAuthChallenge +0x83 | `4648AA: lea ecx, [ebp - 0x24] / 4648AD: call 0x47b0a0 / 4648B2: push 0x3034 / 4648B7: lea ecx, [ebp - 0x24] / 4648BA: ca` | - | - | AscRealmData.cpp:236 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x52AB26 | void CGGameUI::InitializeFrameXML +0x1A6 | `52AB1B: push 0x2d2 / 52AB20: push 0xc24eb0 / 52AB25: call 0x81b5f0 / 52AB2A: add esp, 0xc / 52AB2D: call 0x51d9b0 / 52AB` | - | - | FrameScript.cpp:11 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x605E13 | uint32_t CGCamera::CollideCameraWithWorld +0xB3 | `605E08: and ebx, 0x20000 / 605E0E: fsub dword ptr [esi + 0x38] / 605E11: add ebx, 0x100171 / 605E17: fcomp dword ptr [0x` | - | - | AscCamera.cpp:123 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x617721 | static TSFixedArray<uint8_t>* GetAlphaMask +0x81 | `617715: push eax / 617716: mov ecx, esi / 617718: call 0x6171b0 / 61771D: xor eax, eax / 61771F: cmp edi, 0x80 / 617725:` | - | - | AscGlobalsJ.cpp:1592 / AscGlobalsJ.cpp:1611 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x61773A | static TSFixedArray<uint8_t>* GetAlphaMask +0x9A | `61772E: mov dword ptr [ebp - 0x34], ebx / 617731: mov dword ptr [ebp - 0x14], ebx / 617734: mov dword ptr [ebp - 0x10], ` | - | - | AscGlobalsJ.cpp:1596 / AscGlobalsJ.cpp:1615 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x61779F | static TSFixedArray<uint8_t>* GetAlphaMask +0xFF | `617796: call 0x4b58d0 / 61779B: xor eax, eax / 61779D: cmp edi, 0x80 / 6177A3: jne 0x6177ac / 6177A5: mov eax, 0xa23fa4 ` | - | - | AscGlobalsJ.cpp:1593 / AscGlobalsJ.cpp:1612 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x6177A6 | static TSFixedArray<uint8_t>* GetAlphaMask +0x106 | `61779B: xor eax, eax / 61779D: cmp edi, 0x80 / 6177A3: jne 0x6177ac / 6177A5: mov eax, 0xa23fa4 / 6177AA: jmp 0x6177b6 /` | - | - | AscGlobalsJ.cpp:1597 / AscGlobalsJ.cpp:1616 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x6177B2 | static TSFixedArray<uint8_t>* GetAlphaMask +0x112 | `6177AA: jmp 0x6177b6 / 6177AC: cmp edi, 0x40 / 6177AF: jne 0x6177b6 / 6177B1: mov eax, 0xa23fd8 / 6177B6: push 1 / 6177B` | - | - | AscGlobalsJ.cpp:1598 / AscGlobalsJ.cpp:1617 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x6194ED | void SetPortraitTexture +0x1BD | `6194E1: lea ecx, [ebp - 0x5c8] / 6194E7: call 0x6ae8b0 / 6194EC: mov eax, 0x40 / 6194F1: call 0x6176a0 / 6194F6: xor ecx` | - | - | AscGlobalsJ.cpp:1591 / AscGlobalsJ.cpp:1610 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x619546 | void SetPortraitTexture +0x216 | `61953A: push 0x616b90 / 61953F: push esi / 619540: push eax / 619541: push 2 / 619543: push 2 / 619545: push 0x40 / 6195` | - | - | AscGlobalsJ.cpp:1594 / AscGlobalsJ.cpp:1613 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x619548 | void SetPortraitTexture +0x218 | `61953F: push esi / 619540: push eax / 619541: push 2 / 619543: push 2 / 619545: push 0x40 / 619547: push 0x40 / 619549: ` | - | - | AscGlobalsJ.cpp:1595 / AscGlobalsJ.cpp:1614 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x63200D | int32_t NetClient::ProcessMessage +0x2D | `632001: mov edx, dword ptr [edi] / 632003: mov eax, dword ptr [edx + 0x4c] / 632006: push esi / 632007: mov ecx, edi / 6` | - | - | CNetClient.cpp:150 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x634E30 | void ClientDBInitialize +0x30 | `634E29: push esi / 634E2A: mov esi, 0x6348b0 / 634E2F: call 0x6337d0 / 634E34: call 0x7f6a80 / 634E39: call 0x634ae0` | - | - | CDBCMgr.cpp:71 | ClientDBInitialize: troca o StaticDBLoadAll(0x6348B0) por CDBCMgr::Register do DLL -> patch no fonte (ClientDBInitialize chama callback exportado/registrado pelo ext) |
| 0x72594B | void CGUnit_C::UnitNameplatesReposition +0xBB | `725944: mov edx, dword ptr [ebx + 0x2e8] / 72594A: push 0 / 72594C: mov dword ptr [ebp - 0x18], eax / 72594F: lea eax, [` | - | - | AscNamePlates.cpp:620 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x781426 | void CWorld::Initialize +0x4D6 | `78141D: call 0x769100 / 781422: add esp, 0x30 / 781425: call 0x77ed40 / 78142A: pop esi / 78142B: mov esp, ebp / 78142D:` | - | - | ZoneLightData.cpp:12 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x781730 | void DayNight::Update +0x40 | `781724: cmp eax, edi / 781726: jne 0x78172a / 781728: xor edi, edi / 78172A: cmp dword ptr [0xadfbc4], 0x212 / 781734: j` | - | - | ZoneLightData.cpp:14 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x781751 | void DayNight::Update +0x61 | `781748: mov edi, dword ptr [ebp + 0xc] / 78174B: test edi, edi / 78174D: je 0x781758 / 78174F: push edi / 781750: call 0` | - | - | ZoneLightData.cpp:15 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CD8A0 | void CWorldScene::CreateOcclusionVolumes +0x50 | `7CD897: call 0x984240 / 7CD89C: mov edi, dword ptr [ebp + 8] / 7CD89F: mov esi, 0xaf0044 / 7CD8A4: mov ebx, 0x3e / 7CD8A` | - | - | OcclusionVolumeData.cpp:11 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CD8A5 | void CWorldScene::CreateOcclusionVolumes +0x55 | `7CD89C: mov edi, dword ptr [ebp + 8] / 7CD89F: mov esi, 0xaf0044 / 7CD8A4: mov ebx, 0x3e / 7CD8A9: lea esp, [esp] / 7CD8` | - | - | OcclusionVolumeData.cpp:12 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CDC16 | void CWorldScene::RenderOcclusionVolumes +0x2E6 | `7CDC0F: push ebx / 7CDC10: mov dword ptr [ebp - 0x14], ebx / 7CDC13: call eax / 7CDC15: mov esi, 0xaf0060 / 7CDC1A: mov ` | - | - | OcclusionVolumeData.cpp:13 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CDC1D | void CWorldScene::RenderOcclusionVolumes +0x2ED | `7CDC13: call eax / 7CDC15: mov esi, 0xaf0060 / 7CDC1A: mov dword ptr [ebp - 0xc], 0x3e / 7CDC21: mov ecx, dword ptr [esi` | - | - | OcclusionVolumeData.cpp:14 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CDD23 | void CWorldScene::RenderOcclusionVolumes +0x3F3 | `7CDD1C: push edi / 7CDD1D: mov dword ptr [ebp - 0x14], edi / 7CDD20: call eax / 7CDD22: mov ebx, 0xaf0040 / 7CDD27: mov ` | - | - | OcclusionVolumeData.cpp:15 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x7CDD2D | void CWorldScene::RenderOcclusionVolumes +0x3FD | `7CDD22: mov ebx, 0xaf0040 / 7CDD27: mov dword ptr [ebp - 0xc], ebx / 7CDD2A: mov dword ptr [ebp - 8], 0x3e / 7CDD31: mov` | - | - | OcclusionVolumeData.cpp:16 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x8CA5F0 | void GruntLogin::GetLogonMethod +0x70 | `8CA5E4: mov byte ptr [ebp - 0xf], 3 / 8CA5E8: mov byte ptr [ebp - 0xe], 5 / 8CA5EC: mov word ptr [ebp - 0xc], 0x3034 / 8` | - | - | AscRealmData.cpp:235 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x989B9A | void CMovementShared::SetRawFacing +0x2A | `989B94: fstp dword ptr [esi + 0x20] / 989B97: jne 0x989bab / 989B99: push 0 / 989B9B: call 0x9881d0 / 989BA0: and dword ` | - | - | AscAura137.cpp:35 | precisa patch no fonte (constante/alvo de call no C++) |
| 0x98EA07 | void CGNamePlateFrame::OnLayerUpdate +0x17 | `98E9FF: cmp eax, dword ptr [0xbd07b0] / 98EA05: push edi / 98EA06: jne 0x98ea1c / 98EA08: mov ecx, dword ptr [esi + 0x2a` | - | - | AscNamePlates.cpp:633 | precisa patch no fonte (constante/alvo de call no C++) |

## Dados em .rdata (3)

| end. 12340 | funcao (fonte) +off | instr/contexto x86 | x64 | conf | uso no DLL | nota |
|---|---|---|---|---|---|---|
| 0x9E0360 | void LoadEncryptedModule +0x1AC80 | `` | - | - | ZoneLightData.cpp:17 | ponteiro em .rdata (vtable/tabela) -> mapear pelo STATIC/vtable x64 |
| 0x9F3A88 | void LoadEncryptedModule +0x2E3A8 | `` | - | - | AscGlobalsJ.cpp:1660 | ponteiro em .rdata (vtable/tabela) -> mapear pelo STATIC/vtable x64 |
| 0x9F5200 | void LoadEncryptedModule +0x2FB20 | `` | - | - | AscRealmData.cpp:233 | ponteiro em .rdata (vtable/tabela) -> mapear pelo STATIC/vtable x64 |

## Proposta para tools/extra-addr.txt (so ALTA; dono aplica)

```
0x4CFD20 WowClientDB_RLE<SpellRec>::GetRecord
0x50D641 Script_SendChatMessage +0x48A
0x52B27A CGGameUI::Idle +0x3F7
0x55F4D0 TSHashTable<UIADDON,HASHKEY_STRI>::Ptr
0x59714D CGCharacterModelBase::RotateModel +0x69
0x5971C1 CGCharacterModelBase::UpdateModel +0x15
0x5A8C70 CGActionBar::GetSpell +0x56
0x5B01C8 CGCurrencyTypes::BuildDisplayList +0x1DD
0x60FAB2 Script_UnitLevel +0xF4
0x61935B SetPortraitTexture +0x4C
0x67CA30 DBCache<ItemStats_C,int,HASHKEY_INT>::GetRecord
0x67D770 DBCache<NameCache,unsigned __int64,CHashKeyGUID>::GetRecord
0x67DE90 DBCache<QuestCache,int,HASHKEY_INT>::GetRecord
0x67EF70 DBCache<CGPetition,int,HASHKEY_INT>::GetRecord
0x715586 CGUnit_C::ObjectNameVisibilityChanged +0x2B
0x7370AF CGUnit_C::SetSheatheState +0x1AD
0x792AF2 CWorldScene::AddMapObjDefGroup +0x24
0x792AFF CWorldScene::AddMapObjDefGroup +0x31
0x793115 CWorldScene::CullEntitys +0x94
0x793126 CWorldScene::CullEntitys +0xA5
0x79313F CWorldScene::CullEntitys +0xBE
0x7AD2EE CMapObj::RenderInterior +0x153
0x7EC15A DayNight::LoadLightingBands +0x1F9
0x80D6EF Spell_C_CastSpell +0x7D0
0x989B97 CMovementShared::SetRawFacing +0x31
0x991050 FT_Get_Kerning
0x9911A0 FT_Get_Char_Index
0x991320 FT_New_Library
0x992610 FT_Done_Face
0x992780 FT_Set_Pixel_Sizes
0x992B60 FT_Render_Glyph
0x992CB0 FT_Done_Library
0x992DA0 FT_Load_Glyph
0x993370 FT_New_Memory_Face
```

Obs.: `SetPortraitTexture` tem 3 simbolos no PDB x64 (sobrecargas) -> o mapaddr recusa nome ambiguo; usar o nome decorado/RVA ou estender o mapaddr. Os MEDIA ficaram fora desta lista de proposito.
