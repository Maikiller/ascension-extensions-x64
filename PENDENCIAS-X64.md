# Pendências do porte x64 (Extensions64.dll)

Revisado em 02/11 (c1). O que estava listado aqui foi resolvido; fica o registro de como.

## static_assert de layout 32 bits
Cada um ganhou o equivalente x64 (o assert de 32 bits continua para o build x86):
- `AscBridgeStore.hpp` — `CreatureRecord` x64 0x20 (name +8), `VanityRecord` x64 0xD0 (strings em `s[]` +0xB8).
  A ponte (MMgr64.exe) segue com o wire de 32 bits (0x1C/0xB4); `ToWire`/`FromWire`/`StringSlot` convertem.
- `AscItemAddon.hpp` — `Record` x64 0x48 (name +8, fields +0x18); wire 0x40 pela mesma conversão.
- `AscBuildCreator.cpp` — `ZStream` x64 0x58: é o `z_stream` do zlib 1.2.2 do cliente; `deflateInit2_`/`inflateInit2_`
  do WowClient64 recusam `stream_size != 0x58` e leem zalloc/zfree em +0x30/+0x38 (conferido no disassembly).
- `AscObjectAddon.hpp` — `Field` x64 24 bytes: estrutura interna da DLL, ninguém depende do passo de 12.
- `AscFontRender.cpp` — `Glyph` já tinha o assert x64 (0x40).
- `AscDiscord.cpp` — fora do escopo (precisa do `discord_game_sdk.dll` x64).

## Rotinas naked/asm x86
Todas portadas: os `OriginalXXXX` viraram trampolim do MinHook (`AscRuntime::Detour`), os caves viraram patch no
fonte do cliente. A última, `CachePathHook` (0x667EF9), é o patch 08-cache-path-realm (`GetCacheDirectory`);
o stub `Unported` foi removido.

## Hooks do x86 sem hook no x64 (e por quê)
- `0x744150` seta do círculo de seleção → patch 02 (`CoaBuildSelectionRotMatrix` nos 4 chamadores).
- `0x7EB210` AmbientColors (GetRecord dobrado no x86) → detour em `DayNight::LoadLightIntBand` 0x7EBF30.
- `0x667EF9` cache WDB por reino → patch 08.
- `0x4181B0` (anti-tamper, só relatório), `0x65C290` (CrashContext, só relatório), `0x6348B0`/`0x83CF00` (LargeAlloc,
  só relatório), `0x84E350` (condição que nunca dispara no original), `0x73F660` (lista sem registrante): sem efeito
  no jogo, não portados.

## Por módulo (regra para o que entrar em `AscX64::kPorted`)
1. Cada `reinterpret_cast<F>(0x...)` → `AscX64::Fn<F>(0x...)` com a assinatura conferida contra `out/protos.json` (floats, WOWGUID 64 bits, sem `edx` falso de __thiscall).
2. Cada acesso `ptr + 0xNN` → offset x64 via `tools/layout.py CLASSE 0xNN`.
3. Endereços sem equivalente na tabela → edição no fonte do cliente.
