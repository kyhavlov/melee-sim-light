# RL 1.0 Completion Checklist

This checklist is the current family-level tracker for **finish-the-sim** work:
close shared owner families with decomp-first passes, leave only narrow
decomp-justified residuals, and avoid row-shaped bridge fixes unless the owner
family is already closed.

Last updated:
- Date: 2026-04-15
- Scope baseline: shared throw/thrown substrate, guard, and locomotion / grounded transition /
  motion-entry owner families effectively closed; checklist reflects post-closure RL 1.0 priority
  ordering.

## Status Legend

- **Closed / effectively closed**: the shared owner path is in place; remaining behavior is narrow per-move or adjacent-family detail.
- **Active / next deep pass**: a shared owner family that still needs a completionist decomp audit and cleanup pass.
- **Residual cleanup**: the shared owner is largely closed; remaining work is narrow and should stay within that family boundary.
- **Blocked / split first**: do not patch runtime yet; first make the family seed-visible, decomp-visible, or split out of a mixed bucket.
- **Bridge**: compensating logic that exists because a shared owner path is still missing or mis-owned.
- **Residual**: a real move-specific or adjacent-family difference that should remain if decomp supports it.

## Priority Order

Recommended sequence for the next deep passes:

1. **Knockdown / passive contact owner**
2. **Core combat followup family**
3. **Fox/Falco special-move families**
4. **Ledge / collision-env parity**
5. **Item-owner seed cleanup + item identity family**
6. **Match-flow / entry / respawn ownership**
7. **Mixed-bucket split**

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
  - focused locks cover the Dash -> GuardOn -> GuardReflect BODY followup, locomotion -> GuardSetOff laser handoff,
    GuardSetOff hitlag-exit action-frame parity, the explicit GuardSetOff exit-rate lane, and GuardReflect active-timer handoff rows
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
    the lane is populated only for simultaneous fighter entries or hidden-prior-consumer rows.
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
- Status: `effectively closed`
- Roadmap families: `F07_knockdown_grounding` eliminated; previous
  `F18_damage_tech_timer_seed_surface` selector contradiction fixed; remaining contact substrate
  rows split to `F17_mpcoll_ledge_ecb_residual`
- Owner boundary: DamageFly contact owner for Passive / PassiveStand / DownBound / landing selection
- Primary owner files: `src/knockdown.c`, `src/input.c`
- Seed provenance support: `tools/slippi/seed_history.py`, `tools/slippi/make_dataset_from_slp.py`
- Adjacent/upstream residual owners: `src/mpcoll_ground.c`, `src/ledge.c`, `src/locomotion.c`
- Closure note:
  - `src/knockdown.c::enter_damagefly_ground_contact_followup` is the shared decomp-shaped selector
    for `DamageFly*` / `DamageFall` floor contact.
  - `src/input.c` and `tools/slippi/seed_history.py` now model
    `Fighter_Spaghetti_8006AD10_Inner1` hitlag-latched `input.x668`, so `x680` / `x684` tech
    debounce provenance matches the DownBound / PassiveStand selector rows.
  - `F17_mpcoll_ledge_ecb_residual` is row-level audited as upstream contact substrate:
    same-action `ground_id` / `jumps_left` / hurtbox drift, DamageFly-vs-Passive one-frame
    floor-contact timing, and PassiveWallJump wall-contact timing. Combat and special-adjacent
    rows are not hidden in this bucket.
- Acceptance bar:
  - one damage-fly contact owner chooses grounded outcomes consistently
  - no ledge or ECB regressions

### 5. Core combat followup family
- Status: `effectively closed for post-admission followup; upstream geometry/pose split active`
- Roadmap families: broad `F08_damage_resolution_combat` is eliminated from current cardinal/aggregate taxonomy;
  broad `F09_aerial_combat_resolution` is eliminated after splitting aggregate-only aerial action-entry
  and contact-hitlag residuals, but those residual owners still need closure/proof before this family can
  be called complete.
