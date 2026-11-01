# Ascension Extensions — reconstruction + x64 port (CoA)

This repository (`ascension-extensions-x64`, default branch `x64`) holds two things:

1. **The reconstruction of Project Ascension's `Extensions.dll`** for the WoW 3.3.5a client (build 12340,
   `Ascension.exe`, 32-bit), function by function, built on the WotLK-Extensions scaffold (`WotLKExtensions/`,
   MIT, see `UPSTREAM-README.md` and `LICENSE`). The base is upstream
   `firstoni-dev/ascension-extensions-reconstruction`.
2. **The 64-bit port, `Extensions64.dll`**, which runs alongside **`WowClient64.exe`**: the 3.3.5a client
   rebuilt for x64 from the decompiled client source (branch `coa-cliente-20261031`). This is the
   client for the **Conquest of Azeroth (CoA)** server. Why port: the 32-bit client crashes when it runs out of
   address space (~4 GB ceiling); x64 has no such ceiling.

The same source builds both targets: `Extensions.dll` (x86, identical to the original) and `Extensions64.dll`
(x64). All x64-only code lives under `#if ASC_X64` / `#if defined(_WIN64)`; the x86 branch stays as the
original.

---

## 1. x64 port status (2026-11-01)

| Item | Status |
|---|---|
| Last package released for testing | ext `2286089` + client `5bb308e` |
| Modules enabled on x64 (`AscX64::kPorted`) | 140 |
| Client hooks | 352 measured in the 2026-11-01 test battery, plus `0x6ECF80` enabled afterwards; of the reference's 312 real hooks, 9 are not installed (see §6) |
| Lua natives | 1,351 in `docs/DLL_REFERENCE.md`, all in ported modules; none returns `nil` for lack of porting |
| `checksig` (x64 signatures) | 939 ok / 0 diverging |
| `audit_hooks` | 0 problems |
| `chkexpect` (bytes checked before writing) | 117 sites / 0 problems |
| `chkretaddr` (extra-addr lines) | 164 / 0 problems |
| Patches to the client source | 01–38 (see §4) |

The game enters the world, handles raids and teleports with `/vista 6000` (after patch 37), and runs in a loop
under the test battery (§5.4).

---

## 2. How the x64 port works

The x86 DLL is a large set of hooks and patches **at absolute addresses** in `Ascension.exe`. On x64 those
addresses don't exist: the client was rebuilt, class layouts changed (8-byte pointers) and the compiler inlined
many functions. The port solves this in four layers.

### 2.1 Address table (`WowClient64.addr`)
- DLL code is still written with the **x86** address (e.g. `0x6ECF80`). At runtime `AscX64::Ptr(addr)` /
  `ASC_PTR(addr)` translates it to the x64 address using `WowClient64.addr` (format `WADR`: pairs of
  `u32 addr12340, u32 rva`), shipped with the exe.
- The table is regenerated on **every client build** by `x64-port/tools/mapaddr.py`, from:
  - the client source's `// OFFSET: 0x...` and `// STATIC: 0x...` marks (`tags.json` + PDB line info);
  - `x64-port/tools/extra-addr.txt`, manual entries in these forms:
    - `0xADDR Exact::Name +0xN` (PDB function + offset; `Name [n]` for overloads);
    - `0xADDR =0xBASE+0xN` (data field: mapped base + x64 offset);
    - `0xADDR vft:Class +0xN` (PDB vftable `??_7Class@@6B@` + offset);
    - `0xADDR str:"text"` (unique NUL-terminated string in the exe);
    - `0xADDR @0xRVA` (fixed RVA: **avoid**, it breaks on every client rebuild; 2 remain, checked by
      `chkretaddr`).

### 2.2 Hooks
- Detours and replacements use **MinHook** (vendored in `third_party/minhook`) through `AscRuntime::Detour` /
  `ReplaceFunction` → `AscX64::Hook`, which chains several hooks on the same target (2048-entry table).
- Every signature is checked against `out/protos.json` (generated from the PDB by `tools/protos.py`): floats
  in xmm registers, 64-bit `WOWGUID`, no fake `edx` from x86 `__thiscall`, 8-byte pointer returns and `bool`
  returns (only `al` is defined on x64). `checksig` fails on divergence; `retorno_bool` counts `bool` returns
  read as `int`.
- x86 naked/`__asm` routines became C++ or the MinHook trampoline; anything still unported goes to
  `AscX64::Unported`, which logs and does not run.

