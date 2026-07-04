# Reference Material

This directory contains reference material needed for SSBM simulator development.
The contents are gitignored but should be present locally.

## Directory Structure

| Directory | Source | Description |
|-----------|--------|-------------|
| `melee/` | https://github.com/doldecomp/melee | Melee decompilation project - primary source of truth for game behavior |
| `melee-anim-rs/` | local checkout | Animation reference/visualizer (HSDLib-derived) for inspecting bone/joint behavior (secondary reference) |
| `slippi-ssbm-asm/` | https://github.com/project-slippi/slippi-ssbm-asm | Slippi ASM modifications - includes UCF and other gameplay-affecting changes |
| `slippi-wiki/` | https://github.com/project-slippi/slippi-wiki | Slippi replay format specification |
| `Ishiiruka/` | git submodule | Pinned Slippi Dolphin fork with local engine-dump probe support |
| `ucf/` | https://github.com/AltimorTASDK/UCF | UCF source code (pad buffer / 1.0 cardinals logic, etc.) |
| `melee-disc/` | Extracted from SSBM.iso | Game filesystem (files/, sys/) - raw .dat file access |
| `datasheet/` | slippi-wiki ID spreadsheet | Local export of character/stage/item IDs, action states, struct offsets, character attributes |

## Setup

Clone each repository into this directory:

```bash
cd refs/
git clone https://github.com/doldecomp/melee.git
git clone https://github.com/project-slippi/slippi-ssbm-asm.git
git clone https://github.com/project-slippi/slippi-wiki.git
git clone https://github.com/UnclePunch/UCF.git ucf
```

Initialize the pinned Ishiiruka probe checkout through git submodules:

```bash
git submodule update --init refs/Ishiiruka
```

Or if you have existing non-submodule checkouts for the other references,
symlink or copy them here.

## Usage

When investigating desyncs or implementing new game mechanics:

1. **Decomp (`melee/`)**: Primary reference for game logic. Look at `src/melee/ft/` for fighter code, `src/melee/it/` for items/projectiles.

2. **Animation reference (`melee-anim-rs/`)** (secondary): Useful for inspecting HSD-style joint/track interpolation and visualizing hurtboxes.
   - Note: our **primary** extraction path for simulator runtime data is `tools/extraction/extract_fighter_anims.py`, which is decomp-first and includes MSL trig/spline semantics.
   - `melee-anim-rs` is still useful as a sanity-check / renderer, but it loads exported intermediates (not directly from `.dat`) and may omit some track types (e.g. scale).

2. **Slippi ASM (`slippi-ssbm-asm/`)**: Check for gameplay-affecting modifications like UCF dashback fix that might affect replay behavior.

3. **Slippi Wiki (`slippi-wiki/`)**: Reference for replay file format (`SPEC.md`) when debugging replay parsing issues.

4. **Ishiiruka (`Ishiiruka/`)**: Useful for engine-dump probes and for understanding what values Slippi logs.

5. **UCF (`ucf/`)**: Useful when the Slippi/UCF ASM patch is hard to read (blobs/Gecko), and you want a clean reference implementation (e.g. `apply_cardinals`).

## Quick Links (Common Investigations)

- Strict replay validation / exact prefix:
  - `tools/eval/run_one_step_suite_eval.py`
  - `tools/eval/run_rollout_suite_eval.py`
- ISO-derived stage collision extraction (authoritative stage geometry):
  - `tools/extraction/extract_stage_collision.py`
  - `_iso/` (output from `tools/extraction/iso_extract.py`)
- Playback engine-dump forensic rows (for “what did the game do on this exact frame window?”):
  - `tools/dolphin/dolphin_engine_dump.py` (single dump capture)
  - `tools/dolphin/extract_engine_dump_rows.py` (dump window extraction)
  - `tools/dolphin/slp_scenario_probe.py` (patched replay windows)
- Slippi post-frame field meanings (what the replay actually records):
  - `slippi-ssbm-asm/Recording/SendGamePostFrame.asm`

## Investigation Guide (Slippi/UCF/Niche Code Paths)

When behavior differs between replays, decomp, and the simulator, you often need to answer two separate questions:

1) **What does the game do?** (decomp + raw game ASM)
2) **Was that behavior modified for this replay?** (Slippi ASM / Gecko codes as deployed in Slippi Ishiiruka)

### Recommended workflow

1. **Start from decomp**: find the closest `refs/melee/src/melee/...` function(s) that “own” the behavior.
2. **Check raw game ASM** if needed: `refs/melee/build/GALE01/asm/...` to confirm exact instruction ordering when decomp is incomplete/uncertain.
3. **Check Slippi ASM** for modifications:
   - Search `refs/slippi-ssbm-asm/` for the relevant system or address.
   - Pay attention to files tagged `[affects-gameplay]`.
4. **Determine what was actually enabled in netplay for a given Slippi Ishiiruka version**:
   - Netplay Gecko codes are embedded in Ishiiruka’s `Data/Sys/GameSettings/GALE01r2.ini` (and region variants).
   - Compare tags/releases in `refs/Ishiiruka` to see when a code first appeared and how it changed.
5. **Only then decide replay gating/config**:
   - Prefer replay-recorded config (GameStart fields for UCF enable flags).
   - Be cautious using replay `slippi_version` as a proxy for netplay code deployment; it can lag/lead actual netplay INI contents across releases.

### Example: UCF 0.84 “Pad Buffer + 1.0 Cardinals”

Goal: determine when the “1.0 cardinals” behavior is present in netplay (and therefore should be modeled to match replays).

1) Identify the code in Slippi ASM:

```bash
rg -n \"Pad Buffer \\+ 1\\.0 Cardinals|8006B460\" refs/slippi-ssbm-asm -S
```

2) Confirm the reference implementation in UCF source:

- `refs/ucf/src/pad_buffer/pad_buffer.cpp` contains `apply_cardinals(...)` (thresholds + exact `±1.0f` bit writing).

3) Confirm which Ishiiruka releases include the code:

```bash
# Find tags (fetch additional tags as needed)
git -C refs/Ishiiruka ls-remote --tags origin | rg \"refs/tags/v3\\.\"

# Inspect what netplay actually ships for a release tag
git -C refs/Ishiiruka show v3.4.0:Data/Sys/GameSettings/GALE01r2.ini | rg -n \"UCF 0\\.84|1\\.0 Cardinals\" -n
git -C refs/Ishiiruka show v3.3.1:Data/Sys/GameSettings/GALE01r2.ini | rg -n \"UCF 0\\.84|1\\.0 Cardinals|UCF 0\\.8\" -n
```

Key takeaway from this investigation class:
- The authoritative “was it active in netplay?” answer comes from the Ishiiruka `GALE01r2.ini` for that release, not from any single repository alone.

## Engine-Dump Probes

For some desyncs, decomp/asm is not enough to quickly answer “what did the game do on this exact frame?”
Use the pinned Ishiiruka submodule and the active playback dump tools under `tools/dolphin/`.

```bash
git submodule update --init refs/Ishiiruka
cmake --build refs/Ishiiruka/build_probe
uv run python -m tools.dolphin.dolphin_engine_dump --help
uv run python -m tools.dolphin.extract_engine_dump_rows --help
uv run python -m tools.dolphin.slp_scenario_probe --help
```

Probe outputs should stay under `reports/triage/`. Do not add new process-memory
probe scripts; the old live-memory path was deleted in favor of replay playback
engine dumps.

## Community Resources
