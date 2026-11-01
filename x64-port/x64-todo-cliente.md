# Recursos do Extensions que no x64 precisam de patch no FONTE do cliente
- 0x7F3B60 GetSavedIndexFromDataIndex: devolver 8 em vez de 6 quando chamado de 0x57BF59 (filtro por endereco de retorno).
- 0x744150 BuildSelectionRotMatrix: girar a seta do circulo de selecao com a unidade (ObjectSelectionCircleTexture 2/3); x86 usava o ecx do chamador.

# Fase de offsets (DLL)
- ItemDisplayInfo copiado e repassado ao cliente (0x4EAA70 AddLink): no x64 passar AscClientDbc::Native(0xAD3DDC, id) e nao a copia x86 -- AscItemCollections 361/412, AscGlobalsJ 1627, AscCharacterCreate 888.

## sem_proto restantes (funções sem OFFSET no fonte; identificar no x64)
- 0x65C290 ClientDB GetRow (template WowClientDB, detour CrashContext + DBClient)
- 0x6348B0 thunk virtual de load do WowClientDB (detour LargeAlloc)
- 0x83CF00 M2 load (CM2Shared, entre Initialize e LoadSucceededCallback)
- 0x55F4D0 UIBindings (entre AdjustCommandKeyIndices e ExecCommand)
- 0x6F6020 CObjectEffectManager (entre ctor e PlayOneShotEffect)
- 0x7EB210 DayNight ambient colours
- 0x4181B0 WinMain.cpp (após WinMain)
- 0x4CFD20 Spell row copy: x64 coberto por AscClientDbc::Copy

## Pedido Ragnarok (projects-6a, 30/10)
- World::ValidateFarClip (World.cpp:1324, 0x780770): maxFarClip 2375/3 e 4750/3 -> 2500.0f no x64 (_WIN64). Hoje patch manual no exe do PC (.rdata 0x1407ED880/84).
