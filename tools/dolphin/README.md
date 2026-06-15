# Dolphin Forensic Tooling

This directory supports a single playback-only workflow:

1. Capture an engine dump from playback Dolphin (`dolphin_engine_dump.py`).
2. Extract deterministic frame rows from that dump (`extract_engine_dump_rows.py`).
3. For dataset triage rows, run a single command (`forensic_row_dump.py`) that does both.
4. For controlled vanilla behavior probes, patch short copied replay pre-frame windows first (`patch_slp_preframe_window.py`), then run the same dump/extract flow.

No `libmelee` session control is used by the active tooling. Python only writes playback config,
launches Dolphin with CLI args, and parses the dump output. Active captures run Dolphin in a
separate process group with stdout/stderr redirected beside the dump; do not run the legacy memory
reader scripts for new probes.

## Dolphin revision

All probe instrumentation (engine-dump lanes and interpreter event probes) lives as ordinary
commits in the `refs/Ishiiruka` repo. There are no patch files to apply. The required revision is
pinned by the `refs/Ishiiruka` submodule gitlink; setup is:

```bash
git submodule update --init refs/Ishiiruka
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j8
```

When probe instrumentation changes, commit it in `refs/Ishiiruka` (branch
`engine-dump-v12-probes`), push that branch, then update and commit the superproject gitlink. Do
not leave the nested repo dirty and do not export patch files into this repo.

The pinned revision provides:

- **Engine dump v12** (`EngineDumpWriter`): the v6/v7 fighter record plus item hitlist/callback
  lanes (`xC34`/`xC48`/`xC4C`/`xC50`/`xCA8`/`xCBC`/`xCC0`, `xDA8`/`xDC8`/`xDCE`, laser misc,
  per-item HitCapsule victim entries) and hidden fighter lanes: `fp+0x670/0x674` (lstick tilt
  timers, x672..x677 input counters), `fp+0x2344/0x2348/0x234C` (capturewait anim-rate window, x8
  mash latch), `fp+0x68C..0x694` (TransN-tracked position), `fp+0x1A50` (GrabMash stick-sign
  latches + mash counter). `engine_dump_io.py` parses all dump versions 6..12.
- **Interpreter event probes** (env-gated; Dolphin switches to interpreter only for the requested
  probe frame window — see sections below).

## Primary command (known row triage)

```bash
uv run python -m tools.dolphin.forensic_row_dump \
  --row datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl:2219:0 \
  --dolphin refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso
```

Outputs are written under `reports/triage/<timestamp>_dolphin_forensic_row/`.
The wrapper writes an uncapped playback config (`EmulationSpeed = 0.000`, null video/audio, JIT)
and records `*.stdout.log` / `*.stderr.log` paths in `summary.json`. Interpreter-only probes keep
the fast-forward path on JIT and switch CPU mode only for the target probe frames.

Requires the pinned `refs/Ishiiruka` probe build with engine dump v12. v12 includes the actual
active controller port map, the prior v7 fighter hitlist provenance, v10 item callback/hitlist
lanes, and hidden fighter lanes (`x670/x674`, `x2344/x2348/x234C`, `TransN`, `x1A50`). Initialize
submodules and rebuild the probe binary after changing the nested repo:

```bash
git submodule update --init refs/Ishiiruka
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j2
```

Dump row convention: dump row `R` is end-of-frame `R-1` state plus frame `R` input record. Dataset
seed row `F` corresponds to dump row `F+1`.

## Probe benchmark

Use the fixed benchmark when checking whether an arbitrary probe window is agent-usable in seconds:

```bash
uv run python -m tools.dolphin.probe_benchmark \
  --dolphin refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso
```

It runs shallow, mid, deep, and IPW smoke windows through `forensic_row_dump.py` and prints
`elapsed_sec`, first/last captured frame, row count, event count, and active port ids. Outputs go
under `reports/triage/<timestamp>_dolphin_probe_benchmark/`.

## Interpreter Probe Warning

Interpreter-only probes are orders of magnitude slower than normal JIT engine dumps. Keep the
interpreter CPU window to the exact target frame or two needed for the hidden event. Do not run
large consecutive interpreter windows for surrounding context; use a wider JIT dump window and a
tiny interpreter probe window instead.

`forensic_row_dump.py` defaults the interpreter window to the row's seed/ref target frames even
when the capture window includes surrounding context. `dolphin_engine_dump.py` prints a warning
when the requested interpreter window exceeds three consecutive frames. Treat that warning as a
mistake unless the long interpreter span is deliberate and worth the runtime.

## DamageFall IASA event probe

