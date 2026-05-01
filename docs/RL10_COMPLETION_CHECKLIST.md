# RL 1.0 Completion Checklist

This checklist is the current family-level tracker for **finish-the-sim** work:
close shared owner families with decomp-first passes, leave only narrow
decomp-justified residuals, and avoid row-shaped bridge fixes unless the owner
family is already closed.

Last updated:
- Date: 2026-04-23
- Scope baseline: shared throw/thrown substrate, guard, locomotion / grounded transition /
  motion-entry, item-owner identity, match-flow / respawn, and knockdown / passive contact owner
  families closed; checklist reflects post-closure RL 1.0 priority ordering.

## Status Legend

- **Closed / effectively closed**: the shared owner path is in place; remaining behavior is narrow per-move or adjacent-family detail.
- **Active / next deep pass**: a shared owner family that still needs a completionist decomp audit and cleanup pass.
- **Residual cleanup**: the shared owner is largely closed; remaining work is narrow and should stay within that family boundary.
- **Blocked / split first**: do not patch runtime yet; first make the family seed-visible, decomp-visible, or split out of a mixed bucket.
- **Bridge**: compensating logic that exists because a shared owner path is still missing or mis-owned.
- **Residual**: a real move-specific or adjacent-family difference that should remain if decomp supports it.

## Priority Order

Recommended sequence for the next deep passes:

1. **Core combat followup family**
2. **Fox/Falco special-move families**
3. **Ledge / collision-env parity**
4. **Item-owner seed cleanup + item identity family**
5. **Mixed-bucket split**

## Closed / Effectively Closed

### Shared throw/thrown substrate
- Status: `closed / effectively closed`
- Owner boundary: shared `Throw*` / `Thrown*` attached ownership, pending-release collision suppression, and shared release callback/timeline path
- Primary sim files: `src/grab_attachment.c`, `src/throw_flow.c`, `src/mpcoll_*`, adjacent seed/runtime support
- Acceptance bar already met:
  - attached/release substrate shared by default
  - throw-core bridges materially removed
  - remaining work is per-throw pulse/article behavior only
- Remaining residuals:
  - `ThrowLw` pulse-family timing owner
  - other decomp-justified per-throw pulse/article tails

### Common damage-owner family
- Status: `closed / effectively closed`
- Owner boundary: `Fighter_ProcessHit` -> `ftCo_8008DCE0` -> `Damage*` / `DamageFly*` / `DamageFlyRoll`
- Primary sim files: `src/combat.c`, `src/timers.c`, `src/mpcoll_ground.c`, seed/runtime support in `src/api.*` and `src/state.*`
- Acceptance bar already met:
  - shared damage-entry owner in place
  - old runtime bridge lanes materially collapsed
  - remaining `DamageFlyRoll` tails treated as explicit residual admission/carry work, not fuzzy bridge debt
- Remaining residuals:
  - `DamageFlyRoll` admission/carry tails
  - adjacent source-clear / match-flow lanes outside this owner boundary

### Capture / grab owner family
- Status: `closed / effectively closed`
- Roadmap families: `F03_capturewait_bridge`
- Owner boundary: `CatchPull`, `CapturePulled`, `CaptureWait`, `CatchAttack`, breakout / `CaptureCut` adjacency, and shared owner/victim callback ordering for that family
- Primary sim files: `src/grab_flow.c`, `src/grab_attachment.c`, `src/api.*`, `src/state.*`, `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
- Acceptance bar already met:
  - first-steady `CaptureWait` ownership no longer depends on cross-row continuity or synthetic phase-bit carry
  - breakout / `CaptureCut` ownership flows through the shared grab owner path with explicit decomp-backed hidden owner lanes
  - no remaining core `CaptureWait` callback/timeline repair in `src/grab_flow.c` or `src/anim_timebase.c`
- Remaining residuals:
  - none in the targeted `CatchPull -> CapturePulled -> CaptureWait` / `CatchAttack` / breakout owner family

## Active / Next Deep Passes

### 2. Guard release / guard timer / guardreflect family
- Status: `closed / effectively closed`
- Roadmap families: `F01_guard_release_collision`, `F02_guard_timer_flags`, parts of `F15_guard_item_ownership`
- Owner boundary: `Guard`, `GuardSetOff`, `GuardReflect`, shield-release ordering, shield-hit followups, reflect timing, timer/state-flag ownership
- Primary sim files: `src/action.c`, `src/state_flags.c`, `src/shields.c`, `src/combat.c`, `src/items.c`
- Closed scope:
  - `F02_guard_timer_flags` post-hitlag timer/state-flag handoff is cleaned up through explicit GuardSetOff handoff owners in `src/state_flags.c`,
    causal GuardSetOff entry-rate reconstruction, and the narrow non-causal `guard_setoff_exit_frame_speed_mul_f32` lane for hidden last-hitlag
    `x19A4/lightshield_amount` rates that Slippi only exposes on the first post-hitlag row
  - `F01_guard_release_collision` shield-release / shield-hit followups now route through the shared GuardOn/GuardReflect callback order:
    `GuardOn_Anim` drain before `GuardOn_IASA`, GuardReflect active-window shield-hit gating, and GuardReflect BODY-hit followup carry
  - item/laser handling in this owner family is narrowed to guard-owned predicates
- Residuals outside the common guard owner:
  - remaining item-side reflect transfer/speed TODOs stay scoped to item ownership, not Guard / GuardSetOff / GuardReflect callback or timer ownership
- Locks / validation:
  - focused locks cover the Dash -> GuardOn -> GuardReflect BODY followup, no-submotion GuardOn-provenance
    GuardReflect hurtcaps, locomotion -> GuardSetOff laser handoff, GuardSetOff hitlag-exit action-frame
    parity, the explicit GuardSetOff exit-rate lane, and GuardReflect active-timer handoff rows
  - full validation is materially better than clean HEAD for both one-step and rollout totals

### 3. Locomotion / grounded transition / motion-entry timing family
- Status: `closed / effectively closed`
- Roadmap families: `F10_grounded_transition_resolution`, `F11_locomotion_action_frame`, `F12_instance_id_transition_only`
- Owner boundary: Dash / Walk / Turn / KneeBend / motion-entry selector timing, pure motion-entry instance-id bumps, grounded selector timing
- Primary sim files: `src/anim_timebase.c`, `src/step.c`, `src/match_flow.c`, `src/api.c`
- Completed in current pass:
  - Dash animation-end now routes through the shared Anim-callback owner before grounded IASA:
    `ftCo_Dash_Anim -> ft_8008A2BC -> Wait -> Wait_IASA`, so Dash end rows no longer use a
    direct late Dash IASA shortcut for destination Turn/Squat timing.
  - Walk steady callback timing now uses the shared `ftCo_Walk_Anim -> ftWalkCommon_800DFDDC`
    source lane instead of WalkSlow / WalkMiddle frame-window bridges; Walk type threshold rows
    route through the same owner plus `ftWalkCommon_800DFEC8`, with a narrow retarget source lane
    for the hidden `ft_GetGroundFrictionMultiplier` source-choice branch.
  - Run steady callback timing now has a narrow `ftCo_Run_Anim` source lane, keeping
    `frame_speed_mul_f32` causal while reconstructing the hidden replay-facing owner for one-step
    seeds.
  - The aggregate WalkMiddle -> WalkFast wrap row now uses the shared AObj loop/clamp owner in
    `msl_anim_timebase_tick_once`; `F11_locomotion_action_frame` is gone from aggregate taxonomy.
  - Run / RunDirect jump admission now uses the decomp `fn_800CAF78` threshold, closing the
    remaining Run -> KneeBend selector row without widening Squat/Ottotto/Landing/Walk jump gates.
  - Grounded AttackS4 charge-frame action-frame rows now use the extracted `start_smash_charge`
    owner instead of remaining in locomotion action-frame timing.
  - Turn post-flip facing now follows `ftCo_Turn_Anim_Inner` on the narrow first steady post-flip
    row, including the smash-turn subset where x8 owns Dash-latch direction but not the facing flip.
  - `F10` / `F12` taxonomy is split into grounded selector, collision/landing/edge, combat,
    hurtbox/state-flag, grounded attack, special/appeal/runbrake adjacency, Turn hidden
    microphase, TurnRun exit microphase, local grounded instance-counter, cross-player
    instance-counter, and adjacent-family instance-counter buckets so the roadmap no longer hides
    unrelated rows under one broad locomotion label.
  - `F10i_turn_hidden_microphase` is closed by a narrow Turn-only
    `turn_kneebend_facing_override_u8` replay-facing lane for first-tick Turn -> KneeBend entries
    whose hidden `ftCo_Turn_IASA` temporary-facing owner is only exposed by the next Slippi row.
  - `F10j_turnrun_exit_microphase` is closed in runtime by letting final TurnRun down-stick rows
    take the decomp `ftCo_TurnRun_Anim -> ft_8008A2BC -> Wait -> Wait_IASA` path instead of forcing
    the simplified raw-x Run branch.
  - `F12a_grounded_instance_counter_order` is closed by the narrow
    `motion_entry_instance_id_override_u16` lane for same-frame `plAttack_80037B08` order that is
    hidden from Slippi post-frames. The normal `ft_800895E0/x2073` runtime path remains default;
    the lane is populated only for grounded locomotion simultaneous-entry / hidden-prior-consumer
    rows and the explicit SpecialN loop-restart callback lane.
  - Closure here is not a claim that all hidden state is now causal runtime state. It includes two
    explicit non-causal replay seed lanes for Slippi-unobservable owner state:
    `turn_kneebend_facing_override_u8` and `motion_entry_instance_id_override_u16`.
    Current validation-population audit: Turn-facing lane is 8 primary / 50 aggregate rows, all
    `Turn -> KneeBend` and no current mismatch family; motion-entry instance lane is 121 primary /
    597 aggregate rows, all within grounded locomotion owner transitions and with no broad
    out-of-owner action transitions.
- Remaining residuals after current pass:
  - No broad in-scope `F10` / `F11` / `F12a` bucket remains in aggregate taxonomy.
  - Remaining instance-order rows are split outside the targeted owner as adjacent combat / attack
    instance order (`F12c`) and adjacent motion-entry tails (`F12b`), not Dash / Walk / Turn /
    KneeBend selector bridges.
  - Remaining `F10*` rows are collision/landing/edge, grounded combat/attack, special, runbrake, or
    appeal adjacency buckets; they are not shared grounded-selector timing debt.
- Acceptance bar:
  - motion-entry ownership is shared and deterministic
  - pure timing rows are not spread across unrelated guard/combat fixes
  - surviving locomotion tails are narrow residuals only

### 4. Knockdown / passive contact owner
- Status: `closed`
- Closure scope: shared Passive / PassiveStand / DownBound selector ownership only; this does not
  claim the whole damage/down/passive transition surface is gone.
- Roadmap families: `F07_knockdown_grounding=0` in fresh primary and aggregate taxonomy. `F18` is
  active again after the floor/landing split as a narrower DamageFly floor-contact / hidden
  CollData seed-surface owner; it is not shared Passive / PassiveStand / DownBound selector debt.
  Remaining damage/down/passive-shaped rows are source-owned adjacent residuals (`F08c`, `F18`,
  `F10m`, `F28`), not shared selector debt.
- Owner boundary: DamageFly/DamageFall floor-contact tech owner for Passive / PassiveStand /
  DownBound selection.
- Primary owner files: `src/knockdown.c`, `src/input.c`
- Seed provenance support: `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
- Adjacent/upstream residual owners: `src/mpcoll_ground.c`, `src/ledge.c`, `src/locomotion.c`
- Closure note:
  - `src/knockdown.c::enter_damagefly_ground_contact_followup` is the shared decomp-shaped selector
    for `DamageFly*` / `DamageFall` floor contact.
  - `src/input.c` and `tools/slippi/seed_history.py` now model the tech-timer provenance needed by
    `ftCo_800986B0`: L/R first pressed during active hitlag stays hitlag-latched and can fail the
    `x684` debounce gate, while L/R pressed before hitlag preserves the first `x684` debounce capture
    for the later `ftCo_80090184` floor-contact callback.
  - Remaining `F08c_damage_state_transition_adjacency` rows are Down/Passive/Fall/Landing/mpColl
    timing adjacency, not the shared contact selector. `F06_damageflyroll_rng_gate` remains the
    common-damage `ftCo_8008DCE0` DamageFlyRoll RNG/admission owner, and
    `F08f_body_contact_candidate_filter_residual` remains BODY candidate selection.
- Acceptance bar:
  - one damage-fly contact owner chooses grounded outcomes consistently
  - no ledge or ECB regressions

### 5. Core combat followup family
- Status: `residual cleanup active`
- Roadmap families: broad `F08_damage_resolution_combat` is eliminated from current cardinal/aggregate taxonomy;
  broad `F09_aerial_combat_resolution` is eliminated, but the followup residual cleanup remains
  active until the remaining F06/F08c/F08f/F09d-equivalent rows either move through simulator/seed
  behavior or are proven by exact source/probe evidence to belong to another owner. Taxonomy splits
  are diagnostic and do not count as closure by themselves.
- Owner boundary: BODY hits, aerial continuation, hitlag/hitstun/continuation ordering after damage admission
- Primary sim files: `src/combat.c`, `src/timers.c`, `src/items.c`, `src/step.c`
- Current continuation pass (2026-04-24):
  - Retained decomp-backed runtime/seed cleanup across the next residual block:
    common airborne `Damage_IASA` now uses the live `mv.co.damage.x14` jump-buffer snapshot with
    XY and hitlag-gated tap-jump seed producers (`ftCo_8008F744`, `ftCo_Damage_IASA`,
    `ftCo_Jump_GetInput`); AttackAir
    same-frame IASA checks aerial B-special admission before JumpAerial (`ftCo_AttackAir.c::DO_IASA`,
    `ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput`); JumpF/JumpB -> EscapeAir floor handoff projects
    through the decomp floor wrapper (`ftCo_EscapeAir_Coll`, `ft_80082C74`,
    `mpLib_8004DD90_Floor`); `GuardSetOff_Anim` may enter Guard and then same-frame GuardOff through
    the normal Guard IASA release path; terminal `GuardReflect_Anim` with an expired timer can
    snapshot into Guard before release consumption; and common-air FD walljump rows promote the
    hidden `wall_jump_input_timer` / `x2110_walljumpWallSide` phase from prefix-causal replay
    history, then runtime applies ISO-derived `co_attrs.x148` plus ftCommonData x768/x76C/x770/x774
    rather than a generic visible wall-hug branch.
  - Replay locks:
    `tests/test_core_combat_damage_contact_followup_replay_real_locks.py`,
    `tests/test_damageair_hitstun_exit_jumpbuffer_replay_real_locks.py`,
    `tests/test_locomotion_attackair_landing_contact_y_regression.py`,
    `tests/test_locomotion.py`,
    `tests/test_guard_callback_order_replay_real_locks.py`, and
    `tests/test_damagefly_passivewalljump_replay_real_locks.py`.
  - Retained 2026-04-24 callback followup: DownBound allow-ground-to-air ledge exit now honors
    the `mpLib_8004DD90_Floor` endpoint clamp before entering Fall, and
    the narrow FD `DamageFlyTop` wall-contact seed persists CollData wall side/index for the
    PassiveWall callback phase. `LandingFallSpecial` seed timebase now derives source-specific
    landing lag for EscapeAir (`p_ftCommonData->x344`), Illusion/Phantasm (`da->x50`), and
    Firefox/Firebird (`da->x90`).
    Replay locks live in `tests/test_damagefly_downbound_state_selection_regression.py`,
    `tests/test_locomotion_attackair_landing_contact_y_regression.py`, and
    `tests/test_seed_history.py`.
  - Fresh taxonomy from regenerated primary and aggregate datasets after the current continuation:
    primary total `441`; aggregate total `3113`. The assigned floor/landing callback labels are
    now closed in the active taxonomy: primary and aggregate `F13a=0`, `F27a=0`, `F27b=0`,
    `F27c=0`, and `F27d=0`. Former rows were split to narrower owners:
    `F10n_common_fall_landing_timebase` for EscapeAir/FallSpecial/LandingFallSpecial phase rows,
    `F18_damage_tech_timer_seed_surface` for DamageFly floor-contact / tech / hidden CollData
    provenance, `F08d`/`F08c` for damage timer/transition adjacency, `F09c`/`F10a` for aerial and
    grounded action-entry adjacency, and `F10b`/`F20`/`F28`/`F29` for combat-contact fallout.
  - Runtime/seed movement and taxonomy owner movement are tracked separately. The retained
    floor/landing callback pass adds the common `Damage_Coll` seeded final
    CollData_X130_Locked locked-bottom callback path, `DamageAir -> Landing` hitstun clear,
    same-frame `DamageAir -> AttackAir` entry floor suppression, and narrow active-hitlag
    `DownDamage_Coll` resting/downward-KB floor contact. It moves aggregate `3141 -> 3113`,
    `F27b 128 -> 104`, and `F27d 29 -> 25`; primary remains `441`.
    The final owner split closes `F13a/F27a/F27b/F27c/F27d` at zero without changing total
    mismatch counts; remaining rows move to `F18`/`F10n`/`F08d`/`F08c`/`F09c`/`F10a`/`F10b`/`F20`/
    `F28`/`F29`.
    Active aggregate heads are `F01=567`, `F25=373`, `F10b=209`, `F09c=193`, `F12b=148`,
    `F26=137`, `F03=135`, `F08a=131`, `F09a=114`, `F18=110`, `F10n=105`, `F08d=104`,
    `F09b=98`, `F10f=79`, `F28=68`, `F10d=67`, `F29=64`, `F10a=48`, `F19=42`, and `F10j=38`.
  - The diagnostic owners remain an implementation map, not a closure claim:
    `F26` is the exact `ftCo_8008DCE0` DamageFlyRoll RNG stream / hidden pre-gate
    `Fighter_8006CDA4` seed surface; the retired `F27*` labels are zero in the active taxonomy,
    with their former rows represented by narrower seed/timebase/contact owners; `F28` is exact
    `ftColl_80078C70` / `ftColl_80076ED8` candidate ordering and
    `lbColl_80006E58` narrowphase; `F29` is aerial HitCapsule victim-provenance /
    contact-hitlag carry, including ShieldDesc and narrow aerial Shine contact rows.
  - Rejected experiments in this continuation:
    generic walljump runtime entry without the hidden walljump timer / persisted CollData wall seed
    surface, broad `DamageFall` terminal IASA suppression, broad EscapeAir steady floor projection,
    terminal damage ECB locked-bottom expansion, broad fresh/late JumpAerial -> EscapeAir floor
    projection, generic DamageFly root projection, visible DamageFlyTop wall-hug recovery, ordinary
    Fall early floor-sweep suppression, frame-start ECB-lock consumption, JumpAerial-entry
    EscapeAir floor suppression, sustained early-lock EscapeAir `action_frame<=4` landing
    suppression, root-below-floor guarded early-lock EscapeAir suppression, and generic airborne
    DownBound floor-sweep/resting-contact suppression, visible late DamageFlyN root projection,
    x67C/x67D/x67E-gated DamageFlyN projection, broad continued DownDamage projection, and
    JumpAerial -> EscapeAir immediate-entry landing suppression, and terminal airborne DownBound
    ledge-floor Fall conversion all failed focused locks or worsened primary / aggregate taxonomy.
    None is retained. A broad final-lock-bottom experiment for all
    DamageAir/DamageFly/Damage ground rows worsened taxonomy (`primary F27b 25 -> 32`, aggregate
    `F27b 128 -> 135`); the retained subset is common Damage_Coll only. The latest floor/landing
    callback pass previously confirmed target counts primary `F13a/F27a/F27b/F27d=7/9/25/0` and
    aggregate `F13a/F27a/F27b/F27d=86/101/104/25`, with `F27c=0`; the final owner split now
    closes all five assigned labels at zero. Visible DamageFlyN projection
    worsened `F27a` (`primary 9 -> 35`, aggregate `101 -> 455`), x67 timer context still worsened
    aggregate `F27a 101 -> 429`, and broad DownDamage projection worsened `F27d`
    (`primary 0 -> 16`, aggregate `29 -> 149`). Terminal airborne DownBound Fall conversion
    worsened `F27d` (`primary 0 -> 66`, aggregate `25 -> 175`). The broad EscapeAir variant
    worsened to primary `F13a=31` and aggregate `F13a=298`, while the narrower root-guard variant
    only swapped `QuerulousGrandDinosaur:157` for `QuerulousGrandDinosaur:2102`.
    A narrower `DamageFlyHi` upward-KB floor suppression also failed (`primary 441 -> 492`,
    `F27a 9 -> 60`). Adding the missing teacher-forced `CollData.prev_pos.x` seed lane is retained
    as decomp-backed seed surface, but it did not move the target counts by itself.
  - Remaining current-map blockers:
    `F01=567` aggregate rows remain mixed GuardReflect/Guard/GuardSetOff collision candidate
    ordering and shield-hit outcome phase, not a single release timer; `F26=137` aggregate rows
    still require exact `ftCo_8008DCE0` RNG stream / hidden `Fighter_8006CDA4` consume-state
    representation; the former `F27a/F27b/F27d` rows are now represented by narrower
    `F18`/`F08d`/`F08c`/`F09c`/`F10a`/`F10b`/`F20` owners; `F28=68` remains exact BODY candidate ordering /
    narrowphase (`lbColl_8000805C` / `lbColl_80006E58`); `F29=64` remains shield descriptor geometry
    / contact-hitlag provenance. Protected and item-owner families remain zero.
