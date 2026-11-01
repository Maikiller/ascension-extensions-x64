# Pendências do porte x64 (Extensions64.dll)

Itens desligados no x64 que precisam ser revistos quando o módulo for portado.

## static_assert de layout 32 bits (guardados com `#if !defined(_WIN64)`)
- `AscBridgeStore.hpp` — `CreatureRecord` (0x1C), `VanityRecord` (0xB4): registros DBC com strings → no x64 os ponteiros têm 8 bytes; usar o layout do fonte do cliente (`tools/layout.py`).
- `AscBuildCreator.cpp` — `ZStream` (0x38): z_stream do zlib do cliente.
- `AscDiscord.cpp` — `DiscordCreateParams`: struct do Discord SDK (o SDK x64 tem layout próprio; exige `discord_game_sdk.dll` x64).
- `AscFontRender.cpp` — `Glyph`.
- `AscItemAddon.hpp` — `Record`; `AscObjectAddon.hpp` — `addon field`.

## Rotinas naked/asm x86 (stub `AscX64::Unported` no x64)
AscRuntime (Detour528C30, Detour528F00, Detour402910, EnterWorldDetour, GlueEpilogueCave), AscMain (CipherInitDetour),
AscAddonTaint (Original488540/510430/510450/510A00), AscAttachLate (Original52ED60), AscCVarHooks (Original4E4610),
AscGlobalsJ (Detour74F910/7385C0/4F5F40), AscLoadingScreen (Hook409ED0), AscMovementForce (Original6F1490),
AscRealmData (CachePathHook), AscSmallHooks (Original6E09E0), AscSpellTokens (Original6238A0, Hook6238A0).
Os `OriginalXXXX` são trampolins à mão → no x64 viram o trampolim do MinHook. Os caves no meio de função viram edição no fonte do cliente.

## Por módulo (antes de entrar em `AscX64::kPorted`)
1. Cada `reinterpret_cast<F>(0x...)` → `AscX64::Fn<F>(0x...)` com a assinatura conferida contra `out/protos.json` (floats, WOWGUID 64 bits, sem `edx` falso de __thiscall).
2. Cada acesso `ptr + 0xNN` → offset x64 via `tools/layout.py CLASSE 0xNN`.
3. Endereços sem equivalente na tabela → edição no fonte do cliente.
