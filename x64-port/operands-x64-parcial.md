# (b) operandos x64 -- PARCIAL (sessao projects-2c parou por limite de uso)

Sem diff ainda. Decisoes conferidas no disasm 32/64 (x86 -> x64 VA, bytes novos x64):

| x86 | x64 | bytes x64 | tipo | nota |
|---|---|---|---|---|
| 0x464c4f | 0x1405841fa | 80 7C 24 70 80 | BYTE | HandleCharEnum: cmp byte [n],0xA -> 0x80 (limite de personagens) |
| 0x5663d4 | 0x140438ab0 | 81 FB 00 02 00 00 | BYTE | SetMacroBody: limite 0x100 -> 0x200 |
| 0x56e499 | 0x1404de01a | 05 01 00 00 00 | BYTE | Script_CheckInbox: add 60000 -> 1 |
| 0x574a5f | 0x1404ed997 | 83 B8 C0 00 00 00 00 | BYTE | Script_ConvertToRaid: cmp [..+0xC0],0xA -> 0 |
| 0x611717 | 0x14040c215 | 80 F9 0A 7C 36 | BYTE | Script_GetComboPoints: cmp 0xB;jne -> cmp 0xA;jl |
| 0x6dbe41 | - | - | NA | AreaTriggerCheck: escreve 0x64 sobre push 0x64 (ja e o valor do 12340) |
| 0x7ff2a5 | 0x140298a17 | 90 90 90 90 90 90 | BYTE | SpellRec::GetCastTime: NOP no add ebx,500 |
| 0x5ae56b | 0x14048acde | 83 FB 14 | BYTE | GetNextFreeSetID: 10 -> 20 conjuntos |
| 0x5af142 | 0x1404d1bbf | 83 F8 14 | BYTE | Script_GetEquipmentSetInfo: 9 -> 0x14 (x64: lea eax,[rcx-1]; cmp eax,9; jbe) |
| 0x5af485 | 0x14048a8e6 | 83 FB 14 | BYTE | CanCreateSet: 10 -> 20 |
| 0x5af548 | 0x14048b430 | 41 83 FE 14 | BYTE | SMSG_EQUIPMENT_SET_LIST: 10 -> 20 |
| 0x5af809 | 0x14048aa16 | 83 FB 14 | BYTE | CreateEquipmentSet: 10 -> 20 |
| 0x5afb64 | 0x1404d21ca | 41 B8 14 00 00 00 | BYTE | Script_SaveEquipmentSet: texto "%d sets" 10 -> 20 |
| 0x632163 | 0x1402b5270 | 83 FA 08 | BYTE | GetNetStats: historico de ping 16 -> 8 (1a de 2 copias no x64) |
| 0x632163 | 0x1402b527a | 83 FA 08 | BYTE | GetNetStats: 2a copia do cmp 0x10 |
| 0x63264a | 0x1402b57d2 | 83 F8 08 | BYTE | HandlePong: 16 -> 8 |
| 0x63266a | 0x1402b57f2 | 83 F8 08 | BYTE | HandlePong: 16 -> 8 |
| 0x632de5 | 0x1402b570e | BA 78 EC FF FF | BYTE | HandleIdle: -30000 -> -5000 ms |
| 0x54cfd4 | 0x14044a5e5 | 8D 83 F4 01 00 00 | BYTE | RequestPlayerPositions: +5000 -> +500 ms (x64: lea eax,[rbx+0x1388]) |
| 0x4910db | 0x1400a5f8c | BA D0 07 00 00 | BYTE | SetFrameLevel: teto 0x80 -> 2000 (x64 usa o mesmo mov edx p/ cmp e cmov) |
| 0x4910e2 | 0x1400a5f8c | - | DUP | mesmo imm do 0x4910DB no x64 |
| 0x802221 | 0x140741a42 | 90 90 | BYTE | Script_CancelItemTempEnchantment: o x86 grava 02 SOBRE o opcode do ja (vira add dh,[ebx]) = ja some; x64: NOP2. CONFERIR intencao (talvez fosse o imm 0x802220: cmp 1 -> 2) |
| 0x4da8eb | - | - | NA | CGlueMgr::Resume: memcmp do digest do GlueXML -- o x64 compila com DISABLE_SIGNATURE_CHECKS (nao existe) |
| 0x59dc63 | 0x1404b7d87 | EB 24 | BYTE | Script_PlaceAuctionBid: je -> jmp (apos GetRecord) |
| 0x5a5163 | 0x140452039 | EB 16 | BYTE | GuildBank SwapItems: je (IsBound) -> jmp |
| 0x5a522f | 0x1404520dd | EB 14 | BYTE | GuildBank SwapItems: jne (guid) -> jmp (x64 compara o guid em 64 bits) |
| 0x5a52e4 | 0x14045217e | EB 0F | BYTE | GuildBank SwapItems: jne (permissao 2) -> jmp |
| 0x5a5472 | 0x140451d4b | EB 0F | BYTE | GuildBank SwapBankItems: jne (permissao 2) -> jmp |
| 0x5a6506 | 0x1404d4219 | EB 0F | BYTE | Script_AutoStoreGuildBankItem: jne (saques) -> jmp |
| 0x5a6956 | 0x1404d46a0 | 90 90 | BYTE | Script_GetGuildBankTabInfo: je (bit 1) -> NOP (o 2o je do x64 e o bit 2, outro teste) |
| 0x5ac94d | 0x14049ef8e | 90 90 90 90 90 90 | BYTE | CGGMTicketInfo::CreateTicket: je (IsTicketTextValid) -> NOP6 |
| 0x5cf2f6 | 0x1404e7a57 | EB 0F | BYTE | Script_OfferPetition: jne (CanCooperate) -> jmp |
| 0x5ad0b0 | 0x1404d2cf3 | 90 90 90 90 | BYTE | Script_NewGMTicket: test eax,eax; je (CanPerformAction) -> NOP4 |
| 0x5d6e9c | 0x14042ffaf | EB 07 | BYTE | SendRequestPetInfo: sempre envia (x86: jne->jmp + NOP do je; o pacote nao leva o guid). x64: jne->jmp pula os dois testes |
| 0x5d6ea6 | 0x14042ffb6 | - | DUP | coberto pelo jmp em 0x14042FFAF |
| 0x5f83ec | - | - | NA | LoadAddOn: je depois de xor eax,eax/test (sempre salta) -> jmp: sem efeito; o x64 nem gera o ClientPostClose |
| 0x52abd9 | 0x1403ddcf1 | EB 47 | BYTE | InitializeFrameXML: pula o switch da assinatura do FrameXML (x86: cmp eax,3 -> jmp +0x41). x64: depois do call, jmp p/ 0x1403DDD3A (fim do switch) |
| 0x52ad43 | - | - | NA | InitializeFrameXML: memcmp do digest -- x64 com DISABLE_SIGNATURE_CHECKS |
| 0x52ad45 | - | - | NA | idem (push 0xA; call ClientPostClose do memcmp) |
| 0x52adcc | 0x1403dde51 | 90 90 90 90 90 | BYTE | InitializeFrameXML: NOP no call CGGameUI::Initialize (o DLL chama depois) |
| 0x4e4bc2 | 0x14028bf32 | 90 90 90 90 90 90 | BYTE | CGlueMgr::SurveyExecute: jb (SFileAuthenticateArchiveEx < 5) -> NOP6 |
| 0x4e57fe | 0x14028b12f | 90 90 | BYTE | CGlueMgr::PatchDownloadApply: autenticacao < 5 segue p/ o prepatch (x86 NOP no jmp; x64 NOP no jb que vai p/ o erro) |
| 0x707ba3 | 0x1402f58d8 | EB 3C | BYTE | OnUpdateDurability: jle -> jmp (nunca RemoveHandItem/UnitModelUpdate ao quebrar) |
| 0x707be2 | 0x1402f5869 | 90 90 90 90 90 | BYTE | OnUpdateDurability: NOP no call CGPlayer_C::EquipItem (x86 NOP8 = push/mov/call; x64 so o call, rcx/rdx sao descartaveis) |
| 0x708328 | 0x1402f29ba | 90 90 90 90 90 90 | BYTE | CGItem_C::CanGoInSlot slot 17: x86 je->jmp (ignora tipo 0x1C == ranged); x64 o compilador inverteu: NOP no jne de falha |
| 0x634b07 | 0x14024665e | EB 17 | BYTE | InitResistances: jbe -> jmp (nao corta a tabela em 7) |
| 0x74135d | 0x1402c782b | EB 73 | BYTE | CGUnit_C::Dismount: je (veiculo [+0x11A8]) -> jmp |
| 0x76a67a | - | - | FONTE | RegisterGxCVars: gxWindow default sempre "1" (registro antes do DLL x64) |
| 0x57e9a8 | 0x14043d265 | B1 03 90 90 | BYTE | CGMinimapFrame::CountAvailableTrackingTypes: classe fixa 3 (mov cl,3) no lugar de movzx ecx,[rcx+0x45] |
| 0x57eb27 | 0x14043d539 | 41 B8 01 90 90 90 90 90 90 90 90 90 90 | BYTE | GetTrackingTypeInfo: o resultado final do x86 (57EB27+57EB32+57EB2D) e mov esi,0x90909001 sem shl -- reproduzido: r8d=0x90909001. ESTRANHO mas e o byte-a-byte do original |
| 0x57eb2d | - | - | DUP | coberto por 0x57EB27 |
| 0x57eb32 | - | - | DUP | coberto por 0x57EB27 |
| 0x728aa3 | 0x1402c7b70 | 40 80 FF 00 | BYTE | CGUnit_C::DoPowerRegen: cmp ebx,6 -> 0 (x64 cmp dil,6) |
| 0x731480 | 0x1402d980e | EB 17 | BYTE | CGUnit_C::OnRightClick: jge -> jmp (sem erro 0xEF de nivel minimo) |
| 0x70ed92 | 0x140303411 | 90 90 | BYTE | CGGameObject_C::Reenable: NOP no jne (tipo 0x21) |
| 0x71c2c0 | 0x1402cb240 | 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90 90  | BYTE | CGUnit_C::GetPredictedHealth: NOP no ramo predictedHealth (x86 18 bytes, x64 20) -> sempre a vida do campo |
| 0x783310 | 0x14052a88f | EB 57 | BYTE | CWorld::Update: jne -> jmp (x64 jbe apos comiss) |
| 0x819a74 | 0x1400c695b | EB 07 | BYTE | Script_debuglocals: jne -> jmp |
| 0x634e28 | 0x140246156 | C7 05 A8 B4 7C 00 00 00 00 00 | BYTE | ClientDBInitialize: flag dbCompress = 0 (x86 byte, x64 dword rip-rel; imm em +6) |
| 0x6e2867 | - | - | NA | MeleeModeEnter: o fonte ja tira o UpdateBaseAnimation(0x40,-1) (WoWFix335, Player_C.cpp:8338) |
| 0x5cba74 | 0x14046a572 | 90 90 90 90 90 90 | BYTE | Packet_MSG_GUILD_PERMISSIONS: NOP no store do rank (byte global) |
| 0x67a18e | - | - | PEND | DBCache<NameCache>: je apos call 0x6F6020 -- template inlinado diferente no x64, sem par seguro |
| 0x6db8f9 | - | - | PEND | CGPlayer_C::SwapGuildBankItem: o cmp [tab*4+0xC21E80],esi/DisplayError(0x5F) nao aparece no x64 (so cmp [..],0; je) -- conferir o fonte |
| 0x81c9a8 | 0x14013fe49 | B2 01 | BYTE | CM2Scene::SetTime: GarbageCollect(force 0->1). x64 xor edx,edx -> mov dl,1 (so testa !=0, M2Cache.cpp:180) |
| 0x81c9d9 | 0x14013c9df | B2 01 | BYTE | CM2Scene::AdvanceTime: idem |
| 0x5fd886 | 0x1403badc0 | 90 90 | BYTE | ValidateCameraLinearSpeed: sem teto (2o compare) |
| 0x8c51f0 | - | - | FONTE | BattlenetAssert: troca o ponteiro da string (0xA944A4 -> 0x9FF5FC) -- operando de dado; x64 usa lea rip-rel, ver pacote do cliente |
| 0x517ee8 | 0x1403e254d | EB 37 | BYTE | PlacedFrameCallback: nao grava "FrameLevel: %d" (x86 NOP62; x64 jmp sobre printf+OsWriteFile) |
| 0x5184e1 | 0x1403df64d | EB 41 | BYTE | LoadPlacedFrames: nao le "FrameLevel: " (x86 NOP30+jmp; x64 jmp direto p/ o teste do Anchor) |