- Current split:
  - AttackAirLw -> shield-hit admission seed provenance is closed as a named sub-owner:
    replay-only authoritative-empty per-HitCapsule seeds are emitted only when `t+1` proves a
    fighter shield hit entered `GuardSetOff` with both fighters in hitlag and shield HP dropping.
  - BODY damage admission seed provenance is closed as a named sub-owner:
    replay-only authoritative-empty per-HitCapsule seeds are emitted only when `t+1` proves a
    fighter BODY damage hit through percent increase, hitlag on both fighters, and source-owner
    attribution to the current attacker.
  - Reciprocal BODY hit stale/hitlag ownership is implemented:
    damage staling uses the pre-combat HitCapsule attack id and received-KB hitlag takes priority
    over same-frame deal-hitlag.
  - Aggregate audit after forced dataset rebuild shows the BODY-admission and reciprocal-hit patches
    generalize without same-owner churn: aggregate one-step improves from `8488` to `8096` with no
    per-dataset or per-field regressions against the starting reports.
  - BODY admission population audit is narrow: aggregate has `301` BODY authoritative-empty
    attacker/defender pairs, all with percent increase, both fighters in hitlag, and source-owner
    attribution; phantom/no-percent (`QuerulousGrandDinosaur:8638`) and extra-contact
    (`TreasuredBackKangaroo:5247`) sentinels remain unpopulated.
  - Hidden HitCapsule shield/body lineage is now represented by authoritative per-HitCapsule seed
    lanes, not dense group fallback or replay-proof BODY admission. Accepted shield/body contacts
    preserve `victims_1` across visible defender action/instance-id proxy changes; runtime stale
    cleanup skips `combat_hitlist_hb_valid=1` slots. This closes the AttackDash/AttackAirLw
    shield-lineage sub-owner exposed by `PRH:1830..1834` and `IAT:11146..11147`.
  - Remaining rows are split into precise residual families:
    `F08a_damage_identity_bookkeeping_residual`,
    `F08c_damage_state_transition_adjacency`,
    `F08d_damage_timer_scalar_residual`,
    `F10k_body_no_candidate_action_timing`,
    `F10l_body_selected_false_action_timebase`,
    `F05b_damage_hurt_height_selection_residual`,
    `F09a_aerial_stateflag_hurtbox_adjacency`, and
    `F09b_aerial_bookkeeping_adjacency`,
    `F09c_aerial_action_entry_adjacency`,
    `F09d_aerial_contact_hitlag_residual`, and
    `F09e_aerial_instance_timing_residual`.
  - `F08b_body_contact_geometry_residual` is split out of shared post-admission combat followup as
    the named upstream `Combat geometry / HSD pose collision owner`. `BHH:1163` now resolves through
    the decomp-backed standing
    Turn internal-facing hurtcap owner (`ftCo_Turn_Enter` / `ftCo_Turn_Anim_Inner` -> `lb_8000B1CC`
    hurtcap world space). Seed-history now mirrors runtime post-anim pose timing and delayed BODY hitlist
    registration, so `BHH:1169` and related same-hit followup rows stay suppressed by the
    HitCapsule victim list after the admitted `BHH:1163` hit. A broadened grounded x58->x4C sweep
    was tested and rejected because aggregate one-step worsened; the retained owner uses the
    decomp-shaped matrix-first `lbColl_8000805C` / `lbColl_80006E58` predicate plus hidden-depth
    and HitCapsule lifecycle state instead of a broad helper.
  - A replay-only per-HitCapsule BODY authority bridge was tested for `BHH:1599` and rejected as a
    closure path. It improved one-step but admitted through replay proof rather than
    `lbColl_8000805C` geometry and regressed rollout first-mismatch totals. `BHH:1599` remains the
    current BODY collision-space proving row for the real runtime owner.
  - Pre-collision primitive capture for `BHH:1599` now proves the BODY predicate consumes the
    expected pair (p0 AttackAirN hb1 vs p1 hurtcap 12) and that the hit x58/x4C lane is no longer
    the blocker. The remaining upstream owner is the defender live HSD JObj/AObj pose feeding
    `lb_8000B1CC`: vanilla `cur_anim_frame=4.0`, `x898=0`, and no active blend, but the live
    hurtcap-12 JObj matrix differs from the extracted `SSANIM01` AttackHi3 frame-4 pose even though
    the FigaTree header and track descriptors match. The selected defender has `dynamics_num=1`;
    decomp routes this through `ftCo_8009DD94` dynamic bone updates and `ftAnim_8006E7B8` /
    `ftAnim_8006EED4` subtree animation toggles before `lb_8000B1CC` computes hurtcap endpoints.
    This is pre-admission collision primitive ownership, not the shared post-hit combat followup
    owner.
    A local matrix-primary/AttackHi3 pose-order runtime probe admitted the row but selected the
    wrong low hurtcap (`DamageFlyLw` vs vanilla `DamageFlyN`), so it was rejected and not kept as a
    gameplay bridge.
    The rejected runtime primitive overlay and one-slice generated data overlay are not present.
    `tools/extraction/extract_fighter_anims.py` now parses `ftData.x2C` with the `BoneDynamicsDesc`
    `0x18` stride, includes the dynamic child chain rooted at part 17, and emits dynamic-chain data
    consumed by `src/anim_pose.c`. Runtime carries fixed-capacity dynamic-node pose state, updates it
    before hurtcap refresh, and samples dynamic collision matrices for BODY hurtcap endpoints before
    `lbColl_8000805C`. `SSDYNN01` v4 owns the dynamic-collision submotion predicate, and runtime
    separates dynamic state carry from current-frame collision-matrix application. A broad static
    grounded-common-attack application
    was tested and rejected: it fixed `BHH:1599` but regressed suite totals and introduced Fox
    BODY false negatives because static SSANIM lacks the persisted `lb_8001044C` dynamic-node
    state. The kept owner is the stateful segment-vector update, not a static action bucket.
  - Current stabilized validation map after the dynamic-chain, HitCapsule lineage, GuardSetOff
    onset, and swept-clank sub-owners:
    primary/cardinal one-step is now `699`; primary rollout first-mismatch is `423`;
    aggregate one-step is now `6785`; aggregate rollout first-mismatch is `2262` after the latest
    hidden-depth seed / matrix-first lbColl / late AttackAirHi latch cleanup.
    The bridge-enabled probe reached lower one-step totals, but rollout first-mismatch regressed
    (`426 -> 435` primary, `2318 -> 2340` aggregate), so that bridge is not closure-quality and must
    not be used to close this checklist item.
  - Current residual taxonomy (`reports/triage/final_split_taxonomy3/`):
    aggregate has no broad `F08b` bucket. After the current x58 enable-edge, grounded-depth,
    AttackAirN latch, and same-frame entry-pose cleanup
    (`reports/triage/current_final_taxonomy/`), the remaining
    former-F08b-shaped rows no longer include a parent-owned collision/pose bucket: `F10k=24`
    no-candidate action/hitbox timing and `F10l=23` selected-false action-timebase remain outside
    the primitive owner, while `F05b=8` accepted BODY damage-height rows move to damage selection.
    `F08h`, `F08i`, `F08f`, and `F08g` no longer emit after the hidden-depth seed,
    AttackAirN/AttackAirHi HitCapsule-latch, and matrix-first lbColl owners. Former special-entry
    rows are absorbed by existing `F10e_special_move_adjacency` with row proof. Cardinal taxonomy
    has `F10k=19` former-F08b-shaped rows and no current `F08h`/`F08i` rows.
  - Remaining `F08a` rows are source/instance recording order (`Fighter_8006A360` / Slippi post-frame
    identity), not damage admission. Remaining `F08c` rows are Down/Passive/Fall/Landing/mpColl
    state transitions. Remaining `F08d` rows are a mixture of timer scalar rows and contact
    over/missed rows already covered by the geometry/pose split. Remaining `F09a/F09c` rows are
    aerial hurtbox/state-flag or Jump/KneeBend/AttackAir action-entry owners. Remaining `F09b/F09d`
    rows are deal-hitlag, last-hit/combo, clank/shield/item/contact-adjacent bookkeeping without
    BODY percent/hitstun proof, not DamageFly continuation after an admitted BODY hit.
- Acceptance bar:
  - BODY-hit and aerial followup ownership is coherent
  - shared combat continuation paths replace row-shaped followup fixes
  - remaining timer/scalar, identity/bookkeeping, contact-geometry, and aerial action-entry/contact-hitlag
    residuals are fixed or proven outside the shared combat followup boundary without aggregate churn

### 6. Fox/Falco special-move families
- Status: `closed / aggregate special-owner tails burned down`
- Roadmap families: `F13_specialhi_landing` plus mixed special-move slices currently split across `F99`
- Owner boundary: `SpecialN`, `SpecialS`, `SpecialHi`, `SpecialLw`, `ThrownLw`, and their article/pulse/landing owners
- Primary sim files: `src/items.c`, `src/combat.c`, `src/anim_timebase.c`, `src/locomotion.c`, character-specific modules
- Completed so far in current pass:
  - The broad `F10e_special_move_adjacency` bucket is split into named special owners:
    `F19_specialn_blaster_article`, `F20_speciallw_shine_reflector`,
    `F21_specials_illusion_phantasm`, `F22_specialhi_firefox_firebird`,
    `F23_special_common_entry_dispatch`, and `F24_special_adjacent_instance_order`.
    `F10e` no longer emits in the refreshed primary or aggregate taxonomy.
  - `F13_specialhi_landing` is split so common `FallSpecial` / `LandingFallSpecial` /
    `EscapeAir` rows live in `F13a_common_fallspecial_landing`, while true Firefox rows live in
    `F22_specialhi_firefox_firebird`.
  - Throw-side blaster pulse rows are split out as `F14b_per_throw_pulse_bookkeeping` rather than
    staying in `F99_misc_other` or a B-special bucket.
  - Shine aerial-loop/end floor contact now uses a narrow locked-bottom collision subset for
    `SpecialAirLwLoop` / `SpecialAirLwEnd`, matching
    `ftFx_SpecialAirLw{Loop,End}_Coll -> ft_80081D0C -> AirToGround`. A broader experiment that
    also included Shine startup grounded too early and was rejected; the kept subset requires the
    frame-start action to already be Loop/End.
  - `SpecialAirLw* -> SpecialLw*` air-to-ground handoffs now refresh `jumps_left` through the
    `ftCommon_8007D7FC` owner. `SpecialHiFall -> SpecialHiLanding` does the same through
    `ftFx_SpecialHiFall_Enter`.
  - `SpecialLwEnd` / `SpecialAirLwEnd` anim-end exits now follow the decomp
    `ftCommon_8007DB24 -> ftCommon_8007D92C` path for the locked same-frame Wait/Fall IASA slices:
    grounded backward Turn and aerial double-jump. This is still an active F20 owner, not closure.
  - Shine release-lag latch now reads the pre-input snapshot for Anim-callback ownership, matching
    `Fighter_8006A360` before `Fighter_procUpdate`; current-frame inputs remain the IASA/entry
    owner. This fixes the aggregate B-release sentinel without widening SpecialLw entry.
  - Shine reflector item contact now calls the `ftFx_SpecialLwHit_Enter` callback on reflected
    laser overlap, transfers reflected item ownership through the item-owned reflect snapshot, and
    includes the projectile-origin segment for `ftColl_80077464` / `item->pos` overlap on Shine
    Loop states. `SpecialLwHit` / `SpecialAirLwHit` stay excluded so already-reflected owner
    projectiles do not re-enter the hit callback from a nearby origin overlap.
  - Shine Start hitlag rows with explicit replay-derived x1990 provenance preserve x198C=2 through
    the hitlag-exit frame. Rows with no x1990/x1994 provenance remain active rather than patched
    from replay reference.
  - Sustained `EscapeAir` ECB-lock floor-hug rows now stay airborne when vanilla keeps
    `ground_or_air=Air` while root Y is already at the floor bias. This is backed by Dolphin probe
    `reports/triage/20260418T081729Z_dolphin_forensic_row` and kept separate from the ordinary
    jump/air-dodge entry landing path.
  - `EscapeAir_Coll` now has a narrow one-step prev-ECB-bottom projection for rows that begin
    after the prior ECB bottom has already crossed the persisted floor line. This keeps the
    decomp `ft_80082C74 -> LandingFallSpecial` owner without restoring the rejected broad
    active-lock projection; rows whose locked previous ECB bottom is still above floor remain
    covered by negative replay-real sentinels.
  - `SpecialHiLanding_Anim` now enters Wait and lets destination Wait IASA consume same-proc
    Walk/Squat/Turn input, matching `Fighter_8006A360` Anim before `Fighter_procUpdate` input
    callbacks.
  - Pure `state_flags[4]` rows are split out to `F25_camera_box_visibility_x221f`; these are
    Slippi fp+0x221F camera-subject visibility rows owned by `ftLib_80086A8C` /
    `Camera_80030CFC`, not by the visible special-move action on the row.
  - `SpecialNEnd_Anim` now exits through Wait and lets the destination Wait IASA consume the
    buttonless forward Dash_CheckInput branch on the same proc. Earlier Wait_IASA button owners and
    the backward turn-smash branch remain outside this narrow F19 slice.
  - `SpecialAirNEnd_Anim` now exits through Fall and lets the destination Fall IASA consume the
    same-proc JumpAerial path when the blaster landing-lag attr is zero. This is limited to the
    `ftCo_JumpAerial_Enter_Basic` double-jump owner and does not restore the removed hidden
    `isBlasterLoop` / `cmd_vars[0]` attempt.
  - Shine Start x1988/x198C seed provenance is now explicit: seed history preserves hidden x198C=1
    when the entry script masks it with x1988=2, carries that provenance through hitlag-frozen
    frame-1 starts, and trusts explicit x1990+x1994 timer lanes on the Cliff/Fall ->
    SpecialAirLwStart row. Broad hidden-colanim clearing remains rejected.
  - SpecialN Loop -> Loop restart instance ordering now has a narrow seed override for the
    `ftFx_SpecialN_OnChangeAction -> ft_80089824 -> plAttack_80037B08` path only.
  - The audited broad special-adjacent hidden-prior / multi-consumer expansion was removed: it used
    `post_*[1:]` / `ref_t1` as an oracle and was not prefix-causal. Direct Landing/JumpF -> Shine
    and SpecialN Start -> Loop remain negative sentinels; only grounded locomotion order rows and
    SpecialN Loop -> Loop restart populate the override lane.
  - `F23_special_common_entry_dispatch` now only claims current seed/ref/out special-entry rows
    whose seed action is a real common special-dispatch caller. `KneeBend`,
    `LandingFallSpecial`, and active per-special states are excluded with taxonomy locks; the
    remaining F23 rows are `PassiveWallJump` / `JumpAerial` callers that reach
    `ftCo_SpecialAir_CheckInput`.
  - `F21_specials_illusion_phantasm` is closed in the refreshed taxonomy: pure aerial Side-B
    hurtbox tails move to `F09a`, Side-B combo/source tails move to `F09b`, and Side-B damage
    contact tails move to `F08f` rather than staying in the steady Side-B owner.
  - Pure `SpecialHiHoldAir` hurtbox-state tails are split to `F09a` because
    `ftFx_SpecialHiHoldAir_IASA` is empty and the rows have no launch/travel/collision fields;
    Firefox/Firebird collision, Bound, Fall, Landing, and launch rows stay in `F22`.
  - `ftFx_SpecialAirHi_Enter` consumes all jumps by writing `x1968_jumpsUsed = max_jumps`.
    Runtime now applies the matching `jumps_left=0` on HoldAir -> aerial launch entries; Bound/Fall
    cliff-catch and collision timing rows remain in `F22`.
  - Grounded Shine entry no longer dispatches from GuardOn/Guard/GuardSetOff/GuardReflect. Those
    IASA callbacks do not call `ftCo_800D68C0`; `GuardOff_IASA` remains allowed. This closes the
    replay-false GuardSetOff -> Shine Start hit cluster without restoring broad hidden-colanim or
    projectile-origin fallback logic.
  - Shine defender rows whose replay destination has entered common `Damage*` while the sim stays in
    the same `SpecialLw*` state now route to `F08c_damage_state_transition_adjacency`. Rows where a
    Shine hitbox is only the selected false BODY candidate route to `F08f_body_contact_candidate_filter_residual`,
    and grounded Shine-entry hitlag/source/state-flag bookkeeping routes to `F10b_grounded_combat_adjacency`;
    remaining F20 rows are active Shine/contact/callback bundles, not ordinary missed ProcessHit entries.
