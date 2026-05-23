# Input System Source Completion

Scope: controller input processing and action-command consumption for supported RL 1.0 gameplay.
This covers Slippi/raw input import, UCF/cardinal preprocessing, analog trigger/lightshield lanes,
button held/edge/release state, input-history timers/latches, seed/reseed input provenance, and
the common action-command boundaries that consume those lanes.

Out of scope for this closure level: detailed motion-state state machines after input admission
(`scheduler.md`, `match_flow.md`, grab/throw/combat docs), cosmetic/debug input display that does
not feed gameplay, and unsupported controller features outside the Slippi/PADStatus lanes exposed by
`MslInput`.

Status vocabulary: **CLOSED**, **RETAINED SOURCE-POLICY**, and exceptional **BLOCKED** only. This
document has no BLOCKED rows for the stated scope.

## Closure Summary

| Area | Status | Closure |
|---|---|---|
| Raw input import/export | CLOSED | `MslInput` mirrors Slippi/PADStatus button, stick, c-stick, and analog trigger lanes; processed debug output writes the same runtime input state consumed by gameplay. |
| UCF/cardinal preprocessing | CLOSED | Runtime uses `ucf_process_stick_i8` and the 4-entry raw pad buffer to apply HSD clamp plus optional UCF 1.0 cardinals. |
| Button/edge/timer lanes | CLOSED | `input.c` owns held/pressed/released lanes, hitlag OR-latched edges, Z-to-A/LR synthesis policy, x670..x684 timers, and trigger/lightshield timers from the fighter input block. Action consumers use raw Slippi buttons only at their ABI boundary and apply source x668/held-input mapping where callbacks require it. |
| Action-command input boundaries | CLOSED | Supported common/guard/ledge/catch/escape/special command helpers consume the input lanes with source ordering. Squat/SquatWait, AttackDash, Dash, and Run branch-local IASA gates are represented where their decomp callbacks differ from the shared Wait-family bucket. State-machine aftermath remains delegated to the relevant closed system docs, but admission gates for supported commands are audited here. |
| Seed/reseed hidden input provenance | CLOSED | UCF pad-buffer state, input-history timers, Guard tilt/release/lightshield, Kneebend, Dash/Turn, Shine release, CliffWait x8, and ledge cooldown/floor owners are explicit seed lanes where replay rows do not expose source state. |
| Debug/modelplay input adapters | RETAINED SOURCE-POLICY | Debug/modelplay adapters feed the same `MslInput` ABI. Their UI-level defaults are tooling policy as long as gameplay sees only the canonical runtime input lanes. |

## Source Phase Inventory

