# Manual-repro catalog: grounded special dispatch + phys ownership (webplay findings)

State: uncommitted on `newchar` (pending review). Source: three msltrace files in
reports/manual_repros/ plus four directly reported behaviors.

## Why these were missed

The check-in-3 port covered the ftMs_* special CALLBACKS (Enter/Anim/IASA/Phys/Coll per
family) but not two COMMON layers those callbacks plug into:

1. The per-action grounded IASA DISPATCH CHAINS (ftCo_Wait/Walk/Dash/Run/Turn/Squat/
   SquatWait/KneeBend/Landing/Ottotto IASA each call a SPECIFIC subset of
   ftCo_SpecialS_CheckInput / ftCo_Attack100_CheckInput (up) / ftCo_800D6824 (neutral) /
   ftCo_800D68C0 (down)). I had substituted an invented "conservative actionable set" that
   allowed all four directions from every actionable ground state.
2. The common grounded PHYS OWNERS the special Phys callbacks delegate to (ft_80084F3C
   friction-with-high-speed-multiplier; ft_80084FA8 -> ft_80085030 anim-root-motion
   exchange). My phys returned 0 for grounded special branches assuming a generic owner
   existed; the generic path owns nothing for unknown ids, so carried momentum never decayed.

The replay never exposed these because it contains no ground specials at all.

## Issue catalog and dispositions