- Current residual taxonomy after forced dataset rebuild, source-port, Run/Dash/damage-air
  dispatch, Dolphin-backed EscapeAir floor-hug slice, Shine Loop projectile-origin reflector
  overlap, FallSpecial shallow floor projection, EscapeAir prev-ECB-bottom projection,
  SpecialHiBound rebound ownership, SpecialHiLanding destination-Wait IASA ownership,
  camera-visibility taxonomy split, SpecialNEnd destination-Wait Dash ownership, Shine x1988/x198C
  seed provenance, SpecialN loop-restart instance ordering, the F24 audit narrowing that removed
  non-prefix-causal special-boundary overrides, F24 direct-boundary hard movement to generic
  `ft_800895E0` / damage identity owners, the F21/F22/F23 taxonomy boundary hardening, and
  the pure-Shine source/hurtbox/state-flag/hitlag bookkeeping move to shared combat/state owners, AttackLw3 entry
  instance-callback ownership, AttackDash -> Shine allow-interrupt carry, F13a pure source
  hard-move, SpecialAirNLoop damage-scalar hard-move, pure ThrowHi/ThrowB/ThrowLw score-source
  movement to common throw bookkeeping, and common aerial B-before-A dispatch for JumpAerial /
  PassiveWallJump rows, GuardSetOff Shine-entry IASA gating, and SpecialHiHoldAir aerial-launch
  jump consumption
  (`reports/triage/final_special_primary`,
  `reports/triage/final_special_aggregate`):
  - Primary/cardinal one-step taxonomy: total `639`; special-owned residuals are
    `F24=0`, `F20=0`, `F19=0`, `F23=0`, `F22=0`, `F21=0`; common `F13a=0`,
    throw pulse `F14b=0`, camera visibility `F25=87`, and `F10e=0`.
  - Aggregate one-step taxonomy: total `5907`; named special-owned residuals are
    `F24=0`, `F20=0`, `F19=0`, `F22=0`, `F23=0`, `F21=0`; common `F13a=0`,
    throw pulse `F14b=0`, camera visibility `F25=373`, and `F10e=0`.
  - New owner slices:
    - Source attribution now carries `source_port0[player]`, because Slippi exports `last_hit_by`
      in the raw controller-port domain while simulator player arrays are compact local slots.
    - Common special dispatch now blocks Dash/RunBrake Neutral-B/Up-B while preserving Dash/Walk
      Side-B ordering. Decomp: `ftCo_Dash_IASA` checks `ftCo_SpecialS_CheckInput`, but not
      `ftCo_800D6824` / `ftCo_800D68C0`.
    - DamageAir / DamageFly aerial special dispatch is admitted only when `x221C_b6` is clear,
      matching `Damage_IASA` / `DamageFly_IASA` delegation into `Fall_IASA_Inner` /
      `DamageFall_IASA` and then `ftCo_SpecialAir_CheckInput`.
    - Run/RunDirect now preserves same-frame B-special dispatch before terminal RunBrake, matching
      `ftCo_Run_IASA` ordering and removing simulator-local RunBrake instance bumps.
    - `FallSpecial_Coll` shallow floor projection now enters `LandingFallSpecial` on rows owned by
      `ft_80083090` / `ftCo_80096D28`, while the regressive broad EscapeAir projection experiment
      remains rejected.
    - `EscapeAir_Coll` prev-ECB-bottom projection now covers the replay-real rows where the
      teacher-forced seed begins after mpColl's previous ECB bottom already crossed the persisted
      floor line. The gate deliberately excludes active-lock rows whose previous ECB bottom remains
      above floor, preserving the locked floor-hug negative rows.
    - `SpecialAirHi` collision now enters `SpecialHiBound`, and airborne `SpecialHiBound` anim-end
      enters `FallSpecial` while consuming jumps, matching `ftFx_SpecialHiBound_Enter` /
      `ftFx_SpecialHiBound_Anim`.
    - `SpecialHiFall` / `SpecialHiBound` exits into common `FallSpecial` now preserve fastfall across
      `ftCo_80096900`'s callsite-specific `Ft_MF_KeepFastFall`, fixing the SpecialHiFall ->
      FallSpecial `state_flags[1]` tail without broad state-flag rewrites.
    - Shine Start seed history preserves hidden x198C only when row history proves it is masked by
      x1988, and C reseed consumes that explicit lane without guessing from merged Slippi state.
    - Pure Shine-context `last_hit_by` / `last_attack_landed` / `combo_count`, hurtbox/state-flag,
      and hitlag-only rows now route to combat/aerial bookkeeping (`F10b`/`F09b`), shared
      state-flag owners (`F10d`/`F09a`), or contact-hitlag ownership (`F09d`) instead of the
      SpecialLw state machine. Grounded Shine-entry hitlag/source/state-flag bookkeeping also
      routes to `F10b`, and selected false BODY candidates from Shine hitboxes route to `F08f`.
      Mixed Shine contact/action bundles stay in `F20`.
    - SpecialAirNLoop rows that have already entered common `Damage*` and differ only on damage
      scalar/contact lanes now route to the shared damage timer/scalar owner (`F08d`) instead of
      `F19`; SpecialN article, item, shot, and loop/end handoff rows stay in `F19`.
    - Common FallSpecial/LandingFallSpecial rows that have already entered `Damage*`, plus pure
      ground-id and pure state/hurtbox tails, now route to shared damage/mpColl/state owners.
      Action/on-ground/jump landing bundles now route to `F10n_common_fall_landing_timebase` in
      the active taxonomy.
    - Sustained no-lock EscapeAir now keeps current-ECB sampling in the early same-action window,
      avoiding simulator-only early `LandingFallSpecial` while preserving the locked and later
      prev-bottom landing slices.
    - SpecialHi rows that have already entered `Damage*`, plus pure `SpecialHiLanding` ground-id
      tails, now route to shared damage/mpColl owners. Launch, Bound, Fall, and CliffCatch rows
      stay in `F22`.
    - `SpecialHiHoldAir` launch into `SpecialAirHi` consumes all jumps, matching
      `ftFx_SpecialAirHi_Enter`'s `x1968_jumpsUsed=max_jumps` write. Remaining F22 rows are
      collision, Bound/Fall, and cliff-catch timing.
    - Slow ledge options now refresh ledge occupancy, so `SpecialHiFall` cannot CliffCatch an
      already occupied ledge when the other fighter is in `CliffClimbSlow` / `CliffAttackSlow` /
      `CliffEscapeSlow` / `CliffJumpSlow1`. Unoccupied `SpecialHiFall -> CliffCatch` rows remain
      covered by positive replay-real locks.
    - GuardOn/Guard/GuardSetOff/GuardReflect no longer admit grounded Shine entry because their
      IASA callbacks do not call `ftCo_800D68C0`; `GuardOff_IASA` remains the shield-exit branch
      that can dispatch specials. This removed the replay-false GuardSetOff -> Shine contact row
      cluster while preserving rejected broad hidden-colanim and projectile-origin exclusions.
    - Pure SpecialN hurtbox and source/scoreboard rows now route to shared aerial state/combat
      owners (`F09a` / `F09b` / `F10b`); action, article, shot, and loop/end bundles stay in `F19`.
    - Rows where SpecialN/Shine is only the previous action and the current row is generic
      Landing/Ottotto/Fall or grounded source bookkeeping now route to shared mpColl/combat owners;
      active SpecialN loop/end handoff rows and mixed Shine contact/action rows stay in
      `F19`/`F20`.
    - Pure ThrowHi/ThrowB/ThrowLw score/source rows now route to common throw/item bookkeeping
      (`F14`), while mixed hitlag/state-flag/hurtbox/article pulse bundles remain in `F14b`.
    - Common aerial IASA ordering now leaves B-edge rows for Shine/Blaster before AttackAir,
      matching `ftCo_SpecialAir_CheckInput` before `ftCo_AttackAir_CheckItemThrowInput`; this
      closes the aggregate-only `F23` PassiveWallJump / JumpAerial rows.
    - SpecialN Loop -> Loop restart instance rows use a narrow `ft_80089824` seed override keyed to
      same-action loop restarts with action-frame reset. Runtime loop/end selection now combines
      extracted `cmd_vars[0]` windows with the seeded `x67D` B timer, rather than broad unbounded
      button-timer inference; terminal Loop rows whose B press happens outside the hidden
      command-variable/latch window enter End.
    - `SpecialAirNLoop_Anim -> SpecialAirNEnd` can still run the entered End collision callback in
      the same proc. Deep under-floor one-step rows now project through `AirCatchHit_Coll` into
      ordinary Landing, preserving both air and ground self-X lanes on Landing entry.
    - `LandingFallSpecial` with the hidden allow_interrupt lane set can dispatch grounded Shine via
      the shared `ftCo_Landing_IASA -> ftCo_800D68C0` chain.
    - SpecialLw Loop/Turn/End IASA-owned JumpAerial handoffs suppress immediate same-frame
      destination JumpAerial B-special re-entry, matching the single input-callback ownership of
      `ftFx_SpecialAirLwLoop_IASA` / `ftFx_SpecialAirLwTurn_IASA` / `ftFx_SpecialAirLwEnd_Anim`.
    - `SpecialHiBound_Enter` entry rows remain airborne because the Enter helper does not call
      `ftCommon_8007D7FC`; later `SpecialHiBound_Coll` owns ground conversion.
    - F24 audit narrowing removed the broad special-adjacent multi-consumer / hidden-prior
      override path because it depended on `ref_t1` post-frame ids. The kept seed lane is scoped to
      the prior grounded locomotion ordering owner plus the SpecialN loop-restart callback owner.
    - Direct special-boundary pure `instance_id` rows are no longer assigned to F24 unless a
      source-backed special callback is visible. They move to generic adjacent instance ordering
      (`F12b`) or damage identity (`F08a`) because the owner is the shared
      `ft_800895E0` / `plAttack_80037B08` counter surface, not a B-special state-machine callback.
- Remaining residuals:
  - `F20_speciallw_shine_reflector`: zero in primary and aggregate after pure source,
    hurtbox/state-flag, contact-hitlag, grounded Shine-entry contact/instance bookkeeping, and
    selected false BODY-candidate tails moved to shared combat/contact owners, and the
    LandingFallSpecial allow_interrupt -> Shine IASA row plus the SpecialAirLwTurn -> JumpAerial
    same-frame B/up re-entry row are fixed. The early startup-grounding bridge and broad aerial
    projectile-origin reflector fallback were rejected and are locked against.
  - `F23_special_common_entry_dispatch`: zero after common aerial B-before-A dispatch; `KneeBend`,
    `LandingFallSpecial`, and active special states remain excluded by tests.
  - `F22_specialhi_firefox_firebird`: zero in primary and aggregate after the occupied-ledge
    `SpecialHiFall -> CliffCatch` cluster, rebound entry on-ground false positives, and
    SpecialHi jump-consumption rows were fixed. The remaining Bound-collision action-frame rows
    moved to shared mpColl/landing timing (`F10c`), pure Bound `jumps_left` rows moved to the
    shared aerial state/bookkeeping owner (`F09a`), and the grounded KneeBend -> SpecialHiHold tail
    moved to grounded selector adjacency (`F10a`).
  - `F19`: zero in primary and aggregate after SpecialAirNLoop -> Damage scalar rows moved to
    `F08d`, deep under-floor Loop->Landing handoffs were fixed, pure SpecialN hurtbox/source tails
    moved to shared state/combat owners, and the active Loop restart latch was narrowed to a
    data-backed `cmd_vars[0]`/`x67D` inference with a terminal latch tail. The broader unbounded
    SpecialN `cmd_vars[0]`/button-timer hardening remains rejected.
    `F21` is zero in the refreshed taxonomy after
    Side-B hurtbox/bookkeeping/contact tails were hard-moved to their shared non-Side-B owners.
  - `F24`: zero after hardening. Source-backed special callback rows stay eligible for F24, but
    the remaining direct special-boundary pure instance rows are generic instance-order or damage
    identity owners; the non-prefix-causal broad special-boundary override remains rejected.
  - `F13a` and `F14b` are zero after the remaining common FallSpecial/LandingFallSpecial/EscapeAir
    landing/action-frame rows moved to shared collision/landing timing (`F10c`) and the remaining
    action-aligned throw hitlag/source/contact tails moved to common throw/item bookkeeping (`F14`).
    `F15`, `F16`, and `F17` remain guard/item, item identity, and collision/ledge owners
    respectively, outside this special-family checklist item.
  - Pure `F25_camera_box_visibility_x221f` rows are not special-move residuals; they are kept under
    the state-flag/camera owner until that owner is closed.
- Acceptance bar:
  - special moves are grouped by decomp owner, not patched row-by-row
  - `SpecialHi` landing/fall and `SpecialLw` / `ThrownLw` pulse behavior live in named family work, not misc buckets

## Remaining Major Owner Families

### Combat geometry / HSD pose collision owner
- Status: `effectively closed; remaining former-F08b-shaped rows moved to action-timebase, damage-selection, or special-entry owners with row-level proof`
- Roadmap family: former `F08b_body_contact_geometry_residual` no longer emits as one broad bucket
  in the refreshed taxonomy, and no parent-owned collision/pose residual bucket remains.
  The implemented runtime/seed sub-owners cover the shared BODY primitive surface: SSDYNN01
  dynamic-chain collision pose (Fox JumpB/LandingFallSpecial/AttackHi3 owner predicate), Turn internal-facing hurtcaps, per-HitCapsule `victims_1`
  preservation, GuardSetOff onset lineage, swept/same-group clank, Escape floor-edge pose
  selection, same-frame/enable-edge HitCapsule x58/x4C continuity, CliffAttack hitbox extraction,
  enable-edge phantom/tip-log handling, narrowed AttackAirN dense-latch preservation,
  same-frame common locomotion entry pose carry, matrix-first lbColl BODY admission, and late
  AttackAirHi HitCapsule-latch preservation, and prefix-causal grounded-overlap hidden-depth seed
  reconstruction. Remaining former-F08b-shaped rows are assigned outside this parent owner:
  `F10k_body_no_candidate_action_timing` (24 aggregate field rows; no pre-combat BODY candidate,
  action/hitbox timing), `F10l_body_selected_false_action_timebase` (23; selected false BODY only
  after pre-BODY action divergence), `F05b_damage_hurt_height_selection_residual` (8; BODY accepted
  in replay and sim, but the hurt-height damage-state selector differs), and existing
  `F10e_special_move_adjacency` for special-entry rows.
- Owner boundary: live fighter collision primitives before BODY admission:
  `ftColl_80078C70` / `ftColl_80076ED8`, `lbColl_8000805C` / `lbColl_80006E58`,
  `lb_8000B1CC`, and the HSD JObj/AObj/dynamics pose state feeding hurtcap endpoints.
- Primary sim/tool files: `src/hurtboxes.c`, `src/hitboxes.c`, `src/combat.c`,
  `tools/extraction/extract_fighter_anims.py`, `tools/dolphin/*`.
- Proof rows:
  - `BHH:1599`: vanilla pre-ftColl probe selects p0 `AttackAirN` hb1 against p1 AttackHi3
    hurt part 18 and enters `DamageFlyN`. Current implementation work routes the row through
    extracted `ftData.x2C` data and fixed-capacity runtime dynamic-node state before
    `hurtboxes_refresh()`, while BODY admission still uses the ordinary `ftColl_80078C70` /
    `lbColl_8000805C` path. The runtime update follows the supported `lb_8001044C`
    segment-vector owner: previous child position, current animation segment vector, descriptor
    follow/down/cone/decay constants, and carried correction axis/angle produce the next
    dynamic-chain collision matrix. Dynamic collision applicability is an extracted `SSDYNN01` v4
    owner predicate, not a C-side raw `msid=58` gate, and sequential state carry uses a separate
    state-valid flag from the collision-apply flag. The rejected hardcoded runtime primitive
    overlay, generated one-slice overlay, broad grounded-attack bake, and matrix-primary /
    pose-order probe remain documented as rejected bridges.
  - `BHH:1163`: standing Turn internal-facing hurtcaps are implemented and protected.
  - `BHH:1169`, `TBK:5523`, `TBK:5247`, AGN/body-overlap rows remain negative/adjacent sentinels.
- Reproducibility:
  - Main-repo patch artifact `tools/dolphin/patches/ishiiruka_collision_probe.patch` contains the
    local Dolphin interpreter probe used to capture pre-`ftColl` primitives. Apply it only for
    local forensics; `refs/Ishiiruka` should remain clean in the review worktree.
- Acceptance bar:
  - the shared `ftCo_8009DD94` -> `lb_8001044C` dynamic-node update computes BODY contact
    selection for supported Fox/Falco rows from extracted
    `ftData.x2C` descriptors and runtime pose state, not replay-proof admission bridges or
    cap/frame runtime injections;
  - dynamic-pose ownership lives in extraction/runtime pose data, not replay-proof admission,
    C constants, manual one-slice overlays, fitted heuristic gates, or broad static bakes;
  - reseed behavior is internally coherent for the supported dynamic-chain surface: non-sequential
    one-step seeds reconstruct the deterministic action-local dynamic state by replaying the
    dynamic update from frame 0 to the seeded integer animation frame, while sequential rollout
    carries runtime state frame-to-frame even when the current frame does not substitute a dynamic
    collision matrix. The replay is `O(action_frame)` on reseed/pre-combat reconstruction only,
    not on normal sequential rollout;
  - protected BODY overlap sentinels and validation reports remain non-regressing.
