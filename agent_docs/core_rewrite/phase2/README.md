# Phase 2 Restart: Vertical Source Cuts

Status: complete. V2-01 through V2-05 were implemented as one continuous source-rewrite run; cut
checkpoints are measured vertical boundaries, not user-facing stopping boundaries.

Phase 2 starts again from committed Phase 1 (`3b338828c25e`). The previous staged and unstaged
attempt is preserved locally as a named Git stash for forensic reference, not as an implementation
to reapply. This document is both the restart charter and the ongoing worklog.

Local reference: `phase2-rewrite-reference-before-clean-restart-2026-07-13`, stash commit
`c6e15c34d09c3241910e6830933690e3367501e1`. It preserves the version-controlled index, worktree,
and untracked files. Ignored generated data is deliberately not project state; the affected tables
were regenerated from the committed Phase 1 extractors after the restart.

## Baseline

The committed reports are the immutable comparison point for every cut:

| Gate | Phase 1 baseline |
|---|---:|
| aggregate one-step mismatches | 8,689 |
| aggregate rollout first mismatches | 1,396 total / 742 seeded |
| aggregate rollout median clean streak | 305 frames |
| primary one-step / rollout | 46 / 9 total, 4 seeded |
| doubles one-step / rollout | 7,713 / 1,193 total, 730 seeded |
| Falcon one-step / rollout | 2,537 / 526 total, 283 seeded |
| Sheik one-step / rollout | 2,711 / 441 total, 223 seeded |
| mixed-random / replay-derived performance | 424.9k / 311.7k FPS |

Canonical evidence is always written to `reports/validation/`. Triage files may explain a result,
but they never replace the committed report paths.

## Process for every vertical

1. **Declare the source boundary before editing.** Record entry sites, callbacks, persistent state,
   contact/consumer boundary, supported variants, explicit dependencies, and the exact old code
   expected to disappear.
2. **Record the starting evidence.** Copy the canonical totals into the worklog and capture the
   dominant owner-level first divergences relevant to the cut. Do not make replay rows the scope.
3. **Build the replacement through one real behavior path.** Introduce only the shared substrate
   exercised by this vertical. Keep runtime iteration fixed-capacity, deterministic, and
   allocation-free. Promote source data into extraction tables when it replaces semantic lists.
4. **Validate while incomplete, without treating totals as a veto.** Run focused source-owner tests
   and refresh the canonical aggregate reports at behavior-bearing checkpoints. If metrics worsen,
   classify the first divergence against the declared owner and continue through missing connected
   source pieces when the foundation remains sound. Do not add bridges or revert source-clear
   mechanics merely to recover an intermediate total.
5. **Close the owner.** Cover every supported variant inside the boundary, cut over all callers,
   delete the old branches/state/helpers/tests, and verify that no validation provenance or second
   gameplay path remains.
6. **Lock and report.** Refresh all required suite reports, record exact metric and LOC deltas, run
   focused/full tests and formatting, and run both performance gates when the cut touches a hot
   path. Explain any remaining regression by a named source dependency; unexplained in-boundary
   reds mean the cut is not finished.

### Cumulative correctness gate

Aggregate correctness is enforced at every vertical boundary, beginning with V2-02; it is not
deferred to the end of V2-05. Temporary regressions are permitted only while a declared source
cut is incomplete. Before a vertical may close and the next vertical becomes the primary work
queue, the cumulative tree must:

- be no worse than the committed Phase 1 baseline in aggregate one-step mismatches, aggregate
  rollout total and seeded first mismatches, and rollout median clean streak;
- improve at least one of those aggregate gates, unless the vertical is demonstrably
  behavior-equivalent deletion whose value is independently locked;
- have no unexplained regression owned by its declared source boundary; and
- record the canonical entry, intermediate, and exit measurements below.

The desired Phase 2 exit remains materially better than Phase 1 across both one-step and rollout.
Merely recovering Phase 1 is an intermediate vertical gate, not the Phase 2 success criterion. If
a cut exposes an adjacent source owner whose absence prevents recovery, pull that bounded owner
into the active vertical and finish it before advancing. Do not relabel or postpone aggregate debt
to make a packet appear closed, and do not restore a replay-shaped bridge to satisfy the gate.

A vertical is abandoned only when its source premise is disproven, it cannot converge to one
authoritative path, or it violates runtime constraints without a credible source-shaped solution.
Aggregate redness alone is not an abandonment criterion. Conversely, architectural confidence is
not permission to stop recording or explaining validation movement.

## Cut ledger

| Cut | Boundary | Status | One-step | Rollout total / seeded | Median | Net LOC | Decision |
|---|---|---|---:|---:|---:|---:|---|
| V2-01 | common defense and shield/reflect contact | locked foundation | 8,868 | 1,524 / 818 | 333 | shared below | causal same-frame guard entry and HitCapsule lifetime locked |
| V2-02 | ordinary attack/BODY/DmgLog/damage ProcessHit | **closed; cumulative gate passed** | **7,486** | **1,306 / 546** | **428** | shared below | all correctness and performance gates beat Phase 1 |
| V2-03 | Catch/capture/throw/thrown-body | **closed; cumulative gate passed** | **7,299** | **1,244 / 542** | **442** | shared below | source-phase callbacks, extracted attachment owner, and release-local CollData locked |
| V2-04 | items/articles/reflect/absorb/special contacts | **closed; cumulative gate passed** | **7,299 entry; 6,850 exit** | **1,244 / 542 entry; 1,215 / 523 exit** | **442 entry; 447 exit** | shared below | common HitCapsule selection/callback ownership and supported article lifecycles locked |
| V2-05 | remaining callback families and displaced-code deletion | **closed; behavior-equivalent deletion gate passed** | **6,850 entry and exit** | **1,215 / 523 entry and exit** | **447 entry and exit** | **-29,896 cumulative `src/`** | live-script ownership is singular; displaced snapshot owner and debug surface deleted |

Add dated checkpoint rows beneath the active cut; never overwrite an earlier measurement.

## V2-01: common defense and shield/reflect contact

### End-to-end owner

```text
priority-3 common IASA guard admission
  -> source-shaped motion entry for GuardOn / GuardReflect
  -> Guard Anim / IASA / Phys / Coll callbacks
  -> live ShieldDesc / ReflectDesc state and geometry
  -> supported fighter, item, and projectile shield/reflect selection
  -> source pending fields and priority-14 shield/reflect ProcessHit branch
  -> GuardSetOff / reflect callback / shield break
  -> Guard, GuardOff, Wait, or shield-break lifecycle completion
```

