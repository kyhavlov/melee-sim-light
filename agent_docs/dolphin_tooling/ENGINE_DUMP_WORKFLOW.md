# Engine Dump Validation Workflow

Archived note: this is historical context from the older Rust-oriented engine-dump workflow. Prefer
current commands in `agent_docs/DEVELOPMENT_WORKFLOWS.md` unless you are auditing old provenance.

This project now validates **simulator parity against live engine dumps**, not against Slippi replay
post‑frame data. The canonical loop is:

1) Generate engine dumps using playback Dolphin + EngineDumpWriter.
2) Validate with the **Rust** validator (`engine_dump_validate`).
3) Fix mismatches in the sim (decomp‑first), then re‑run.

## Fresh context: find the next blocker (canonical)

When loading fresh context, determine “what to work on next” by running the suite validator and
looking at the **suite‑wide earliest failing frame** (the minimum first‑fail frame across all
replays in the suite).

Canonical command (also mirrored in `PROGRESS.md`):
```bash
cargo run -p ssbm_sim --bin engine_dump_suite -- \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --dump-dir engine_dumps \
  --ucf 1 \
  --max-frames 600
```

What “progress” means:
- Primary: the **suite‑wide earliest failing frame** increases (or ties with fewer mismatches).
- Secondary: per‑replay first‑fail frame + the mismatch list at that frame (this guides the investigation).
- Ignore legacy Slippi‑replay `exact_steps` numbers; engine dumps are the gate.

## Why engine dumps?

Engine dumps record the **actual in‑memory post‑frame state** with raw float bits. This avoids
Slippi replay quirks (rounding/truncation, derived fields, and recording offsets) and makes
validation a direct parity check against the game engine.

## Build the dumping Dolphin (Slippi Ishiiruka)

The dumping logic lives in:
- `refs/Ishiiruka/Source/Core/Core/Slippi/EngineDumpWriter.cpp`

**Important:** Use the **custom playback build** in `refs/Ishiiruka` (branch `engine-dump-tooling`).
The normal ExiAI AppImage does **not** support the playback flags/config (`--slippi-input` /
`engineDumpPath`) needed for engine dumps.

Build Dolphin (nogui):
```bash
cmake --build refs/Ishiiruka/build
```

Use the resulting binary:
- `refs/Ishiiruka/build/Binaries/dolphin-emu-nogui`
- (GUI build also exists: `refs/Ishiiruka/build/Binaries/dolphin-emu`)

Playback config fields (written by `scripts/dolphin_engine_dump.py`):
- `engineDumpPath` — enables dump output
- `blockOnFrame` / `isRealTimeMode` — used for stepping/throttling

## Generate a dump (single replay)

```bash
uv run python scripts/dolphin_engine_dump.py \
  --replay replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --user-dir /tmp/ish_playback_user_dump \
  --out-bin /tmp/engine_dump.bin
```

Notes:
- The script writes a playback config file with `engineDumpPath` and boots Dolphin.
- Fast‑forward is disabled during dumping to prevent frame skips.

## Generate dumps for the suite

```bash
uv run python scripts/engine_dump_suite.py \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --out-dir engine_dumps \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --user-dir-base /tmp/ish_playback_user_dump_suite
```

Dumps are written to `engine_dumps/` (gitignored in most workflows).

## Validate (Rust)

Single dump:
```bash
cargo run -p ssbm_sim --bin engine_dump_validate -- \
  --dump engine_dumps/AttachedGoodNaturedGuanaco.bin \
  --stage data/stages/final_destination.json \
  --p1-moves data/moves/falco.json --p2-moves data/moves/fox.json \
  --p1-attrs data/characters/falco.json --p2-attrs data/characters/fox.json \
  --ucf 1
```

Suite validation is also run by `scripts/engine_dump_suite.py` after dumps are created.

## Debugging loop

1. Run the Rust validator (or the suite script) and note the first failing frame.
2. Use `agent_docs/dolphin_tooling/engine_dump_schema.md` (Field Reference Index) to find where each field
   comes from in decomp/asm/Dolphin.
3. Implement the missing behavior in the sim (decomp‑first), then re‑run.

See also:
- `agent_docs/dolphin_tooling/LOCAL_SOURCES.md` (where the decomp + asm + Slippi mods live locally)
- `agent_docs/dolphin_tooling/DOLPHIN_TOOLING.md` (custom playback build + dumping details)
- `agent_docs/DEVELOPMENT_WORKFLOWS.md` (current validator/suite runner commands)

## Implementation policy (current phase)

- When a mismatch points to an unimplemented system (e.g., RNG, ECB, item state),
  implement the **actual Melee logic** from decomp/asm rather than stubbing or
  hardcoding to pass validation. This phase is about slow, exact 1:1 parity.

## Replay alignment note

Engine dumps are captured at the **post‑frame** boundary. If you compare to Slippi replay data
for sanity checks, use `canonicalize_slippi_sample_last` and align **dump frame `f`** with
**replay frame `f‑1`**.
