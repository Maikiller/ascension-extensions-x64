# 2c — AscGlobalsJ x64: andamento (30/10, parado a pedido do usuário; nada aplicado no wt-2c ainda)
Achados para o diff g1 (encadeado depois do lote3-g6):
- CombatLogAllowed: CombatLogEntry x64 -> m_event 0xC->0x14, m_spellId 0x44->0x5C, m_info.values[0..2] 0x58/0x5C/0x60 -> 0x80/0x84/0x88.
- ScriptErrorHandler: ar[100] = lua_Debug x86; no x64 lua_Debug = 0x78 -> estouro de pilha. Usar ar[ASC_OFF(100, 0x80)].
- GetItemTemplate: tabela kTemplate em offsets x86 do ItemStats_C; x64 desloca +4 (ID/vfptr 0; Name 0x1F4->0x200; Description 0x17C->0x180 é char*; após ele +8: PageText 0x180->0x188 ...). Mapa completo: layout.py ItemStats_C (tmpl64 no scratchpad 2c). memcpy 0x1F8 precisa do tamanho x64 ou ler direto do registro.
- SetItemQualityColor: WriteCode(0xAD2D84 + q*4) -> só a base mapeada; usar ASC_PTR(0xAD2D84) + q (uint32_t[8], .data).
- ApplyComponents: 0x4EAA70 recebe cópia x86 -> AscClientDbc::Native(0xAD3DDC, 0x11) (igual CharacterCreate).
- SetMaskTexture/ClearMaskTexture: 10 patches de código x86 (GetAlphaMask/SetPortraitTexture), WriteMem recusa no x64. Proposta: x64 retorna com 1 log; port real depois via detour.
Conferidos OK: 0x770/0x771/0x94E handlers, Read/WriteCustomWTF, RegisterSavedCVar, addons, ArenaRewardInfo, SetWorldTime (CGameTime sem ponteiros até +0x50; 8 bytes = rate+fracMinutes nos dois), SetAutoRangedCombatSpell, sites já com ramo x64.
Faltam: revisar linhas 760-830, 1080-1240 (EncounterInfo, spellbook, ícones) e depois varredura global de endereço cru + Graphics/ClientOptions/NamePlates/Camera.
