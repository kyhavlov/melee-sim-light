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
For item HitCapsule/callback probes, apply the v10 item extension patch first:

```bash
git -C refs/Ishiiruka apply ../../tools/dolphin/patches/ishiiruka_engine_dump_item_hitlist_v10.patch
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j2
```

The v10 patch records item callback/damage latches (`xC34`, `xCA8`, `xCBC`, `xCC0`), laser misc
fields, `xDA8`, and per-item HitCapsule victims_1/victims_2 entries. It is required for throw-laser
item hitlist/callback F14 probes and is stored in the main repo so local `refs/Ishiiruka` changes
are reviewable.

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

## Throw-laser intra-frame event probe

Some throw-laser F14 article rows spawn and delete an item within one engine frame, before Slippi
post-frame item serialization. The v10 post-frame item-hitlist dump cannot observe those no-article
rows after deletion. For that surface, apply the throw-laser event probe:

```bash
git -C refs/Ishiiruka apply ../../tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j2
```

Then pass `--throw-laser-event-probe <path.jsonl>` through `dolphin_engine_dump.py`, or use
`forensic_row_dump.py --throw-laser-event-probe`. The wrapper sets
`MSL_THROW_LASER_EVENT_PROBE_PATH` and forces interpreter mode. The probe records JSONL events for
Fox/Falco throw-laser item spawn, BODY hitlist propagation, damage callback, hitlag/give-damage,
and destroy paths:

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

Keep this patch reviewable under `tools/dolphin/patches/`; do not leave a local-only
`refs/Ishiiruka` interpreter diff in handoffs.

## Laser shield/reflect intra-frame event probe

The remaining `F15` item-owner rows are on the hidden same-frame shield/reflect branch through
`ftColl_80077464`, `ftColl_80077688`, `Item_80269DC8`, and `Item_80269F14`. The post-frame v10
item dump shows the surviving state, but not the exact callback order or branch selection. Apply
the shield/reflect event probe on top of the throw-laser probe:

```bash
git -C refs/Ishiiruka apply ../../tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch
git -C refs/Ishiiruka apply ../../tools/dolphin/patches/ishiiruka_laser_shield_reflect_event_probe.patch
cmake --build refs/Ishiiruka/build_probe --target dolphin-nogui -j2
```

Then pass `--laser-shield-reflect-event-probe <path.jsonl>` through `dolphin_engine_dump.py`, or
use `forensic_row_dump.py --laser-shield-reflect-event-probe`. The wrapper sets
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
- `engine_dump_io.py`: v6/v7/v10 dump parser (schema + typed readers, including fighter and item hitlist provenance).
- `extract_engine_dump_rows.py`: deterministic JSON/txt extraction for frame windows (including item/fighter hitlist provenance lanes when available).
- `forensic_row_dump.py`: dataset row => frame window => dump + extracted rows.
- `throw_laser_event_dump.py`: read/summarize throw-laser intra-frame event JSONL probes.
- `laser_shield_reflect_event_dump.py`: read/summarize laser shield/reflect event JSONL probes.
- `compare_hitlist_provenance.py`: frame-by-frame comparison of extracted hitlist provenance between two row captures.
- `patch_slp_preframe_window.py`: copied `.slp` pre-frame patcher for controlled vanilla playback probes.

## Legacy scripts

Older `libmelee` / memory-engine probes were moved to `tools/dolphin/legacy/`.