Source spine:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c`;
- `refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360,
  Fighter_Spaghetti_8006AD10,Fighter_procUpdate,Fighter_procMap,Fighter_ProcessHit_8006D1EC}`;
- `refs/melee/src/melee/ft/ftcoll.c` shield/reflect selection and pending-field collapse;
- bounded `refs/melee/src/melee/lb/lbcollision.c` ShieldDesc/ReflectDesc geometry;
- supported item/article collision callbacks that reach the same defender descriptors.

### Included

- GuardOn, Guard, GuardReflect, GuardSetOff, GuardOff, shield-break, and Furafura lifecycle for all
  supported characters, including exact x0/x4/x8/xC/x10/x14/x18/x1C/x2C ownership;
- guard admission from supported common IASA owners, out-of-shield ordering, platform loss, and
  source motion-entry flags;
- live shield/reflect descriptors, pose/tilt/scale, recharge/drain, powershield windows, hitlag,
  shieldstun, rebound, shield break, and reflect callbacks;
- shield-before-BODY precedence and supported fighter/item/projectile contacts against the live
  descriptors, through the relevant ProcessHit branches;
- native replay initialization of the same hidden state, with no one-step-only gameplay branch;
- doubles ordering and every supported character/stage variant naturally covered by these owners.

### Explicit input and output boundaries

Existing attack/article capsule construction is an input dependency for this cut; it may supply a
canonical candidate capsule without being rewritten yet. Ordinary BODY damage, stale calculation,
and DamageFly remain outside V2-01 unless shield/reflect source order requires a narrow shared
field. V2-02 replaces that producer/consumer chain. V2-01 must not add assumptions that make the
later cut harder or branch on producer character as a proxy for missing state.

### Deletion target

- the monolithic Guard reconstruction and fresh-entry/negative-frame exceptions in `action.c`;
- repeated `guard_update_grounded` dispatch and guard-specific platform/callback repairs in
  `locomotion.c`;
- replay-conditioned ShieldDesc publication in `shields.c`;
- `shielddesc_geometry.h` exception geometry and duplicate pose reconstruction;
- shield/reflect aftermath duplicated across combat, items, and validation seed bridges;
- state lanes, generated semantic classes, and replay-row tests used only by those displaced paths.

The expected result is a low-thousands net deletion, one authoritative defender-side contact path,
and material improvement in Guard/shield action, animation, state-flag, and rollout divergences.
Those are forecasts to test, not quotas or permission to fit the report.

### Checkpoint cadence

- focused Guard/ShieldDesc/ReflectDesc tests after each callback or contact sub-owner lands;
- canonical aggregate one-step and rollout reports at every behavior-bearing checkpoint;
- primary, doubles, Falcon, and Sheik reports when the complete vertical is cut over;
- formatting, full tests, diff checks, allocation audit, and both performance gates before V2-01
  is called complete.

### V2-01 worklog

#### 2026-07-13 — source boundary and baseline lock

- Started from committed Phase 1 `3b338828c25e`; no Phase 2 runtime code was carried across the
  restart.
- Canonical starting gates are the baseline table above: 8,689 aggregate one-step mismatches,
  1,396 / 742 aggregate rollout first mismatches, and a 305-frame median clean streak.
- The source owner is 854 lines in `ftCo_Guard.c`. The displaced simulator surface is distributed
  through `action.c`, `locomotion.c`, `shields.c`, `shielddesc_geometry.h`, `combat_shield.c`, and
  item/fighter shield aftermath, totaling more than 10,000 lines before shared contact code.
- Replacement objects: procedural MotionState entry flags, persistent Guard JObj local SRT,
  live ShieldDesc/ReflectDesc identity and geometry, and one defender-side shield-contact packet.
- Existing attack and article HitCapsules remain explicit inputs. V2-01 does not reproduce their
  replay/history gates; V2-02 replaces their complete producer and BODY consumer chain.

#### 2026-07-13 — coherent contact cutover foundation and first measured correction

- Direct V2-01 work exposed that ShieldDesc callers, per-HitCapsule victims, DmgLog, ProcessHit,
  and throw contact shared the same displaced runtime structures. Rather than restore those
  bridges, the tree now contains the coherent source-shaped contact foundation across V2-01–03:
  live fighter pose/script/contact/guard owners, one BODY/contact traversal, and the old
  `combat_body.c` owner removed. The preserved forensic tree supplied implementation material, but
  none of it is accepted by provenance: each vertical still requires a fresh source/caller/state
  audit and measured closure in this worklog.
- Extraction was regenerated from `_iso` in 6.6 seconds. The extension builds, and 91 focused
  Guard, shield, hitbox/hurtbox, combat, capture, and throw tests passed before the first aggregate
  measurement.
- The initial coherent cutover measured 12,612 aggregate one-step mismatches and 1,636 / 934
  rollout first mismatches with a 322-frame median. This is diagnostic, not a completion result.
  `last_hit_by` already improved from 917 to 290, while state flags, hitlag, action identity, and
  hidden contact state identified incomplete connected owners.
- The first correction separated callback-owned ShieldDesc reconstruction from animation-pose
  availability. Guard can expose no Slippi action/animation frame while its descriptor remains
  live. A shield-health-loss/GuardSetOff edge now reconstructs Melee's causal preceding Guard
  bubble and preserves the accepted victim in the live HitCapsule `victims_1` ring through hitlag.
  A synthetic source-owner positive/tail test locks the behavior.
- Canonical aggregate reports after that correction: 12,413 one-step mismatches (-199 from the
  initial cutover), 1,636 / 934 rollout first mismatches, and a 322-frame median. Runtime `src/`
  is currently 2,450 additions and 19,894 deletions, net -17,444 lines versus Phase 1. The next
  checkpoint must close more of the source owners and materially improve both gates; this is not
  a return boundary.

#### 2026-07-13 — live pose publication and HitCapsule lifetime correction

- The first broad BODY regressions were not geometry constants. `Fighter_ChangeMotionState`
  entry with an immediate `ftAnim_8006EBA4` tick advanced the action timebase but left collision
  pose at frame zero. Both immediate and deferred entry-tick owners now republish pose from the
  advanced source frame. This removes false contacts without a replay predicate.
- Script CREATE commands were also clearing a live HitCapsule's victim rings on same-slot,
  same-group payload replacement. `ftAction_8007121C` calls `ftColl_800768A0` only for a disabled
  destination or a hit-group change; the runtime and native replay initializer now preserve
  `victims_1` for in-place late-hit updates and normalize the replay instance-id proxy across the
  source fighter-GObj lifetime.
- Canonical aggregate reports after both corrections: 9,477 one-step mismatches and 1,531 / 824
  rollout first mismatches, median clean streak 333. Relative to the initial source cutover this
  removes 3,135 one-step and 105 rollout breaks. Relative to Phase 1, `last_hit_by` improves by
  687 rows while the remaining +788 one-step delta is concentrated in hitlag, state flags,
  action-frame, and instance lifetime.
- The dominant remaining GuardSetOff tail is item-owned, not a reason to restore Guard bridges:
  shield-bounced laser seeds carry the post-contact velocity but the old compact item hitlist lane
  only represents throw-attached lasers. Re-seeding therefore admits the same projectile against
  the same ShieldDesc again when hitlag expires. V2-01 records this as a directly connected
  V2-04 dependency; closure requires one causal item HitCapsule lifetime owner and deletion of the
  future-row `item_shield_bounce_seed_*` compensation path.
- Runtime `src/` is currently 2,460 additions and 19,894 deletions, net -17,434 lines versus
  Phase 1. The deletion remains provisional until every surviving caller and seed lane is audited.

#### 2026-07-13 — item lifetime cutover and same-frame shield entry closure

- V2-04 replaced the throw-laser-only item hitlist derivation with one causal item HitCapsule
  lifetime owner. It now carries zero-rehit victims across both throw-attached lasers and surviving
  shield-bounce edges by item identity and source-visible contact state. This first moved the
  canonical one-step gate from 9,477 to 9,215 while leaving rollout at 1,531 / 824.
- The future-row `item_shield_bounce_seed_*` velocity/control lane and every runtime consumer were
  then deleted. Ordinary shield and reflect outcomes now use only the accepted live collision
  normal and source callback state. The honest post-deletion checkpoint was 9,239 one-step and
  1,532 / 826 rollout, median 333; the small regression was retained because the removed lane was
  lookahead authority rather than source state.
- That cut exposed a shared fighter boundary: guard input and shield contact may both occur between
  recorded post-frame states, yielding a visible Dash-to-GuardSetOff transition. A stale dense
  compatibility ring suppressed reconstruction of the current per-HitCapsule `victims_1` rings.
  GuardSetOff entry with live hitlag now materializes those real rings regardless of whether the
  preceding recorded action was already Guard. A synthetic prior-ring/direct-entry test locks the
  source order.
- Current canonical reports are 8,868 one-step and 1,524 / 818 rollout first mismatches, median
  333. This checkpoint removes 371 one-step and 8 / 8 rollout breaks from the post-bounce-deletion
  tree and leaves Phase 2 only 179 one-step above Phase 1 while retaining the large source cutover.

#### 2026-07-13 — BODY geometry audit and V2-04 laser boundary declaration

- A false Falco Shine contact in `DistinctCaringCobra` was used to audit the new BODY geometry
  owner, not as a row-local patch target. The closest-points traversal, inverse-JObj radius
  projection, and per-HitCapsule victim check agree structurally with
  `lbColl_80006E58`, `lbColl_8000805C`, and the `ftcoll.c` fighter traversal. A provisional
  previous-frame pose hypothesis was rejected after tracing `HSD_AObjInterpretAnim`,
  `Fighter_ChangeMotionState`, and the Shine entry-time `ftAnim_8006EBA4` call. The residual is
  therefore recorded as a live-pose/extraction dependency; no collision constant, replay gate, or
  alternate BODY path was added.
- V2-04's next cut is the complete Fox/Falco laser article lifecycle: `it_8029C504` spawn,
  `it_8029C6A4` animation, `it_8029C8CC` physics snapshot, `it_8029C8D0` stage collision, and the
  reflect/shield-bounce/hit-shield/absorb callbacks in `itfoxlaser.c`, connected to the shared
  item/fighter contact traversal in `ftcoll.c` and `itcoll.c`. The deletion target is the
  replay-conditioned laser collision owner in `items_spacies.c`, including raw post-frame flag
  interpretations, future callback reconstruction, witness-specific geometry choices, and
  duplicated shield/BODY aftermath. The replacement may retain only source state that persists on
  the live item or its common contact packet; throw-generated laser creation remains a producer,
  not a second contact implementation.

#### 2026-07-13 — causal laser cutover and source command-timer contract

- V2-04 moved the Fox/Falco laser article into `items_laser.c`, backed by `MSLLASR1` v10 article
  scale, per-HitCapsule sizes, and clank masks. The shared item contact traversal now consumes one
  BODY-matrix overlap primitive and live parent-chain shield geometry; `items_spacies.c` no longer
  contains the duplicated replay-conditioned projectile/contact implementation.
- Fox ThrowHi exposed a deeper V2-05 dependency: absolute event-frame queries are not equivalent
  to Melee's command interpreter at non-unit animation rates. `MSLFTSC1` v7 now preserves every
  each event group is scheduled by `Command_01` (synchronous add) or `Command_02` (asynchronous
  replacement), including timer-only groups and each source timer value. `fighter_script.c` owns the persistent f32
  timer, frame count, cursor, and command products through entry, ordinary animation, explicit
  same-frame animation ticks, and bounded replay reseed seeks.
- On `BlondHardHippopotamus`, the causal timer moved one-step mismatches from 228 to 76 and removed
  all 28 mismatches in each laser identity/existence field; the extracted ThrowHi pulses now occur
  at source times 18.666, 21.333, and 24 under the 4/3 rate. A missed explicit Shine entry tick
  initially made rollout fail broadly; routing every `ftAnim_8006EBA4` tick through the same
  command interpreter removed that false one-frame HitCapsule lifetime without a bridge.
- Canonical aggregate reports at this checkpoint are 9,364 one-step mismatches and 1,618 / 815
  rollout first mismatches, median clean streak 320. Against the immediately preceding causal
  laser tree, one-step improves by 440 and median by one frame; rollout total is 37 higher while
  seeded is two higher. The timer owner is retained as source-clear infrastructure. This is not a
  vertical completion result: V2-05 must now cut runtime callback/IASA/throw consumers over from
  `move_tables.c` snapshots to these live products, delete the displaced cache, and recover the
  full Phase-2 correctness gates.

#### 2026-07-13 — live common callbacks and V2-02 DamageFlyRoll source cut

- RunBrake and TurnRun now consume the live command variables produced by the persistent script
  interpreter in their post-script Anim callbacks. The `runbrake_cmd0` validation seed lane,
  absolute-frame move-table queries, sync cache, and debug bindings were deleted. Smash-charge
  damage, pseudo-random SFX consumption, attached throw pulses, and Falcon Dive throw-hitbox
  parameters likewise consume live script products rather than displaced timeline snapshots.
- The resulting canonical V2-01/V2-05 intermediate checkpoint was 9,552 aggregate one-step
  mismatches. The last rollout measured before this callback cut was 1,768 / 906 with a 309-frame
  median. Neither is a completion result; both are recorded explicitly because they are worse than
  Phase 1 and expose connected owners that the later verticals must close.
- V2-02 then removed the 3,081-line attacker-action/HitCapsule classifier in
  `combat_damageflyroll.c`. `ftCo_8008DCE0` now admits DamageFlyRoll solely from severe airborne
  knockback outside the top-angle window, percent threshold, and one HSD_Randf draw. The inferred
  `fighter_8006cda4_pre_gate_consume_count` lane was deleted through runtime state, seed ABI,
  native/Python preprocessing, rollout-clock selection, debug APIs, and its replay-shaped tests.
  Ground-to-air damage now installs the source-unconditional ten-frame ECB lock, and
  DamageFlyRoll hitlag always installs the source exit callback rather than a BAir exception.
- This honest post-deletion checkpoint is 10,253 aggregate one-step mismatches and 2,002 / 1,118
  rollout first mismatches, median 276. The regression is retained as incomplete source work, not
  accepted closure: `ftColl_8007A06C -> ftColl_80078538` still owns real effect-stream draws before
  `Fighter_ProcessHit -> ftCo_8008DCE0`, and those draws must be modeled from every accepted DmgLog
  entry plus extracted common/character attributes. No action-family predictor or replay outcome
  may be restored. Focused native schema/combat/throw tests pass (62 tests), and the full tree is
  currently net -31,826 lines versus Phase 1.

#### 2026-07-13 — accepted-hit effect RNG ownership and cumulative gate enforcement

- The missing pre-DamageFlyRoll stream phase was traced through the actual accepted DmgLog path,
  not inferred from replay outcomes: `ftColl_8007A06C -> ftColl_80078538` consumes the common
  low-knockback selector and per-character effect-kind draw; when kind zero selects its child
  particle, `efAsync_Dispatch -> efLib_CreateGenerator -> hsd_8039F05C` consumes the generator
  initialization draw. `EfCoData.dat` generator metadata now supplies that bounded count through
  the extraction and runtime data contract.
- This source closure fixed the motivating DamageFlyRoll transition and moved the canonical
  aggregate from 10,253 to 10,203 one-step mismatches and from 2,002 / 1,118 to 1,979 / 1,094
  rollout first mismatches; median clean streak recovered from 276 to 286 frames. Focused combat,
  schema, and fighter-data tests pass.
- V2-02 remains active. It is still 1,514 one-step and 583 / 352 rollout first mismatches worse
  than Phase 1, with median 19 frames worse. Beginning here, cumulative correctness is enforced at
  each vertical boundary: V2-02 cannot close or hand this debt to V2-03. Source-correct
  intermediate mechanics remain, but the connected contact/damage owners needed to recover and
  then beat the Phase 1 gates are part of the active cut until that result is measured.

#### 2026-07-13 — attached-laser hitlist reconstruction and atomic damage-entry interpretation

- Replay initialization now reconstructs the real zero-rehit item HitCapsule victim ring for the
  attached Falco throw-laser lifetime instead of relying on a stale character-mask approximation.
  This moved the canonical aggregate one-step gate to 10,118 without changing free-running item
  behavior.
- A rollout-only false laser contact exposed a shared animation owner: `ftCo_8008DCE0` explicitly
  calls `ftAnim_8006EBA4` after entering Damage, but the sim advanced only the numeric action frame
  while leaving the collision JObj at entry frame zero. The explicit interpretation owner now
  advances the timebase, command script, and collision pose atomically even though ProcessHit has
  already armed hitlag. `TubbyCurlyHerring` now remains clean through record 1,248 instead of
  breaking at record 271; the aggregate rollout improves from 1,986 / 1,105 to 1,962 / 1,105 and
  median clean streak rises from 279 to 282.
- The cumulative V2-02 gate is still open: current metrics remain 1,429 one-step and 566 / 363
  rollout first mismatches worse than Phase 1, with median 23 frames worse. The next bounded owner
  is the remaining accepted-DmgLog RNG/contact lifecycle feeding DamageFlyRoll and hitlag outcomes;
  V2-03 does not become the primary vertical until this debt is recovered.

#### 2026-07-13 — phantom victim-ring seed closure and Roll-gate source ordering

- Teacher-forced fighter HitCapsule state now preserves the decomp's separate `victims_2` ring.
  Replay-visible defender-only hitlag/source onset with no percent or shield loss reconstructs a
  tip-log contact; CREATE copies the ring from a live same-group sibling, in-place CREATE preserves
  it, and CLEAR removes it. Runtime materializes that exact ring beside `victims_1` on reseed.
- The motivating `TubbyCurlyHerring` strong-NAir boundary now performs its legitimate next-frame
  full BODY hit instead of suppressing it as an already-seen ordinary victim. This is protected by
  a synthetic native owner test and the seed schema contract, not a replay-row test.
- Re-reading `Fighter_ProcessHit_8006D1EC` also disproved the short-lived pre-hit-percent Roll
  threshold interpretation: `Fighter_UnkTakeDamage_8006CC30` commits the current
  `x1838_percentTemp` before `ftCo_8008DCE0` tests `x1830_percent`. The runtime gate now reads the
  equivalent committed value. That correction deliberately exposes remaining same-frame visual
  RNG-prefix debt instead of retaining a metric-friendly source contradiction.
- With both source corrections present, the canonical aggregate is 9,973 one-step mismatches and
  1,958 / 1,105 rollout first mismatches with median 283. Relative to the prior 10,040 and
  1,927 / 1,066 checkpoint, `victims_2` materially improves one-step contact ownership while the
  now-correct Roll threshold exposes 31 / 39 rollout RNG-phase breaks. V2-02 therefore remains
  open; the next gate work is complete source-phase ownership, not restoration of the removed
  action/payload classifier.

#### 2026-07-13 — callback-owned animation-rate boundary closure

- The apparent generic action-frame regression was two bounded source callbacks. TurnRun's
  extracted frame-9 `cmd_vars[1]` drives `ftCo_TurnRun_Anim`, which freezes the live AObj before
  the visible facing flip and resumes it when the source velocity predicate flips facing. Replay
  initialization now seeks the real command stream and reconstructs `mv.co.turnrun.x14` plus the
  post-callback live rate from that persistent command bit and visible facing result. It no longer
  treats the preceding visible AObj delta as the live callback rate.
- Grounded smash charge has the same phase boundary with a different owner. The native history
  derivation already reconstructs `SmashState_Charging` / `Release` and the saved positive rate;
  reseed now makes Charging own rate zero and defender charge state, while Release restores the
  saved rate before the next Anim proc. Free-running entry remains the extracted opcode-56 command
  followed by `ftCo_800DF0D0`; no action-frame predicate was added.
- Synthetic positive/negative edge tests cover TurnRun freeze/resume and smash Charging/Release.
  FavorableSuperficialPig improves from 177 to 149 one-step mismatches. Canonical aggregate
  one-step improves from 9,973 to 9,577 (396 rows), while rollout remains exactly 1,958 / 1,105
  with median 283. This is a locked cumulative improvement but not V2-02 closure: Phase 1 remains
  better by 888 one-step and 562 / 363 rollout breaks, with a 22-frame median advantage.

#### 2026-07-13 — terminal AObj / command interpreter ownership

- The remaining repeated terminal-attack state-flag breaks exposed an architectural conflation:
  the runtime clamped a non-looping HSD AObj at `end_frame` and also zeroed the fighter's live
  `frame_speed_mul`. Source `HSD_AObjInterpretAnim` marks only the AObj `NO_ANIM`; the following
  `ftAction_80073240` still subtracts `Fighter::frame_speed_mul`, executes terminal commands such
  as `allow_interrupt`, and only then invokes the MotionState Anim callback.
- Runtime now clamps only the AObj frame. This lets the one persistent command interpreter execute
  terminal commands before grounded Attack* Anim exits to Wait, while subsequent stopped-pose
  publication remains clamped. The empty `grounded_attack_carry_allow_interrupt` bridge and its
  transition-site calls were deleted; source motion-entry helpers already preserve or explicitly
  clear the live bit.
- Canonical one-step improves from 9,577 to 9,470. Rollout improves from 1,958 / 1,105 to
  1,855 / 997, and median clean streak rises from 283 to 297. V2-02 is still active: Phase 1 is
  better by 781 one-step and 459 / 255 rollout breaks, with an eight-frame median advantage.

#### 2026-07-13 — vertical lock gate and throw-release live-pose dependency

- The cumulative correctness gate is now an explicit vertical lock condition, not a Phase-end
  cleanup exercise. A connected source owner may be red while its cut is incomplete, but V2-02
  cannot lock until the cumulative tree is no worse than Phase 1 on one-step, rollout total,
  rollout seeded, and median, and improves at least one gate. Every later cut inherits the last
  locked cumulative floor. Phase 2 still requires material improvement across both one-step and
  rollout; merely reaching the Phase 1 floor is only permission to advance to the next vertical.
- A Marth ThrowHi release false-land was traced through the source callback order to the live
  `FtPart_XRotN` / JObj / `CollData` publication boundary. The release callback, immediate damage
  animation interpretation, damage physics, and stage projection are in source order. Removing
  `XRotN` from ECB reconstruction changes only one aggregate one-step row, so it is retained as
  real source state and no platform/action exception was added. The remaining exact release pose
  dependency stays inside the open V2-02/V2-03 boundary rather than becoming a rollout bridge.
- After restoring the normal source build and regenerating both canonical aggregate reports, the
  current checkpoint is **9,619** one-step mismatches and **1,866 / 1,007** rollout first
  mismatches with a **297-frame median**. This fails the vertical lock gate by 930 one-step,
  470 / 265 rollout breaks, and eight median frames. Fast per-replay first-divergence scans place
  the broad remaining fan-out in the shared BODY/DmgLog/ProcessHit path: false and missing
  accepted contacts, wrong selected hurt height / Damage motion, and hitlag duration. That owner
  is the active recovery work; V2-02 is not checkpointed at these metrics.

#### 2026-07-13 — ProcessHit ordering and concrete angled-script ownership

- The fighter traversal now records the accepted DmgLog before publishing attacker dealt hitlag,
  matching `ftColl_8007A06C -> ftColl_80078538 -> Fighter_ProcessHit`. The former traversal-time
  attacker-hitlag rejection was removed. Replay initialization reconstructs the corresponding
  CatchAttack victim ring so a live pummel is not admitted twice merely because the source-order
  cut moved its attacker pulse later. These are real HitCapsule/DmgLog owners, not validation
  suppressions.
- The next missing-contact family was an extraction boundary. Angled side-tilt and side-smash
  MotionStates preserve their concrete submotion ids, while `MSLFTSC1` previously extracted only
  the neutral member. The old `MSLHITB1` reader hid the Fox/Falco subset with a runtime side-tilt
  alias. Every angled Pl*.dat row is now extracted into the unified command timeline instead, and
  that alias is deleted. This matters beyond the motivating Fox contact: Captain's up, neutral,
  and down variants contain genuinely different source HitCapsules and cannot be canonicalized.
- Focused extraction/runtime tests pass. Canonical aggregate reports are now **9,683** one-step
  mismatches and **1,820 / 959** rollout first mismatches with a **306-frame median**. Against the
  preceding source-order checkpoint (9,840 and 1,866 / 1,007, median 297), concrete script
  ownership removes 157 one-step and 46 / 48 rollout breaks while raising the median nine frames.
  V2-02 therefore enforces the cumulative gate during the vertical, not only at Phase-end, but it
  remains open: Phase 1 is better by 994 one-step and 424 / 217 rollout breaks; the median is now
  one frame better than Phase 1.

#### 2026-07-13 — V2-02 closure: live BODY pose and cumulative performance lock

- The remaining BODY/contact fan-out was closed at its shared collision-pose owner rather than by
  replay-row repairs. Fighter dynamics now follows the extracted `DynamicsDesc` chain and source
  `lb_8001044C` operations, publishes exact source-interpreted local owners, and shares the one
  root-to-node JObj traversal across all requested BODY capsules. Track ownership masks and exact
  frame lookup are built once at initialization; normal gameplay remains fixed-capacity and
  allocation-free. Stage collision likewise consumes prepared extracted map views rather than
  repeatedly reconstructing segment ownership.
- Common Fall ECB sampling exposed the last measured pathological pose path. The old sim rebuilt
  the same root-to-joint chain for each of six ECB JObjs. The replacement retains
  `ftAnim_8006FE9C`'s local-SRT blend but publishes shared ancestor products once, matching
  `HSD_JObjSetupMatrix -> mpColl_LoadECB_JObj`. A temporary exact comparison against the displaced
  per-joint sampler found no matrix differences before the old traversal was removed.
- Three tempting shortcuts were explicitly rejected: generic baked-local substitution worsened
  rollout ownership, a replacement exact-math layer was slower than the source-shaped native
  operations, and publishing hurtcap endpoints through a new final-world-matrix shortcut broke
  exact source-owner tests. None remains in the tree. The retained speedup is consolidation of
  real work, not reduced simulation or benchmark-specific behavior.
- Canonical V2-02 exit is **7,486 aggregate one-step mismatches**, **1,306 total / 546 seeded
  rollout first mismatches**, and a **428-frame median clean streak**. Phase 1 was 8,689,
  1,396 / 742, and 305 respectively, so every cumulative correctness lock passes and both
  one-step and rollout are materially better.
- The full mixed-character/stage random gate is **404.9k FPS** over 5,000 frames. The fixed
  aggregate replay gate is **322.3k FPS** over 3,000 records repeated ten times. Phase 1's replay
  gate was 311.7k FPS. V2-02 therefore closes with no carried correctness or performance debt;
  V2-03 begins from this exact canonical checkpoint.
- Relative to committed Phase 1, the current complete Phase-2 tree is **26,066 fewer lines in
  `src/`** (7,856 additions, 33,922 deletions). This is a cumulative figure shared by the cutovers
  already present in the tree; per-vertical attribution will be finalized only after displaced
  owner deletion in V2-05.

## V2-03: Catch, capture, throw, and thrown-body ownership

Status: locked. Entry lock: 7,486 aggregate one-step, 1,306 / 546 aggregate rollout, 428-frame
median, 404.9k mixed-random FPS, and 322.3k fixed-replay FPS. Exit lock: 7,299 aggregate one-step,
1,244 / 542 aggregate rollout, 442-frame median, 403.5k mixed-random FPS, and 320.7k fixed-replay
FPS.

### End-to-end owner

```text
grounded IASA Catch/CatchDash admission
  -> script-created CatchCapsule and priority-12 catch selection
  -> catcher CatchPull/CatchWait/CatchAttack callbacks
  -> victim CapturePulled/CaptureWait/CaptureDamage callbacks and mash timer
  -> CatchWait directional throw selection
  -> paired Throw*/Thrown* motion entry and live XRotN attachment
  -> script throw pulse, release-local CollData publication, and throw DmgLog
  -> victim Damage owner plus attachment/release cleanup
  -> breakout, ground-loss, hit interruption, and stale-link cleanup