### 2.3 Writing code and data
- Byte patches go through `AscX64::WriteCodeX64(addr, bytes, n, expect)`: **it only writes if the current
  bytes match the expected ones** (otherwise it logs `bytes nao conferem ... nao escrito`). `chkexpect` checks
  every expected sequence against the CI's exe.
- RIP-relative operands (`movss [rip+disp]`, `lea r,[rip+disp]`, `call rel32`) change on every client
  rebuild. Where possible the expected bytes are **built at runtime** from the target (e.g. `AscCamera`
  OnTurnSpeed with `s_trackingTurnRate`, `AscAttachPatches::RetargetLeaX64`). Those still written as fixed
  bytes need a "rebase" whenever the client changes (§5.3).
- Data: `AscX64::WriteMem` (with `VirtualProtect`) and offsets through `ASC_OFF(x86, x64)`, translated with
  `x64-port/tools/layout.py CLASS 0xOFF` (x64 PDB layout; e.g. `CMovement_C+0x144 m_unit -> +0x178`).

### 2.4 Client source changes
When the x86 hook sat in the middle of a function, on an operand, or in a function that x64 inlined, there is
nothing to hook. Those cases became **patches to the client source** (§4): usually a global `g_coa*`
variable marked `// STATIC: 0x<x86 address>`, which mapaddr puts in the table and the x64 DLL writes through
`ASC_PTR`. That is also where fixes go for original client bugs that only show up with CoA data (races up to
id 63, custom M2s, high farclip).

---

## 3. Repository layout

```
src/Ascension/          DLL modules (Asc*.cpp); AscX64.cpp = kPorted, Ptr, Hook, WriteCodeX64, WriteMem
WotLKExtensions/        upstream scaffold (CDBCMgr, FrameScript, ZoneLightData...)
third_party/minhook/    MinHook (x64 hooks)
docs/DLL_REFERENCE.md   full DLL reference (natives, packets, events, CVars, hooks per module)
x64-port/
  ci/                   Jenkinsfile, Jenkinsfile.publicar, ci-build.sh, ci-publicar.sh, build-*.sh
  tools/                mapaddr, protos, checksig, audit_hooks, chkexpect, chkretaddr, layout, pdbfields,
                        extra-addr.txt, bateria.ps1 (test machine) and bateria.py (analysis)
  patches-cliente/      diffs of the client source patches (01–17, 37, 38; 18–36 exist only as src commits)
  diffs/                historical diffs delivered by helper sessions during the port
  *.md                  measurements, module triage, classification of addresses with no x64 equivalent
PENDENCIAS-X64.md       32-bit layout static_asserts and asm routines still pending
```

---

## 4. Client source patches (branch `coa-cliente-20261031`)