## Sites de modulo (toggle runtime: stock <-> on)

| x86 | x64 | stock | on | modulo | nota |
|---|---|---|---|---|---|
| 0x720020 | 0x1402c04e8 | 0F 85 3C 01 00 00 | 90 90 90 90 90 90 | AscClientOptions:58 | ChatBubbleShow: jne [+0x1470] <-> NOP6 |
| 0x725737 | 0x1402d42d8 | 74 | EB | AscClientOptions:60 | NamePlateAddOrUpdate: je <-> jmp |
| 0x73d46a | 0x1402d617a | 7E | EB | AscClientOptions:326 | OnCollideFallLand: jle <-> jmp (on=stock 7E) |

## Falta
- Modulos: 0x80D7C1/0x80C91D/0x621766 (ClientOptions), AscGlobalsJ (720E68/524DC1/524276 + PatchJumps), AscCamera, AscGraphics, AscCAMgr, AscInitHooks, AscTooltipScaling, AscAttachFixes.
- FONTE (diff cliente): CVar defaults, tabelas de raca, minimap g_levels/g_distances, caves do mouse, ponteiros float.
- Gerar extra-addr (Func +off), tabela x64 no AscAttachPatches::Write, syntax-check, out/operands-x64.diff.
- Parte (c) asm->MinHook nao iniciada.
- Obs: WriteCodeX64 precisa de delta p/ 0x632163 (2 copias no x64).