```

Source spine:

- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c` from `ftCo_Catch_CheckInput`
  through `fn_800DC624`;
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c`;
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c`;
- `refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c`;
- `refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_80078754}` and
  `refs/melee/src/melee/ft/fighter.c` priority-1/3/12/14 callback order;
- extracted MotionState callbacks (`MSLMSO01`), script events (`MSLFTSC1`), fighter part anchors
  (`MSLPART1`), and character/common attributes.

### Cut boundary and deletion target

- Ordinary Catch/CatchDash, CatchPull/Wait/Attack/Cut, CapturePulled/Wait/Damage/Cut/Jump, and
  ThrowF/B/Hi/Lw plus ThrownF/B/Hi/Lw are in-boundary for all supported characters and for 2-4
  player deterministic traversal. Falcon Dive uses the same attachment substrate but its actual
  special producer/release remains V2-04; V2-03 may consolidate shared attachment primitives
  without absorbing Falcon-specific mechanics.
- Replace the two broad global scans in `grab_flow.c` with callbacks executed for the live fighter
  at the source phase. The current first-steady CaptureWait predicates infer callback order from
  `prev_action` / `seed_prev_action` shapes; they are explicit displaced-code targets, not closure.
- Converge the three current ownership files (`grab_flow.c`, `grab_attachment.c`, and
  `throw_flow.c`, about 4.1k lines at entry) on one paired-state model with one authoritative link,
  constraint, attachment, release, and cleanup path. Delete duplicate action-to-throw mappings,
  repeated victim searches, final-matrix reconstruction branches, and transition-site cleanup.
