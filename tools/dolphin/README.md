# Dolphin Forensic Tooling

This directory supports a single playback-only workflow:

1. Capture an engine dump from playback Dolphin (`dolphin_engine_dump.py`).
2. Extract deterministic frame rows from that dump (`extract_engine_dump_rows.py`).
3. For dataset triage rows, run a single command (`forensic_row_dump.py`) that does both.
4. For controlled vanilla behavior probes, patch short copied replay pre-frame windows first (`patch_slp_preframe_window.py`), then run the same dump/extract flow.

No `libmelee` session control is used by the active tooling. Python only writes playback config,
launches Dolphin with CLI args, and parses the dump output.

## Dolphin revision

All probe instrumentation (engine-dump lanes and interpreter event probes) lives as ordinary
commits in the `refs/Ishiiruka` repo. There are no patch files to apply. The required revision is
pinned in `tools/dolphin/ISHIIRUKA_REVISION`; setup is:

```bash
git -C refs/Ishiiruka checkout "$(sed -n 's/^commit=//p' tools/dolphin/ISHIIRUKA_REVISION)"
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j8
```

When probe instrumentation changes, commit it in `refs/Ishiiruka` (branch
`engine-dump-v12-probes`) and update the pin file — do not leave the nested repo dirty and do not
export patch files into this repo.

The pinned revision provides:

- **Engine dump v12** (`EngineDumpWriter`): the v6/v7 fighter record plus item hitlist/callback
  lanes (`xC34`/`xC48`/`xC4C`/`xC50`/`xCA8`/`xCBC`/`xCC0`, `xDA8`/`xDC8`/`xDCE`, laser misc,
  per-item HitCapsule victim entries) and hidden fighter lanes: `fp+0x670/0x674` (lstick tilt
  timers, x672..x677 input counters), `fp+0x2344/0x2348/0x234C` (capturewait anim-rate window, x8
  mash latch), `fp+0x68C..0x694` (TransN-tracked position), `fp+0x1A50` (GrabMash stick-sign
  latches + mash counter). `engine_dump_io.py` parses all dump versions 6..12.
- **Interpreter event probes** (env-gated, interpreter mode only — see sections below).

## Primary command (known row triage)

```bash
uv run python -m tools.dolphin.forensic_row_dump \
  --row datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl:2219:0 \
  --dolphin refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso
```

Outputs are written under `reports/triage/<timestamp>_dolphin_forensic_row/`.

## DamageFall IASA event probe

Logs every `Fighter_Spaghetti`, `ftCo_DamageFall_IASA`, and `ftCo_Fall_Enter` entry (with
`lstick.x`, `lstick1.x`, and `x670`) as JSONL. Enable by exporting `MSL_DAMAGEFALL_PROBE_PATH`
(plus optional `MSL_DAMAGEFALL_PROBE_FRAME_START`/`_END`) and forcing interpreter mode (e.g. run
`forensic_row_dump.py` with `--collision-probe`); budget ~3 fps of playback in interpreter mode.
This probe is how the UCF 0.84 tumble component (x670==1 wiggle with a UCF 1f x-smash) was
identified.

## Collision primitive probe

Some BODY-contact investigations need primitives at the pre-`ftColl_80076ED8` phase, before the
normal engine dump has already observed the post-hit state. Pass `--collision-probe <path.jsonl>`
through `forensic_row_dump.py` or `dolphin_engine_dump.py`. The wrapper sets
`MSL_COLLISION_PROBE_PATH` and forces interpreter mode so the hook can record:

- `ftColl_80076ED8`
- `lbColl_8000805C`
- `lbColl_80006E58`

## Throw-laser intra-frame event probe

Some throw-laser F14 article rows spawn and delete an item within one engine frame, before Slippi
post-frame item serialization. The post-frame item-hitlist dump cannot observe those no-article
rows after deletion. Pass `--throw-laser-event-probe <path.jsonl>` through
`dolphin_engine_dump.py`, or use `forensic_row_dump.py --throw-laser-event-probe`. The wrapper
sets `MSL_THROW_LASER_EVENT_PROBE_PATH` and forces interpreter mode. The probe records JSONL
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

## Laser shield/reflect intra-frame event probe

The remaining `F15` item-owner rows are on the hidden same-frame shield/reflect branch through
`ftColl_80077464`, `ftColl_80077688`, `Item_80269DC8`, and `Item_80269F14`. The post-frame item
dump shows the surviving state, but not the exact callback order or branch selection. Pass
`--laser-shield-reflect-event-probe <path.jsonl>` through `dolphin_engine_dump.py`, or use
`forensic_row_dump.py --laser-shield-reflect-event-probe`. The wrapper sets
`MSL_LASER_SHIELD_REFLECT_EVENT_PROBE_PATH` and forces interpreter mode. The probe records JSONL
events for:

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

Use `patch_slp_preframe_window.py` when a modelplay symptom needs vanilla confirmation but exact state recreation is not available from Slippi post-frames alone. The script edits only 0x37 pre-frame payloads in a copied `.slp`; Dolphin then plays the replay normally. By default it refuses `--out == --slp`; use `--in-place` only when intentionally overwriting a disposable copy. Keep windows short and write all generated specs, patched replays, dumps, and rows under `reports/triage/`.

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
- `engine_dump_io.py`: dump parser for versions 6..12 (schema + typed readers, including fighter and item hitlist provenance and the v11/v12 hidden fighter lanes).
- `extract_engine_dump_rows.py`: deterministic JSON/txt extraction for frame windows (including item/fighter hitlist provenance lanes when available).
- `forensic_row_dump.py`: dataset row => frame window => dump + extracted rows.
- `throw_laser_event_dump.py`: read/summarize throw-laser intra-frame event JSONL probes.
- `laser_shield_reflect_event_dump.py`: read/summarize laser shield/reflect event JSONL probes.
- `compare_hitlist_provenance.py`: frame-by-frame comparison of extracted hitlist provenance between two row captures.
- `patch_slp_preframe_window.py`: copied `.slp` pre-frame patcher for controlled vanilla playback probes.

## Legacy scripts

Older `libmelee` / memory-engine probes were moved to `tools/dolphin/legacy/`.
