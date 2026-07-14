# Grab / Throw / Capture Source Completion

Scope: supported Fox/Falco common grab, catch, capture, pummel, throw, thrown-victim, and
throw-release gameplay for RL 1.0. This covers grab-box contact, Catch/CatchDash/CatchWait/
CatchAttack/CatchCut, CapturePulled/CaptureWait/CaptureDamage/CaptureCut/CaptureJump, mash and
breakout, throw selection, throw entry/release, attached victim placement, throw damage, owner/victim
scheduler order, and seed/reseed hidden state needed by those owners.

Out of scope for this closure level: character-special captures such as Kirby/Yoshi/Koopa/cargo
variants, item lifecycle after throw-side projectile spawn, full item/projectile reflection/clank
lifecycle, and cosmetic camera/effect exactness unless it changes gameplay state.

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only. This
document has no BLOCKED rows for the stated scope.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| Catch input and catch-box entry | CLOSED | Catch/CatchDash input gates, entry, animation end, physics friction, and floor-loss collision are modeled from `ftCo_Attack100.c` / `ft_081B.c` source paths. |
| Catch contact and victim acquisition | CLOSED | `ftColl_80078A2C` target filtering, catch-box geometry, target-mask exclusion, wall obstruction, CatchPull entry, victim CapturePulled entry, and outgoing hitbox retirement are explicit source-shaped owners. |
| Capture attachment and wait loop | CLOSED | CapturePulled/CaptureWait/CaptureDamage placement, hi/lw floor handoff, timer/counter, mash rate, jump latch, pummel, and breakout ownership are modeled with explicit hidden state. |
| Throw selection and throw entry | CLOSED | CatchWait IASA throw priority, thrower/victim action pairing, weight-based throw animation rate, immediate `ftAnim_8006EBA4`, and attached Thrown* entry placement are modeled. |
| Throw release and throw damage | CLOSED | `set_throw_flags` release/flip, attached release anchor, throw hitbox parameters, throw damage, same-frame damage callback phase, and throw-end routing are modeled. |
| Seed/reseed hidden state | RETAINED SOURCE-POLICY | Replay rows do not expose raw GObj pointers, capture wait internals, grab mash sign latches, throw command cursor, or item HitCapsule victim rings. Seed lanes initialize those hidden source states for teacher-forced rows and require live provenance for free-running runtime. |
| Throw-side projectile boundary | RETAINED SOURCE-POLICY | Throw-side blaster pulses use extracted move-script events and shared item/combat substrates; detailed projectile lifecycle belongs to `projectiles_reflect.md` and `items_core.md`. |

