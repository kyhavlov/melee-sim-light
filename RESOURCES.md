# Resources to Carry Over (from `melee-sim`)

This document lists the **high-value resources/tooling** from the current `melee-sim` repo that should be copied or re-cloned into `melee-sim-light`, with a suggested directory layout and what should be committed vs gitignored.

The goal is to keep `melee-sim-light` **clean and unconfused** (lite sim spec + implementation), while still having easy access to authoritative sources (decomp/asm, Slippi asm, UCF) and practical tooling (ISO extraction, Dolphin playback/probes, validation harnesses).

## Proposed Layout

```
melee-sim-light/
  docs/
    SPEC.md
    RESOURCES.md
  tools/
    extraction/                 # ISO extraction + game-file → data pipeline
    slippi/                     # Replay parsing + input/state extraction utilities
    dolphin/                    # Playback/probe scripts + configs
  data/
    common/                     # extracted common constants
    stages/                     # extracted stage collision/metadata
    characters/                 # extracted character attrs + move/anim/hit/hurt data
    overrides/                  # explicit hand overrides (small, audited)
  replays/
    suites/                     # committed: suite definitions (JSON)
    samples/                    # optional committed small clips (tiny .slp only)
  refs/                         # large subrepos + binaries (mostly gitignored)
  _iso/                         # extracted ISO files (gitignored)
  SSBM.iso                      # (gitignored)
  legacy/
    # (optional) notes pointing at the old repo locally
```

## Authoritative Code References (`refs/`)

Carry over by cloning/copying these (typically **gitignored** in the lite repo, or added as submodules if you prefer pinning):

- `refs/melee/`
  - Doldecomp/melee decomp (primary reference for constants, action logic, collision, etc.).
- `refs/melee/build/GALE01/asm/`
  - Symbolized ASM output (critical for ordering/float behavior when decomp is ambiguous).
- `refs/slippi-ssbm-asm/`
  - Slippi gameplay-affecting patches (UCF-related changes often live here).
- `refs/ucf/`
  - UCF source/reference (for input semantics, cardinals, etc.).
- `refs/slippi-wiki/`
  - Replay format reference (secondary, but useful for tooling).
- `refs/Ishiiruka/`
  - Custom Dolphin fork used for playback/engine-dump/probing workflows.
- Optional helpers:
  - `refs/melee-anim-rs/` (useful if it accelerates extracting anim/hit/hurt data)
  - `refs/melee-disc/` (if needed for disc/DTK workflows)
  - `refs/datasheet/` (if it contains extraction notes or mapping tables you still use)

Recommendation:
- Keep `refs/` out of the main “agent surface area” by default (gitignore it and document setup).
- If you want reproducibility, make `refs/*` **git submodules** pinned to known commits; still don’t vendor their contents directly.

## Game Assets (local-only)

These should exist locally but stay out of git:

- `SSBM.iso`
  - The raw ISO used for extraction and Dolphin playback.
- `_iso/`
  - Extracted ISO filesystem (big; used by extraction scripts and for reference).

If you want multiple ISOs/regions, keep them under something like:
`local/isos/GALE01.iso` and point tools via config/env vars.

## Dolphin Tooling / Binaries

Carry over (mostly **gitignored**, but scripts/configs should be committed):

- Playback/engine-dump Dolphin:
  - `refs/Ishiiruka/build/Binaries/dolphin-emu-nogui` (or build instructions + pinned commit).
- ExiAI AppImage and extracted squashfs (if still useful):
  - `Slippi_Online-x86_64-ExiAI.AppImage`
  - `squashfs-root/` (large; usually gitignored)
  - `dolphin-emu-ExiAI` symlink (or replace with a config path)
- Local Dolphin user dirs:
  - `.local_dolphin/`, `.local_ExiAI/` (gitignored; keep as disposable caches)

Also carry over probe scripts/config patterns:
- `scripts/dolphin_mem_probe_ecb.py`
- `scripts/dolphin_probe_ecb.py`
- `scripts/playback_step_probe.py`
- `docs/DOLPHIN_TOOLING.md` (copy relevant parts into lite docs; don’t bring the whole old doc set verbatim unless it still matches the new repo.)

## Extraction / Data Pipeline

Carry over and adapt into `tools/extraction/`:

- `scripts/extraction/` (ISO extract + data extraction pipeline)
- Any stage/character/common-data extractors currently used to produce:
  - `data/common/*`
  - `data/stages/*`
  - `data/characters/*`
  - `data/moves/*` (if you keep this split)

Guidelines for the lite repo:
- Treat extracted outputs as “build artifacts” but **commit the small, stable outputs** you want reproducibility on.
- Keep a separate `data/overrides/` for any manual fixes with explicit provenance.

## Replay Suites / Test Inputs

Carry over:

- `replays/suites/` (commit these JSON suite definitions)
- A tiny curated set of short `.slp` clips if you want CI-like smoke tests (otherwise keep `.slp` gitignored and fetch on demand)

The lite repo’s evaluation should primarily consume:
- replay inputs (per-frame controller)
- replay-derived “reference state/obs” for reseeded one-step metrics

## Validation/Evaluation Harnesses

Carry over scripts that remain conceptually useful, even if they’re rewritten:

- Engine-dump workflows (optional but high value if reseeding requires hidden/internal fields):
  - `scripts/engine_dump_suite.py`
  - `scripts/engine_dump_capture.py`
  - `scripts/dolphin_engine_dump.py`
  - `docs/engine_dump_schema.md`
  - `docs/ENGINE_DUMP_WORKFLOW.md`

Even if `melee-sim-light` does not target full dump parity, engine dumps can be used to:
- validate “hard-to-derive” internal timers/flags during reseeding,
- debug why the one-step transition differs.

## Legacy Reference Implementation (optional)

If you want the existing Rust sim code available purely as a reference (without contaminating the lite project’s core), prefer a simple local-path reference rather than vendoring/submodules.

Suggested convention:
- Document a local pointer to your existing checkout, e.g.:
  - `../melee-sim/` (the “exact parity” repo) and/or the specific paths you want to consult.

Strong suggestion:
- Keep legacy references out of the default include paths/build.
- Make it explicit in docs that this is **reference-only** and not the implementation target.

## Suggested `.gitignore` for `melee-sim-light`

At minimum, ignore:
- `SSBM.iso`
- `_iso/`
- `refs/` (or ignore specific heavy subdirs and commit submodule pointers)
- Dolphin binaries/build outputs and user dirs
- large replay corpora (`replays/*.slp`), keeping only `replays/suites/` committed

## Next Step (after this doc)

Pick a minimal “bring-up set” to copy first:
1) `tools/extraction/` + `SSBM.iso` + `_iso/`
2) `refs/melee/` + `refs/slippi-ssbm-asm/` + `refs/ucf/`
3) a small Fox/Falco FD replay suite and a few `.slp` clips
4) Dolphin playback/probe tooling (only if/when needed)
