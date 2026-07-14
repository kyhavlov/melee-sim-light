# Movescript Events Source Completion

Scope: fighter action-script extraction and runtime event substrate for supported RL 1.0 gameplay:
decoded `MSLFTSC1` timelines, persistent command-interpreter ordering on the animation timebase,
script-owned transient pulses, and handoffs into hitbox, throw, input, combat, item/projectile,
and state-flag systems. Detailed HitCapsule geometry and hurt capsule geometry live in
[hitbox_hurtbox_geometry.md](hitbox_hurtbox_geometry.md); detailed projectile lifecycle lives in
[projectiles_reflect.md](projectiles_reflect.md).

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| MSLFTSC1 extraction and binary schema | CLOSED | `tools/extraction/extract_fighter_moves.py` decodes supported Fox/Falco action-script opcodes; `extract_fighter_script_timeline.py` encodes the runtime event subset; `src/script_events.c` loads typed payloads at init. |
| Runtime command owner | CLOSED | `src/fighter_script.c` owns one allocation-free command cursor, timer, frame count, and persistent command products per fighter. V2-05 deleted the parallel absolute-frame cache and its debug/query surface. |
| Event ordering and timing | CLOSED | `MSLFTSC1` v7 preserves every `Command_01` synchronous and `Command_02` asynchronous wait, including timer-only groups. Runtime executes their f32 residual semantics through the same persistent cursor used by `ftAction_80073240`/`ftAction_80073354`; absolute animation-frame windows are not equivalent at non-unit rates. |
| Hitbox create/clear/mutation lifecycle | CLOSED | `MSLFTSC1` carries create/clear, damage, and interaction opcodes; `MSLHITB1` preserves damage and `x42_b5`/`x42_b7` interaction changes as distinct active-slot mutations without create-edge side effects. |
| Script variables and state side effects | CLOSED | `cmd_vars`, `allow_interrupt`, hit status, hurt state, x221C flags, jab combo/rapid, airborne-state events, smash charge, and throw products are live interpreter outputs consumed directly by their callbacks. |
| Throw script flags, hitboxes, and projectile pulses | CLOSED | Throw release/flip flags, throw hitbox payloads, command variables, and projectile pulses are sourced from `MSLFTSC1` and consumed from the live cursor at callback time. |
| Seed/reseed interaction | CLOSED | Teacher-forced reseed performs a bounded seek of the same interpreter to initialize real persistent products. Callback-consumed pulses are normalized at the post-frame boundary without changing free-running semantics. |
| Article/projectile detailed script lifecycle | RETAINED SOURCE-POLICY | This system closes only the movescript pulse boundary into item/projectile systems. Detailed item/article update, slot, reflect, and consume behavior belongs to `items_core.md` / `projectiles_reflect.md`. |
| Deferred non-runtime fighter opcodes | RETAINED SOURCE-POLICY | `ftAction_8007169C` set hitbox scale remains deferred until supported scripts emit it. `remove_hitbox` is folded in `MSLHITB1` where it affects active hitbox publication. |