## Source Phase Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Acceptance Locks |
|---|---|---|---|---|---|
| Catch input checks: `ftCo_Catch_CheckInput`, `ftCo_800D8A38`, `ftCo_800D8C54` | `grab_flow_try_enter_catch_from_iasa`, `grab_flow_try_enter_catchdash_from_iasa`, `grab_flow_enter_catchdash_from_attackdash_pregate` | Current/previous buttons, A/Z/L/R predicates, x14/x18 lockouts, Dash/CatchDash entry speed owner, Catch/CatchDash motion ids | Runtime input lanes, common params, `trigger_input`, `dash_iasa`, explicit Catch/CatchDash entry helpers | CLOSED | Source-shaped input gates are covered by grab dash, JC pummel, GuardReflect catch, and catch-facing tests. |
| Catch/CatchDash Anim/Phys/Coll: `ftCo_Catch_Anim`, `ftCo_CatchDash_Anim`, `ftCo_Catch_Phys`, `ftCo_CatchDash_Phys`, `ftCo_Catch_Coll`, `ftCo_CatchDash_Coll` | `grab_flow_update_anim_callbacks_pre_input`, catch physics in `physics.c`, collision in `mpcoll_ground.c` | Anim end, ground friction scalar, floor-loss callback, catch pull transition | Anim timebase, extracted common/character friction, source floor-loss owner | CLOSED | Catch end -> Wait, CatchDash friction, and modelplay jump-cancel grab/offstage locks cover the owner. |
| Catch target selection: `ftColl_80078A2C`, `ftColl_80078754`, `lbColl_80007ECC`, `ft_80084CE4` | `combat_select_catch_hits_one_mutating`, `grab_flow_on_catch_connect` | Catch HitCapsule, grabbable hurtcap pose, target x1A68/x1A6A mask, nearest target, side-wall obstruction, GObj order | Hitbox/hurtcap metadata, catch-only skeleton sampler, target masks, MSLSTG01 wall graph | CLOSED | Catch skeleton-scale, DownBound/DownWait target-mask, wall-obstruction, GuardReflect grabbable, and outgoing-hitbox retirement locks cover positive/negative boundaries. |
| Catch connect owner/victim entry: `fn_800D9CE8`, `fn_800DAADC`, `fn_800DA8E4`, `fn_800DAA10`, `Fighter_UnkProcessGrab_8006CA5C` | `grab_flow_on_catch_connect`, `clear_outgoing_hitboxes_after_catch_connect` | Owner CatchPull/CatchDashPull timeline, victim grounded/airborne capture variant, owner/victim facing, initial capture timer, grab owner pointer, old hitbox retirement | Explicit `grab_owner_port`, `attached_victim_port`, capture timer/counters, velocity clears, hitbox clears | CLOSED | CapturePulled entry velocity, source-clear, catchdash state flags, shield recharge, and stale outgoing BODY locks cover this phase. |
| Capture attachment Phys/Coll: `fn_800DAD18`, `ftCo_CapturePulled{Hi,Lw}_Phys/Coll`, `ftCo_CaptureWait{Hi,Lw}_Phys/Coll`, `ftCo_CaptureDamage{Hi,Lw}_Phys/Coll` | `grab_attachment_update_pre_collision`, `grab_attachment_apply_capture_delta_now`, capture floor handoff in `grab_flow.c` / `mpcoll_ground.c` | Owner anchor, victim `x1A70`, capture hi/lw variant, floor-mask result, floor skip, jumps refresh, same-frame catch-connect CollData floor | `grab_offset_{y,z}`, owner port, char anchor part id, CollData floor packet, capture hidden lanes | CLOSED | Capture delta once, CapturePulledHi floor callback, CaptureWaitLw projection, FoD floor skip, and low-capture ground projection tests cover this owner. |
| Capture wait timer/mash/jump/breakout: `ftCo_CaptureWaitHi_Anim`, `ftCommon_GrabMash`, `fn_800DC014`, `ftCo_800DA698`, `fn_800DC070` | `capturewait_anim_callback_apply`, `capturewait_resolve_anim_breakout_owner`, grab mash seed/native lanes | Grab timer, capturewait.x0/x4/xC, stick/button mash sign latches, anim-rate hold timer, jump latch, owner/victim ordering | `capture_grab_timer`, `capture_wait_counter`, `capture_wait_anim_rate_timer`, `capture_wait_jump_latch`, `capture_breakout_pending`, `grab_mash_stick_*` | CLOSED | CaptureWait mash/rate, first-steady owner tick, timer-expired breakout, jump/cut selection, and prefix-invariance tests cover this phase. |
| CatchWait pummel: `fn_800DA4C0`, `fn_800DA4FC`, `ftCo_CatchAttack_Anim`, `ftCo_800DC284`, `ftCo_800DC3A4` | CatchAttack entry/Anim in `grab_flow.c`; CaptureDamage entry/return helpers | Pressed A edge, CatchAttack motion, CaptureDamageHi/Lw entry, pummel damage/capture wait return | Current input, CaptureWait state, CaptureDamage state, capture hidden state reset/restore | CLOSED | JC pummel, CaptureDamage wait return, pummel/breakout ordering, and capture wait owner tests cover pummel boundaries. |
| CatchWait throw selection: `ftCo_800DD1E4`, `ftCo_800DF7F4`, `ftCo_800DF844`, `ftCo_800DF878` | `catch_wait_throw_action_from_inputs`, `enter_throw_from_wait` | Current/previous L-stick/C-stick, facing, x98/xB0/Y thresholds, source priority order | Input lanes, common params, explicit priority implementation | CLOSED | Throw selection tests cover X/Y priority, c-stick/down hold, facing, and no-throw controls. |
| Throw entry: `ftCo_800DD398`, `ftCo_800DD4B0`, `ftCo_800DE3FC` | `enter_throw_from_wait`, `grab_attachment_use_static_offsets_for_thrown_entry`, `grab_attachment_apply_thrown_anchor_now` | Throw action/victim action mapping, throw index, weight-independent mask, victim weight, throw anim rate, immediate anim tick, victim facing, attached owner pointer | Action helpers, extracted `weight_independent_throws_mask`, char weights/common throw speed scalar, `throw_anim_rate_fp_q16_16`, attachment lanes | CLOSED | Live script throw data, throw entry position, throw animation speed, and thrower/victim timing tests cover this owner. |
| Thrown attachment and low/non-low anchor: `ftCo_800DB368`, `ftCo_800DE508`, `Fighter_UnkUpdateVecFromBones_8006876C` | `grab_attachment.c` static/float anchor paths | Thrower FtPart_TransN2, victim XRotN, static x1A70, live float AObj/JObj sampling, facing root rotation, low-throw reparenting | MSLPART1 anchor part id, integer/float animation pose samplers, `grab_offset_{y,z}`, `attached_victim_port` | CLOSED | Thrown position, low-throw frame-25 attachment, Dream ThrowB/FoD platform throw, and modelplay grab-throw locks cover positive/negative anchor paths. |
| Throw release flags and facing flip: `ftAction_800718A4`, `ftCo_800DD724`, `ftCo_800DDDE4` | `fighter_script.c`; `throw_flow_update_anim_callback_pre_input` | Live move-script `set_throw_flags`, owner pose-facing before flip, victim pointer, attached release root | Persistent script cursor/flags plus release floor-sweep seed lanes | CLOSED | ThrowB root-facing, throw release pending, thrower ThrowHi exit timing, and release floor-sweep locks cover this phase. |
| Throw hitbox damage: `ftAction_80071E04`, `ftCo_800DE2A8`, `ftCo_800DE7C0`, shared ProcessHit path | `fighter_script_throw_hitbox_params`, `combat_apply_throw_hit`, post-release damage callback helper | Live throw HitCapsule params, damage/angle/KB, stale/source, victim hurt status, Damage* callback phase | Persistent script throw payload, shared `MslCombatDamageProduct`, generated damage owner predicates | CLOSED | Throw release damage, attached thrown no-damage controls, source identity, stale/combo, DamageFlyTop floor contact, and combat closure tests cover this owner. |
| Throw Anim end / cut end / capture jump end: `ftCo_Throw{F,B,Hi,Lw}_Anim`, `ftCo_CatchCut_Anim`, `ftCo_CaptureCut_Anim`, `ftCo_CaptureJump_Anim`, `ftCommon_8007D92C` | `enter_wait_or_fall_from_throw_end`, `enter_common_wait_or_fall_from_grab_cut_end`, `enter_fall_from_capture_jump_end` | Anim end, grounded/airborne state, Wait/Fall routing | Anim tables/timebase and on-ground state | CLOSED | Throw end, CatchCut/CaptureCut, CaptureJump, and grounded/airborne routing tests cover this owner. |
| Throw-side blaster command pulses: `ftAction_80071974`, `ftAction_80073354`, `ftFx_Throw_Anim`, `it_8029C6CC` | `items.c` throw-side laser spawn/pulse paths, native throw pulse seed lanes | Command cursor, `throw_flags_b0`, throw action, char laser article kind, hold-joint pose, existing live shot state, item victim rings | MSLFTSC1 `set_throw_spawn_projectile`, native seed lanes, item hitlist seed lanes, generated laser/item data | RETAINED SOURCE-POLICY | Shared throw scheduling is closed here; detailed projectile lifecycle remains in item/projectile systems. ThrowHi/ThrowB/ThrowLw pulse, event-dump, and item-hitlist locks cover the shared boundary. |
| Seed/reseed hidden owner reconstruction: grab owner pointer, capture wait internals, grab mash signs, throw pending release, throw pulse cursor, throw-laser item victims | `src/api.c`, `tools/slippi/seed_history.py`, `tools/slippi/validation_buffer_builder.py`, native preprocess helpers | Raw GObj owner/victim pointers, callback-local timers/counters, one-shot throw flags, item victim rings | Explicit seed lanes with source comments and prefix-causal/native derivation where hot | RETAINED SOURCE-POLICY | These lanes initialize hidden source state for teacher-forced validation only. Prefix-invariance and replay-real seed tests cover the retained policy. |

