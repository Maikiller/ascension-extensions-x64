# x64-port

Tools, CI and history of the x64 port (Extensions64 + WowClient64). The full documentation (status, how it
works, build/CI/rebase, test battery and open items) is in the root [`README.md`](../README.md).

- `ci/`: CI Jenkinsfiles and scripts (`ci-build.sh`, `ci-publicar.sh`, `build-*.sh`).
- `tools/`: `mapaddr.py` + `extra-addr.txt` (address table), `protos.py`, `checksig.py`, `audit_hooks.py`,
  `chkexpect.py`, `chkretaddr.py`, `layout.py`/`pdbfields.py` (x64 PDB layout), `bateria.ps1` (test machine) and `bateria.py`.
- `patches-cliente/`: diffs of the client source patches (the README there covers the original 01–17; later
  numbers are in the root README's table).
- `diffs/` and `*.md`: history of the helper deliveries and measurements from 2026-10-30/31.
