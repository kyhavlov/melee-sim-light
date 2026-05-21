# Local Sources of Truth (Decomp / ASM / Dolphin / Slippi)

Archived note: this file preserves path conventions from the old repo. Prefer current setup notes
in `agent_docs/DEVELOPMENT_WORKFLOWS.md` and `refs/README.md`.

This project is **decomp-first**. When a mismatch is found, fixes must be grounded in local
decompiled C and/or the disassembly with symbols.

## Decomp + Raw ASM (Primary)

### Decompiled C
- `refs/melee/src/melee/`
  - Fighter code: `refs/melee/src/melee/ft/`
  - Items / projectiles: `refs/melee/src/melee/it/`
  - Stages / collision: `refs/melee/src/melee/gr/`
  - “lb” utilities (vector/matrix, dynamics helpers): `refs/melee/src/melee/lb/`

### Disassembled ASM with symbols
- `refs/melee/build/GALE01/asm/`
  - Mirrors the decomp directory structure (e.g. `asm/melee/ft/...`, `asm/melee/lb/...`).
  - Use this when C is incomplete/ambiguous, or when 1‑ULP differences suggest instruction-order details.

### Practical navigation
- Search by function name in C:
  - `rg -n "ftCo_8009DD94" refs/melee/src/melee`
- Search by function label/address in asm:
  - `rg -n "ftCo_8009DD94" refs/melee/build/GALE01/asm`

## Slippi Modifications (Primary when replay-affecting)

Slippi applies gameplay-affecting modifications (including UCF and replay playback behavior).

- Slippi gameplay ASM mods:
  - `refs/slippi-ssbm-asm/`
- Slippi Dolphin / Ishiiruka:
  - `refs/Ishiiruka/` (this repo contains our **custom build** for engine-dump generation)

When something matches in live engine but differs in replay/dump, confirm whether Slippi playback
or codes are altering state for recording or determinism.

## Engine Dump Tooling (Local)

- Dump generator scripts:
  - `scripts/dolphin_engine_dump.py` — generate a single dump from a replay using playback Dolphin.
  - `scripts/engine_dump_suite.py` — generate dumps for a suite.
- Hot-path routing and source lookup were old-repo docs:
  - `agent_docs/HOT_PATH_INDEX.md` (not present in this repo)
  - `agent_docs/RESOURCE_MAP.md` (not present in this repo)
- Dump format docs:
  - `agent_docs/dolphin_tooling/engine_dump_schema.md` (field index)
  - `agent_docs/engine_dump_layout.rs` (old repo reference layout, not present in this repo)
- Validation:
  - `crates/ssbm_sim/src/bin/engine_dump_validate.rs` — validate sim vs one dump.
  - `crates/ssbm_sim/src/bin/engine_dump_suite.rs` — validate a suite of dumps.

## Community / Secondary References (Not authoritative)

- Community datasheet snapshot:
  - `refs/datasheet/`
- Reference links and notes:
  - `refs/COMMUNITY_RESOURCES.md`

Use these for intuition only; do not treat them as proof for a sim change without backing from
decomp/asm (or Slippi Dolphin/asm when it’s specifically a Slippi behavior).
