# Dolphin Forensic Tooling

This directory keeps playback-only Dolphin dump/extract helpers. Old validation row-cache wrappers
have been deleted; active validation now runs from replay-derived validation buffers instead.

Use the lower-level dump/extract tools directly for new probes:

```bash
uv run python -m tools.dolphin.dolphin_engine_dump --help
uv run python -m tools.dolphin.extract_engine_dump_rows --help
```

Probe outputs should stay under `reports/triage/`.