| Source/Data Owner | Current MSL Owner | Required Source State/Data | Current Representation | Status | Decision / Acceptance Locks |
|---|---|---|---|---|---|
| PADStatus / Slippi pre-frame input import | `MslInput`, `tools/slippi/make_dataset_from_slp.py`, `input_apply_pre_input_snapshot`, `input_apply` | Physical buttons, raw main/c-stick bytes, analog L/R, previous/current rows | Packed `MslInputPlayer` fields and processed runtime lanes | CLOSED | ABI and debug output are locked by `tests/test_ucf_input.py`, `tests/test_input_source_completion.py`, and replay dataset schema checks. |
| HSD stick clamp and UCF 1.0 cardinals | `src/ucf.c`, `src/input.c` pad-buffer ring | Raw PAD bytes, UCF enabled flags, four-frame pad buffer | Init-time deterministic lookup table plus raw pad-buffer state | CLOSED | `tests/test_ucf_input.py` covers cardinal snap, disabled cardinal behavior, and clamp rounding. |
| Main/c-stick unit conversion and deadzones | `input_axis.h`, action-command helpers | `p_ftCommonData->x0/x4` deadzones, x/y thresholds, HSD max 80 clamp | `stick_i8_to_unit`, `apply_deadzone`, common params | CLOSED | Source references in `input.c`; command-boundary tests cover tap-jump, smash/tilt, CliffWait, and guard input consumers. |
| Trigger/lightshield effective lane | `input.c`, `trigger_input.h`, `shields.c`, guard/catch helpers | `input.x650`, digital L/R full-press policy, analog max(L,R), Z LR macro, x672/x67B/x678 timers | Trigger unit lanes and seedable x672 timers | CLOSED | Locks include GuardReflect/powershield, CliffEscape LR-lane, L-cancel/passive timing, and `tests/test_input_source_completion.py` stale-word guard. |
| Button edge/release lanes and Z synthesis | `input_edge_with_z_macro`, `source_x668_button_edges_with_z_a`, `input_buttons_pressed/released`, x67C..x684 timers | `held_inputs`, `x668`, `x66C`, Z maps to A + LR lane; hitlag OR-latch | Runtime button masks and per-button press timers | CLOSED | Locks include passive/tech hitlag edges, CliffAttack Z route, CliffEscape LR-lane route, capture mash buttons, jab/attack timers, common airborne AttackAir Z-as-A, and Catch Z-as-LR+A. |
| Input-history timers x670/x671/x673/x674/x676/x677/x679/x67A/x2228_b7 | `input.c`, native preprocessing helpers | Fighter input counter block in `fighter.c`, lb_8000D148 crossing reset | Runtime timers plus seed lanes | CLOSED | Prefix and runtime locks include tap-jump, dash/smash/tilt, Damage x14 jump buffer, SpecialAir sign bit, and `tests/test_seed_guard_tilt_prefix_invariance.py`. |
| Hitlag input-latch lifetime | `input_apply_pre_input_snapshot`, `input_apply`, scheduler hitlag phases | `Fighter_Spaghetti_8006AD10_Inner1` OR-latched x668/x66C while hitlag active | Runtime preserves and ORs pressed/released lanes until hitlag clears | CLOSED | Passive/tech, no-damage hitlag, shield, and combat hitlag tests cover stale edge suppression. |
| Opening input lock | `opening_input_lock_active_for_step`, `opening_input_lock_apply_*` | VS overlay/input lock flags, frame -40 clear boundary, input timer reset | `opening_input_lock_timer` and frame-id policy | CLOSED | `tests/test_opening_input_lock_landing_iasa_replay_real_locks.py` and scheduler tests lock pre-input ordering. |
| Grounded smash charge input owner | `grounded_smash_charge_update_ftCo_800DF0D0_subset`, MSLFTSC1 charge events | Script PreCharge event, held A, saved anim rate, hold max | Smash-charge state/hold/saved-rate seed lanes and runtime state | CLOSED | Smash-charge seed sanitization and released hitbox damage tests cover live and reseed behavior. |
| Kneebend / jump input classification | `derive_kneebend_internals`, jump/locomotion entry helpers | `ftCo_Jump_GetInput`, X/Y vs tap-jump vs c-stick, short-hop owner | `kneebend_jump_input` and `kneebend_is_short_hop` seed lanes | CLOSED | Jump, tap-jump, Damage x14, and landing IASA locks cover positive/negative command boundaries. |
| Dash/Turn/Run action-command latches | `derive_dash_x4`, `derive_turn_internals`, `locomotion.c` input helpers | `mv.co.dash.x4`, `mv.co.turn.{frames_to_turn,has_turned,x8}`, run/runbrake cmd vars | Explicit seed/runtime lanes plus MSLFTSC1 command-var extraction | CLOSED | Turn/dash/runbrake tests and motion-state owner table locks cover stale/fresh input boundaries. |
| Guard input boundaries | `shields.c`, `guard_lifecycle.h`, guard seed lanes | GuardOn/Guard/GuardOff/GuardReflect input callbacks, x672 frame-start, guard xC/x10/lightshield | Guard release, lightshield, tilt, special-enable, x672 seed/runtime lanes | CLOSED | Guard entry, GuardReflect, powershield, shield HP/recharge, and capture/guard boundary tests cover admission and lifetime. |
| Catch / pummel / throw / capture command inputs | `grab_flow.c`, `throw_flow.c`, native capture mash derivation | Catch/CatchDash input priority, CaptureWait mash/buttons/stick signs, throw/pummel selection | Capture hidden timers, mash lanes, throw pulse lanes | CLOSED | Detailed state-machine closure is in `grab_throw_capture.md`; input consumption is locked by capture mash, pummel, throw, catch/guard, Ottotto catch-priority, and Z-as-grab tests. |
| CliffWait / ledge option input ownership | `src/ledge.c`, `_derive_cliff_option_stick_latch_x8` | `mv.co.cliff.x8`, `x4` wait timer, `x2064_ledgeCooldown`, ledge side/floor owner | `cliff_option_stick_latch_x8`, ledge cooldown, cliff floor seed lanes | CLOSED | `tests/test_input_source_completion.py` proves x8 is source state, not previous-visible-input reconstruction; ledge/collision tests lock attack/escape/jump/drop routing. |
| Airborne common action inputs | `locomotion.c`, `action.c`, `knockdown.c`, `ledge.c` | EscapeAir, AttackAir, JumpAerial, FallSpecial, Damage IASA, Down/Passive action inputs | Runtime helper predicates plus generated MSLMSO01/MSLFTSC1 data where source expresses callback/script ownership | CLOSED | System-specific tests cover EscapeAir, AttackAir platform pass, common airborne Z-as-A AttackAir admission, Damage x14, Down/Passive, Ottotto, and FallSpecial boundaries. Input doc owns the consumed lanes and ordering; entered-state physics/combat is delegated to those system docs. |
| Grounded branch-local IASA input ordering | `locomotion.c`, `grab_flow.c`, `shine.c`, `blaster.c` | Squat/SquatWait/SquatRv IASA, AttackDash -> Wait_IASA delegation, Dash/Run branch-local appeal/jump/run tails, B-special priority | Runtime source-order gates for lower-priority consumers and branch-local source callbacks | CLOSED | Locks include Squat B+down Reflector before AttackLw/pass, AttackDash B+down Reflector before Squat, Dash/Run common Appeal priority, and existing AttackDash guard/catch/jump controls. |
| Specials and Fox/Falco-specific input latches | `shine.c`, `specialhi`, `specials`, motion-state data | Shine release lag/isRelease, SideB/Firefox callbacks, B button/angle inputs | Character-specific generated data and explicit state lanes | RETAINED SOURCE-POLICY | Character-specific mechanics remain table/data-backed and covered by Fox/Falco special tests; core input code does not branch on character as a state proxy. |
| Modelplay/debug input adapters | `tools/modelplay`, `viewer` trace adapters, public C/Python ABI | Policy-facing input rows converted to `MslInput` | Tooling adapters feed canonical `MslInput`; gameplay consumes no viewer-specific state | RETAINED SOURCE-POLICY | Tooling policy is outside vanilla engine source, but gameplay invariants are locked by modelplay trace/regression tests and the ABI tests above. |

