# Dolphin Forensic Tooling

This directory now supports a single playback-only workflow:

1. Capture an engine dump from playback Dolphin (`dolphin_engine_dump.py`).
2. Extract deterministic frame rows from that dump (`extract_engine_dump_rows.py`).
3. For dataset triage rows, run a single command (`forensic_row_dump.py`) that does both.
4. For controlled vanilla behavior probes, patch short copied replay pre-frame windows first (`patch_slp_preframe_window.py`), then run the same dump/extract flow.

No `libmelee` session control is used by the active tooling. Python only writes playback config,
launches Dolphin with CLI args, and parses the dump output.

## Primary command (known row triage)

```bash
uv run python -m tools.dolphin.forensic_row_dump \
  --row datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl:2219:0 \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso
```

Outputs are written under `reports/triage/<timestamp>_dolphin_forensic_row/`.

Requires `refs/Ishiiruka@3e676fab03b19faf1a6b00cb63934bd2f6502827` for v7 hitlist provenance lanes.

## Collision primitive probe

Some BODY-contact investigations need primitives at the pre-`ftColl_80076ED8` phase, before the
normal engine dump has already observed the post-hit state. The main repo carries that Dolphin hook
as a patch artifact:

```bash
git -C refs/Ishiiruka apply ../../tools/dolphin/patches/ishiiruka_collision_probe.patch
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j2
```

Then pass `--collision-probe <path.jsonl>` through `forensic_row_dump.py` or
`dolphin_engine_dump.py`. The wrapper sets `MSL_COLLISION_PROBE_PATH` and forces interpreter mode
so the patched interpreter hook can record:

- `ftColl_80076ED8`
- `lbColl_8000805C`
- `lbColl_80006E58`

The patch is intentionally stored in the main repo instead of leaving `refs/Ishiiruka` dirty. Apply
it only for local forensics and restore the nested repo afterwards.

## Controlled playback probes

Use `patch_slp_preframe_window.py` when a modelplay symptom needs vanilla confirmation but exact state recreation is not available from Slippi post-frames alone. The script edits only 0x37 pre-frame payloads in a copied `.slp`; Dolphin then plays the replay normally. By default it refuses `--out == --slp`; use `--in-place` only when intentionally overwriting a disposable copy. Keep windows short and write all generated specs, patched replays, dumps, and rows under `reports/triage/`.

```bash
uv run python -m tools.dolphin.patch_slp_preframe_window \
  --slp replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.slp \
  --patch-spec reports/triage/<probe>/patch_spec.json \
  --out reports/triage/<probe>/probe.slp
```

Patch specs use raw Slippi frame numbers. Parser/viewer frame displays usually add 123, and modelplay `trace.json` frame numbers are sequential trace/viewer indices rather than native `.slp` frame ids.

The rerun7 frame-2124 shield investigation used this workflow: neutral/toward shield produced vanilla `GuardSetOff` with shield HP loss, while facing-away plus down-tilted shield produced vanilla damage with shield HP unchanged. That confirmed the observed hit as a legitimate shield poke, not a shield-release or sim-only bug.

## Active scripts

- `dolphin_engine_dump.py`: playback CLI wrapper -> `.bin` engine dump.
- `engine_dump_io.py`: v6/v7 dump parser (schema + typed readers, including v7 hitlist provenance).
- `extract_engine_dump_rows.py`: deterministic JSON/txt extraction for frame windows (including hitlist provenance lanes when available).
- `forensic_row_dump.py`: dataset row => frame window => dump + extracted rows.
- `compare_hitlist_provenance.py`: frame-by-frame comparison of extracted hitlist provenance between two row captures.
- `patch_slp_preframe_window.py`: copied `.slp` pre-frame patcher for controlled vanilla playback probes.

## Legacy scripts

Older `libmelee` / memory-engine probes were moved to `tools/dolphin/legacy/`.