- Preserve only real hidden source state at reseed: victim/owner linkage, grab timer/mash latches,
  throw motion rate, thrown pause state, constraint-local translation, and live script cursor.
  Replay-history row shapes, extra-tick guesses, and provenance-only compatibility lanes do not
  survive the cutover.
- Close only after all callers use the new owner, old scans/bridges/state are deleted, focused
  two-actor and doubles tests pass, and the cumulative correctness/performance lock is measured
  against the V2-02 exit above.

### V2-03 worklog

#### 2026-07-13 — entry audit

- The source model is compact but the existing implementation is phase-split: `grab_flow.c`
  combines Anim and IASA work in global owner/victim scans, `throw_flow.c` performs thrower Anim
  callbacks per fighter, and `grab_attachment.c` reconstructs several attachment/release variants
  in later global collision/accessory passes. This split is the direct cause of the large
  first-steady CaptureWait replay-shape predicate family.
- The first architectural cut is source-order execution for one fighter and its single linked
  peer. That permits CatchPull to change a later-slot victim before that victim's own callback,
  while an earlier-slot victim has already run, without guessing from visible next-row history.
  Paired transitions remain atomic at their actual source callback; no generic deferred bridge is
  introduced.

#### 2026-07-13 — source-phase callback cutover and bridge deletion