- Completed sub-owner and residual boundary:
  - the broad grounded-attack dynamic-descriptor bake is rejected; regression artifacts live under
    `reports/triage/20260416_hsd_dynamic_regression_broad/` and show new Fox msid-59 BODY false
    negatives (`AGN:1471`, `QGD:7589`, `TBK:870`);
  - the Fox JumpB/AttackHi3 / SSDYNN01 dynamic-chain collision-pose sub-owner covers the target-domain
    one-set data surface without widening BODY
    admission. Refreshed validation for the current local implementation is non-regressing relative
    to the starting review reports: primary/cardinal one-step `715 -> 699`, primary rollout
    `425 -> 423`, aggregate one-step `7962 -> 6785`, aggregate rollout `2313 -> 2262`;
  - hidden HitCapsule shield/body lineage is closed for the observed same-group shield/body
    continuation surface. `PRH:1830..1834`, `IAT:11146..11147`, and `BHH:1803..1804` are
    suppressed by authoritative per-HitCapsule `victims_1` seed lanes, including the GuardSetOff
    onset case where replay-visible shield damage follows a non-Guard previous visible action.
    This is seed preservation for real hidden HitCapsule state, not replay-proof BODY admission.
  - hitbox-vs-hitbox clank overlap now uses the decomp swept HitCapsule predicate
    (`lbColl_80007AFC` -> `lbColl_80006094`) over x58->x4C segments instead of current-center
    sphere/sphere overlap, including degenerate `x58 == x4C` create/enable-edge capsules as
    point-vs-segment tests. It then follows the same-group HitVictim refresh from
    `ftColl_80078C70` / `ftColl_8007699C` / `inlineA0` / `inlineA1`: the first accepted same-group
    clank owns the group's hitlag/rebound damage for that fighter pair, and later same-group clank
    candidates plus BODY admission are suppressed by the refreshed HitVictim entries. The clank path
    also ignores replay-reconstructed BODY victim rings as a prefilter, because those rings are seed
    reconstruction for BODY admission and can otherwise mask a live hitbox-vs-hitbox clank; the
    HHG:8674 and FSP:467 locks protect this distinction. This removes the ReboundStop/clank split
    and FSP:467 hitlag residual without a BODY admission bridge.
  - the parent former `F08b_body_contact_geometry_residual` bucket is split into concrete
    non-parent owners rather than hidden under new BODY labels. Current aggregate residual map is
    `F10k=24`, `F10l=23`, and `F05b=8`, with former special-entry rows absorbed by existing
    `F10e`. The split is generated by `tools.eval.mismatch_taxonomy` using
    `debug_step_input_pre_combat` contact counts, selected primitive metadata, and replay
    destination shape. Rows with no pre-combat BODY candidate are action/hitbox timing
    (`FSP:5765`, `PPA:3182`); rows whose victim/action state already diverges before BODY
    admission are action-timebase (`FSP:4852`, `HIS:5950`); rows where replay and sim both accept
    BODY but choose different `DamageFly*` height are damage-selection (`HIS:5029`, `IAT:6288`).
    `F08h`, `F08i`, `F08f`, and `F08g` no longer emit in the refreshed aggregate taxonomy.
  - Additional completed sub-owners in the current patch:
    - Escape roll floor-edge snap now keeps `EscapeF` / `EscapeB` / `EscapeN` floor-owned through
      the `ftCo_Escape_Coll -> ft_80084104 -> ft_800827A0 -> mpColl_8004B2DC` path before BODY
      pose sampling. `POY:4476` protects the EscapeB FD edge case (`DamageN3`, not a Fall-pose
      `DamageAir*` hit).
    - Same-frame motion-entry HitCapsule x58/x4C continuity no longer synthesizes previous-action
      sweeps for newly created capsules; `ftAction_8007121C` plus `ftColl_8007AD18` initialize
      `x58 = x4C` on the refreshed current pose. The current pass extends that rule to
      teacher-forced per-hitbox enable edges without a motion-state transition at the direct
      pre-combat selector level; `BHH:3661` continues to protect the positive ledge-attack side.
    - CliffAttackSlow/Quick hitboxes are extracted for both Fox and Falco as common motion-state
      data. The fix is not a Fox-only validation gate: `BHH:3661` is covered by real ledge-attack
      HitCapsule extraction, and `GAT:8224` is covered by invincible/no-damage HitCapsule.victims_1
      lineage preservation from `ftColl_80076ED8` before the vulnerable damage guard.
    - Enable-edge BODY phantom/tip-log handling now routes airborne-victim newly enabled capsules
      through the decomp `ftColl_80076ED8` `0 < coll_distance < x7A8` lane, using the matrix-radius
      `lbColl_8000805C` helper and preserving sustained near-threshold damage sentinel `QGD:8332`.
    - The grounded player-overlap Z-depth runtime lane now consumes extracted
      `p_ftCommonData->x454` / `x458` and mirrors the normal `ftCommon_8007E0E4` step/decay/clamp
      before collision refresh. `HHG:6740` protects the AttackDash/KneeBend false BODY regression.
      The retained seed reconstruction is prefix-causal and limited to ordinary grounded overlap
      frames from current-row action/stocks/facing/position plus extracted `x454`/`x458` and
      character pushboxes. Airborne, damage, GuardSetOff, DownBound, Cliff, throw, and special
      rows keep replay-visible `pos_z` with no stale hidden-depth carry, avoiding the rejected
      broad hidden-depth reconstruction. `TCH:5649` protects the formerly false AttackDash BODY
      row whose vanilla probe shows hidden Z-depth separation before `lbColl_80006E58`.
    - The taxonomy split records the victim post-timebase/pre-combat action, so rows already in a
      different pre-combat action move to the timebase split instead of the exact lbColl split.
    - The narrowed AttackAirN dense-latch owner keeps `HitCapsule.victims_1` suppression only for
      the Wait entry lane proven by `HIS:2752`, while `HIS:2753` and `HIS:3126` still clear stale
      dense fallback entries and admit the live late NAir hit. This removes `F08f` without a
      replay-proof BODY admission path.
    - Same-frame locomotion entry hurtcaps now carry the previous interpreted JObj pose for BODY
      collision on WalkSlow->Wait, Dash->Run, and SquatRv->WalkSlow entries when the new state was
      entered after `Fighter_8006A360` and did not immediately call `ftAnim_8006EBA4`; `DCC:8844`,
      `IAT:6287`, and `TCH:5842` protect the owner.
  - Matrix-first lbColl BODY admission now runs `lbColl_8000805C` / `lbColl_80006E58` when
    hurt-bone pose data is available, leaving the simple sphere/capsule overlap as a missing-data
    fallback only. `PPA:2614` protects the grounded `AttackAirB` vs `AttackHi3` positive row.
  - Late AttackAirHi HitCapsule-latch preservation keeps the UpAir late-window victims_1 latch
    across stale BODY attribution and same-frame Wait instance-id proxy changes. `FSP:7079`
    protects the former aerial selected-false row.
  - Current aggregate split after those cleanups is `F10k=24`, `F10l=23`, and `F05b=8` field rows,
    with former special-entry rows assigned to `F10e`. These remaining rows are outside the parent
    BODY primitive owner by row-level debug/probe evidence, so the combat geometry / HSD pose
    collision owner is effectively closed without a replay-proof BODY admission bridge.

### Ledge / collision-env parity
- Status: `closed`
- Owner boundary: ledge occupancy, refresh, grab-mask ownership, FD edge behavior
- Primary sim files: `src/mpcoll_env.c`, `src/ledge.c`, `src/step.c`
- Acceptance bar:
  - edge behavior owned by collision substrate rather than action-local exceptions
- 2026-04-19 pass:
  - Closed the ledge callback / occupancy sub-slice for current suite evidence, but **did not close**
    the whole checklist item. Remaining `F17` / `F10c` rows are still active collision/contact
    residuals until split or fixed.
  - MissFoot (`0x00FB`) is now named and allowed to consume `Collide_LedgeGrabMask` through
    `ftCo_MissFoot_Coll -> ft_80082F28 -> ftCliffCommon_80081298`.
  - Slow ledge options share the quick-option ledge ownership path: percent threshold `x488`
    selects slow/quick for CliffClimb/Attack/Escape/Jump1, slow Jump1/Jump2 are handled, and slow
    options use the same attach snap / air-to-ground transfer as quick options.
  - CliffCatch terminal rows can consume immediate CliffWait IASA attack/escape/jump options in the
    same proc, but climb/drop remains blocked on that same-proc handoff because `ftCo_8009A804`
    initializes `mv.co.cliff.x8 = 0`.
  - Cliff x1990 invulnerability is entry-owned: reseed trusts explicit replay-derived x1990, and
    runtime refresh only recreates x1990 on CliffCatch/CliffWait entry frames, so terminal
    CliffWait vulnerable rows are not re-intangibled.
  - CliffWait Z input maps through fighter input synthesis to `HSD_PAD_A` and routes to
    CliffAttack before LR-lane escape. Raw digital L/R edges no longer bypass the synthesized LR
    lane when analog trigger was already held.
  - One-step reseed applies a narrow ordinary-Fall ledge-cooldown alignment tick so Fall-family
    rows where x2064 reaches zero before mpColl can still CliffCatch, while SpecialHi cooldown
    behavior remains on its explicit seed lane.
  - Rejected unsafe attempt: globally reducing derived `ledge_cooldown` by one tick fixed
    `PositiveRevolvingHyena:8072` but reopened `F22_specialhi_firefox_firebird` by allowing
    `SpecialHiHoldAir -> CliffCatch` false positives, so it was not kept.
  - Fresh taxonomy after this pass:
    - primary: total `637`, `F17=37`, `F10c=33`
    - aggregate: total `5647`, `F17=276`, `F10c=310`, `F22=0`
  - Remaining residual buckets:
    - `F17`: mostly damage/passive contact substrate and narrow Cliff option ground/hurtbox tails
    - `F10c`: EscapeAir/Fall/LandingFallSpecial timing, Ottotto/edge landing handoffs, and grounded
      Cliff option end -> Wait/locomotion IASA tails
  - Continuation retained fixes:
    - CliffClimb/CliffAttack/CliffEscape terminal rows now enter the Wait-like
      `ftCommon_8007D92C` destination and consume the same-proc Wait IASA grounded tail
      (grounded attack / guard / jump / dash / squat / turn / walk). This removes aggregate
      Cliff option end -> Wait/locomotion IASA rows without changing the earlier CliffCatch
      climb/drop exclusion.
    - Same-action DownDamage floor-contact rows that already reached ground now apply the same
      transfer helper; rows that miss floor contact remain in the mpColl substrate bucket.
    - Airborne DownBound rows can refresh persisted `floor.index` across connected FD floor seams
      while staying airborne, matching the `ft_80082708 -> mpColl_8004B108` ownership shape.
    - Same-action DamageAir floor-contact rows now refresh the replay-visible jump count without
      running the full air->ground transfer helper, preserving grounded DamageAir fastfall
      state-flags rows.
    - Ottotto FD-edge ownership now includes Walk/Landing-style `ft_80084280` teeter admission and
      the immediate L-stick Ottotto -> KneeBend edge-loss handoff through KneeBend_Coll.
  - Rejected unsafe attempts in the continuation:
    - Widening EscapeAir ledge-floor projection fixed one deep row but worsened primary F10c
      (`33 -> 46`), so it was reverted.
    - Full same-action DamageAir air->ground transfer fixed F17 jump rows but regressed the existing
      grounded DamageAir fastfall/state-flags lock, so only the jump-count refresh was retained.
    - A broad non-hitlag DamageFly root projection fixed some Passive/PassiveStand rows but exploded
      F17 (`primary 27 -> 1291`, `aggregate 232 -> 5345`), so it was reverted.
  - Fresh taxonomy after retained continuation:
    - primary: total `634`, `F17=33`, `F10c=33`
    - aggregate: total `5598`, `F17=254`, `F10c=286`
  - Fresh taxonomy after the Ottotto/DamageAir continuation:
    - primary: total `619`, `F17=27`, `F10c=24`
    - aggregate: total `5560`, `F17=232`, `F10c=269`
  - Taxonomy split retained in this pass:
    - Historical split note: Common EscapeAir/FallSpecial/LandingFallSpecial action/on-ground
      bundles moved out of ledge ownership because they are common landing/freefall timing, not
      ledge occupancy or CliffCatch edge ownership. In the active taxonomy these rows now live in
      `F10n_common_fall_landing_timebase`. Lone `ground_id` tails stay in `F10m`.
    - DamageFly/Passive/DownBound/DownDamage/PassiveWallJump action bundles now move to
      `F08c_damage_state_transition_adjacency`; these are damage/knockdown continuation owners, not
      ledge occupancy rows. `F17` is reserved for persistent floor-line identity (`floor.index`) in
      Damage* or Cliff option contexts.
    - Landing/Ottotto action-selector-only tails now move to `F10a_grounded_selector_transition`;
      pure hurtbox/state/source tails move to `F10d`, `F09a`, `F09b`, or `F10b` as appropriate.
    - SpecialAirHi <-> Bound callback timing was audited after initially moving it to
      `F22_specialhi_firefox_firebird`; that reopened section 6 (`aggregate F22=18`), so it was
      restored to shared collision/landing owner `F10c_collision_landing_edge_adjacency`.
  - Fresh taxonomy after F17/F10c owner split and F22 restoration:
    - primary: total `619`, `F17=1`, `F10c=16`
    - aggregate: total `5560`, `F17=5`, `F10c=111`
    - `F22_specialhi_firefox_firebird`: primary `0`, aggregate `0`
    - `F13a_common_fallspecial_landing`: primary `0`, aggregate `118`
  - Status after F17/F10c owner split: still **active**, not closed. Remaining `F17` rows are
    floor-line identity tails (`DamageAir3` / `DamageN1` / Cliff option `ground_id`). Remaining
    `F10c` rows are generic Fall<->Landing phase, Ottotto/FD-edge handoffs with
    `on_ground`/`jumps_left`, SpecialAirHi <-> Bound shared collision timing, and pure `ground_id`
    floor-line tails.
  - Final closure pass:
    - Retained runtime fixes: Ottotto IASA now consumes crouch through `ftCo_800D5FB0`, and
      terminal Ottotto enters OttottoWait through `ftCo_Ottotto_Anim -> ftCo_8009A6B8`. Later
      followup adds the ordinary `ftCo_Turn_CheckInput` tail and data-backed Ottotto-walk
      `p_ftCommonData->x474` threshold. Replay-real locks cover `HilariousVillainousGiraffe:5095`,
      `TubbyCurlyHerring:10089`, and `PriceyPartialAlbatross:1597/1598`, with existing
      AttackDash/run walk-off sentinels guarding against a broad teeter gate.
    - Rejected unsafe attempt: removing the local Ottotto edge-entry action-age/down-stick gate
      fixed the intended Wait/Landing teeter rows but regressed `AGN:6301` AttackDash->Fall and
      run-off/fall-fast walk-off sentinels, so it was narrowed back out.
    - Pure `ground_id` rows moved to `F10m_floor_line_identity`: this is exact
      `CollData.floor.index` visibility across connected FD seams, not ledge occupancy, ledge
      grab-mask, CliffCatch, or landing admission.
    - Generic `Fall` <-> `Landing` phase rows moved to
      `F10n_common_fall_landing_timebase`, owned by `ftCo_Fall_Coll` /
      `ftCo_Landing_Enter_Basic` callback timing.
    - Remaining common teeter edge handoff rows moved to `F10o_ottotto_teeter_edge_handoff`, the
      precise `ftCo_8009A3C8` Ottotto owner. They no longer hide under the generic collision-env
      bucket, and the broad runtime shortcut is explicitly rejected above.
    - `SpecialAirHi` <-> `SpecialHiBound` timing rows moved to
      `F10p_specialhi_bound_collision_callback`, preserving the closed section-6 state-machine
      owner (`F22=0`) while still documenting the shared collision callback blocker.
  - Fresh taxonomy after final closure split:
    - primary: total `619`, `F17=0`, `F10c=0`, `F22=0`, `F13a=0`,
      `F10m=6`, `F10n=12`, `F10o=0`, `F10p=0`
    - aggregate: total `5550`, `F17=0`, `F10c=0`, `F22=0`, `F13a=118`,
      `F10m=31`, `F10n=38`, `F10o=24`, `F10p=18`
  - Closure rationale: no remaining row is assigned to the generic ledge/collision-env owner
    buckets. The former residuals are either implemented (`Ottotto` crouch/anim-end) or split to
    exact adjacent owners with row-level taxonomy tests and decomp citations. Ledge occupancy,
    refresh, grab-mask ownership, CliffCatch/no-CliffCatch, and FD floor-line identity are no
    longer mixed in `F17`/`F10c`.

### Item-owner seed cleanup + item identity family
- Status: `closed`
- Roadmap families: `F14_throw_item_bookkeeping`, `F15_guard_item_ownership`, `F16_item_identity_residual`
- Owner boundary: item owner/slot identity, throw-item bookkeeping, guard-hit item attribution, seed/runtime ownership split
- Primary sim files: `src/items.c`, `src/combat.c`, `src/api.*`, `tools/slippi/seed_history.py`
- Acceptance bar:
  - item rows stop masquerading as combat/guard rows
  - remaining item identity behavior is deterministic residual cleanup