## Retained Source Policies

| Policy | Status | Why It Remains |
|---|---|---|
| UCF as configurable compatibility layer | RETAINED SOURCE-POLICY | Vanilla does not include UCF, but RL 1.0 enables UCF by default. The implementation is tied to the UCF source and runtime config flags. |
| Debug/modelplay input adapters | RETAINED SOURCE-POLICY | These are simulator tooling surfaces, not vanilla source functions. They remain valid only because they translate into the canonical `MslInput` ABI before gameplay. |
| Character-specific special input internals | RETAINED SOURCE-POLICY | Fox/Falco special inputs are source/data-backed through character motion-state/script data. They stay conditional on character-specific source callbacks, not core input proxies. |
| Branch-local action-command buckets | RETAINED SOURCE-POLICY | Squat/SquatWait, AttackDash, Dash, and Run do not all consume the same shared Wait-family helper in source. Runtime keeps source-order gates branch-local where the decomp callback has different priority, while tests lock the supported admission boundaries. |

## Validation And Performance Evidence

Evidence expected from this closure pass:

- `make build BUILD_FORCE=1`
- `make validate-all`
- `make test`
- `make fmt-check`
- `git diff --check && git diff --cached --check`
- `uv run python -m tools.eval.validation_report_diff --before HEAD --after reports/validation --top 220`
- A light clean-vs-dirty benchmark because the CliffWait input gate changed hot runtime state.

## Historical References

- `refs/melee/src/melee/ft/fighter.c` input build/counter block around x630..x684.
- `refs/melee/src/melee/ft/ft_0DF0.c` and `ft_0DF1.c` common action-command predicates.
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c`, `ftCo_Dash.c`, `ftCo_Turn.c`, `ftCo_Jump.c`, `ftCo_Guard.c`, `ftCo_CliffWait.c`, `ftCo_CliffClimb.c`.
- `refs/ucf/src/pad_buffer/pad_buffer.cpp` and `refs/ucf/include/util/melee/pad.h`.