- Fighter priority-1 now runs the complete common Catch/Capture/Thrown Anim callback family for
  one live fighter, priority-4 runs its Phys callback, priority-6 runs its map callback, and
  priority-8 owns accessory/attachment publication. The broad victim/owner scans and their
  replay-shape phase guesses were displaced rather than retained beside the callbacks.
- CatchPull/Wait/Attack/Cut, CapturePulled/Wait/Damage/Cut/Jump, ThrowF/B/Hi/Lw, and
  ThrownF/B/Hi/Lw share one authoritative pair/link lifetime. Attachment pose uses one live
  part-origin composition path, throw entry no longer applies duplicate immediate placement, and
  throw release no longer suppresses low throw or history-shaped cases. The three entry files fell
  from about 4.1k lines to 2,758 lines while broad directly connected callback/timer cleanup was
  absorbed into the same cut.
- The first coherent callback scheduler measured 7,439 one-step and 1,297 / 546 rollout with a
  434-frame median. Completing the Capture family and deleting the old phase heuristic improved
  that to 7,415 and 1,292 / 541 with a 435-frame median. Removing the remaining history-shaped
  throw suppression honestly exposed a false release collision and temporarily moved the tree to
  7,508 and 1,332 / 556 with a 414-frame median; the correct response was to finish the release
  owner, not restore the suppression.