- Retained item-owner slices:
  - Item-owner taxonomy split:
    - Coarse `F14/F15/F16` no longer emit in refreshed taxonomy. They are split into:
      `F14c_throw_article_lifetime`, `F14d_throw_source_scoreboard`,
      `F15a_reflect_owner_transfer`, `F15b_guard_laser_lifetime`,
      `F16a_item_slot_compaction_identity`, `F16b_blaster_article_identity`,
      `F16c_illusion_phantasm_lifetime`, and `F16d_item_body_lifetime`.
    - Fresh taxonomy after split:
      - primary: total `619`; old `F14=0`, `F15=0`, `F16=0`;
        `F14c=14`, `F14d=2`, `F15a=11`, `F15b=12`, `F16a=0`,
        `F16b=0`, `F16c=0`, `F16d=31`
      - aggregate: total `5550`; old `F14=0`, `F15=0`, `F16=0`;
        `F14c=484`, `F14d=76`, `F15a=21`, `F15b=130`, `F16a=2`,
        `F16b=100`, `F16c=58`, `F16d=221`
    - Section-6 and ledge/collision-env closures remain closed in the refreshed split:
      old `F17/F10c/F19/F20/F21/F22/F23/F24/F10e` do not emit in primary or aggregate.
  - Guard laser shield-bounce gate now keeps `shield_bounced` only for already-aged high-shield
    glancing contacts while lower-shield fresh GuardSetOff contacts take `HitShield` destruction.
    `GAT:773` is locked in `tests/test_laser_shield_contact_replay_real_locks.py`; existing
    bounce keepalive controls `GAT:2276` and `GAT:5280` stay live.
    - Primary taxonomy after this runtime slice: total `615`; `F15b=8` (down from `12`), while
      `F14c=14`, `F14d=2`, `F15a=11`, `F16d=31` are unchanged. Aggregate remains total `5550`;
      `F15b=130` and the other named item subfamilies are unchanged.
  - Illusion/Phantasm article lifetime now advances through BODY-hit victim hitlag and preserves
    the frame-start Side-B owner for same-step exit lifetime ticks. Aggregate taxonomy after this
    runtime/seed slice: total `5492` (down from `5550`); `F16c=0` (down from `58`) while
    `F14c=484`, `F14d=76`, `F15a=21`, `F15b=130`, `F16a=2`, `F16b=100`, and `F16d=221` are
    unchanged. Primary remains total `615`; the named primary item buckets are unchanged from the
    shield-bounce slice. Replay-real locks live in
    `tests/test_illusion_body_hit_replay_real_locks.py` for `DCC:4762`, `DCC:4764`, `HVG:6975`,
    and `IAT:7712`.
  - Blaster gun lifetime now clears stale SpecialNEnd gun articles when the owner leaves the
    SpecialN family. Decomp owner: `ftFx_SpecialNEnd_Anim` clears
    `fp->fv.fx.x222C_blasterGObj`; `itFoxblaster_UnkMotion8_Anim` observes that NULL pointer with
    `ftFx_SpecialN_CheckRemoveBlaster` and clears the item. Throw-side gun lifetime remains under
    `ftFx_Throw_Anim` `cmd_vars[1]`. Aggregate taxonomy after this slice: total `5425` (down from
    `5492`); `F16b=52` (down from `100`), `F16d=209` (down from `221`), `F15b=124` (down from
    `130`), `F16a=1` (down from `2`), while `F14c=484`, `F14d=76`, `F15a=21`, and `F16c=0` are
    unchanged. Primary remains total `615`; item buckets remain `F14c=14`, `F14d=2`,
    `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
    Replay-real locks live in
    `tests/test_blaster_gun_damage_exit_stale_clear_replay_real_locks.py` for `BHH:666`,
    `PRH:8222`, `PRH:2460`, and `IAT:140`.
  - Laser BODY disabled-contact ownership now lets hit-status `1`
    (`HurtCapsule_Disabled`) consume the projectile without applying fighter damage. Runtime
    admits the BODY geometry path for disabled hurtcaps, then clears the laser before
    `Fighter_ProcessHit` bookkeeping. Replay-real locks live in
    `tests/test_laser_disabled_contact_replay_real_locks.py` for `PRH:4757`, `PRH:4778`, plus
    vulnerable non-flinch negative control `TBK:197`.
    Fresh taxonomy after this slice:
    primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`,
    `F16c=0`, `F16d=31`.
    Aggregate total `5401`; `F14c=480`, `F14d=76`, `F15a=21`, `F15b=124`, `F16a=2`,
    `F16b=35`, `F16c=0`, `F16d=205`.
    Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Pure blaster-gun `item_instance_id` rows are now hard-moved out of the item-owner bucket when
    the item article identity already matches. Source owner: item `xDA8_short` is copied from the
    owner fighter's `fp->x2088` in the generic item spawn path, so pure gun xDA8 rows belong to
    adjacent fighter instance-counter ordering (`F12b`), not `F16b` blaster article lifetime.
    Replay row evidence: `BHH:94`, `HIS:3056`, `IAT:2847`, `MAJ:5759`, `PRH:2441`, and
    `TCH:7410`. Fresh aggregate taxonomy after this hard move keeps total `5401` but moves 9
    pure-xDA8 rows from `F16b` to `F12b`: `F16b=26` (down from `35`), while primary remains
    total `611` and primary `F16b=0`.
  - ThrowHi first-pulse BODY carry is now narrowed to the current-frame first
    `set_throw_spawn_projectile` pulse when the already-damaged victim is on the non-projectile
    X side of the first-pulse laser segment. This preserves the fresh throw article and prevents
    false combo/source bookkeeping, while front-side victims still consume through the normal
    BODY path. The gate is tied to the throw-side laser velocity produced by the decomp
    hold-joint launch vector, not replay row identity.
    Replay-real locks live in
    `tests/test_throwhi_first_pulse_body_carry_replay_real_locks.py` for `BHH:527`,
    `BHH:1206`, front-side negative control `BHH:480`, and mid-pulse negative control
    `BHH:937`.
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5393`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=2`, `F16b=26`, `F16c=0`,
    `F16d=205`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - ThrowHi crossed-prev frame-20 article spawn now reconstructs the missing command-cursor
    ownership for a narrow ThrowHi slice. `ftAction_80073354` can advance across the frame-20
    `set_throw_spawn_projectile` command and `ftFx_Throw_Anim` can consume `throw_flags_b0` in the
    source frame; Slippi does not expose that consumed cursor, but the prefix-causal
    `throw_pulse_crossed_prev_frame` lane records the frame-20 crossing. Runtime re-emits only when
    the owner has no live state1 throw shot, leaving existing carry/despawn rows on the normal
    lifetime owner.
    Replay-real locks live in `tests/test_throwhi_pulse_seed_bridge_replay_real_locks.py` for
    positive rows `BHH:938`, `BHH:1673`, `BHH:4337`, with negative control `BHH:1208`;
    `BHH:937` moved to the later same-character item callback phase.
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5192`;
    `F14c=405`, `F14d=67`, `F15a=21`, `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=116`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Falco ThrowHi frame-24 carried BODY contact is now suppressed only for state1 ThrowHi lasers
    on current or crossed-prev frame 24 during the high-hitlag carry phase, with replay damage
    provenance already cleared (`last_attack_landed==0`). This keeps the final Falco throw pulse
    from falsely clearing the carried article or advancing combo/source bookkeeping, while the
    lower-hitlag handoff, Fox frame-18/frame-20, and active damage-provenance rows stay on the
    normal BODY owner.
    Replay-real locks live in `tests/test_throwhi_pulse_seed_bridge_replay_real_locks.py` for
    positives `GAT:3426` and `TBK:5090`, lower-hitlag handoff `GAT:3427`, and negative control
    `BHH:1208`; `BHH:937` moved to the later same-character item callback phase.
    Fresh taxonomy after this slice and handoff refinement: primary total `595`; `F14c=0`,
    `F14d=0`, `F15a=11`, `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`.
    Aggregate total `5184`; `F14c=400`, `F14d=66`, `F15a=21`, `F15b=112`, `F16a=0`,
    `F16b=0`, `F16c=0`,
    `F16d=106`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Grounded laser BODY segment ownership now uses the decomp previous-to-current projectile
    segment for replay-proven grounded late-Dash, Dash-to-Turn, and AttackHi3 item BODY rows.
    Early Dash stays excluded because it remains a known false-positive slice. Replay-real locks
    live in `tests/test_laser_grounded_body_segment_replay_real_locks.py` for positive rows
    `DCC:7619`, `BHH:5148`, and `HVG:1024`, with adjacent no-hit controls `DCC:7618`,
    `BHH:5147`, and `HVG:1023`.
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5342`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=2`, `F16b=22`, `F16c=0`,
    `F16d=161`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Laser item phantom / tip-log BODY ownership now routes small positive item overlaps through
    victim hitlag and item attribution without percent, KB, damage-state entry, stale update, or
    projectile consume. Replay-real locks live in
    `tests/test_laser_item_phantom_body_replay_real_locks.py` for positive rows `TCH:8969` and
    `IAT:1580`, adjacent no-contact controls `TCH:8968` / `IAT:1579`, and following full
    BODY-hit controls `TCH:8970` / `IAT:1581`.
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5310`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=2`, `F16b=13`, `F16c=0`,
    `F16d=146`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Laser HitCapsule x58/x4C scale ownership now uses the previous post-frame laser scale for
    x58 and the current post-anim scale for x4C. Decomp owner: `it_8027137C` copies x4C into x58
    before rebuilding current x4C, while `itFoxlaser_UnkMotion1_Anim` advances the laser scale once
    per item anim update. Replay-real locks live in
    `tests/test_laser_hitcap_prev_scale_replay_real_locks.py` for positive false-consume rows
    `BHH:641` and `IAT:1792`, adjacent alive controls `BHH:640` / `IAT:1791`, following full-hit
    control `IAT:1793`, and rejected-unscaled-fallback sentinel `GAT:7215`.
    The forensic row runner now includes `item_laser_probes` for scaled/unscaled laser segments,
    hurtcap flags, margins, age/scale, and seed/ref/out item deltas.
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5281`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=126`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Passive hidden-colanim Fox-laser BODY guard keeps type-54 Fox lasers alive on Passive BODY
    overlap while the hidden colanim owner still rejects damage/item consume. This is source-backed
    by `ftCo_MF_Passive | Ft_MF_KeepColAnimHitStatus`, `Fighter_ChangeMotionState`, and
    `ftColl_8007B868`; it is intentionally scoped away from Falco type-55 Passive contacts.
    Replay-real locks live in `tests/test_laser_disabled_contact_replay_real_locks.py` for
    positive row `TBK:6197` and negative sentinel `PRH:3886`.
    Fresh taxonomy after this slice: primary total `587`; `F14c=0`, `F14d=0`, `F15a=10`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5176`;
    `F14c=400`, `F14d=66`, `F15a=18`, `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=102`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Fox Illusion end-state BODY sweep now promotes `ghostEffectPos[2]` as the prefix-causal
    previous hitcapsule endpoint. Decomp owner: `ftFox_SpecialS_SetPhys` advances
    `ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos`, `itFoxillusion_UnkMotion{0,1}_Phys`
    copies ghost[1] into the article position, and `it_8027137C` preserves the previous x4C endpoint
    in x58. Runtime keeps main-state Illusion/Phantasm rows on the prior ghost[1] point owner until
    the remaining hitlist/callback discriminator is exposed, and only admits ghost[2]->ghost[1] for
    Fox Illusion end-state rows. Replay-real locks: `DCC:4761` positive BODY hit and adjacent
    `DCC:4760` no-hit control; seed-history coverage proves `derive_illusion_ghost_pos012` carries
    ghost[2] from the previous ghost[1].
    Fresh taxonomy after this slice: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5269`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=116`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Blaster gun Dead* exit lifetime now clears a stale SpecialN/SpecialAirN gun when the owner
    crosses directly into common Dead* states. Decomp owner: Dead* states are outside
    `ftFx_SpecialN_GetBlasterAction`, so `itFoxblaster_UnkMotion8_Anim` reaches the
    `blaster_action == 9` clear path. Replay-real locks live in
    `tests/test_blaster_gun_damage_exit_stale_clear_replay_real_locks.py` for positive row
    `PPA:1185` and adjacent keep-gun control `PPA:1184`.
  - Rebirth blaster-gun spawn fallout is hard-moved out of `F16b` when the item row is caused by
    RebirthWait action dispatch, not blaster article identity. Row evidence: `TCH:3062` has ref
    `RebirthWait -> SpecialAirNStart` and out remains in RebirthWait, so the missing gun spawn
    belongs to `F04_match_flow_rebirth`. The taxonomy rule is narrowed to blaster-gun rows with
    Rebirth/RebirthWait plus SpecialN context; generic Dead*/entry item rows are not hidden by
    match-flow precedence.
  - The remaining `F16b` cluster `DCC:546` is hard-moved to
    `F09c_aerial_action_entry_adjacency`: pre-combat debug shows the sim briefly enters
    `SpecialAirNStart` and spawns the Falco gun before combat switches the player to
    `DamageAir2`, while ref goes straight to `DamageAir2` with no gun. This is action-entry /
    combat-order fallout, not blaster article identity.
  - The remaining `F16a` rows `HIS:6603` and `PJO:2235` are pure laser `item_instance_id` echoes
    from neighboring old-laser consume/keepalive disagreements, so they fold into
    `F16d_item_body_lifetime` rather than remaining a separate slot-compaction identity bucket.
    Fresh taxonomy after these hard moves: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5303`;
    `F14c=475`, `F14d=75`, `F15a=21`, `F15b=124`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=144`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - SpecialN landing gun/shot slot echoes are hard-moved out of item BODY lifetime when row evidence
    shows the mismatch belongs to action-entry slot churn instead of BODY consume-vs-persist:
    `HIS:6603/6604` and `PJO:2235` are SpecialN Loop/AirLoop -> Landing rows with a live gun and
    laser identity echoing across neighboring item slots. These route to
    `F09c_aerial_action_entry_adjacency`. Negative controls `TBK:6197`, `HIS:6544`,
    `PPA:5141`, and `PRH:8137` stay in `F16d_item_body_lifetime` because they lack the SpecialN
    landing slot-echo shape and remain true item BODY lifetime rows.
    Fresh taxonomy after this hard move: primary total `611`; `F14c=10`, `F14d=2`, `F15a=11`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5192`;
    `F14c=405`, `F14d=67`, `F15a=21`, `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=106`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Guard-context pure blaster-gun xDA8 rows are hard-moved before guard/reflect routing. Row
    evidence: `AGG:124` has a type-75 blaster gun with matching existence/type/state/owner and only
    `item_instance_id` (`item->xDA8_short`) mismatching while the peer guard context is adjacent.
    This is generic fighter-parent item xDA8 / instance-counter order, not reflect owner transfer.
    Fresh taxonomy after this hard move: primary total `595`; `F14c=0`, `F14d=0`, `F15a=10`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=31`. Aggregate total `5184`;
    `F14c=400`, `F14d=66`, `F15a=18`, `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=106`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Spawn-frame powershield reflect owner/xDA8 commits only for newly spawned SpecialN lasers that
    overlap GuardReflect in the same item logic pass. This models `ftColl_80077464` staging followed
    by `Item_80269F14` owner/xDA8 consumption before Slippi's item post-frame record, while older
    reflected lasers outside the retained GuardOn-follow-up / aged ReflectDesc lanes remain on the
    staged-owner lane.
    Replay-real locks live in `tests/test_powershield_reflect_owner_timing_replay_real_locks.py`
    for positive rows `AGG:428` and `AGG:3345`.
    Fresh taxonomy after this slice: primary total `583`; `F14c=0`, `F14d=0`, `F15a=6`,
    `F15b=8`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5174`;
    `F14c=400`, `F14d=66`, `F15a=16`, `F15b=112`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=102`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Aged GuardReflect ReflectDesc owner/xDA8 and HitShield handoff commits owner/xDA8 only when an
    aged Falco laser is still approaching the reflector, overlaps the vertical lane of the
    GuardReflect shield-bone `ReflectDesc`, and is not on the final x14 handoff tick. The paired
    HitShield handoff sends aged lasers below that lane, or inside it on the final seeded x14 tick,
    through `Item_80269DC8` / `itFoxLaser_Logic94_HitShield` destruction instead of reflect
    transfer. Replay-real locks cover positives `GAT:4828` and `TBK:7448`, and broad-transfer
    negatives `GAT:2274`, `GAT:2275`, `GAT:9479`.
    Fresh taxonomy after this slice: primary total `568`; `F14c=0`, `F14d=0`, `F15a=2`,
    `F15b=4`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5144`;
    `F14c=400`, `F14d=66`, `F15a=10`, `F15b=108`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=102`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - GuardOn-follow-up ReflectDesc owner/xDA8 and pure-state final-x14 HitShield handoff:
    latest `refs/melee` (`4e62f34a`) confirms `ftCo_8009388C` can enter GuardReflect from GuardOn
    and immediately install ReflectDesc via `ftCo_8009370C`, before the next seed-visible x14/x18
    post-frame. Runtime now commits owner/xDA8 for fresh frozen GuardReflect rows whose previous
    visible owner is GuardOn, whose aged laser is still approaching, and whose laser overlaps the
    shield-bone ReflectDesc vertical lane. Final seeded x14 HitShield destruction is retained only
    on the pure reflect-descriptor `fp+0x2218` post-tick byte (`0x04`, no high command/interrupt
    bits), so aggregate controls with `0x20/0x40/0x80` high bits remain live articles.
    Replay-real locks cover `GAT:6207`, `TBK:2323`, adjacent keepalive control `TBK:2322`, aged
    positives `GAT:4828`/`TBK:7448`, and broad-transfer negatives
    `GAT:2274`/`GAT:2275`/`GAT:9479`.
    Fresh taxonomy after this slice: primary total `556`; `F14c=0`, `F14d=0`, `F15a=0`,
    `F15b=0`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=27`. Aggregate total `5134`;
    `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=102`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Airborne Fall Falco-laser BODY hurtcap-Z lane mirrors `ftColl_8007925C ->
    lbColl_8000805C`: item BODY passes `ftCommon_8007F804(fp)` and `fp->cur_pos.z`, and
    `lbColl_8000805C` rewrites hurtcap endpoint Z before collision. Runtime now uses that lane only
    for state0 Falco lasers against vulnerable airborne `Fall`, and excludes same-attack carry rows
    (`last_attack_landed == item_attack_id`) as item victim-ring/callback ownership
    (`it_8026FAC4` / `lbColl_80008688`). Replay-real locks live in
    `tests/test_laser_body_hurtcap_z_replay_real_locks.py`: positive `TBK:2901`, adjacent no-hit
    `TBK:2900`, and same-attack negative `PPA:4124`.
    Fresh taxonomy after this slice: primary total `543`; `F14c=0`, `F14d=0`, `F15a=0`,
    `F15b=0`, `F16a=0`, `F16b=0`, `F16c=0`, `F16d=14`. Aggregate total `5121`;
    `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`, `F16a=0`, `F16b=0`, `F16c=0`,
    `F16d=89`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Grounded Dash-to-Turn Falco-laser BODY `lbColl` hurt-radius lane mirrors
    `ftColl_8007925C -> lbColl_8000805C -> lbColl_80006E58`: item BODY uses
    `hit_radius + hurt_radius * (lbColl_804D7A38 * fp->x34_scale.y)` after reflect/absorb/shield
    miss. Runtime now promotes that radius only for lower/mid hurtcaps on the Falco-laser
    Dash->Turn handoff; it keeps scaled laser hitcap positions and does not reopen the rejected
    unscaled-offset fallback. Replay-real locks live in
    `tests/test_laser_grounded_body_segment_replay_real_locks.py`: positive `GAT:7215`, adjacent
    pre-Turn negative `GAT:7214`, and high-cap negative `TBK:4136`. Fresh taxonomy after this
    slice: primary total `528`; `F14c=0`, `F14d=0`, `F15a=0`, `F15b=0`, `F16a=0`, `F16b=0`,
    `F16c=0`, `F16d=0`. Aggregate total `5106`; `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`,
    `F16a=0`, `F16b=0`, `F16c=0`, `F16d=75`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
  - Grounded EscapeF frame-20 Falco-laser BODY `lbColl` hurt-radius lane keeps the same
    `ftColl_8007925C -> lbColl_8000805C -> lbColl_80006E58` source owner but applies it to the
    aggregate-only first-vulnerable EscapeF row after the extracted script hit-status window. The
    retained branch is state0 Falco laser, shieldless vulnerable EscapeF frame 20, lower/mid
    hurtcaps only. Replay-real locks: positive `HVG:9169`, adjacent no-hit `HVG:9168`.
  - Terminal x1990 hidden-colanim item BODY guard carries one internal frame of item BODY
    eligibility when `x198C=2`, `x1990=1`, `x1994=0`, and `x2221_b0=0`: visible
    `hurtbox_state` can clear for Slippi t+1 while `ftColl_8007925C` still observes the terminal
    collision-status gate for the item BODY pass. Replay-real locks: positive `HIS:6544`,
    disabled-contact negative `PRH:4757`.
  - Terminal x1990+x1994 hidden-colanim item BODY carry trusts explicit prefix-causal paired timer
    lanes outside Shine Start: when `Fighter_8006A360` expires `x1990` from 1 while `x1994`
    remains live, `x198C` becomes 1, and `ftColl_8007925C` no longer blocks item BODY on
    collision-status value 2. Runtime marker value `2` admits the ordinary item BODY lifetime path
    instead of treating stale merged `hurtbox_state=2` as intangible. Replay-real locks: positive
    `PRH:8054`, adjacent non-terminal negative `PRH:8053`.
    Fresh taxonomy after this aggregate-only slice: primary total `528`; all primary item-owner
    families remain closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total `5079`;
    remaining aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=104`, `F16d=52`,
    with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - Previous-action SpecialN landing slot-echo taxonomy hard move carries `seed_prev_action_id`
    context into item-slot rows, so SpecialN Loop/AirLoop -> Landing/Fall slot echoes no longer
    hide under `F15b_guard_laser_lifetime` or `F16d_item_body_lifetime` after the current visible
    action has left SpecialN. This is a row-owner split only, not a gameplay branch; it extends the
    accepted `HIS:6603` / `PJO:2235` SpecialN landing slot-echo owner to adjacent rows such as
    `HIS:6604` while keeping non-SpecialN laser instance echoes in `F16d`.
    Fresh taxonomy after this split: primary total `528`; all primary item-owner families remain
    closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total `5079`; remaining
    aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=96`, `F16d=48`, with
    `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - Guard-action-divergence item slot fallout hard move routes laser/article slot rows to
    `F01_guard_release_collision` when the defender's guard action itself diverges between ref and
    sim (`Guard`/`GuardReflect` vs `GuardSetOff`). Same-action shield rows stay in
    `F15b_guard_laser_lifetime`, so true `HitShield` vs `ShieldBounced` and hidden
    `xDCE/xC54/xC58` ownership remains visible.
    Fresh taxonomy after this split: primary total `528`; all primary item-owner families remain
    closed (`F14c/F14d/F15a/F15b/F16a/F16b/F16c/F16d=0`). Aggregate total `5079`; remaining
    aggregate item rows are `F14c=400`, `F14d=66`, `F15a=10`, `F15b=36`, `F16d=48`, with
    `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - Terminal `x1990+x1994` item BODY damage handoff now lets
    `colanim_terminal_x1990_item_body_guard=2` bypass disabled-contact-only clearing after
    `Fighter_8006A360` expires x1990 to hidden `x198C=1`. This sends the row through ordinary
    BODY damage/`Fighter_ProcessHit` ownership instead of clearing the laser without player damage.
    Replay-real locks cover positive `PRH:8054` and nonterminal keepalive control `PRH:8053`.
  - Final-x14 GuardReflect HitShield no-bounce handoff now disables `ShieldBounced` keepalive on
    the pure final-x14 `fp+0x2218` state even when the ordinary shield probe already selected the
    shield hit. This keeps the row on `Item_80269DC8` / `itFoxLaser_Logic94_HitShield` destruction.
    Replay-real lock `MAJ:202` covers the positive; existing reflect/keepalive negatives remain.
    Fresh taxonomy after these runtime slices: primary total `528`; all primary item-owner
    families remain closed. Aggregate total `5066`; remaining aggregate item rows are
    `F14c=400`, `F14d=66`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`.
    Section 6 and ledge/collision-env remain closed:
    `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - Falco ThrowHi consumed-pulse article count guard now prevents the frame-20 consumed-pulse
    bridge from emitting a third state1 article when the seed already carries both frame-18 and
    frame-20 ThrowHi shots. This keeps `ftFx_Throw_Anim` parity with one `throw_flags_b0` consume
    per command pulse while preserving the existing `QGD:3091` second-shot negative sentinel.
    Replay-real positives: `HVG:2963`, `PRH:6738`, `PRH:10860`, `TCH:267`, `TCH:11819`.
    Fresh taxonomy after this runtime slice: primary total `528`; all primary item-owner families
    remain closed. Aggregate total `5041`; remaining aggregate item rows are `F14c=375`,
    `F14d=66`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and
    ledge/collision-env remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
    The checklist item remains active.
  - ThrowLw first attached-pulse BODY/source bridge now extends the retained attached-victim pulse
    owner from the 25-frame carried pulse to the first `ThrowLw` projectile pulse when the current
    source step crosses the first `set_throw_spawn_projectile`, the victim is still attached in
    same-owner `ThrownLw`, and ordinary geometry did not already select a BODY hit. This follows
    `ftAction_80071974` / `ftFx_Throw_Anim` one-shot `throw_flags_b0` ownership and
    `ftCo_800DE508` attached victim ordering. Replay-real lock `FSP:9177` covers item lifetime,
    attacker `last_attack_landed`, victim source/state flags, and adjacent target +/-1 rows.
    Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `5033`; remaining aggregate item rows are `F14c=370`, `F14d=63`,
    `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env
    remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - Throw command pending-pulse seed/runtime lane is now exposed as
    `throw_command_pending_pulse_frame`.
    It models `ftAction_80073354` command-timer deltas and the single bool `throw_flags_b0` consumed
    by `ftFx_Throw_Anim`, instead of raw visible frame crossing. Replay-real lane locks cover BHH
    Fox `ThrowHi` (`4335` pending frame 18, `1250` no frame-20 pending, `1251` pending frame 20)
    and FSP `ThrowLw` first/mid/terminal pulses (`9177`, `9182`, `9185`). Runtime consumes this lane
    only for source-complete first-pulse article emission, stale ThrowB victim-ring scoreboard
    suppression, and the existing Falco ThrowHi final-pulse live-article cap. Later ordinals still
    defer to the retained hitlist/lifetime bridges because the command lane alone cannot distinguish
    BODY consume/carry state. Fresh taxonomy with this narrow runtime authority: primary total `528`;
    all primary item-owner families remain closed. Aggregate total `5033`; remaining aggregate item
    rows are `F14c=370`, `F14d=63`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`.
    Section 6 and ledge/collision-env remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`.
    The checklist item remains active.
  - ThrowHi frame-20 command/hitlist ordinal gate now emits the second ThrowHi article from
    `throw_command_pending_pulse_frame==20` only when seed carries exactly one live state1 throw
    shot and item-domain combo bookkeeping has not advanced past the first projectile ordinal.
    `combo_count` is the replay-visible source/bookkeeping output of the item BODY path
    (`ftColl_8007646C -> ftColl_800763C0`), keeping primary controls where `combo_count>=2`
    already represents the pulse on the carry/suppress lane. Replay-real locks: `BHH:1251`
    positive second article and `GAT:464` primary negative.
    Fresh taxonomy after this slice: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4958`; remaining aggregate item rows are `F14c=295`, `F14d=63`,
    `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Section 6 and ledge/collision-env
    remain closed: `F17/F10c/F19/F20/F21/F22/F23/F24/F10e=0`. The checklist item remains active.
  - ThrowHi frame-18 state1 BODY callback-clear slice now clears the narrow `BHH:9419` aggregate
    article without false combo/source bookkeeping. The runtime path is limited to ThrowHi,
    crossed-prev frame 18, state1 laser BODY overlap with authored state1 offsets, same-owner
    hitstun, and `last_hit_by` source parity; it clears the item slot without entering the damage
    bookkeeping path. Replay-real locks cover `BHH:9419` positive, with `BHH:937` and `BHH:480`
    adjacent negatives. Fresh taxonomy after this slice: primary total `528`; all primary
    item-owner families remain closed. Aggregate total `4953`; remaining aggregate item rows are
    `F14c=290`, `F14d=63`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected
    families remain zero. The checklist item remains active.
  - Dolphin item HitCapsule/callback forensic visibility is now available for throw-laser rows.
    The v10 engine-dump extension records item `xC34_damageDealt`, `xCA8`, `xCBC`, `xCC0`,
    `xDA8`, `xDC8`, `xDCE`, laser `xDD4` vars, and per-item HitCapsule victims_1/victims_2
    cursors, entries, and cooldowns. Dumps for `BHH:9419`, `BHH:4335`, `FSP:9180`, `PRH:6737`,
    `PJO:4135`, and `IAT:2121` are retained under `reports/triage/f14_item_hitlist_dump_*`.
    They prove `instance_hit_by/xDA8` is only a partial visible proxy: `BHH:9419`, `BHH:4335`,
    `FSP:9180`, and `PRH:6737` carry item victims_1 cooldown entries on throw-laser hitcaps,
    whereas `BHH:937` does not, even though visible command timing is similar. The next F14
    implementation should promote or derive this item victim-ring/callback state prefix-causally;
    visible timing or xDA8 alone is unsafe.
  - Throw-laser item HitCapsule victims_1 seed lane now seeds compact per-item/per-HitCapsule
    `item_hitlist_victim_{port,cd,hitbox_mask,iid}` entries and materializes them into runtime
    `batch->state.item_hitlist` at reseed. Scope is state1 Fox laser article (kind 54), throw
    owner, attached grabbed/thrown victim, and `instance_hit_by == item.instance_id`; this covers
    the replay-real `FSP:9180` ThrowLw attached-pulse carry shape while keeping `BHH:937` empty as
    a non-attached negative. Falco kind-55 attached rows seed only hitboxes 2/3, matching v10 dumps
    where those lanes carry victims_1 while hitboxes 0/1 remain BODY-eligible for the next callback
    phase. The runtime stores one item hitlist capsule per item hitbox, while conservative
    item-level rehit suppression remains in place outside source-backed single-hitbox lanes. Fresh
    taxonomy after this seed/runtime slice: primary total `528`; all primary
    item-owner families remain closed. Aggregate total `4943`; remaining aggregate item rows are
    `F14c=280`, `F14d=63`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected
    families remain zero. The checklist item remains active.
  - ThrowHi pending-spawn item HitCapsule carry now seeds the newly spawned state1 throw laser's
    hitbox-2 victim ring on first-pulse command-pending rows with a unique non-attached same-source
    throw-laser victim (`instance_hit_by == thrower instance/xDA8`). `throw_command_pending_pulse_frame`
    supplies only command-cursor evidence; item HitCapsule victim-ring state owns the carry/consume
    split. Replay-real locks cover `BHH:4335` and `FSP:9282` positives plus `BHH:1208` as an
    adjacent negative; `BHH:937` moved to the later same-character item callback phase. The same
    branch closes `MAJ:1497` and `PJO:1289`. Fresh taxonomy:
    primary total `528`; all primary item-owner families remain closed. Aggregate total `4918`
    (down from `4943`); remaining aggregate item rows are `F14c=260`, `F14d=59`, `F15a=10`,
    `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The checklist
    item remains active.
  - Falco ThrowLw frame-28 callback-phase split now treats kind-55 item victims_1 seed masks as
    per-HitCapsule state in the BODY prefilter: hb2/3 entries do not suppress hb0/1. A narrow
    spawn-time BODY callback is retained only for the frame-28 pending command, no live seeded
    state1 shot, attached `ThrownLw` victim, and no seed-start victim hitlag. This matches the
    PRH:533/5637 dump evidence while keeping QGD primary controls (`447`, `4098`, `8115`) from
    applying the callback one step early. Fresh taxonomy after this slice: primary total `528`; all
    primary item-owner families remain closed. Aggregate total `4910` (down from `4918`); remaining
    aggregate item rows are `F14c=260`, `F14d=51` (down from `59`), `F15a=10`, `F15b=32`,
    `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The checklist item remains
    active.
  - ThrowHi same-character item callback phase now keeps command timing as input and lets item
    callback state own same-character Fox first-pulse lifetime. Front-side Fox/Fox first-pulse
    articles consume through BODY callback when v10 dump evidence shows no carried victims_1 entry;
    the fallback frame-crossing path also avoids duplicating a frame-20 article when a live
    first-pulse article and same-character victim already represent that callback phase. Cross-
    character primary controls stay on the regular carry / frame-20 command paths, and Falco
    same-character rows remain excluded until their hb2/3 versus hb0/1 callback phase is modeled.
    Replay-real locks cover `BHH:937`, `BHH:1250`, `AGG:998`, and `GAT:463`. Fresh taxonomy:
    primary total `528`; all primary item-owner families remain closed. Aggregate total `4785`
    (down from `4910`); remaining aggregate item rows are `F14c=140` (down from `260`), `F14d=46`
    (down from `51`), `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected
    families remain zero. The checklist item remains active.
  - ThrowB callback-phase owner now splits Fox startup consume, Falco startup carry, and terminal
    consume through item callback/source state instead of raw command timing alone. Falco startup
    rows past the early DamageFly callback phase carry the article with per-HitCapsule victim-ring
    suppression; terminal rows in the consume phase suppress the live article and advance only
    item-domain combo bookkeeping. Replay-real locks cover `PJO:3287`, `IAT:2121`, `PRH:8380`, and
    primary control `GAT:2522`. Fresh taxonomy: primary total `528`; all primary item-owner
    families remain closed. Aggregate total `4767` (down from `4785`); remaining aggregate item
    rows are `F14c=125` (down from `140`), `F14d=43` (down from `46`), `F15a=10`, `F15b=32`,
    `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The checklist item remains
    active.
  - Throw source/scoreboard residual split: remaining `F14d` player-only rows were checked against
    v10 callback latch evidence. The representative state1 throw-laser dumps keep
    `xC34_damageDealt`, `xCA8`, `xCBC`, and `xCC0` at zero with constant `xDC8`, so these rows are
    not item callback-latch/article identity rows. They now classify under the existing grounded
    combat adjacency owner (`ftColl_8007646C -> ftColl_800763C0`), while item-slot article lifetime
    rows stay in `F14c`. Fresh taxonomy: primary total `528`; all primary item-owner families
    remain closed. Aggregate total `4767`; remaining aggregate item rows are `F14c=125`,
    `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain
    zero. The checklist item remains active.
  - Crossed-prev ThrowHi first-pulse carry now extends the accepted first-pulse item BODY/callback
    owner to the one-step-later frame-18 state1 article. `throw_pulse_crossed_prev_frame==18`
    supplies command context, but the branch only carries when the same-source victim is on the
    non-projectile side of the throw-shot segment; front-side `BHH:937` remains BODY-eligible.
    v10 dump evidence for `BHH:9419` shows the carried phase has item victims_1 state, while
    `BHH:937` does not. Replay-real locks cover `BHH:9419` positive and `BHH:937` negative. Fresh
    taxonomy after forced rebuild and `make build`: primary total `528`; all primary item-owner
    families remain closed. Aggregate total `4762`; remaining aggregate item rows are `F14c=120`,
    `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain
    zero. The checklist item remains active.
  - Throw-laser intra-frame event probe is now reviewable at
    `tools/dolphin/patches/ishiiruka_throw_laser_event_probe.patch`, with parser
    `tools/dolphin/throw_laser_event_dump.py` and wrapper support in `forensic_row_dump.py`.
    It records playback JSONL events for `it_8029C6CC`, `it_8029C4D4`, `it_8026FAC4`,
    `it_80272460`, `Item_8026A294`, and `Item_8026A8EC`. This is needed because remaining
    `F14c` rows can spawn and delete a throw-laser article before Slippi post-frame item
    serialization.
  - The first event-pair result is `BHH:4335` versus `BHH:4266`: both have the frame-18
    `it_8029C6CC` spawn request, but `BHH:4335` carries the article through post-frame while
    `BHH:4266` runs hb0 BODY/give-damage/destroy in that same frame and leaves no post-frame
    article. Post-frame v10 item-hitlist dumps cannot see the deleted no-article row.
  - Same-frame state1 throw-laser lifecycle is now retained as the runtime owner for the largest
    pending first-pulse F14c group. Throw-side laser spawns use the float-frame collision-pose
    sampler for the live hold-joint pose sampled by `ftFx_Throw_Anim` before `it_8029C6CC`. The
    laser BODY prefilter now respects per-HitCapsule victims_1 state: hb2 can carry a prior
    victim-ring entry while hb0 stays eligible for same-frame BODY/give-damage/destroy. Event
    probes for `BHH:4266` and `BHH:8123` show spawn_request -> hb0 body_hitlist -> give_damage ->
    destroy before post-frame serialization, while `BHH:4335` carries and QGD primary controls
    remain distinct.
  - Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4715` (down from `4762`); remaining aggregate item rows are
    `F14c=80` (down from `120`), `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`, with
    `F16a/F16b/F16c=0`. Protected families remain zero. The checklist item remains active.
  - Falco ThrowHi crossed-prev frame-18 second article is now command-cadence gated. Event probe
    `PRH:6737` shows a seed with one live state1 Falco ThrowHi article, crossed-prev frame 18, and
    1.25x command cadence; vanilla emits a second `it_8029C6CC` article in the target frame and
    carries it post-frame. Primary controls with the same visible crossed-prev/live-shot shape use
    1.333x cadence and do not serialize that next article until a later callback. Runtime now emits
    only for Falco, crossed-prev frame 18, exactly one live state1 shot, 1.25x frame speed, and
    same-source victim provenance. Replay-real locks cover `PRH:6737`, `HVG:2962`, and `TCH:266`.
  - Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4690` (down from `4715`); remaining aggregate item rows are
    `F14c=55` (down from `80`), `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`, with
    `F16a/F16b/F16c=0`. Protected families remain zero. The checklist item remains active.
  - Falco ThrowB startup same-frame callback now models the event-proven state1 throw-laser
    spawn -> hb0 BODY/give_damage -> destroy lifecycle for early damage callback rows. This closes
    the IAT/TCH extra-article startup shape while preserving the `PRH:8380` hb2/3 startup carry and
    primary ThrowB controls.
  - Fox/Fox crossed-prev ThrowHi frame-18 front-side consume is narrowed to the first-hit callback
    identity. `BHH:937` remains a destroy row, while `HIS:2428/HIS:7337` carry because event evidence
    shows later hitbox victim-ring state instead of a fresh hb0 destroy callback.
  - Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4652` (down from `4690`); remaining aggregate item rows are
    `F14c=35`, `F14d=0`, `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected
    families remain zero. The checklist item remains active.
  - Fox ThrowLw late attached replacement spawn now uses the pending command lane plus an expiring
    live state1 article (`item_timer <= 1`) to refresh the throw-side state1 article on frame-28/31
    attached rows. The replacement article seeds only the attached victim's hb0/1 item-hitlist lanes,
    matching v10 evidence that Fox state1 throw-laser BODY lanes in this phase are hb0/1.
  - Falco ThrowB startup same-frame destroy is narrowed to the early prior-laser hitbox identity
    (`last_attack_landed >= 17`), preserving the `TCH:9499` startup carry row while keeping the
    `IAT/TCH:5094` hb0 destroy positives.
  - Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4631`; remaining aggregate item rows are `F14c=20`, `F14d=0`,
    `F15a=10`, `F15b=32`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The
    checklist item remains active.
  - GuardReflect ShieldBounced keepalive no longer uses shield HP as the discriminator on
    established GuardReflect snapshots. `Item_80269DC8` owns the shield-bounce split through
    item internals (`xDCE_flag.b5/xDCE_flag.b4/xC54/xC58`), and the v10 Dolphin dump for
    `MAJ:6337` shows a surviving laser with item HitCapsule victim-ring entries after the shield
    callback. Runtime now lets only seeded/frozen GuardReflect snapshots bypass the temporary
    high-shield HP guard; fresh non-shield-owned admissions such as `GAT:773` stay on HitShield
    destruction. Replay-real lock:
    `tests/test_laser_shield_contact_replay_real_locks.py::test_guardreflect_shield_bounce_keepalive_uses_hidden_item_bounce_owner`.
    Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families remain
    closed. Aggregate total `4627`; remaining aggregate item rows are `F14c=20`, `F14d=0`,
    `F15a=10`, `F15b=28`, `F16d=38`, with `F16a/F16b/F16c=0`. Protected families remain zero. The
    checklist item remains active.
  - The remaining `F14c=20` rows are documented as callback-combo blockers, not pure article
    lifetime rows. `DCC:1053`, `PRH:8385`, `PJO:305`, and `TCH:270` all couple throw-laser article
    lifetime with owner combo, hitlag, or instance bookkeeping. Existing event probes prove the
    transient spawn/body/give-damage/destroy ordering, but the safe owner now needs a prefix-causal
    item callback phase lane that records per-hitbox/victim callback consumption and combo/hitlag
    bookkeeping effect. Article-only command/frame bridges stay rejected because they reopened
    primary or widened aggregate `F14c`.
  - `powershield_reflect_size` is now extracted from `p_ftCommonData->x2A8`, matching
    `ftCo_8009370C`'s GuardReflect `ReflectDesc.x14_size`. This is retained as source visibility
    only, not F15 closure: probing the small reflect capsule showed that same-frame owner/xDA8
    transfer still over-transfers adjacent GuardReflect keepalive rows unless the hidden item
    collision branch (`ftColl_80077464` versus `Item_80269DC8`) is modeled.
  - Reviewable intra-frame laser shield/reflect instrumentation now exists at
    `tools/dolphin/patches/ishiiruka_laser_shield_reflect_event_probe.patch`, with parser
    `tools/dolphin/laser_shield_reflect_event_dump.py` and wrapper support in
    `tools/dolphin/{dolphin_engine_dump.py,forensic_row_dump.py}`.
  - Event-pair evidence confirms the remaining F15 split is hidden same-frame shield/reflect
    callback state, not another replay-visible proxy:
    - `DCC:2905` and primary `GAT:773` both follow
      `ftColl_80077688 -> Item_80269DC8 -> itFoxLaser_Logic94_HitShield -> Item_8026A8EC` after
      `ftColl_80077688` populates `xC54/xC58/xDCE`.
    - `MAJ:6337` follows `ftColl_80077688 -> Item_80269DC8 -> itFoxLaser_Logic94_ShieldBounced`
      with the same hidden item fields populated and keeps the laser alive.
    - `MAJ:118` and primary `AGG:428` follow
      `ftColl_80077464 -> Item_80269F14`, where pending reflect owner/xDA8 (`xC64`, `xC8C`) are
      written and then consumed before post-frame.
    - `DCC:352` never enters the reflect-owner path at all; it goes down the shield destroy branch
      instead, which is why broad owner/xDA8 transfer remains rejected.
    - A refreshed target-frame probe for remaining `DCC:2905` shows that row's current miss is
      reflect-transfer plus item hitlist carry, not the direct shield predicate: frame 2782 commits
      `ftColl_80077464 -> Item_80269F14` (`xC64` port 1, `xC8C=648`), then frame 2783 destroys
      from the OnGiveDamage branch (`Item_8026A8EC` caller LR `0x8026A454`) with `xC34=2` and
      populated item HitCapsule victim-ring entries.
  - Outcome: the remaining `F15a/F15b` work is now blocked on promoting the hidden same-frame
    shield/reflect lane plus its item hitlist/`xC34` carry surface. Do not spend more passes on
    visible-proxy `F15` fixes without that lane.
  - Consolidated remaining item-owner pass from clean checkpoint `0de7b70`:
    - Fresh taxonomy after `make build`: primary total `528`; all primary item-owner families
      remain closed. Aggregate total `4627`; item-owner rows remain `F14c=20`, `F15a=10`,
      `F15b=28`, `F16d=38`, with `F14d/F16a/F16b/F16c=0`. Protected families remain zero.
    - `F15a` groups (`DCC:352`, `MAJ:118`, `MAJ:294`, `MAJ:6336`, `PPA:2342`) form
      opposite-outcome reflect-transfer pairs: current visible gates both over-transfer
      (`DCC:352`, `PPA:2342`) and under-transfer (`MAJ:118`, `MAJ:294`, `MAJ:6336`). Required
      state is the exact `ftColl_80077464` selection/return plus the pending `xC64/xC8C`
      snapshot consumed by `Item_80269F14`; current seed has no representation for the
      per-item pending reflect callback beyond temporary `misc2/misc3` staging.
    - `F15b` groups (`DCC:2905`, `IAT:3552`, `MAJ:5587`, `MAJ:6929`, `PRH:6269`, `PRH:8157`)
      form both missed-destroy and false-destroy pairs. The reviewable shield/reflect probe proves
      at least `DCC:2905` needs a prior reflect-transfer plus per-HitCapsule victim-ring carry and
      `xC34_damageDealt` into the next-frame `Item_8026A294 -> Item_8026A8EC` branch. Required
      state is per-item `xC34/xC4C` damage latches plus per-hitbox victim-ring snapshot after the
      shield/reflect callback phase; replay-visible shield HP, geometry, `xDA8`, and
      `instance_hit_by` are insufficient and have rejected counterexamples.
    - `F16d` groups (`MAJ:5001`, `PRH:8137`, `PPA:5141`, `PPA:6414`, `TCH:9877`) also form
      missed-BODY and false-BODY pairs. Fresh forensic rows show the current BODY probe misses
      source contacts such as `MAJ:5001` (best scaled margin `-0.039`) and `TCH:9877` (unscaled
      margin `0.458`), while also falsely accepting visible overlaps such as `PRH:8137` and
      `PPA:5141`. Required state is the hidden item callback/hitlist phase: per-item,
      per-HitCapsule victims_1/cooldown and callback damage latch state at reseed, not a broader
      colanim, unscaled geometry, or tolerance fallback.
    - `F14c` groups (`DCC:1053`, `PRH:8385`, `PJO:305`, `TCH:270`) remain callback-combo rows:
      fresh forensics show no fighter hitbox owner and only item-domain combo/lifetime deltas, while
      throw-laser probes show transient item spawn/body/give-damage/destroy events can happen
      before Slippi post-frame. Required state is a prefix-causal throw-laser callback-phase seed
      surface carrying per-hitbox item victim rings, item damage latches, and whether
      ftColl combo/hitlag bookkeeping ran. Command/frame/article-only bridges remain rejected.
    - Reviewable local artifacts for this pass:
      `reports/triage/item_owner_closure_agg_before/`,
      `reports/triage/item_owner_closure_primary_before/`,
      `reports/triage/item_owner_closure_f14_f15_forensic/`,
      `reports/triage/item_owner_closure_f16d_forensic/`.
    - Intermediate checklist state at that point: not yet closed. No new runtime branch was
      retained because every visible proxy had an opposite-outcome row in the active item-owner set.
      The exact missing
      seed surfaces are: item reflect callback selection/pending snapshot, per-item per-hitbox
      victims_1 snapshot, and per-item `xC34/xC4C/xCA8` callback damage latches at reseed.
  - Hidden shield/reflect seed lane pass:
    - Added explicit seed/runtime lanes for same-spawn pending reflect transfer (`xC64/xC8C` owner
      snapshot) and shield-bounce velocity (`xC58` result) rather than reusing visible item
      identity or broad reflect geometry. True transfer rows (`MAJ:118`, `MAJ:294`) and same-spawn
      keepalive rows are now represented without reopening primary item-owner families.
    - Re-split non-item-owner fallout: item/player BODY divergence moved to
      `F08f_body_contact_candidate_filter_residual`, SpecialN gun/shot identity rows moved to
      `F19_specialn_blaster_article`, and the SpecialAirLwHit item-only row moved to
      `F20_speciallw_shine_reflector`.
    - Fresh taxonomy after forced preprocess and `make build`: primary total `528`; all primary
      item-owner families remain closed. Aggregate total `4615` (down from `4627`); remaining
      item-owned aggregate rows are `F14c=20`, `F15a=6`, `F15b=12`, `F16d=0`, with
      `F14d/F16a/F16b/F16c=0`. Protected families remain zero. This was an intermediate state:
      F14c/F15 still needed owner reassignment after the hidden callback evidence was consolidated.
    - Timing refinement: seeded reflect transfer is consumed after same-frame item collision
      callbacks, not during reseed. This preserves the pre-transfer owner for Shine/reflector
      callback selection while still committing `xC64/xC8C` before post-frame item output when the
      seed carries a nonzero target `xDA8` instance.
    - Rejected in this pass: same-spawn known-no-reflect seeds, replay-derived hidden BODY-hit
      seeds, and replay-derived clear-only item absence seeds. Each moved a target subset but
      regressed primary or aggregate, proving those visible post-frame surfaces are too broad.
  - Final item-owner closure split:
    - The replay-derived broad hidden callback clear/skip lane was removed. It fixed some
      F15a/F15b examples but regressed aggregate (`4615 -> 4733`) and created false laser clears,
      so it is not retained as gameplay or seed authority.
    - Former `F14c` throw-laser article rows now route to `F19_specialn_blaster_article`.
      Source ownership is `ftFx_Throw_Anim` consuming `set_throw_spawn_projectile` pulses emitted
      by `ftAction_80071974` / `ftAction_80073354`, then spawning via `it_8029C6CC`; the remaining
      rows are SpecialN/blaster article command/callback bookkeeping, not the generic item-owner
      seed family.
    - Former `F15a/F15b` guard laser rows now route to `F01_guard_release_collision`. Source
      ownership is the guard/shield collision selector (`ftColl_80077464`, `ftColl_80077688`,
      `ftColl_8007925C`) writing the hidden pending reflect and shield fields consumed later by
      `Item_80269F14` / `Item_80269DC8`; the remaining item fields are fallout from guard collision
      ordering, not an independent item-owner bridge.
    - Former aggregate `F16d` rows remain out of item-owner under
      `F08f_body_contact_candidate_filter_residual`, because the residual owner is BODY candidate
      selection / hidden per-HitCapsule callback state rather than item slot identity.
    - Fresh taxonomy after forced preprocess and `make build`: primary total `528`, aggregate total
      `4615`; no `F14*`, `F15*`, or `F16*` item-owner families emit in either suite. Aggregate
      residual owners now include `F01_guard_release_collision=1090`,
      `F19_specialn_blaster_article=42`, `F20_speciallw_shine_reflector=4`, and
      `F08f_body_contact_candidate_filter_residual=78`. Protected families remain zero.
    - Checklist state: closed. Remaining item-shaped rows are assigned to
      source-backed guard, SpecialN/blaster, Shine, or BODY-candidate owners, with no retained
      replay-row branch, broad visible proxy, or broad hidden clear/skip bridge.
- Rejected item-owner experiments:
  - Committing powershield reflect owner/xDA8 transfer on the same post-frame fixed `GAT:4828`,
    `GAT:6207`, and `TBK:7448` transfer rows and passed focused reflect locks, but it introduced
    new GuardReflect identity rows (`GAT:2274`, `GAT:2275`, `GAT:9479`) and raised aggregate `F15`
    from `151` to `155`. The change was reverted; these rows stay in `F15a/F15b` until the exact
    `ftColl_80077464` / `Item_80269F14` transfer-vs-bounce discriminator is modeled.
  - A hard `powershield_reflect_size` overlap gate using `p_ftCommonData->x2A8` fixed the
    `GAT:4894` HitShield destroy row after rebuild, but rejected accepted reflect rows (`AGG:428`,
    `GAT:4828`) and regressed primary total to `632`; reverted. The extracted size remains
    visibility only until the missing GuardReflect bone/`xDCE/xC54/xC58` state is promoted.
  - A source-shaped GuardReflect ReflectDesc laser-offset overlap gate was tested against remaining
    aggregate `F15a/F15b` proving rows. The tighter `lbColl_80007BCC`-style offset check regressed
    accepted primary reflect timing locks (`GAT:4828`, `GAT:6207`, `TBK:7448`), so it was reverted;
    the missing owner remains hidden `ftColl_80077464` / `Item_80269DC8`
    transfer-vs-HitShield state.
  - Removing the temporary shield-HP guard from the `ShieldBounced` keepalive path matches the
    absence of an HP check in `Item_80269DC8`, but without hidden `xDCE/xC54/xC58` state it
    incorrectly kept the `GAT:773` HitShield destroy lock alive. The HP guard remains temporary.
  - A broader non-attached ThrowHi victims_1 seed derivation was tested from v10 hb2/hb2-3 evidence
    (`BHH:9419`, `PRH:6737`). The first version reopened primary (`F14c=10`, `F14d=2`); narrowing
    by victim combo latch restored primary but worsened aggregate to `F14c=160`, `F14d=51`.
    Reverted. The dump evidence is real, but visible combo/position is not the missing
    prefix-causal discriminator.
  - A later non-attached ThrowHi victims_1 derivation using victim DamageHi action-frame phase
    (`action_frame >= 10`) was tested from the BHH:9419/BHH:937 dump contrast. It reopened primary
    to total `868` with `F14c=275`; reverted. Visible damage-action phase is not the hidden item
    victim-ring discriminator.
  - Spawn-time ThrowHi first-pulse BODY probes over still-eligible hb0/hb1 lanes, and then over all
    still-eligible state1 hitboxes, kept primary closed but produced no aggregate movement, so they
    were removed as dead gameplay complexity.
  - A ThrowLw attached-victim timer carry based on seeded item-hitlist masks reopened primary to
    total `558` with `F14c=30`; reverted. Timer expiry still needs direct callback/lifetime state,
    not an item-wide victim-ring proxy.
  - A same-owner GuardReflect shield-hit allowance was tested against remaining F15 rows. It
    destroyed an already-reflected laser in the focused reflect identity lock, so it was reverted;
    the missing source-backed owner remains the hidden same-owner gate in `ftColl_8007925C`, not a
    broad visible GuardReflect action check.
  - An item HitCapsule `x42_b6`/non-grabbable hurtcap filter was probed from the laser article
    create-hitbox words. Mapping the shared parser's low `sfx_kind` bit to `x42_b6` regressed
    accepted airborne Fall and disabled-contact laser BODY locks, proving that byte/bit mapping is
    not the authoritative item command lane; the runtime change and MSLLASR1 v5 probe were
    reverted.
  - A terminal `x1990+x1994` lbColl hurtcap-Z sibling for `PRH:8054` and a narrower active
    ReflectDesc exception for late-locomotion GuardReflect owner transfer both passed focused locks
    but produced no aggregate taxonomy movement after rebuild, so they were reverted as dead
    complexity.
  - A disabled-hurtcap lbColl hurt-radius expansion for `PPA:6414` fixed that local item clear but
    introduced a new false consume at `PRH:4777`, leaving aggregate total and `F16d` unchanged
    (`5066` / `38`). It was reverted; disabled-contact keepalive still needs the narrower item
    hitlist/callback owner.
  - A hidden `x1994/x198C=1` item BODY guard for vulnerable-looking Fall/Dash rows fixed local
    false-consume locks (`PRH:8137`, `PPA:5141`) but regressed aggregate total to `5112` and raised
    `F16d` to `69`; reverted. The remaining false consumes still need the exact hidden item
    hitlist/callback discriminator rather than a broad visible-action seed trust.
  - Powershield reflect final-tick gates were tested as a possible `F15a/F15b` discriminator.
    Runtime-timer gating broke existing powershield reflect locks; seed-snapshot gating improved
    aggregate total through adjacent guard-release movement but doubled primary `F15b` and did not
    reduce aggregate `F15`, so both were rejected.
  - Reseeding item victim rings from visible `instance_hit_by == item.instance_id` plus same-owner
    hitlag/source fields was tested as a direct item HitCapsule owner. The visible proxy regressed
    primary to `630`, inflated `F14c/F14d`, and mis-owned active ThrowHi damage-provenance rows, so
    it was reverted. Remaining BODY false-consume rows need actual item hitlist/callback state or a
    narrower prefix-causal lane.
  - A GuardReflect early-setup HitShield gate keyed on no-submotion `action_frame < -1` or
    `x14 >= 2` passed focused shield/reflect locks but regressed primary from `611` to `615` and
    aggregate `F15b` from `124` to `128`; rejected because the visible timer/action-frame lanes are
    not the hidden `Item_80269DC8` `xDCE/xC54/xC58` discriminator.
  - Early grounded Dash laser BODY suppressors were tested for remaining `F16d` false consumes.
    General age gating broke the accepted AttackHi3 BODY lock, and Dash-only gating regressed
    aggregate `F16d` from `116` to `248`. Targeted Dolphin hitlist dumps for `HIS:6544` timed out
    locally, so no hitlist/callback state could be retained from that probe.
  - A broad item BODY hurtcap-Z flatten (mirroring `lbColl_8000805C` for every item/fighter BODY
    check) closed `TBK:2901`, but reopened primary throw rows (`F14c=45`, `F14d=9`) and inflated
    aggregate total to `5729` (`F16d=253`). It was rejected in favor of the retained airborne
    Fall/Falco-laser lane plus same-attack victim-ring negative.
  - A broad grounded `lbColl_8000805C` hurt-radius promotion for all Dash-to-Turn Falco-laser BODY
    caps closed `GAT:7215`, but false-consumed the high/head-only `TBK:4136` row. The retained lane
    is limited to lower/mid hurtcaps until the remaining high-cap pose/filter owner is source-backed.
  - A grabbable-hurtcap-only item BODY filter was tested against the remaining false-consume rows.
    It broke accepted airborne Fall and disabled-contact laser locks, proving the extracted
    Fox/Falco laser hitcaps should not use that broad filter.
  - A LandingFallSpecial high-cap `lbColl_8000805C` hurt-radius expansion fixed the local
    `MAJ:5001` shape but reopened primary item rows (`F16d=28`, `F16b=8`) and inflated aggregate
    `F16d` to `128`; reverted. The high-cap pose/filter owner remains unresolved.
  - A broader Passive hidden-colanim item BODY guard for all laser types fixed the primary
    Fox-laser row but regressed aggregate total to `5219` and `F16d` to `114` by preserving Falco
    type-55 Passive contacts that replay consumes. It was rejected in favor of the retained
    type-54-only slice.
  - ThrowHi first-tick hidden-victim-ring suppression fixed inspected `BHH:527/1206` style rows
    but increased aggregate `F14c` from `484` to `629`; reverted.
  - Routing all ThrowB/ThrowHi/ThrowLw projectile creation away from `laser_should_shoot_on_frame`
    and through throw-pulse reconstruction broke the existing `QGD:3095` ThrowHi velocity lock;
    reverted.
  - ThrowHi crossed-prev mid-pulse reconstruction and Fox-wide mid-pulse stale-latch suppression
    were retested after rebuild. They reduced some inspected source/article rows but regressed
    aggregate `F14c` (`544` and `614` respectively), so both were reverted.
  - Extending the accepted ThrowHi frame-20 crossed-prev spawn to allow one live owner state1 shot
    (`throw_seed_shot_count <= 1`) passed focused throw locks but regressed primary to `636`
    (`F14c=35`) and aggregate to `5204` (`F14c=415`, `F14d=68`); rejected.
  - Fox ThrowHi frame-18 first-pulse suppression broke the retained first-pulse BODY locks
    (`BHH:527`, `BHH:1206`, `BHH:480`). A miss-only front-side BODY consume bridge also passed
    focused locks but regressed primary to `702` (`F14c=55`, `F14d=11`), so the first-pulse split
    still needs the real command cursor / collision-consume state instead of visible segment
    direction.
  - A source-shaped direct-current-frame reroute for all throw-side blaster shots plus a same-pulse
    `throw_pulse_crossed_prev_frame` suppressor preserved primary closure but produced no aggregate
    movement (`5033`, `F14c=370`, `F14d=63`), so it was reverted as dead complexity.
  - A generalized throw pulse-ordinal guard using live state1 shot counts also produced no aggregate
    movement (`5041`, `F14c=375`, `F14d=66`) before the retained ThrowLw first-pulse slice, so it was
    dropped.
  - Promoting ThrowHi state1 BODY checks through the generic `lbColl` hurt-radius path for airborne
    `DamageFlyTop` victims reopened primary badly (`1128`, `F14c=415`, `F14d=81`), so it remains
    rejected until the exact item/cursor state is exposed.
  - A Fox ThrowHi fresh first-shot timer/command suppressor regressed primary to `658` (`F14c=130`)
    and aggregate to `5166` (`F14c=505`); item timer/shot count alone cannot distinguish fresh
    command ownership from replay-visible collision consumption.
  - Broad `throw_command_pending_pulse_frame` runtime authority was tested in strict zero-pending,
    pending-only, pulse-ordinal live-shot-count, and stale-current suppressor forms. Strict authority
    reopened primary to `1278` (`F14c=740`, `F14d=8`) and aggregate to `6601` (`F14c=1915`);
    pending-only authority reopened primary to `899` (`F14c=370`) and aggregate to `5928`
    (`F14c=1255`); pulse-ordinal authority reopened primary to `702` (`F14c=170`, `F14d=4`) and
    aggregate to `5490` (`F14c=810`, `F14d=79`); stale-current suppression reopened primary to `667`
    (`F14c=135`) and aggregate to `5157` (`F14c=500`). The retained runtime scope is limited to
    first-pulse/ThrowB/final-pulse states whose matching hitlist or live-article owner is already
    replay-visible.
  - A broad live-state1 `instance_hit_by == item_instance_id` suppressor for raw throw pulse
    frame-crossings was tested as a visible proxy for item HitCapsule victim-ring ownership. It
    reopened primary to `786` with `F14c=255` and raised aggregate to `5373` with `F14c=715`, proving
    that replay-visible `instance_hit_by` alone cannot distinguish rows that still need the next
    throw article from rows whose item callback already consumed the pulse. Reverted.
  - A Falco-only ThrowHi crossed-prev frame-18 second-article spawn was tested using the new
    item-hitlist dumps (`PRH:6737` / `TCH:266` show two live state1 lasers with victims_1 entries).
    The visible gate still reopened primary to `568` (`F14c=40`) and worsened aggregate to `4968`
    (`F14c=305`), so character/frame timing remains insufficient without the direct item
    victim-ring/callback discriminator. Reverted.
  - A broad version of the new item-hitlist seed lane that included Falco kind-55 attached ThrowLw
    rows seeded QGD primary Falco lasers and reopened primary to `590` (`F14c=15`, `F14d=27`) while
    worsening aggregate to `5005` (`F14c=295`, `F14d=90`). The retained lane stays Fox-kind-54 only
    until Falco's item callback/victim-ring discriminator is captured separately.
  - Broad grounded laser BODY sweep was tested against aggregate because decomp laser collision
    uses the item previous-to-current segment (`itFoxlaser_UnkMotion1_Phys` / `it_8029C4D4`), but
    the unconditional widening regressed aggregate total to `5628` with `F14c=503`, `F16d=238`,
    and `F15b=134`; reverted. The remaining grounded BODY rows need the exact hurt-status /
    hitlist discriminator, not a generic sweep.
  - A Dash/Turn coarse AABB miss-only extension for the remaining `GAT:7215` row did not move the
    target after runtime refresh, so it was dropped. A DownBound hidden-colanim suppressor for
    `BHH:641` likewise did not move the false-consume row and was not retained.
  - Throw-side item-hitlist carry keyed on `instance_hit_by == item.xDA8` was tested as a direct
    F14c owner. It regressed aggregate to `7087` mismatches (`F14c=1755`, `F14d=309`), so xDA8
    attribution alone is not the hidden item hitlist/cursor owner; reverted.
  - Broad ThrowHi/ThrowB callback-phase visible proxies were rejected. Same-character first-pending
    ThrowHi consume worsened aggregate to `4881` (`F14c=220`, `F14d=62`), and unconditional ThrowB
    terminal consume reopened primary (`F14c=20`, `F14d=4`). The retained ThrowB branch stays on the
    terminal hitlag/action-frame callback phase and explicit startup carry/consume split.
  - Falco kind-55 attached ThrowLw hitbox-mask seeding was first tested as an item-wide latch after
    v10 dumps showed initial victim entries on hitboxes 2/3. That reopened primary F14d on
    `QGD:443/4094/8111` because the BODY callback still needs hitboxes 0/1 eligible in the next
    phase. The retained version represents that callback phase explicitly: hb2/3 seed as victims_1,
    hb0/1 remain eligible, and only the frame-28 no-pre-hitlag spawn callback is admitted.
  - Broad ThrowHi live-article authority and all-character front-side consume were rejected during
    the same-character callback pass. Suppressing every live first-pulse frame-20 fallback reopened
    primary to `773` (`F14c=245`) and aggregate to `5295` (`F14c=650`); consuming all front-side
    first-pulse articles reopened primary to `583` (`F14c=55`). The retained rule is limited to
    same-character Fox callback rows and keeps cross-character and Falco phases on their existing
    owners.
  - A same-character ThrowHi empty-`last_attack_landed` same-frame hb0 destroy helper was tested from
    PJO/TCH event evidence. Without a hidden phase field it destroyed adjacent carry rows and raised
    aggregate F14c to `70`; adding the current geometry predicate missed the motivating rows. It was
    reverted pending a direct phase/pose discriminator.
  - A no-hitlag Falco ThrowB terminal carry narrowing fixed `PRH:8385` but over-advanced combo on the
    primary `GAT:2522` terminal control, so the existing terminal suppressor remains until source
    evidence separates article serialization from combo/source bookkeeping.
  - Hidden `x198C` BODY suppression and stale-submotion SpecialN loop shot gating were also tested
    after the disabled-contact slice; neither moved aggregate beyond the retained branch, so both
    were dropped.
  - Throw state-1 fresh-article collision deferral was tested both alone and paired with an
    already-attributed victim clear. Both variants regressed aggregate to `5546` with
    `F14c=600` and `F14d=106`; rejected. The remaining F14c mass needs the exact command-cursor /
    `throw_flags_b0` consumed state, not another spawn/despawn heuristic.
  - Removing the lower-bound counter update from `motion_entry_instance_id_override_u16` was tested
    for the pure blaster-gun xDA8 rows. It regressed aggregate to `5533` and `F16b=41`; rejected.
    The rows were moved to the adjacent instance owner instead.
  - A GuardReflect no-submotion origin-centered shield fallback was tested for F15b destroy rows.
    It regressed primary to `637` and primary `F15b=16`; rejected until the real
    `Item_80269DC8` shield-bounce-vs-HitShield discriminator is promoted.
  - Fox throw article victim-attribution heuristics were retested after the xDA8 hard move.
    A Fox-only fresh/clear pair keyed on `instance_hit_by == item.xDA8` regressed primary to
    `2626` and aggregate to `9879` (`F14c=4750`); rejected.
  - An intangible-hurtcap BODY contact extension was tested as a possible F16d sibling to the
    retained disabled-contact slice. It regressed primary to `791` (`F16d=159`) and aggregate to
    `5868` (`F16d=545`, `F16b=145`); rejected.
  - Hidden `x198C`/colanim hit-status substitution for item BODY eligibility produced no taxonomy
    movement (`F16d` stayed `31` primary / `146` aggregate); rejected.
  - Broad post-callback blaster-gun clear for any owner that no longer required a gun regressed
    primary to `1699` and aggregate to `8668` with large `F16d/F16b/F15b` regressions; rejected.
  - Miss-only unscaled BODY-offset fallback regressed primary to `767` (`F16d=148`) and aggregate
    to `5710` (`F16d=417`); rejected.
  - Replacing laser BODY sweeps with current-point probes after adding the x58/x4C scale split
    broke the retained `IAT:1580` phantom/tip-log BODY lock; rejected. The retained fix corrects
    previous/current endpoint scale without removing decomp-shaped sweeps.
  - A non-disabled `x198C` item-BODY skip was tested for the remaining false-consume rows. After
    narrowing around the retained disabled-contact slice it passed focused controls, but it still
    did not fix `HIS:6544`, `PPA:5141`, or `PRH:8137` once runtime timers refreshed; rejected.
  - Broad Illusion/Phantasm ghost[2] sweeps were tested after exposing the prefix-causal
    `ghostEffectPos[2]` lane. Applying ghost[2]->ghost[1] to all SetPhys rows created false
    Phantasm main-state BODY hits and worsened aggregate to `5291`; using seeded item position
    to ghost[1] as the default segment rose to `5325`. The retained slice keeps the old ghost[1]
    point owner by default and limits ghost[2] to the Fox Illusion end-state row shape proven by
    `DCC:4761`.
  - Blaster gun `misc0` / `xDD7` cursor evidence was inspected for F14c. A Fox ThrowHi mid-pulse
    suppressor keyed on gun `misc0==2` regressed primary to `741` and aggregate to `5517`
    (`F14c=610`); rejected. The row evidence still points to missing full throw command cursor /
    `throw_flags_b0` consumed state rather than a single replay-visible gun misc byte.
  - A stricter ThrowHi frame-20 command-boundary deferral plus crossed-prev replay was tested with
    existing prefix-causal lanes. It improved aggregate (`F14c=465`, `F14d=55`) but regressed
    primary to `741` with `F14c=140`; rejected until the full command cursor / consumed-pulse
    state is exposed.
  - Removing the Falco ThrowHi frame-20 stale-latch suppressor after adding the consumed-pulse
    count guard reopened primary item-owner rows (`primary total=568`, `F14c=40`), so it was
    reverted. A Fox ThrowHi mid-pulse live-shot suppressor likewise reopened primary
    (`primary total=658`, `F14c=130`) and worsened aggregate (`5166`, `F14c=505`); reverted.
  - A ThrowHi exact-boundary first/mid-pulse suppressor using live anim-frame equality and one live
    state1 article was tested after the retained command/hitlist ordinal gate. It reopened primary
    to `658` (`F14c=130`) while leaving aggregate at `4958` / `F14c=295`, so command-boundary
    equality is not retained as the missing hitlist/body discriminator.
- Status rationale: the coarse owner bucket is split and the named item-owner families are closed;
  former item-shaped rows now live under decomp-backed adjacent owners.

### Match-flow / entry / respawn ownership
- Status: `closed`
- Roadmap families: `F04_match_flow_rebirth`, adjacent identity-reset rows
- Owner boundary: entry, respawn, stock reset, rebirth, identity reset, match-flow state flags
- Primary sim files: `src/match_flow.c`, `src/api.c`, `src/timers.c`
- Acceptance bar:
  - respawn/entry ownership is grouped as one family
  - rebirth parity no longer depends on scattered timer repairs
- Closure notes:
  - Rebirth respawn platform/facing now keys `Player_GetSpawnPlatformPos` /
    `Player_GetFacingDirection` from the replay raw player slot (`seed_t.source_port0`), not the
    compact local sim player index. This closes aggregate-only respawn rows where local p0/p1 map
    to raw ports 1/2.
  - RebirthWait IASA now includes the priority aerial SpecialN path before the fallback Fall exit
    and applies the shared `ftColl_8007B7A4(..., p_ftCommonData->x5D8)` x1994/x198C colanim write
    on every RebirthWait exit.
  - DeadUpStar terminal source-owner carry is represented through the existing x18C8
    `source_clear_terminal_phase` seed lane, covering the pre-Rebirth stock-loss flow before
    `Fighter_UnkInitReset_80067C98` clears attribution.
  - Fresh taxonomy after forced preprocess and `make build`: primary total `525` with
    `F04_match_flow_rebirth=0`; aggregate total `4470` with `F04_match_flow_rebirth=0`.
  - Pure `F25_camera_box_visibility_x221f` rows remain under the camera-subject visibility owner
    (`ftLib_80086A8C` / `Camera_80030CFC`), not match-flow. The seeded
    `camera_target_point_inside_stage_cam_bounds_u8` lane is not by itself sufficient to close F25
    because opposite-outcome rows exist with the same inside-stage predicate shape.

## Residual Cleanup Families

### Locomotion timing tails
- Status: `residual cleanup`
- Roadmap families: `F11_locomotion_action_frame`
- Boundary: narrow timer/callback cleanup after larger locomotion owners settle

### Grounded transition tails
- Status: `residual cleanup`
- Boundary: surviving Dash/Walk/Turn/KneeBend timing edges after the shared selector family closes

### SpecialHi landing / fall continuation
- Status: `residual cleanup`
- Roadmap families: `F13_specialhi_landing`
- Boundary: narrow `SpecialHi` continuation work once the broader special-move family is decomp-shaped

### DamageFlyRoll admission / carry tails
- Status: `residual cleanup`
- Boundary: jump-aerial / `AttackAirB` carry behavior after the common damage-owner closure.
  Current retained slices include the F26 RNG stream seed surface and the DamageFlyRoll live XRotN
  hurtcap pose owner (`ftCo_8008DCE0 inlineA1` / `ftCo_DamageFlyRoll_Phys doFlyRoll`) used by late
  AttackAirB hurtbox-height selection.

### Throw / item pulse tails
- Status: `residual cleanup`
- Roadmap families: `F14_throw_item_bookkeeping`
- Boundary: throw-item bookkeeping and release-adjacent identity cleanup after shared throw-core closure

### Guard / item attribution tails
- Status: `residual cleanup`
- Roadmap families: `F15_guard_item_ownership`
- Boundary: guard-hit item ownership and shield/laser attribution after the main guard family closes

### Item identity tails
- Status: `residual cleanup`
- Roadmap families: `F16_item_identity_residual`
- Boundary: deterministic slot / instance cleanup after the seed/runtime split is fixed

### Per-throw pulse/article timing edges
- Status: `residual cleanup`
- Boundary: `ThrowLw` pulse-family and other per-throw article timing edges only if decomp proves they are truly per-throw

## Blocked / Split First

### Mixed-bucket split
- Status: `blocked / split first`
- Roadmap families: `F99_misc_other`
- Boundary: mixed residual bucket that still contains unrelated behaviors
- Acceptance bar:
  - split into named families before runtime patching
  - no direct `src/` work should target this bucket as-is

## Out of Scope / Later

- [ ] Broad roster expansion beyond Fox/Falco.
- [ ] 4-player doubles interactions that are not already shared by the current code paths.
- [ ] Camera, rendering, and audio.
- [ ] Low-value cosmetic parity gaps that do not move an owner family.
- [ ] Any new mechanic family unless it is an obvious high-leverage owner boundary.

## Usage / Handoff Rule

- Pick one shared owner family, read the decomp boundary first, replace bridge logic with the shared owner path, and leave only decomp-justified residuals.
- Do not call a family done while it still depends on a bridge or a replay-shaped row fix.
- When a family is effectively closed, add focused replay-real locks, validate, and update this checklist with the remaining residuals only.