- Owner boundary: BODY hits, aerial continuation, hitlag/hitstun/continuation ordering after damage admission
- Primary sim files: `src/combat.c`, `src/timers.c`, `src/items.c`, `src/step.c`
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
    `lbColl_8000805C`. `SSDYNN01` v2 owns the dynamic-collision submotion predicate, and runtime
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
- Status: `active / next deep pass`
- Roadmap families: `F13_specialhi_landing` plus mixed special-move slices currently split across `F99`
- Owner boundary: `SpecialN`, `SpecialS`, `SpecialHi`, `SpecialLw`, `ThrownLw`, and their article/pulse/landing owners
- Primary sim files: `src/items.c`, `src/combat.c`, `src/anim_timebase.c`, `src/locomotion.c`, character-specific modules
- Acceptance bar:
  - special moves are grouped by decomp owner, not patched row-by-row
  - `SpecialHi` landing/fall and `SpecialLw` / `ThrownLw` pulse behavior live in named family work, not misc buckets

## Remaining Major Owner Families

### Combat geometry / HSD pose collision owner
- Status: `effectively closed; remaining former-F08b-shaped rows moved to action-timebase, damage-selection, or special-entry owners with row-level proof`
- Roadmap family: former `F08b_body_contact_geometry_residual` no longer emits as one broad bucket
  in the refreshed taxonomy, and no parent-owned collision/pose residual bucket remains.
  The implemented runtime/seed sub-owners cover the shared BODY primitive surface: SSDYNN01
  dynamic-chain collision pose (Fox AttackDash/AttackHi3 owner predicate), Turn internal-facing hurtcaps, per-HitCapsule `victims_1`
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
    dynamic-chain collision matrix. Dynamic collision applicability is an extracted `SSDYNN01` v2
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
  - the Fox AttackHi3 / SSDYNN01 dynamic-chain collision-pose sub-owner covers the target-domain
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
    point-vs-segment tests. It then suppresses active same-group HitCapsules at or after the
    clanking slot as `ftColl_80078C70` / `ftColl_8007699C` does through the per-HitCapsule
    collision loop and `inlineA0`/`inlineA1`. A later same-group clank cannot retroactively
    suppress an earlier BODY hitbox. The clank path also ignores
    replay-reconstructed BODY victim rings as a prefilter, because those rings are seed
    reconstruction for BODY admission and can otherwise mask a live hitbox-vs-hitbox clank; the
    HHG:8674 and FSP:467 locks protect this distinction. This removes the ReboundStop/clank split
    without a BODY admission bridge, though scalar hitlag residuals remain outside the BODY
    admission decision.
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
- Status: `active`
- Owner boundary: ledge occupancy, refresh, grab-mask ownership, FD edge behavior
- Primary sim files: `src/mpcoll_env.c`, `src/ledge.c`, `src/step.c`
- Acceptance bar:
  - edge behavior owned by collision substrate rather than action-local exceptions

### Item-owner seed cleanup + item identity family
- Status: `active`
- Roadmap families: `F14_throw_item_bookkeeping`, `F15_guard_item_ownership`, `F16_item_identity_residual`
- Owner boundary: item owner/slot identity, throw-item bookkeeping, guard-hit item attribution, seed/runtime ownership split
- Primary sim files: `src/items.c`, `src/combat.c`, `src/api.*`, `tools/slippi/seed_history.py`
- Acceptance bar:
  - item rows stop masquerading as combat/guard rows
  - remaining item identity behavior is deterministic residual cleanup

### Match-flow / entry / respawn ownership
- Status: `active`
- Roadmap families: `F04_match_flow_rebirth`, adjacent identity-reset rows
- Owner boundary: entry, respawn, stock reset, rebirth, identity reset, match-flow state flags
- Primary sim files: `src/match_flow.c`, `src/api.c`, `src/timers.c`
- Acceptance bar:
  - respawn/entry ownership is grouped as one family
  - rebirth parity no longer depends on scattered timer repairs

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
- Boundary: jump-aerial / `AttackAirB` carry behavior after the common damage-owner closure

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