Logs every `Fighter_Spaghetti`, `ftCo_DamageFall_IASA`, and `ftCo_Fall_Enter` entry (with
`lstick.x`, `lstick1.x`, and `x670`) as JSONL. Use `forensic_row_dump.py --damagefall-probe` or
`dolphin_engine_dump.py --damagefall-probe <path.jsonl>`. The wrapper sets
`MSL_DAMAGEFALL_PROBE_PATH` plus `MSL_DAMAGEFALL_PROBE_FRAME_START/END`; the wrapper also sets a
bounded interpreter CPU window for the target frames.
This probe is how the UCF 0.84 tumble component (`x670 == 1` wiggle with a UCF 1f x-smash) was
identified. Budget interpreter probes around the requested target frames, not the surrounding JIT
dump context.

## Fall floor publication probe

Records entry/return for `mpColl_80047E14`, `mpColl_80044628_Floor`, and
`mpColl_80044838_Floor`, including CollData position, floor index, ECB, floor skip, env flags,
return value, and the owning fighter where available. Use `forensic_row_dump.py
--fall-floor-probe` or `dolphin_engine_dump.py --fall-floor-probe <path.jsonl>`. Keep the
interpreter window to the exact witness frame; this probe is for mpColl source-boundary evidence,
not broad replay tracing.

## Collision primitive probe

Some BODY-contact investigations need primitives at the pre-`ftColl_80076ED8` phase, before the
normal engine dump has already observed the post-hit state. The pinned probe build already includes
this interpreter hook.

Then pass `--collision-probe` through `forensic_row_dump.py`, or pass
`--collision-probe <path.jsonl>` through `dolphin_engine_dump.py`. The wrapper sets
`MSL_COLLISION_PROBE_PATH` plus `MSL_COLLISION_PROBE_FRAME_START/END`; the wrapper switches to
interpreter only for the target probe frames so the probe interpreter hook can record:

- `ftColl_80076ED8`
- `lbColl_8000805C`
- `lbColl_80006E58`

## Throw-laser intra-frame event probe

Some throw-laser F14 article rows spawn and delete an item within one engine frame, before Slippi
post-frame item serialization. The v10 post-frame item-hitlist dump cannot observe those no-article
rows after deletion. The pinned probe build includes this event hook.

Then pass `--throw-laser-event-probe <path.jsonl>` through `dolphin_engine_dump.py`, or use
`forensic_row_dump.py --throw-laser-event-probe`. The wrapper sets
`MSL_THROW_LASER_EVENT_PROBE_PATH` and a bounded interpreter CPU window. The probe records JSONL
events for Fox/Falco throw-laser item spawn, BODY hitlist propagation, damage callback,
hitlag/give-damage, and destroy paths:

- `it_8029C6CC`
- `it_8029C4D4`
- `it_8026FAC4`
- `it_80272460`
- `Item_8026A294`
- `Item_8026A8EC`

Parse or summarize the JSONL with:

```bash
uv run python -m tools.dolphin.throw_laser_event_dump reports/triage/<probe>/events.jsonl
```

## Throw-release position publication probe

Capture/throw victim handoff rows sometimes need the exact `ftCo_800DDDE4` intra-frame state before
the normal post-frame engine dump. The pinned probe build includes an env-gated hook for that
release boundary.

Then pass `--throw-release-probe <path.jsonl>` through `dolphin_engine_dump.py`, or use
`forensic_row_dump.py --throw-release-probe`. The wrapper sets `MSL_THROW_RELEASE_PROBE_PATH` and
uses a bounded interpreter CPU window. The probe records JSONL for `ftCo_800DDDE4` entry, selected
TransN2 and XRotN bone returns, the sampled TransN2 vector, the post-`x1A70` vector, the internal
`mpColl_800471F8` load/clamp/air-collision/end phases, and function return. Each event includes the
thrower/victim roles, selected sample/publish fighter, `x221B_b7`, `x2226_b2`, `x1A70`, `x2174`,
CollData cur/prev/last/ECB, and root JObj translate.

## Laser shield/reflect intra-frame event probe

The remaining `F15` item-owner rows are on the hidden same-frame shield/reflect branch through
`ftColl_80077464`, `ftColl_80077688`, `Item_80269DC8`, and `Item_80269F14`. The post-frame v10
item dump shows the surviving state, but not the exact callback order or branch selection. The
pinned probe build includes this event hook.

Then pass `--laser-shield-reflect-event-probe <path.jsonl>` through `dolphin_engine_dump.py`, or
use `forensic_row_dump.py --laser-shield-reflect-event-probe`. The wrapper sets
`MSL_LASER_SHIELD_REFLECT_EVENT_PROBE_PATH` and a bounded interpreter CPU window. The probe records
JSONL events for:

