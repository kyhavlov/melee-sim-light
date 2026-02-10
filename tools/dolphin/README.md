# Dolphin Forensic Tooling

This directory now supports a single playback-only workflow:

1. Capture an engine dump from playback Dolphin (`dolphin_engine_dump.py`).
2. Extract deterministic frame rows from that dump (`extract_engine_dump_rows.py`).
3. For dataset triage rows, run a single command (`forensic_row_dump.py`) that does both.

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

## Active scripts

- `dolphin_engine_dump.py`: playback CLI wrapper -> `.bin` engine dump.
- `engine_dump_io.py`: v6 dump parser (schema + typed readers).
- `extract_engine_dump_rows.py`: deterministic JSON/txt extraction for frame windows.
- `forensic_row_dump.py`: dataset row => frame window => dump + extracted rows.

## Legacy scripts

Older `libmelee` / memory-engine probes were moved to `tools/dolphin/legacy/`.