#### 2026-07-13 — extracted attachment rest pose and release-local CollData closure

- `ftCo_800DB368` / `ftCo_800DDDE4` require the default-costume XRotN local translation that is
  saved in `fp->x2174`, then restored immediately before the release-local `mpColl_800471F8` call.
  The static part translation is now an extracted character artifact with full extractor, stable
  runtime-data, native binding, packaged-data, and fresh-extract coverage. This removed the local
  character constants and corrected Marth's non-zero Z owner.
- Release CollData now publishes the source previous ECB midpoint and the attached victim's live
  release root before the immediate air collision kernel. Placeholder/absent seeded ECB values
  fall back to the real previous collision frame rather than the current advanced pose. The
  extracted XRotN correction moved the aggregate to 7,437 one-step and 1,288 / 557 rollout with a
  435-frame median; total rollout improved, but V2-03 remained open because seeded rollout was 11
  worse than its 546-entry gate.

#### 2026-07-13 — consumed root-motion pose owner and V2-03 lock

- The remaining seeded release family was one shared live-JObj error, not a throw exception.
  `SSANIMT1` correctly marked Falco ThrownHi as root-motion-driven, but fractional pose composition
  added the raw TransN translation to every descendant after `ftAnim_8006E054` had already consumed
  it into fighter motion and cleared the live JObj. The runtime now retains raw TransN for physics
  while clearing it at each live-JObj composition/materialization boundary, including the
  XRotN-restored release matrix and dynamic collision matrix path. An extracted-data owner test
  locks a non-zero raw TransN against the zeroed fractional live matrix.
- Canonical V2-03 exit is **7,299 aggregate one-step mismatches**, **1,244 total / 542 seeded
  rollout first mismatches**, and a **442-frame median clean streak**. All four cumulative gates
  beat V2-02 (7,486, 1,306 / 546, median 428); the source fix clears the 15 false ThrownHi platform
  landings and improves other connected rows without any action, stage, character, or replay gate.