- `ftColl_80077688` shield-collision entry
- `ftColl_80077464` reflect-collision entry
- `Item_80269DC8` shield branch entry/return
- `itFoxLaser_Logic94_ShieldBounced`
- `itFoxLaser_Logic94_HitShield`
- `Item_80269F14` reflect owner/xDA8 apply
- `Item_8026A8EC` destroy entry/return

Each event includes the laser item's owner/xDA8 state plus hidden shield/reflect branch fields
(`xC54`, `xC58`, `xDCC`, `xDCE`, pending reflect owner/xDA8, fighter `0x2218/0x221B`, and capsule
flags/distance) at decision time.

Parse or summarize the JSONL with:

```bash
uv run python -m tools.dolphin.laser_shield_reflect_event_dump reports/triage/<probe>/events.jsonl
```

## Controlled playback probes

Use `slp_scenario_probe.py` when a modelplay/webplay symptom needs vanilla confirmation but exact
state recreation is not available from an existing replay alone. The tool authors Slippi `0x37`
pre-frame fields in a copied replay, runs playback Dolphin, and extracts rows for named windows.
This is not full hidden Melee memory injection: fields that Slippi playback consumes, including
controller input and some pre-frame state fields, can affect the vanilla run; unexposed fighter,
item, collision, RNG, and callback state still comes from vanilla simulation.

```json
{
  "source_replay": "replays/validation/aggregate_recent/HungryImportantSnake.slpz",
  "patches": [
    {
      "note": "hold left and alter pre-frame playback state for a short branch",
      "start_frame": 376,
      "end_frame": 420,
      "player": 0,
      "input": {"joystickX": -1.0, "joystickY": 0.0},
      "pre_state": {"x": 0.0, "y": 30.0, "action": 14}
    }
  ],
  "windows": [
    {"name": "after_patch", "start_frame": 421, "end_frame": 425, "ports": [1]}
  ]
}
```

```bash
uv run python -m tools.dolphin.slp_scenario_probe \
  --scenario reports/triage/<probe>/scenario.json \
  --dolphin refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --baseline
```

The lower-level `patch_slp_preframe_window.py` remains available when you only need to write a
patched replay and run capture manually. It edits only `0x37` pre-frame payloads in a copied `.slp`;
Dolphin then plays the replay normally. By default it refuses `--out == --slp`; use `--in-place`
only when intentionally overwriting a disposable copy. Keep windows short and write all generated
specs, patched replays, dumps, and rows under `reports/triage/`.

```bash
uv run python -m tools.dolphin.patch_slp_preframe_window \
  --slp replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.slp \
  --patch-spec reports/triage/<probe>/patch_spec.json \
  --out reports/triage/<probe>/probe.slp
```

Patch specs use raw Slippi frame numbers. Parser/viewer frame displays usually add 123, and modelplay `*.msltrace.json` frame numbers are sequential trace/viewer indices rather than native `.slp` frame ids.

The rerun7 frame-2124 shield investigation used this workflow: neutral/toward shield produced vanilla `GuardSetOff` with shield HP loss, while facing-away plus down-tilted shield produced vanilla damage with shield HP unchanged. That confirmed the observed hit as a legitimate shield poke, not a shield-release or sim-only bug.

## Active scripts

- `dolphin_engine_dump.py`: playback CLI wrapper -> `.bin` engine dump.
- `engine_dump_io.py`: dump parser for versions 6..12 (schema + typed readers, including fighter/item hitlist provenance, v11/v12 hidden fighter lanes, and v12 active port ids).
- `extract_engine_dump_rows.py`: deterministic JSON/txt extraction for frame windows (including item/fighter hitlist provenance lanes when available).
- `forensic_row_dump.py`: dataset row => frame window => dump + extracted rows.
- `slp_scenario_probe.py`: scenario JSON => patched replay => baseline/scenario dump rows.
- `throw_laser_event_dump.py`: read/summarize throw-laser intra-frame event JSONL probes.
- `laser_shield_reflect_event_dump.py`: read/summarize laser shield/reflect event JSONL probes.
- `compare_hitlist_provenance.py`: frame-by-frame comparison of extracted hitlist provenance between two row captures.
- `patch_slp_preframe_window.py`: copied `.slp` pre-frame patcher for controlled vanilla playback probes.

## Legacy scripts

Older `libmelee` / memory-engine probes were moved to `tools/dolphin/legacy/`.
