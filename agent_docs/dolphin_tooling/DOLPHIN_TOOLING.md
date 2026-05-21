# Dolphin Tooling (Playback + Engine Dumps)

This repo uses a **custom Slippi Ishiiruka build** (in `refs/Ishiiruka/`) to generate **engine dumps**
from replay playback.

## Where the custom Dolphin lives

- Source tree: `refs/Ishiiruka/`
- Build output (typical):
  - `refs/Ishiiruka/build/Binaries/dolphin-emu-nogui`
  - `refs/Ishiiruka/build/Binaries/dolphin-emu`

If a future agent can’t find the binary, build output paths may differ; search for:
- `rg -n "EngineDumpWriter" refs/Ishiiruka/Source/Core/Core/Slippi -S`
- `find refs/Ishiiruka -maxdepth 3 -type f -name 'dolphin-emu*'`

## Engine dump writer code

- `refs/Ishiiruka/Source/Core/Core/Slippi/EngineDumpWriter.cpp`

This is the authoritative source for:
- the dump boundary (post-frame snapshot point),
- which fields are captured,
- and any playback-specific adjustments.

## Generate dumps (recommended scripts)

Single replay:
```bash
uv run python scripts/dolphin_engine_dump.py \
  --replay replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --user-dir /tmp/ish_playback_user_dump \
  --out-bin /tmp/engine_dump.bin
```

Suite:
```bash
uv run python scripts/engine_dump_suite.py \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --out-dir engine_dumps \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --user-dir-base /tmp/ish_playback_user_dump_suite
```

## Stepping, throttling, and “fast-forward”

Dump generation intentionally avoids aggressive fast-forwarding because it can cause frame skips
in the dump output. If you change playback speed/stepping behavior, verify frame counts and
monotonic `frame_index` in the resulting dump.

## Memory probes (caution)

There are additional Python probes under `scripts/` (e.g. ECB probes) that can attach to a running
Dolphin process and read memory.

**Read this before running any live probe:**
- These probes can wedge Dolphin (bad pointer read / stale Gecko hook) and that can wedge the probe.
  In this repo’s setup, that has *occasionally taken down the surrounding terminal session*.
- Always run probes from an **isolated terminal** (`tmux` / separate window). Do **not** run probes
  in the same terminal session where you’re running long validations/builds.
- Prefer a hard timeout and small windows; avoid “continuous” probing unless you’re actively watching it.

These probes are powerful, but they are also *easy to misuse*:
- A stale/incorrect Gecko hook or a bad pointer read can wedge Dolphin, which in turn can wedge the
  probe process (and has historically taken down the surrounding terminal session).
- Always run probes from an **isolated shell** (`tmux` / separate terminal), and prefer `timeout`:
  - `timeout 60s uv run python scripts/playback_step_probe.py ...`
- Keep probe windows small (`--start-frame/--end-frame`), and keep `--max-seconds` small.
- `scripts/playback_step_probe.py` also supports `--max-seconds` (recommended). By default it will
  refuse to run without a hard timeout unless you pass `--i-accept-risk` or set
  `SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING=1`.
- If Dolphin becomes unresponsive, kill Dolphin first; don’t keep retrying the probe in a tight loop.

Notes:
- `scripts/playback_step_probe.py` is the preferred live probe.
- `scripts/dolphin_mem_probe_ecb.py` exists for deeper Gecko-hook debugging, but it is riskier.

### `dolphin_memory_engine` hook note (Linux)

Some probes (notably `scripts/playback_step_probe.py`) use `dolphin_memory_engine`, which on
Linux typically searches for a process named `dolphin-emu`. If you launch the nogui binary as
`dolphin-emu-nogui`, the hook may fail.

Fix: invoke the binary via a `dolphin-emu`-named path, e.g.:

```bash
ln -sf "$PWD/refs/Ishiiruka/build/Binaries/dolphin-emu-nogui" /tmp/dolphin-emu
```

Then pass `--dolphin /tmp/dolphin-emu` to the probe.