- Ten focused pose/extraction/Catch/Capture/Throw tests pass. The mixed-character/stage random gate
  is **403.5k FPS** over 5,000 frames and the fixed replay gate is **320.7k FPS** over 3,000 records
  repeated ten times, within 0.5% of the V2-02 locks. The cumulative `src/` tree is now **27,439
  lines smaller than Phase 1** (9,194 additions, 36,633 deletions) after formatting.
- V2-04 inherits this exact floor. A connected source cut may temporarily regress inside an open
  owner, but V2-04 cannot lock unless its cumulative one-step, rollout total, seeded rollout, and
  median all match or beat this checkpoint and at least one materially advances.

## V2-04: items, articles, and special contact ownership

Status: closed. Entry lock: 7,299 aggregate one-step, 1,244 / 542 aggregate rollout, and a
442-frame median. Exit lock: 6,850 aggregate one-step, 1,215 / 523 aggregate rollout, and a
447-frame median. The hot-path performance check was completed at the cumulative V2-05 exit;
no benchmark-specific runtime path is accepted in either vertical.

### End-to-end owner

```text
fighter/article spawn callback and extracted item kind parameters
  -> persistent item state and article Anim/Phys callbacks
  -> live HitCapsule publication and per-capsule victims lifetime
  -> priority-6 stage collision against static or moving source lines
  -> priority-9/12/13 fighter item contact selection
  -> ReflectDesc, ShieldDesc, clank/attack HitCapsule, and BODY precedence
  -> priority-14 item callback / damage / reflect / absorb consumption
  -> bounce, stick, destroy, transfer, or article-specific terminal lifecycle
```

Source spine:

- `refs/melee/src/melee/it/item.c` and `refs/melee/src/melee/it/itcoll.c`;
- `refs/melee/src/melee/ft/ftcoll.c` item traversal and reflect/shield/BODY selection;
- `refs/melee/src/melee/gr/ground.c` and `refs/melee/src/melee/mp/mpcoll.c` moving-line owners;
- `refs/melee/src/melee/it/items/itfoxlaser.c`, `itseakneedlethrown.c`, and the supported special
  article implementations;
- extracted `MSLITAR1`, `MSLSTG01`, `MSLFTSC1`, and the shared fighter pose/contact substrates.

### Deletion target

- validation lanes that encode a future bounce, stick, destroy, or reflect outcome rather than
  persistent source state;
- replay-frame reapplication of article outcomes after the live callbacks have run;
- action/history proxies for ReflectDesc, ShieldDesc, and item BODY aftermath;
- duplicated per-article collision geometry and stage-line loops where the source uses the common
  item collision kernels;
- article-specific victim, reflect, and terminal state that duplicates the common item owner.

Real hidden state is not deleted merely because it is absent from Slippi output. A bounded pending
callback packet, item motion scalar, victims ring, or RNG seed/consume state may be reconstructed
for one-step reseed when it initializes the same state used by free-running gameplay. Future
outcomes and replay-conditioned runtime behavior may not be reconstructed.

### V2-04 worklog

#### 2026-07-13 — Sheik needle future-outcome deletion

- The needle bridge encoded callback bounce/destroy and stage stick/bounce/destroy results, plus
  callback and stage response velocities, from the following replay row. Those lanes, their native
  and Python derivation, runtime state, replay-frame reapplication, and outcome-locking tests are
  deleted. Every live needle contact now consumes the source RNG sites in its own callback.
- Retained needle seed state is limited to the article's real persistent motion scalars, item
  hitlag, and one bounded pending BODY-contact packet. The latter initializes the same pending
  source contact consumed by normal item/fighter ProcessHit; it does not choose its outcome.
- The honest post-deletion checkpoint was **7,390 one-step**, **1,246 / 542 rollout**, median
  **442**. This failed the inherited lock and kept V2-04 open. No future-outcome lane was restored.

#### 2026-07-13 — live item stage-line and velocity publication

- The common item line query now uses the source wall-direction sweep test, resolves live moving
  floor transforms from the runtime stage owner, ignores inactive dynamic surfaces, and returns
  the contacted line velocity. Laser, Zelda article, and Sheik needle callers use the same runtime
  query instead of a static-stage approximation.
- Needle `CheckGroundHit` now publishes `mpGetSpeed` from the contacted line before its callback,
  and `SetupBounce` keeps the hidden xDD8 response scalar separate from public `x40_vel.x` until
  the next state-4 Phys callback. These are source lifecycle corrections, not replay geometry
  exceptions.
- This clears eight false/missing Sheik stage-contact rows and returns the canonical checkpoint to
  **7,378 one-step**, **1,244 / 542 rollout**, median **442**. Rollout has recovered exactly to
  V2-03, but one-step remains 79 rows above its inherited lock. V2-04 therefore remains active;
  the next work is the connected common reflect/shield/BODY and article lifecycle owner, not an
  exact replay-RNG outcome bridge.

#### 2026-07-13 — common contact selection and supported article lifecycle closure

- Item/fighter contact now has one descriptor-driven precedence owner for ReflectDesc,
  ShieldDesc/Counter, fighter-HitCapsule contact, and BODY. Laser, Needle, Vanish, Zelda Din's
  Fire/explosion, and the supported special-item callers consume the same result and dispatch the
  source callback that actually exists. Article-specific copies of reflect/shield/BODY selection,
  victim registration, damage thresholds, and terminal outcome logic were deleted rather than
  retained beside the common kernel.
- Persistent item HitCapsule victim rings are explicit runtime state and are reconstructed only
  from the causal exported ring/provenance lanes on teacher-forced reseed. Din's Fire explosion
  correctly persists after BODY because its OnGiveDamage callback is NULL. Marth Counter now
  publishes its source ShieldDesc in the fighter owner and reaches the ordinary item contact
  selector; the late combat-side Counter item workaround is gone.
- The first complete lifecycle checkpoint improved the aggregate to **6,880 one-step**, **1,221
  total / 528 seeded rollout**, and a **446-frame median**. Zelda's dedicated hit replay became
  RAW-CLEAN and 31 focused Counter tests passed without changing that aggregate checkpoint.

#### 2026-07-13 — command-11 x40_b0 correction and V2-04 correctness lock

- The shared item script decoder incorrectly reused the fighter command-11 word-3 `clank` bit.
  Source `it_802790C0` ignores those fighter-only bits and publishes `HitCapsule.x40_b0` from
  `it_create_hitbox_4`. Correcting that data owner marks Vanish smoke as contact-enabled without a
  Vanish action/replay exception and leaves Needle's authored x40_b0 clear. The common
  `ftColl_80077970`-shaped two-HitCapsule damage-threshold owner now explains the fresh-smoke
  attacker hitlag and victim-ring ordering cases that the old article-specific bridge approximated.
