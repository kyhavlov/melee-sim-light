# Movescript Events Source Completion

Scope: fighter action-script extraction and runtime event substrate for supported RL 1.0 gameplay:
decoded `MSLFTSC1` timelines, command ordering on the animation timebase, script-owned transient
pulses, hot-path cache products, and handoffs into hitbox, throw, input, combat, item/projectile,
and state-flag systems. Detailed HitCapsule geometry and hurt capsule geometry live in
[hitbox_hurtbox_geometry.md](hitbox_hurtbox_geometry.md); detailed projectile lifecycle lives in
[projectiles_reflect.md](projectiles_reflect.md).

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| MSLFTSC1 extraction and binary schema | CLOSED | `tools/extraction/extract_fighter_moves.py` decodes supported Fox/Falco action-script opcodes; `extract_fighter_script_timeline.py` encodes the runtime event subset; `src/script_events.c` loads typed payloads at init. |
| Runtime cache products | CLOSED | `src/move_tables.c` builds allocation-free windows/pulses/masks from `MSLFTSC1`; runtime systems consume those helpers instead of reparsing scripts or adding frame constants. |
| Event ordering and timing | CLOSED | Events are keyed by source animation/script frame (`fp->cur_anim_frame`) and consumed through `ftAction_80073240`/`ftAction_80073354`-shaped crossing/window helpers. |
| Hitbox create/clear/mutation lifecycle | CLOSED | `MSLFTSC1` carries create/clear, damage, and interaction opcodes; `MSLHITB1` preserves damage and `x42_b5`/`x42_b7` interaction changes as distinct active-slot mutations without create-edge side effects. |
| Script variables and state side effects | CLOSED | `cmd_vars`, `allow_interrupt`, hit status, hurt state, x221C flags, jab combo/rapid, airborne-state events, smash charge, and pseudo-random SFX pulses are extracted and cached. |
| Throw script flags, hitboxes, and projectile pulses | CLOSED | Throw release/flip flags, throw hitbox payloads, command variables, and projectile pulses are sourced from `MSLFTSC1` and cached in `move_tables`. |
| Seed/reseed interaction | CLOSED | Teacher-forced reseed paths consume the same cache products as free-run runtime; hidden owner lanes initialize source state rather than changing movescript semantics. |
| Article/projectile detailed script lifecycle | RETAINED SOURCE-POLICY | This system closes only the movescript pulse boundary into item/projectile systems. Detailed item/article update, slot, reflect, and consume behavior belongs to `items_core.md` / `projectiles_reflect.md`. |
| Deferred non-runtime fighter opcodes | RETAINED SOURCE-POLICY | `ftAction_8007169C` set hitbox scale remains deferred until supported scripts emit it. `remove_hitbox` is folded in `MSLHITB1` where it affects active hitbox publication. |