## Retained Source Policies

| Policy | Status | Why It Remains |
|---|---|---|
| Teacher-forced hidden grab/throw state | RETAINED SOURCE-POLICY | Replay rows do not expose raw owner/victim GObj pointers, capturewait internals, grab mash signs, or one-shot throw flags. The simulator carries explicit seed lanes for those source states instead of fitting runtime rows. |
| Throw-side projectile lifecycle boundary | RETAINED SOURCE-POLICY | Grab/throw owns throw command timing and release/source handoff. Projectile lifetime, reflect/bounce, item-only clank, and item article edge cases remain in `items_core.md` / `projectiles_reflect.md`. |
| Character-special captures outside Fox/Falco RL 1.0 | RETAINED SOURCE-POLICY | Kirby/Yoshi/Koopa/cargo capture variants are decomp-visible but outside the current supported Fox/Falco fighter gameplay domain. Core helpers are character-general where source data is common. |

## Validation Evidence

Evidence from this closure pass:

- Focused grab/throw/capture tests: passed.
- `make build BUILD_FORCE=1`: passed.
- `make validate-all`: passed.
- `make test`: passed.
- `make fmt-check`: passed.
- `git diff --check && git diff --cached --check`: passed.
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`: no hard reds and no unclassified reds.
- No hot-path semantic broadening was introduced; the runtime cleanup replaced a local throw-release Damage* switch with existing generated damage owner predicates.

## Historical References

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c`
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c`
- `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_80078754,ftColl_80078C70}`
- `refs/melee/src/melee/ft/ft_081B.c`
- `refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_8006CB94,Fighter_8006A360,Fighter_procUpdate}`
- `data/moves/{fox,falco}.json`
- `data/motion_state/owners/{fox,falco}.bin`
- `data/model_parts/{fox,falco}.bin`
