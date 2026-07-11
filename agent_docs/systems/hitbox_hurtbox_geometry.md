# Hitbox / Hurtbox Geometry Source Completion

Scope: fighter HitCapsule lifecycle and fighter geometry for supported RL 1.0 gameplay. This
includes fighter hitbox create/clear/mutate records, world-space HitCapsule centers, hurtcaps,
body-part/anchor provenance, dynamic pose selection, BODY/tip geometry ownership, victim-list
geometry lifetime at reseed, and seed reconstruction for fighter HitCapsules. Detailed item and
projectile lifecycle belongs to [items_core.md](items_core.md) and
[projectiles_reflect.md](projectiles_reflect.md); shared combat result consumption belongs to
[combat_hit_resolution.md](combat_hit_resolution.md).

Status vocabulary: **OPEN**, **INVENTORY NEEDED**, **CLOSED**, **RETAINED SOURCE-POLICY**, and
exceptional **BLOCKED** only.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| HitCapsule create/clear/restart lifecycle | CLOSED | `MSLFTSC1`/`MSLHITB1` preserve source create, clear, active-slot damage mutation and interaction mutation, authored hit group, and enable-edge victim-list copy/clear semantics. |
| HitCapsule world geometry | CLOSED | Runtime centers use extracted fighter part ids, extracted animation/dynamic pose matrices, source offset component order, root facing rotation, fighter scale, and source ignore-scale policy. |
| HurtCapsule geometry and BODY mask | CLOSED | `MSLHURT1`, `MSLPART1`, and `MSLFTSC1` hurt-state events drive hurtcap endpoints, state masks, grabbability, and lazy contact-geometry demand. |
| Dynamic pose/source-part provenance | CLOSED | Supported runtime consumers use extracted part ids, dynamic pose state, and f32/source-pose samplers where decomp call sites require them. |
| HitCapsule victim-pointer lifetime at seed/reseed | RETAINED SOURCE-POLICY | Vanilla stores raw `HitVictim*` pointers in HitCapsule rings. Replays cannot encode those pointers, so teacher-forced seed reconstruction uses explicit victim identity/provenance lanes and never changes free-running collision semantics. |
| Hitbox size and interaction mutation opcodes | CLOSED | Falcon emits `set_hitbox_interaction`; it is a distinct `MSLHITB1` active-slot mutation and gates fighter/item contact through source `x42_b5`/`x42_b7`. Hitbox size remains retained policy until supported data emits it. |

