# RL 1.0 Completion Checklist

This checklist is the current family-level tracker for **finish-the-sim** work:
close shared owner families with decomp-first passes, leave only narrow
decomp-justified residuals, and avoid row-shaped bridge fixes unless the owner
family is already closed.

Last updated:
- Date: 2026-04-19
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
      Action/on-ground/jump landing bundles stay in `F13a`.
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
    - Common EscapeAir/FallSpecial/LandingFallSpecial action/on-ground bundles now move to
      `F13a_common_fallspecial_landing` because they are common landing/freefall timing, not ledge
      occupancy or CliffCatch edge ownership. Lone `ground_id` tails stay in `F10c`.
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
      terminal Ottotto enters OttottoWait through `ftCo_Ottotto_Anim -> ftCo_8009A6B8`. Replay-real
      locks cover `HilariousVillainousGiraffe:5095` and `TubbyCurlyHerring:10089`, with existing
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
- Status: `active`
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
- Rejected item-owner experiments:
  - Committing powershield reflect owner/xDA8 transfer on the same post-frame fixed `GAT:4828`,
    `GAT:6207`, and `TBK:7448` transfer rows and passed focused reflect locks, but it introduced
    new GuardReflect identity rows (`GAT:2274`, `GAT:2275`, `GAT:9479`) and raised aggregate `F15`
    from `151` to `155`. The change was reverted; these rows stay in `F15a/F15b` until the exact
    `ftColl_80077464` / `Item_80269F14` transfer-vs-bounce discriminator is modeled.
  - ThrowHi first-tick hidden-victim-ring suppression fixed inspected `BHH:527/1206` style rows
    but increased aggregate `F14c` from `484` to `629`; reverted.
  - Routing all ThrowB/ThrowHi/ThrowLw projectile creation away from `laser_should_shoot_on_frame`
    and through throw-pulse reconstruction broke the existing `QGD:3095` ThrowHi velocity lock;
    reverted.
  - ThrowHi crossed-prev mid-pulse reconstruction and Fox-wide mid-pulse stale-latch suppression
    were retested after rebuild. They reduced some inspected source/article rows but regressed
    aggregate `F14c` (`544` and `614` respectively), so both were reverted.
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
  - Blaster gun `misc0` / `xDD7` cursor evidence was inspected for F14c. A Fox ThrowHi mid-pulse
    suppressor keyed on gun `misc0==2` regressed primary to `741` and aggregate to `5517`
    (`F14c=610`); rejected. The row evidence still points to missing full throw command cursor /
    `throw_flags_b0` consumed state rather than a single replay-visible gun misc byte.
  - A stricter ThrowHi frame-20 command-boundary deferral plus crossed-prev replay was tested with
    existing prefix-causal lanes. It improved aggregate (`F14c=465`, `F14d=55`) but regressed
    primary to `741` with `F14c=140`; rejected until the full command cursor / consumed-pulse
    state is exposed.
- Status rationale: the coarse owner bucket is split and inspectable, but this checklist item is
  **not closed** because the named item owners still contain real runtime residuals.

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