| # | What it does |
|---|---|
| base | `WowAddr` bridge and loading `Extensions64.dll` in `MainFiberProc`; `patch-*.MPQ` and more than 64 MPQs; `OsValidateFunctionPointer` accepts Extensions64 |
| 01–17 | mid-function / operand sites turned into variables: minimap zoom, selection-circle arrow, POI range, AFK timers, raw SetPoint, portrait mask, per-realm WDB cache, nameplates, build info, config file, script events, DBC loading, camera collision, zone lights, occlusion volumes, aura 137 |
| 18–25 | attach-inline, world-entry tweaks, client options, realm enum, spell shadow/tokens, attach-late (32 sites), teleport/chat bubble |
| 26–28 | overflows with CoA races/classes: `GetRandomClassID`, race tables `[64]` (ChrRaces goes up to id 63), LFG/ComponentUtils/NamePlate/Emotes |
| 29–34 | robustness with custom M2s/sounds: emotes, `TSGrowableArray`, material/batch layer, collision index, `IntersectHitTestTriangles` |
| 35 | `bit.*` and `lua_number2int/integer` convert double→int like x86 does (CoA classes opened the wrong talent window) |
| 36 | globals `g_coaMistDensity`, `g_coaDayProgressOverride`, `g_coaGroundEffectDistMax` (mist, time of day, groundEffect) |
| 37 | `CMap::scCollideList` with 0x10000 entries and bounds in the SX/SY rasterizers: it overflowed with farclip > ~4266 yards (crash in `~CM2Shared` with `/vista 6000`) — an original x86 bug |
| 38 | `IsTitleKnown` limited to the 192 bits of `m_playerFieldKnownTitles`: a mask ≥ 192 read the currency field and reported a nonexistent title (Ascension's TitlesPanel broke while sorting) — an original x86 bug |

Numbering: each new patch takes the next number; its diff goes to `x64-port/patches-cliente/NN-name.diff` and
the src commit is titled `CoA x64: NN-name (...)`.

---

## 5. Build, CI and testing

### 5.1 Manual builds
From the working root on the build host:
- `build-client.sh` — builds `WowClient64.exe` + PDB on a Windows build machine (MSVC x64) and generates the `.addr`;
- `build-ext.sh` — `Extensions64.dll` (CMake, VS 2022, `-A x64`, `RelWithDebInfo`);
- `build-ext32.sh` — the same source as x86 (`Extensions.dll` must keep compiling).

`git archive` only takes committed files: commit before building.

### 5.2 CI (Jenkins)
- **Build job**: pollSCM every 2 min on both repos (`ascension-extensions-x64@x64` and the
  client source at `coa-cliente-20261031`). It runs `x64-port/ci/ci-build.sh <ext> <src>`:
  1. rebuilds the client if src changed (`out/cliente-<src7>/`: exe, pdb, `.addr`, `procs64.json`);
  2. builds Extensions64 and Extensions32 (`out/Extensions64-<ext7>.{dll,pdb}`);
  3. runs `mapaddr` → `protos` → `checksig` → `audit_hooks` → `chkexpect` → `chkretaddr`;
  4. **fails** on a compile error, on any check reporting a problem, or if an extra-addr `vft:`/`str:` line does
     not resolve. Lists go to `out/ci/rebase-<src7>-*.txt`; the last green pair is in `out/ci/ultimo-ok`.
  The checks read the build host's working tree: `ext/` and `src/` must be at the requested commit with no
  local changes (there is a lock at `.ci.lock`).
- **Publish job** (manual; optional `EXT7`/`SRC7`, empty = last green): `ci-publicar.sh` copies
  exe/pdb/`.addr` + DLL/pdb + `bateria.ps1` to the test machine and checks every file's md5. **It refuses
  while the game is running.**

### 5.3 "Rebase" after a client change
Every client patch shifts code. CI then goes red in `chkexpect`/`chkretaddr`, listing the bytes that changed
(almost always shifted RIP-relative displacements or `call rel32`). Steps:
1. read `out/ci/rebase-<src7>-chkexpect.txt` and `-chkretaddr.txt`;
2. update the expected bytes in ext and the byte comments in `extra-addr.txt`;
3. run `mapaddr.py` + `chkexpect.py` + `chkretaddr.py` locally against `out/cliente-<src7>/` until clean;
4. commit `x64: rebase NN (...)`, push → CI green.

To reduce this: prefer expected bytes built at runtime (§2.3) and `vft:`/`str:` instead of `@rva`.

### 5.4 Test battery
In PowerShell, in the x64 game folder:
```powershell
powershell -ExecutionPolicy Bypass -File .\bateria.ps1          # rounds in a loop until you type q
powershell -ExecutionPolicy Bypass -File .\bateria.ps1 -SemProcdump
```
Each round starts the game under `procdump -e -ma` (crashes and `__fastfail`) and, when it closes, collects
`Logs\`, `Errors\`, `Crashes\`, dumps and a note into `Bateria\<YYYYMMDD-HHMMSS>\`. The DLL's `AscBateria`
module records first-chance exceptions, exits with stack and hangs (`TRAVADO`, from a per-frame heartbeat). Afterwards,
`python3 x64-port/tools/bateria.py tudo` pulls, symbolizes, groups and splits by session; a
`TRAVADO` right after a fatal exception on the main thread becomes `TRAVADO_POS_CRASH` (procdump writing the
dump, not a real hang).

Notes: `procdump -x` turns on the debug heap (slow logout/exit) — the script sets `_NO_DEBUG_HEAP=1`; `-mp`
can produce an empty dump. The DLL's own log (`Logs\Extensions.log`) needs `-extlog 1`.

---

## 6. x64 open items

**No effect on x64 (need a client patch):**
- `0x744150` selection-circle arrow turning with the unit (the detour does not receive the unit);
- `0x7EB210` AmbientColors (inlined on x64);
- `SetMaskTexture`; `AscClientDbc::Insert` for SMSG `0x92A` (new spell sent by the server).

**Reference hooks x64 does not install (9) and why:**
`0x4181B0` (anti-tamper, report only), `0x65C290` (GetRecord folded by ICF on x86; crash report only),
`0x6348B0` and `0x83CF00` (LargeAlloc, crash report only), `0x84E350` (condition that never fires in the
original), `0x73F660` (list with no registrant), `0x744150` and `0x7EB210` (above), `0x667EF9` (per-realm WDB
cache: covered by patch 08, verify in game).

**Open:**
- ~15 s hang on world entry and on logout (waiting for the per-frame detector's stack);
- the game closes at character creation without producing a dump;
- replace the remaining fixed RIP-relative bytes with runtime-built expectations (ends the manual rebase);
- `PENDENCIAS-X64.md`: 32-bit layout `static_assert`s and asm routines still pending.

**Out of scope:** Discord (x64 SDK) and NativeLogon (also off on x86); d3d9 proxy, VolFog and DLSS.

### Resolved problems worth remembering
| Symptom | Cause |
|---|---|
| crash on world entry | pointer return truncated to `uint32_t` (SpellVisuals 0x720F80, 0x7E6DC0, 0x61E5C0) |
| game closes after SMSG `0x96B` | `InsertRecord` freed an interior pointer of `WowClientDB_RLE` (heap corruption) |
| false 30 s disconnect | `AscProfiler::Received` only stamped for opcode ≥ 0x55F |
| SpellActivationOverlays lit up | `HasAuraBySpellId` returns `bool`, read as `int` |
| wrong talent window for CoA classes | `(int)double` ≥ 2³¹ became 0x80000000 on x64 (patch 35) |
| crash in `~CM2Shared` with `/vista 6000` | `CMap::scCollideList` overflow (patch 37) |
| `SetFadeOutGameObjects` writing into `.rdata` | stale `@rva` in extra-addr pointed at the string "SPELL_FA…"; now `vft:CGObject_C +0x30` |
| `cameraTargetFocusTurnSpeed` had no effect | fixed RIP-relative bytes; now built at runtime |
| TitlesPanel "compare nil with string" | `IsTitleKnown` without bounds (patch 38) |

---

## 7. Client data (x86 and x64)

### `patch-B.MPQ`
Ascension's `patch-B.MPQ` is built for the Live realms. To use another realm, replace four glue files
(`RealmData.lua`, `RealmList\RealmList.lua`, `RealmList\Realm.lua`, `RealmList\RealmScrollListItem.lua`) and
point `Data\<locale>\realmlist.wtf` at your realm list. Sizes/SHA-256 and the reason for each file: see this
README's history (commit `03744a1`) or `UPSTREAM-README.md`. Our own interface fixes (taint, custom races,
translations) live in `patch-ZZ.MPQ`.

---

## 8. x86 reconstruction (the original `Extensions.dll`)

The reconstruction aims to be **exact**: every Lua native, packet handler, hook, CVar and code patch of the
genuine DLL, with the same behaviour, including its bugs (recorded separately, not fixed).

- Lua natives: 1,277 / 1,277 (namespace, name) pairs — 1,351 entries counting widget/metatable methods
- Packet handlers: 530 / 530 · Hooks: 400 / 400 · CVars: 93 / 93

**Reference:** [`docs/DLL_REFERENCE.md`](docs/DLL_REFERENCE.md) documents everything per module, with the
genuine DLL's address and our `file:line`.

**Deliberate differences from the genuine DLL:**
- **Logon:** the genuine DLL replaces SRP6 with Ascension's own protocol (hooks `0x8CCE00`, `0x9A83E0`,
  `0x9A88C0`). Here it is **off** (stock SRP6 against an AzerothCore authserver); the native reconstruction
  (X25519 / ChaCha20 / HMAC-SHA256) is built with `-DASCENSION_NATIVE_LOGON=ON` — see `docs/NATIVE-LOGON.md`.
- **Character advancement:** the `0x726` handler syncs the build's parameters before notifying listeners.
- **Tooltip:InsertLine(s):** wrapped so lines past the end are appended (`AscTooltipInsertFix.cpp`).
- **Interface hot patches:** only with `-DASCENSION_HOT_PATCH=ON` (SMSG `0x8F0`, CMSG `0x8F1`/`0x8F2`,
  `/hotpatch on|off|ask`; see `AscHotPatch.cpp`).

**x86 build** (VS 2022 x86 developer prompt, `vcvars32.bat`):
```bat
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B build
cmake --build build
```
Output `build\Extensions.dll`, next to `Ascension.exe` (which loads it itself).

**Portable tests:** `tests/native_ca_packets.py` (character-advancement handlers against client API doubles),
`tests/native_callback_order.py` (the original's measured callback list order),
`tests/hot_patch_formatter.py`.