## Source Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Locks |
|---|---|---|---|---|---|
| `ftAction_8007121C` create HitCapsule | `extract_fighter_script_timeline.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Hitbox id, hit group, bone/part id, damage, scale, offsets, angle/KB, flags, `hit_grabbed_victim_only` | `MSLFTSC1` typed payload plus `MSLHITB1` create records loaded at init | CLOSED | Source component order and authoring fields are preserved. Enable-edge copy/clear is modeled through HitCapsule group/slot state, not action-row gates. Locks: hitbox table and pose tests; `test_runtime_set_hitbox_damage_does_not_create_edge`. |
| `ftAction_8007162C` adjust HitCapsule damage | `extract_fighter_script_timeline.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Active HitCapsule id and new damage value | `MSLFTSC1` event plus `MSLHITB1` kind-2 active-slot mutation record | CLOSED | Damage mutation updates the active slot only and does not publish a create edge, update pose-create counters, or call `ftColl_800768A0`. Locks: `test_mslhitb1_folds_set_hitbox_damage_into_active_slot_record`; `test_runtime_set_hitbox_damage_does_not_create_edge`. |
| `ftAction_8007169C` adjust HitCapsule scale | `extract_fighter_script_timeline.py`; `extract_fighter_hitboxes.py` | Active HitCapsule id and new scale value | Source owner identified; no current supported Fox/Falco script emits it | RETAINED SOURCE-POLICY | Keep out of runtime until supported data needs it. Guard: `test_supported_scripts_do_not_emit_deferred_hitbox_geometry_mutations`. |
| `ftAction_80071708` set HitCapsule `x42_b5` / `x42_b7` interaction flags | `extract_fighter_script_timeline.py`; `extract_fighter_hitboxes.py`; `src/hitboxes.c`; combat/item collision owners | Active HitCapsule id, flag selector, value | `MSLFTSC1` typed event plus `MSLHITB1` kind-3 mutation; low `u16_6` bits carry valid/fighter/item gates | CLOSED | Create initializes both gates. Mutation changes only the selected live slot and does not create, clear victims, or republish geometry. Fighter BODY/HitCapsule passes, including item HitCapsule-vs-fighter HitCapsule, consume `x42_b5`; fighter HitCapsule-vs-item hurtbox consumes `x42_b7`. |
| `ftAction_800717D8`; `ftColl_8007AFF8` clear HitCapsules | `extract_fighter_hitboxes.py`; `src/hitboxes.c` | Clear-all frame or per-slot remove frame | `MSLHITB1` clear records; runtime clears active definition, world slot, HitCapsule group, and victim list state | CLOSED | Clear publication is source-modeled and distinct from create/mutate records. Locks: hitbox lifecycle tests and movescript event tests. |
| `ftColl_800768A0`; `lbColl_CopyHitCapsule`; `lbColl_80008440` HitCapsule enable-edge victim-list copy/clear | `src/hitboxes.c`; `src/hitlist.c` | Old/new enabled state, hit group, source slot order, victim rings | Fixed-slot HitCapsule state, generated hit group metadata, and fixed-capacity hitlist rings | CLOSED | Same-group create/restart copies source victims; different-group or disabled-to-enabled clears. Reseed materialization initializes equivalent hidden state only for teacher-forced rows. Locks: grounded attack restart, AttackAir no-clear, and dense hitlist seed tests. |
| `lbColl_80008688`; `lbColl_80008820`; `lbColl_80008A5C` HitVictim insertion/tick/lifetime | `src/hitlist.c`; `src/hitlist.h` | Victim pointer, object kind, rehit/cooldown counters, finite ring capacity | Fixed-capacity victim rings keyed by fighter port/source identity with explicit seed proxy lanes | RETAINED SOURCE-POLICY | Free-run semantics are modeled; seed rows cannot carry raw engine pointers, so explicit source identity/proxy lanes reconstruct the source lifetime. This is documented as a seed representation policy, not a runtime collision bridge. |
| `lb_8000B1CC`; `lbColl_80007ECC`; `lbColl_8000805C` HitCapsule-vs-HurtCapsule geometry | `src/hitboxes.c`; `src/hurtboxes.c`; `src/anim_pose.c` | JObj matrix, source part id, local offsets, fighter position/scale, root facing, HitCapsule/hurtcap state | Extracted `MSLPART1`, `MSLHITB1`, `MSLHURT1`, SSANIM/dynamic pose samplers, f32 source-pose paths where required | CLOSED | Runtime computes world centers/endpoints from source part ids and extracted poses. Root-facing rotation and ignore-fighter-scale policy match decomp-shaped HitCapsule fields. Locks: hitbox/hurtbox pose, scale flag, hurtcap demand, and BODY geometry tests. |
| `ftColl_HurtboxInit`; `FighterHurtCapsule` data | `extract_fighter_hurtcapsules.py`; `hurtcaps_tables.*`; `src/hurtboxes.c` | Hurtcap bone, offsets, scale, height, grabbable flag, default state | `MSLHURT1` tables loaded at init | CLOSED | Hurtcap endpoints and attributes are data-backed; unsupported common-bone indirection is rejected at extraction rather than guessed. Locks: `test_hurtboxes_pose.py`, `test_hurtcap_geometry_demand.py`. |
| `ftAction_80071A58`; `ftAction_80071A9C` hurt-state script effects | `extract_fighter_script_timeline.py`; `move_tables.c`; `src/hurtboxes.c` | All-hurt and per-bone hurt state windows | `MSLFTSC1` events plus generated hurtcap BODY mask cache | CLOSED | Per-bone script effects use extracted hurtcap bone ids and runtime BODY masks. Locks: `test_hurtbox_state_from_hit_status.py`, `test_hurtbox_state_sim_owned.py`. |
| `ftParts_GetBoneIndex`; fighter part metadata | `extract_fighter_parts.py`; `part_tables.*`; `src/anim_pose.c` | Part id, parent/order, anchors, source JObj mapping | `MSLPART1` tables and generated part helpers | CLOSED | HitCapsules, hurtcaps, catch regions, and attachment anchors share the same part provenance table. Locks: known-data artifact and part/pose tests. |
| Dynamic/source pose variants consumed by collision | `src/anim_pose.c`; `src/hitboxes.c`; `src/hurtboxes.c`; `src/grab_attachment.c` | Current animation frame, dynamic state lanes, source f32 rounding, callback-local pose owner | Extracted SSANIM pose clips plus runtime dynamic pose state and source-pose helpers | CLOSED | Supported source call sites that need dynamic/f32 pose use explicit helpers; viewer/debug pose does not define gameplay geometry. Locks: pose, grab attachment, catch region, and modelplay geometry tests. |

## Retained Source Policies

- Raw engine `HitVictim*` values are not replay-visible. Seed/reseed geometry reconstruction uses
  explicit source identity lanes and fixed-capacity victim rings; free-running runtime collision
  still owns the actual victim-list lifetime.
- `set_hitbox_size` remains source-known but absent from supported extracted scripts. It stays
  deferred only while the data guard proves that absence.
- Item/projectile-only geometry is out of this document except where fighter HitCapsule or shared
  combat geometry consumes it.

## Evidence

- Decomp references: `refs/melee/src/melee/ft/ftaction.c`,
  `refs/melee/src/melee/ft/ftcoll.c`, `refs/melee/src/melee/lb/lbcollision.c`,
  `refs/melee/src/melee/lb/types.h`.
- Generated data contracts: `MSLFTSC1`, `MSLHITB1`, `MSLHURT1`, `MSLPART1`.
- Validation package for this closure pass: focused hitbox/hurtbox geometry tests,
  `make build_data`, `make build BUILD_FORCE=1`, `make validate-all`, `make test`,
  `make fmt-check`, diff checks, and `validation_report_diff`.