## Source Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Locks |
|---|---|---|---|---|---|
| `ftAction_80073240` / `ftAction_80073354` command execution on animation frame | `tools/extraction/extract_fighter_moves.py`; `src/script_events.{h,c}`; `src/move_tables.c` | Subaction command stream, command frame, kind-specific payload, crossed/contains timing | `MSLFTSC1` tables loaded at init; `MslScriptFrameWindow` and pulse caches | CLOSED | Keep script timing sourced from animation frame, not action-frame literals. Locks: `test_move_tables_throw_data.py`, `test_movescript_events_source_completion.py`. |
| `ftAction_8007121C` create HitCapsule | `tools/extraction/extract_fighter_moves.py`; `tools/extraction/extract_fighter_hitboxes.py`; `src/hitboxes.c` | Hitbox id, bone, group, damage, size, offsets, angle/KB, flags | `MSLFTSC1` create payload; `MSLHITB1` active hitbox records | CLOSED | Create payloads remain data-backed; geometry details are covered by hitbox/hurtbox system. |
| `ftAction_8007162C` set HitCapsule damage | `extract_fighter_script_timeline.py`; `known_data_artifacts.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Hitbox index and damage value for already-live HitCapsule | `MSLFTSC1` typed payload plus `MSLHITB1` kind-2 active-slot mutation record | CLOSED | Removed the unsupported-event bridge for `set_hitbox_damage`; mutation records update only active damage and never count as create/enable. Lock: `test_mslftsc1_encodes_set_hitbox_damage_payload`; `test_mslhitb1_folds_set_hitbox_damage_into_active_slot_record`; `test_runtime_set_hitbox_damage_does_not_create_edge`. |
| `ftAction_80071708` set HitCapsule interaction | `extract_fighter_script_timeline.py`; `known_data_artifacts.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c`; combat/item collision owners | Active HitCapsule id, `x42_b5`/`x42_b7` selector, value | `MSLFTSC1` typed payload plus `MSLHITB1` kind-3 active-slot mutation record | CLOSED | Create initializes both gates; mutation updates only the selected active gate. Fighter BODY/HitCapsule passes, including item HitCapsule-vs-fighter HitCapsule, consume `x42_b5`; fighter HitCapsule-vs-item hurtbox consumes `x42_b7`. Locks: interaction extraction/runtime/contact-filter tests. |
| `ftAction_800717D8` / clear HitCapsules | `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Clear-all command frame | `MSLHITB1` clear records and runtime clear semantics | CLOSED | Clear-all is source-modeled; per-slot remove is retained only where it affects hitbox cache publication. |
| `ftAction_80071820` set `cmd_vars` | `src/script_events.c`; `src/move_tables.c` | Command variable index, value, windows and pulses | Value windows and pulse arrays for AttackAir, EscapeAir, Dash, RunBrake, TurnRun, Special, Throw | CLOSED | Runtime consumers use generated cache helpers. Locks: move-table and throw projectile pulse tests. |
| `ftAction_800718A4` throw flags b3/b4 | `src/move_tables.c`; throw/grab runtime | Hit index and script frame | Throw release and facing-flip windows/pulses | CLOSED | Source boundary closed in grab/throw/capture system; movescript substrate provides data. |
| `ftAction_80071950` allow interrupt | `src/move_tables.c`; action IASA paths | First allow-interrupt command frame | Open-ended allow-interrupt window | CLOSED | AttackAir, grounded attacks, Escape, and related IASA paths consume table helpers. |
| `ftAction_80071974` projectile pulse flag | `src/move_tables.c`; `src/items.c`; `src/anim_timebase.c` | Pulse frame(s) for throw/projectile handoff | Fixed-capacity pulse cache with ordinal/first/last helpers | CLOSED | Detailed projectile lifecycle remains in projectile system; pulse ownership here is closed. |
| `ftAction_80071998` airborne state event | `src/move_tables.c`; locomotion/knockdown consumers | Event frame and state enum | Frame-indexed cache capped to supported action-script horizon | CLOSED | Source-owned per-frame state event table. |
| `ftAction_80071A14`, `80071A50`, `80071A90` hit/hurt state events | `src/move_tables.c`; combat/hurtcap consumers | Hit status, all-hurt state, per-bone hurt state | Frame-indexed hit status and hurtcap BODY mask cache | CLOSED | Per-bone mapping uses extracted hurtcap tables. |
| `ftAction_80071AE8`, `80071B28` jab combo / rapid flags | `src/move_tables.c`; jab IASA/loop owners | Flag value and frame | Open windows in move cache | CLOSED | Source-shaped script flags replace local jab thresholds. |
| `ftAction_80072C6C` x221C state flags | `src/move_tables.c`; state flag publication/runtime locks | Flag payload by frame | Frame-indexed low-bit cache | CLOSED | Used by state-flag owners and seed locks. |
| `ftAction_80073008` smash charge | `src/move_tables.c`; `src/api.c` reseed/smash charge runtime | Hold frames, color anim, damage multiplier, crossing frame | Smash-charge frame, hold frames, damage multiplier cache | CLOSED | Source event drives runtime charge initialization and seed sanitization. |
| `ftAction_80072E04` pseudo-random SFX | `src/move_tables.c`; combat RNG ownership | Random range and crossed frame(s) | Fixed-capacity SFX pulse range cache | CLOSED | Combat RNG owner consumes pulse ranges; cosmetic sound playback is out of scope. |
| `ftAction_80071E04` throw hitbox payload | `src/script_events.c`; `src/move_tables.c`; `src/combat.c` | Throw hitbox damage/angle/KB/element/SFX | `MslScriptThrowHitboxPayload` cached by hit index | CLOSED | Shared combat hit-resolution consumes source payloads for throw damage. |
| Unsupported item/article action-script details | Items/projectiles docs | Full article/item lifecycle state | Only command pulses crossing into fighter gameplay are modeled here | RETAINED SOURCE-POLICY | Ownership intentionally lives in `items_core.md` and `projectiles_reflect.md`. |

## Validation Evidence

Closure should be kept current with:

- `uv run pytest tests/test_movescript_events_source_completion.py tests/test_move_tables_throw_data.py tests/test_throw_command_pending_pulse_seed_lane.py -q`
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