## Source Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Locks |
|---|---|---|---|---|---|
| `ftAction_80073240` / `ftAction_80073354` command execution | `tools/extraction/extract_fighter_moves.py`; `src/script_events.{h,c}`; `src/fighter_script.c` | Full control stream, synchronous/asynchronous timer kind and target, f32 residual, command frame count, kind-specific payload | `MSLFTSC1` v7 plus a persistent per-fighter cursor/timer/product state | CLOSED | Preserve every timer group rather than replacing timing with absolute-frame crossing. Locks include Fox ThrowHi async timers, Sheik Vanish timer-only groups, and the non-unit-rate runtime boundary in `test_movescript_events_source_completion.py`. |
| `ftAction_8007121C` create HitCapsule | `tools/extraction/extract_fighter_moves.py`; `tools/extraction/extract_fighter_hitboxes.py`; `src/hitboxes.c` | Hitbox id, bone, group, damage, size, offsets, angle/KB, flags | `MSLFTSC1` create payload; `MSLHITB1` active hitbox records | CLOSED | Create payloads remain data-backed; geometry details are covered by hitbox/hurtbox system. |
| `ftAction_8007162C` set HitCapsule damage | `extract_fighter_script_timeline.py`; `known_data_artifacts.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Hitbox index and damage value for already-live HitCapsule | `MSLFTSC1` typed payload plus `MSLHITB1` kind-2 active-slot mutation record | CLOSED | Removed the unsupported-event bridge for `set_hitbox_damage`; mutation records update only active damage and never count as create/enable. Lock: `test_mslftsc1_encodes_set_hitbox_damage_payload`; `test_mslhitb1_folds_set_hitbox_damage_into_active_slot_record`; `test_runtime_set_hitbox_damage_does_not_create_edge`. |
| `ftAction_80071708` set HitCapsule interaction | `extract_fighter_script_timeline.py`; `known_data_artifacts.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c`; combat/item collision owners | Active HitCapsule id, `x42_b5`/`x42_b7` selector, value | `MSLFTSC1` typed payload plus `MSLHITB1` kind-3 active-slot mutation record | CLOSED | Create initializes both gates; mutation updates only the selected active gate. Fighter BODY/HitCapsule passes, including item HitCapsule-vs-fighter HitCapsule, consume `x42_b5`; fighter HitCapsule-vs-item hurtbox consumes `x42_b7`. Locks: interaction extraction/runtime/contact-filter tests. |
| `ftAction_800717D8` / clear HitCapsules | `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Clear-all command frame | `MSLHITB1` clear records and runtime clear semantics | CLOSED | Clear-all is source-modeled; per-slot remove is retained only where it affects hitbox cache publication. |
| `ftAction_80071820` set `cmd_vars` | `src/script_events.c`; `src/fighter_script.c`; callback consumers | Command variable index and value at source command time | Four persistent live command variables per fighter | CLOSED | AttackAir, EscapeAir, Dash, RunBrake, TurnRun, specials, and throws consume the live values. |
| `ftAction_800718A4` throw flags b3/b4 | `src/fighter_script.c`; `src/throw_flow.c` | Hit index and one-shot command edge | Live b3/b4 flags consumed by the throw Anim callback | CLOSED | Release/flip follows command execution at non-unit rates; functional throw boundary tests lock it. |
| `ftAction_80071950` allow interrupt | `src/fighter_script.c`; action IASA paths | First allow-interrupt command | Live x2218 allow-interrupt bit | CLOSED | AttackAir, grounded attacks, Escape, and related IASA paths consume the source bit. |
| `ftAction_80071974` projectile pulse flag | `src/fighter_script.c`; `src/items_spacies.c` | One-shot throw/projectile handoff | Live throw flag consumed once by the fighter Anim item producer | CLOSED | Detailed projectile lifecycle remains in the projectile system. |
| `ftAction_80071998` airborne state event | `src/fighter_script.c`; locomotion/knockdown consumers | Event state at source command time | Immediate kinetic transition | CLOSED | No frame-indexed secondary owner remains. |
| `ftAction_80071A14`, `80071A50`, `80071A90` hit/hurt state events | `src/fighter_script.c`; combat/hurtcap consumers | Hit status, all-hurt state, per-bone hurt state | Persistent live hit status and per-hurtcap state | CLOSED | Per-bone mapping uses extracted hurtcap tables. |
| `ftAction_80071AE8`, `80071B28` jab combo / rapid flags | `src/fighter_script.c`; jab IASA/loop owners | Flag value at command time | Live x2218 combo/rapid bits | CLOSED | Source-shaped script flags replace local jab thresholds. |
| `ftAction_80072C6C` x221C state flags | `src/fighter_script.c`; state flag publication | Flag payload at command time | Persistent live low-bit state | CLOSED | State publication consumes the script product directly. |
| `ftAction_80073008` smash charge | `src/fighter_script.c`; smash-charge runtime | Hold frames, color anim, damage multiplier | Live smash-charge state and parameters | CLOSED | Source event initializes the charge owner; reseed seeks the same cursor. |
| `ftAction_80072E04` pseudo-random SFX | `src/fighter_script.c`; combat RNG ownership | Random range at command time | Immediate bounded RNG-site consumption | CLOSED | Cosmetic sound playback is out of scope; the gameplay RNG stream phase is source-owned. |
| `ftAction_80071E04` throw hitbox payload | `src/script_events.c`; `src/fighter_script.c`; `src/combat.c` | Throw hitbox damage/angle/KB/element/SFX | Two persistent live xDF4 throw HitCapsules per fighter | CLOSED | Shared combat hit resolution consumes the live payload. |
| Unsupported item/article action-script details | Items/projectiles docs | Full article/item lifecycle state | Only command pulses crossing into fighter gameplay are modeled here | RETAINED SOURCE-POLICY | Ownership intentionally lives in `items_core.md` and `projectiles_reflect.md`. |

## Validation Evidence

Closure should be kept current with:

- `uv run python -m pytest tests/test_movescript_events_source_completion.py tests/test_grab_throw_capture_source_completion.py tests/test_validation_dtypes_schema_guard.py -q`
- `make build_data`
- `make build BUILD_FORCE=1`
- `make validate-all`
- `make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`

Historical references:

- `refs/melee/src/melee/ft/ftaction.c`
- `tools/extraction/extract_fighter_moves.py`
- `tools/extraction/extract_fighter_script_timeline.py`
- `tools/extraction/extract_fighter_hitboxes.py`