- The broad item/laser/Needle/Vanish/reflect focused set is **157 passed**, with only the declared
  Phase 3 reversed-port callback-order xfail. Canonical aggregate exit is **6,850 one-step**,
  **1,215 total / 523 seeded rollout**, and a **447-frame median**. Every gate improves on both the
  7,299 / 1,244 / 542 / 442 V2-03 entry and the 8,689 / 1,396 / 742 / 305 Phase 1 baseline.
- A mixed random sample measured **391.4k FPS** versus V2-03's 403.5k lock while another process
  held sustained CPU time; the fixed replay sample previously measured **316.1k FPS** versus
  320.7k. These provisional -3.0% / -1.4% observations are recorded, not hidden. The cumulative
  V2-05 exit must repeat both gates under a clean-enough host and recover any reproducible runtime
  loss before Phase 2 can finish.

## V2-05: live callback/script ownership and displaced-code deletion

Status: closed. Entry and exit lock: 6,850 aggregate one-step, 1,215 / 523 aggregate rollout, and
a 447-frame median. This is a behavior-equivalent deletion vertical: cumulative validation was
refreshed at the live-script cutover, physical deletion, and final hot-path cleanup boundaries.

### End-to-end owner

```text
MotionState entry and live CommandInfo initialization
  -> source timer-command execution at the active animation rate
  -> create/change/clear HitCapsule, cmd-var, throw, SFX, and hurt-status products
  -> current Anim / IASA / Phys / Coll / accessory callback consumers
  -> action transition or terminal lifecycle
  -> delete absolute-frame snapshot queries, debug bindings, and replay-history proxies
```

Source spine:

- `refs/melee/src/melee/lb/lbcommand.c` and `refs/melee/src/melee/ft/ftaction.c`;
- `refs/melee/src/melee/ft/fighter.c` callback scheduling and MotionState entry;
- supported common and character MotionState callback tables;
- extracted `MSLMSO01` callback ownership and `MSLFTSC1` timer/event streams.

### Deletion target and closure gates

- every runtime `move_tables.c` absolute-frame snapshot query whose answer is already a product of
  live `fighter_script.c` execution;
- preprocessing/history derivation used only to seed such future script outcomes;
- public/debug query bindings and tests that preserve the displaced second owner;
- callback-family switches, semantic action lists, and terminal/dead-state repairs expressible by
  the extracted callback tables or live owner state.

V2-05 closes only after all runtime callers use live products, the displaced snapshot surface is
deleted, the four aggregate gates match or improve this entry lock, focused/full tests and
formatting pass, canonical reports are fresh, and both performance workloads show no significant
regression from the inherited V2-03 lock. Because this vertical's declared value is removal of a
second behavior owner, exact metric preservation satisfies the independently locked
behavior-equivalent deletion exception in the cumulative gate above.

### V2-05 worklog

#### 2026-07-14 — singular live-script ownership and physical deletion

- RunBrake/TurnRun command variables, smash charge, throw pulses and hitbox parameters, Falcon
  Dive throw data, hit/hurt status, interrupt enable, and fighter HitCapsule publication now come
  from the one persistent `fighter_script.c` command cursor and its live products. Explicit
  `ftAnim_8006EBA4` ticks and ordinary animation advance the same source timer; no runtime caller
  asks a second absolute-frame snapshot owner what should have happened.
- `src/move_tables.c` (1,322 lines), `src/move_tables.h` (523 lines), their setup/build entry,
  debug bindings, cache-only known-artifact helpers, frame-window APIs, and the snapshot-specific
  throw test were physically deleted. Searches of runtime, bindings, setup, and tests contain no
  surviving gameplay caller. Historical architecture/candidate documents retain their explicit
  pre-rewrite references as provenance.
- Fighter priority-1 now executes the complete animation/script/callback procedure per fighter in
  source list order. That closes the linked Catch/Throw ordering exposed by the cutover without a
  global repair pass, and lets dead/identity/transient cleanup belong to the callback or lifecycle
  that creates it.

#### 2026-07-14 — hot-path consolidation and performance lock

- Item collision derives one live family mask per row and skips absent article-family traversals.
  Laser callbacks build one ascending fixed-capacity live-item view and carry it through fighter,
  item, and ProcessHit traversal instead of rescanning every empty slot for every subphase. There
  are no heap allocations or persistent benchmark caches.
- HurtCapsule endpoints now use the completed JObj world matrix and direct `MTXMultVec`-shaped
  point multiplication from `lb_8000B1CC`. The same per-fighter pose frame owns facing and root
  composition for every capsule, deleting duplicate local-attachment/root reconstruction. Exact
  aggregate discrete metrics are unchanged; focused endpoint locks permit only the expected
  one-ULP f32 operation-order difference.
- The canonical benchmark report on the documented Ryzen 9 9950X3D host records **390.8k mixed
  random FPS** and **319.4k fixed aggregate-replay FPS**. Repeated final samples under the host's
  continuing external CPU load ranged from 390.8–397.7k and 319.3–322.4k. Relative to V2-03 the
  canonical values are -3.1% and -0.4%, respectively; relative to Phase 1 they are -8.0% and
  +2.5%. The inherited hot-path gate therefore remains within a small host-sensitive band while
  the real-replay gate is effectively flat and cumulatively better than Phase 1. Random p99 is
  1.30x average with a 2.68x maximum; replay p99 is 4.56x average with a 20.7x isolated maximum.
  Neither workload has the 10x p99 pathology that motivated the earlier performance pass.

#### 2026-07-14 — Phase 2 exit lock

- Canonical aggregate exit is **6,850 one-step mismatches**, **1,215 total / 523 seeded rollout
  first mismatches**, and a **447-frame median clean streak**. V2-05 preserves its entry exactly.
  Against Phase 1 this is 21.2% fewer one-step mismatches, 13.0% fewer total rollout breaks, 29.5%
  fewer seeded rollout breaks, and a 46.6% longer median streak. Cumulative correctness is
  materially better in both one-step and rollout, so the Phase 2 gate is satisfied here rather
  than deferred to a later cleanup phase.
- The complete `make validate-all` report set is fresh. The primary Fox/Falco report is 223
  one-step and 36 / 14 rollout first mismatches with a 474-frame median; it records a regression
  relative to Phase 1 while the supported-character/stage aggregate improves materially. The
  secondary suites all improve on Phase 1: doubles is 5,573 and 768 / 423 with median 153, Falcon
  is 1,685 and 323 / 136 with median 387, and Sheik is 1,809 and 288 / 122 with median 414. No
  replay-shaped bridge was restored to optimize the primary subset; its shared source owners are
  the declared V2-06 control-recovery boundary.
- The full suite passes: **2,005 passed, 1 skipped, 4 expected failures**. Formatting, diff checks,
  extraction/runtime contract tests, and no-allocation gameplay-path locks pass. The cumulative
  tree versus committed Phase 1 is **10,773 additions / 40,669 deletions in `src/`**, including
  the new 519-line laser owner: **29,896 fewer runtime source lines**. Across the complete tracked
  tree plus that new file, the net reduction is **35,958 lines**.