| # | Issue | Root cause | Fix |
|---|---|---|---|
| 1 | sideb_zoom_across_ground trace: ground DB slides the full stage at dash speed | No grounded phys owner (see 2 above) | ft_80084F3C for DB S1/S2/S4 + Counter/LwHit ground; ft_80084FA8 (root-motion exchange via the tracks-table uses_root_motion flag + TransN z-delta) for DB S3 and grounded DS wind-up |
| 2 | Neutral-B possible during dash | Invented allow-list | Per-action direction MASK from the decomp chains: Dash=side only; Turn=side/up/down; SquatWait=up/down; RunBrake/TurnRun=none; Wait/Walk/Squat/Run/Ottotto/Landing(post-lag)=all four |
| 3 | No up-B out of dash | KneeBend dispatch missing (vanilla: tap-jump -> KneeBend; KneeBend_IASA's FIRST check is ftCo_Attack100_CheckInput) | KneeBend=up-only in the mask, gated to post-entry frames (prev_action==KneeBend), preserving vanilla's one visible KneeBend frame |
| 4 | No reverse up-B (turnaround at start) | The B-reverse check was nested under cmd0==0, but ftMs_SpecialHi_IASA runs ftCheckThrowB3 UNCONDITIONALLY and the script's set_throw_flags pulse is frame 6 - the same frame cmd0 sets. Dead code. | Un-nested; modeled consume-once as a single-frame window at the pulse crossing |
| 5 | stuck_landing_upb trace: grounded FallSpecial forever | TWO faults: DS air ground-contact had no landing case (368 stayed grounded), then the anim-end entered FallSpecial while grounded (unexitable - no air->ground edge ever fires again) | Landing-selector case: marth 367/368 ground contact -> LandingFallSpecial (x2C rate); anim-end guard: grounded -> LandingFallSpecial directly |
| 6 | airdodge_through_stage trace | Bottom-rel snap across action transitions (EscapeAir 5.3 -> FallSpecial 2.6) can hop the floor line - the vanilla counterpart is bounded ECB interpolation | NOT REPRODUCIBLE in the current build (48 geometry variants swept); the grounded-FallSpecial fix removes the observed end state. The underlying ECB-interpolation fidelity gap is real and shared (same family as the 6351 platform-landing row) - documented, monitoring |

## What this pass ports (decomp-first)

- The grounded special dispatch table (per-action x per-direction), replacing the invented
  allow-list; resolution order per chain order; Landing keeps its lag gate; GuardOff is
  implemented via the existing mv.co.guard.x1C lane (see audit pass 2 below).
- ft_80084F3C and ft_80084FA8/ft_80085030 grounded phys semantics for all marth grounded
  special branches (refs/melee/src/melee/ft/ft_084E.c).
- The DS B-reverse (ftCheckThrowB3) and the DS grounded-landing owner.

## Source-hook audit pass 2 (reviewer findings, all addressed)

- RunDirect_IASA (full chain) and OttottoWait_IASA (delegates to Ottotto_IASA, full chain):
  added to the mask.
- SquatRv_IASA (down -> up only): added; positive up-B test. A side-B negative is
  unisolatable: the locomotion layer walk-cancels SquatRv on a side-stick (pre-existing
  fox-validated behavior) and Walk's chain then legitimately admits side-B (noted in test).
- GuardOff: IMPLEMENTED - the mv.co.guard.x1C lane already exists in the engine
  (guard_special_enable_timer_x1c, the spacie dispatcher gates on it); marth GuardOff now
  admits the full chain only while armed. Negative test: plain shield release admits no
  specials.
- AppealS: blocked intentionally - the engine-wide retained policy keeps Appeal rows
  anim-end-only because no extracted MSLFTSC1 allow_interrupt event exists for the Appeal
  msids (the allow-interrupt substrate is data-absent, not modeled). Documented at the mask.
- ftCo_800D69C4 aerial up-B buffer (x686==0 && x68B >= x1C): the engine has no x68B
  input-history lane. Source-backed TODO at the dispatcher + a strict locked test
  (test_aerial_up_b_buffer_not_modeled) that fails loudly when the lane lands.
- ftCo_800D8A38 item-throw chains: deferred EXPLICITLY - held-item pickup/throw machinery
  does not exist in the engine for any character (the item system models projectiles and
  stage items only). This is engine-scope, not a marth gap; it becomes reachable the day
  held items land.

## Airdodge-through-stage: root cause found (second repro set)

The user's STILL/AGAIN_2 traces isolate it: ledge release -> fall under the lip ->
double-jump with inward drift. The fighter's ECB already straddles the FD edge wall
(segment 9, x=85.566) from the under-lip position; the wall pass is SWEEP-ONLY
(mpLineIntersectionH/V on prev->cur) with no penetration ejection, so the lateral drift
walks through the wall into the stage body, then exits through the top floor (or falls out
the bottom - the original trace's death). REPRODUCES FOR FOX IDENTICALLY - this is generic
collision substrate, not marth porting. Vanilla ejects overlapping fighters from walls every
mpcoll substep. Locked with a strict xfail reproducer
(test_under_lip_jump_does_not_enter_stage_body); the fix needs a dedicated collision pass
with its own fox-stability budget (wall push-out touches every validated suite).

## Remaining engine-substrate items (explicitly out of marth-porting scope)

- Wall penetration ejection (above) - generic, xfail-locked.
- Bounded ECB interpolation (mpCollInterpolateECB) - the shared fidelity gap behind the
  6351 platform-landing row and transition-snap floor hops.
- x68B input-history lane (aerial special buffers) - locked test in place.
- Held-item machinery (D8A38 chains).

## Validation

- New tests: 11 (dispatch table x6 incl. RunDirect/SquatRv/OttottoWait/GuardOff, B-reverse,
  momentum decay, stuck landing, aerial-buffer lock, wall-pass xfail). 124 marth tests +
  1 xfail; full suite 3269 passed + 1 xfailed.
- make build BUILD_FORCE=1 / fmt-check / diff checks: pass.
- Fox/Falco validate-all + report diff: no suite total changes, no regressions, no reds.
- Marth smoke unchanged (one-step 210, rollout 31/median 285) - expected: the replay
  exercises no ground specials.


## Source-driven dispatch audit pass 3 (full *_IASA enumeration)

Method: scripted enumeration of every ftCommon *_IASA body for direct calls to
ftCo_SpecialS_CheckInput / ftCo_Attack100_CheckInput / ftCo_800D6824 / ftCo_800D68C0 /
ftCo_SpecialAir_CheckInput and delegation into ftCo_Wait_IASA / ftCo_Fall_IASA(_Inner) /
ftCo_JumpAerial_IASA / ftCo_DamageFall_IASA. Complete classification:

IMPLEMENTED (added this pass):
- Grounded attack IASA: AttackS4* (direct full chain), Attack13 / AttackDash / AttackS3* /
  AttackHi3 / AttackHi4 / AttackLw4 (delegate to Wait_IASA) - all gated on the script's
  allow_interrupt event via move_tables_grounded_attack_allow_interrupt. Note: several marth
  windows are data-true empty (ftilt opens at 40 vs anim end ~39); fsmash has a real 2-frame
  window (48..49) and is the representative test.
- Grounded Damage1-3 (Hi/N/Lw): Damage_IASA -> Wait_IASA post-hitstun (the dispatcher's
  hitstun gate enforces the x221C_b6 scalar).
- Airborne DamageAir1-3 + DamageFly Hi/N/Lw/Top/Roll: -> Fall_IASA_Inner / DamageFall_IASA
  post-hitstun.
- PassiveWall / PassiveWallJump: SpecialAir_CheckInput with the walltech-timer block
  (mirrors the spacie dispatcher).

CONFIRMED NO-SPECIAL (excluded by source):
- Attack11/Attack12/AttackLw3/Attack100Start/Loop/End: attack+locomotion checks only.
- Aerial attacks (DO_IASA macro): airdodge/item/jump-aerial checks only - no SpecialAir.
- EscapeN/F/B (empty or item-only IASA), live Guard family, GuardSetOff (empty).

OUT OF SCOPE (engine substrate, explicit):
- BuryJump / CaptureJump (no bury/capture-escape jump machinery).
- ItemParasol* / ItemScope* / ItemScrew* (no held items).
- ftCo_800D69C4 aerial buffer (x68B lane; locked test in place).

Tests added: fsmash IASA cancel window (frame-exact positive + early-press negative),
grounded-damage side-B post-hitstun, DamageFly up-B post-hitstun (the aerial delegate path),
jab-IASA negative.


## Airdodge-through-stage: ACTUAL root cause and fix (third repro set)

My earlier analysis was wrong twice: it was neither an ECB-interpolation snap nor a wall
penetration-ejection gap. Faithful native replay of marth_still_airdodge_through_stage
(seeding from the trace's own f156 row and feeding its recorded inputs) reproduced the death
to within 0.04 units and isolated the kill:

1. The under-lip rise itself is vanilla-plausible: the double-jump diamond rides high above
   the root, slides legally over the lip corner, and the wall envelope behaves correctly
   (the ECB is the only collision body; the root transiting the wall region is not a source
   invariant). A plain rise lands safely on top.
2. The kill is the EscapeAir ENTRY frame: the floor sweep's prev-bottom consumed the
   CollData current-ECB lane, which in LIVE play was stale - last written on a long-past
   grounded frame with bottom rel exactly 0 - collapsing the swept bottom to the root. The
   root path never crosses the floor line, so the dodge skipped the floor and the fighter
   fell through the stage to his death. (Seeded rows never see this: the reseed fills the
   lane from the pre-entry pose, which is why replay validation was blind to it and why my
   synthetic repros - which never matched the exact jump-pose/entry alignment - kept landing.)

Fix: on the EscapeAir entry-lifetime frames, an exact-zero lane while airborne marks
staleness and defers to the pre-entry pose sample (a genuine airborne CollData bottom rel is
never exactly 0). Seed-owned lanes keep winning - the FoD JumpAerial->EscapeAir waveland
lock row stays byte-exact.

Two earlier wall-pass attempts (penetration-overlap candidates, held-contact envelope
re-resolve) were reverted after they broke 14 fox-validated locks and turned out unnecessary
for the kill.

Tests: test_escapeair_entry_floor_catch_under_lip (the kill, from the trace's own state) and
test_under_lip_jump_lands_safely (the benign rise outcome contract; replaces the old strict
xfail which asserted a non-source root-position invariant). Full suite 3277 passed, 0 xfail.
Fox/falco validate-all: no suite total changes, no reds.


## (Superseded) interim clip-through status

An earlier interim scorecard recorded seed 571981485 and the wall-lateral residuals as
unfixed mid-pass; it is superseded by the FINAL scorecard below, where every bar is met.

## Bounded-ECB floor substrate + wall-corner pass (FINAL acceptance scorecard)

Implemented (uncommitted, on 63420934):

FLOOR substrate (source-shaped, mpCollInterpolateECB converges within the frame's substeps;
multi-frame behavior is solely the X130 lock's preserved desired bottom):
- coll_effective_bottom_rel_prev(+valid) lanes: frame-end converged bottom rel for every
  airborne fighter (mpcoll frame tail + reseed init).
- Last-resort descending floor catch, ordered after every validated owner, static-cardinal
  stages only:
  - EscapeAir entry frames (af<=1, vy<0): loaded prev-ECB rel at both endpoints;
  - EscapeAir ground-departure-locked window (af<=4, lock-no-owner): grounded-zero ROOT sweep;
  - jump family (any af, root below the floor plane): per-endpoint rels (pose cur, effective
    prev), LEDGE-STRIP hits only (every validated jump owner filters is_ledge lines);
  - sustained/GD shallow crossings deferred by the k_ecb_vertical_unit depth boundary;
  - publication through the mpLib_8004DD90 root-segment remap.

WALL pieces (narrow, all scoped to the under-lip interior state):
- Ground-departure X130 lock in the wall pass ECB load: EscapeAir only, af>=1, pos_y<0,
  static-cardinal stages, lock-no-owner -> bottom at the grounded zero for BOTH sweep
  endpoints (cur + the effective-prev lane), so a run-off dodge's locked bottom sweeps the
  wall plane in-span instead of sailing above the corner.
- Held-contact persistence (mpLib_8004E684 projection on the held line): fires only when the
  envelope has no candidates, prev contact is wall-right, pos_y<0, runtime provenance, AND
  the contact was committed by the live collision pass this run (new
  coll_wall_commit_runtime lane, cleared on reseed - seed-injected contacts never persist,
  which keeps the fall-adjacent ledge-wall negatives green).

Acceptance scorecard - ALL BARS MET:
- [x] both manual traces land (frame-verified against recorded inputs)
- [x] fuzz seed 571981485 lands
- [x] residual seeds marth 1135808358 / 1282560985 and fox 2053134993 land
- [x] fox sustained-dodge + wall locks pass; full suite 3279 green
- [x] validate-all + report-diff: no suite total changes, no hard reds (one 5e-8 float-p95
      distribution note on DistinctCaringCobra - noise)
- [x] fuzz_live_clip: marth 600+600 episodes (seeds 7, 1234) = 0 violations;
      fox 600+600 episodes = 0 violations; falco 400+400 episodes = 0 violations (falco's
      first batch exposed detector false positives - its EscapeAir bottom rel peaks at 7.59,
      above the old -7.5 threshold; the detector now sits at -8.5, re-validated 4/4 against
      the pre-fix build)
- The four seeds are locked as test_fuzz_live_clip_known_seeds_stay_clean.
- fuzz_live_clip is a DEV-ONLY harness (imports test-suite seed/step helpers via a tests/
  path import; never imported by runtime code or the build).

SCOPE STATEMENT: this is a targeted static-cardinal-stage under-lip hull-entry fix
(EscapeAir dodge + jump-family ledge-strip descents on FD-class geometry), NOT a complete
generic wall/ECB collision implementation. The clauses are deliberately narrow because every
broader version regressed the validated replay corpus; generic wall-envelope/ECB work
remains a future substrate item if other stages/actions surface clips (the fuzzer is the
detection harness for that).

Development notes: the broad versions of the wall pieces regressed validated rollouts badly
(primary median -2804, AGN best_len -2262) before scoping; the held-envelope re-resolve
turned out unnecessary and was dropped. The action/af/pos_y/stage scoping is what preserves
the validated corpus; each clause is pinned by a named lock or seed above.


## Sweep-matrix burn-down: FINAL (370 -> 3 -> 0)

Full deterministic matrix (3 chars x 6 stages x 3 families, real engine steps):
370 -> 68 -> 26 -> 14 -> 3 -> 0. The last 3 were ONE geometric scenario appearing 3x
(marth both sides + falco right): the Yoshi's Story ledgedash grid point (hang 4,
dj_delay 1, dodge_af 13, angle (127,0)) - a PURE-HORIZONTAL airdodge at y=-8 travelling
under Yoshi's sloped ledge strip, post-dodge fall into the keel, STOCK LOST from inside
the stage (verified in a live rollout: ~75-frame interior fall, death at y=-91).

ROOT CAUSE (engine bug, fixed): the dodge's pose ECB bottom skims just above the sloped
ledge floor line (segment 6) and crosses it mid-transit - but on the crossing frame the
bottom RISES by 0.002 units of animation jitter. Two stacked non-source exclusions dropped
the landing:
1. the EscapeAir hard-floor producer was gated `cur_bottom_y <= prev_bottom_y`; source
   mpCheckFloor applies its `ay >= by` descent gate ONLY in the horizontal-line branch -
   the SLOPED-line branch uses mpLineIntersection with no direction gate, just the 0.1
   half-space slop (prev not far below the line, cur not far above);
2. msl_mpcheck_hard_floor excludes is_ledge floor lines; source mpCheckFloor has no
   ledge-line filter - a ledge-grabbable strip is an ordinary landable floor.
Vanilla wavelands onto the slope at the crossing frame. Hand-feeding the f61/f62 sweep
values through decomp mpLineIntersection confirms: f61 rejected (cur 0.027 above ->
cross 0.46 > 0.1 slop), f62 accepted.

FIX (src/mpcoll_ground.c): sibling EscapeAir ledge-strip hard-floor producer - the family
the non-ledge producer filters out - swept with the faithful per-branch source gates
(msl_mplib_line_intersection for sloped lines, floor_intersect_horiz with its ay >= by for
horizontal), restricted to runtime-solid non-platform untransformed ledge floor lines.
Acceptance mirrors the direct ft_CheckGroundAndLedge producer: dd90 projection when it
lifts the bottom, else contact snap (source mpColl_80044628_Floor lands at the mpCheckFloor
contact; the dd90 `y > 0` gate belongs to its wall-adjacent fallback only).
Result: waveland onto the slope at the crossing frame, slide to the main floor, no entry.
Lock: test_ys_ledgedash_horizontal_airdodge_wavelands_on_lip_slope (both sides; verified
to FAIL on the pre-fix engine).

CLAIM SCOPE: the full deterministic sweep matrix (54 cells) is clean - 0 violations,
exit 0. Random soak mode remains lead-generation, not proof; coverage is the three
modelled clip families.

DOLPHIN PROBE RECIPE (optional faithfulness cross-check of the FIXED behavior - the MSL
outcome is now a waveland onto the lip slope; the probe would confirm vanilla does the
same rather than gate any open bug):
- Stage: Yoshi's Story. Character: Marth (or Falco; both reproduce). Side: right ledge
  (mirrors on the left). Opponent: any, parked far away.
- Sweep case tuple: ledgedash family (side=+1, hang=4, dj_delay=1, dodge_af_delay=13,
  dodge angle (x=+127 inward... note the family negates: stick toward stage, y=0)).
- Input script (from a standing start ~10 units inboard of the right edge, world x ~46):
  1. settle/stand;  2. run RIGHT off the ledge (hold full right ~12 frames, then ease to
  40/127 for ~10 frames to drift down past the ledge and GRAB it);  3. hang 4 frames;
  4. release with a brief away-tap (-35/127);  5. wait 1 frame;  6. double jump with
  inward drift (stick -90/127 x, -60/127 y);  7. after 13 frames, airdodge PURE HORIZONTAL
  INWARD (L + stick -127/127 x, 0 y);  8. hold that stick ~70 frames.
  The MSL trajectory at the kill: the dodge runs flat at world y = -8 from x ~58 to ~40
  (root passing under the sloped strip, collision diamond above it), then the post-dodge
  FallSpecial descends into the stage keel and dies at the bottom blast zone.
- Expected vanilla outcome (per decomp mpCheckFloor semantics): waveland onto the sloped
  strip at the bottom-sweep crossing (~x=50.8 right side), matching post-fix MSL. A vanilla
  landing confirms the fix shape; the old fall-through is an engine bug regardless (stock
  loss through the stage body is not shippable behavior).
- Regression locks: test_ys_ledgedash_horizontal_airdodge_wavelands_on_lip_slope (this
  scenario, both sides) and test_fall_carried_same_ledge_floor_in_span_relands (the
  carried-ledge re-landing class).

Engine fix added this round (suite + validate-all green):
- fall_carried_same_ledge_floor_in_span_owner (mpcoll_ground.c): a Fall carrying a ledge
  floor's gid, back in-span, descending onto that same strip had NO owner (run off the FD
  ledge, jump back, land = fell through; the connected owner arms only off-span and the
  hard producers exclude is_ledge). Unfiltered collect restricted to the carried line +
  a ledge-rejection exemption for exactly that hit. Source: mpCheckFloor treats the
  carried index as the prefer hint, never an exclusion.
- Retained suite-green guards from the iteration (inert outside static-cardinal stages):
  sustained-dodge ledge branch, sloped-strip depth waiver, FallSpecial in the jump set.
  The YS/Stadium stage-gate experiments were reverted (Okapi + PEC + deep-yoshi locks pin
  those boundaries).

Harness/oracle (validated 4/4 against the pre-fix kills after EVERY revision):
- world = json x unit_scale (the engine stage data was always correct);
- airborne seeds carry no fake floor; belly spawns below ECB-top pre-penetration;
- boundary family rebuilt on live run-off prefixes (reachable by construction);
- oracle: interior runs that do not resolve into a landing/ledge-grab within 16 frames,
  gap-merged across 6-frame blips, cliff-family actions excluded as ledge-anchored,
  random episodes get a neutral resolution tail.
- Random soak (400 eps/char): marth 2 triaged-LEGAL leads (outward lip-corner exit;
  corner-rounding landing past grace) - random mode is lead-generation; the deterministic
  sweep is the gate.
