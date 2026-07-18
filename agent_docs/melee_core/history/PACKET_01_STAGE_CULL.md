# Packet 1 history — supported-stage headless animation/callback cull

## Packet 1 — supported-stage headless animation/callback cull

### Final boundary map

- **Final owner:** hosted stage scheduling rooted at
  `gameplay/src/melee/gr/ground.c::Ground_801C1CD0` and the supported-stage
  priority-4 callbacks installed by each stage's `StageCallbacks.callback2`.
- **Canonical state:** existing `Ground`, JObj/AObj, stage-event, and mpLib state remains canonical
  for every dynamic gameplay value. Gameplay-observed values proven invariant are published during
  stage construction/reset. `mpColl_804D64AC` remains the collision-cache epoch owner.
- **Consumers that must remain exact:** `Ground_801C2ED0`/`Ground_801C2FE0`, mpLib transformed
  joints/lines, fighter and item collision, ledges, moving platforms, FoD replay-event publication,
  Dream Land wind/device state, camera/blast-zone state, terminal state, and source RNG consumption.
- **Displaced code:** only common stage JObj animation walks or stage-specific scheduled callbacks
  whose products are proven invariant or presentation-only across the supported runtime. The exact
  callback list will be locked below from per-stage profiling and source/data consumer tracing
  before the first production edit.
- **Deletion boundary:** a displaced per-frame owner is either absent from the hosted schedule or
  replaced by a one-time construction/reset publication. There will be no runtime flag, fallback,
  duplicate representation, compatibility dispatch, or substitute per-frame traversal.
- **Explicit exclusions:** no stage-collision representation rewrite, broad-phase work, pose work,
  camera-owner packet, callback batching, or other Packet 2+ optimization. Dynamic platform,
  collision, wind, hazard, and stage-event owners are not culled.
- **Confidence:** definite improvement for each completely removed measured owner; the available
  removable budget is potential. Independent retention requires a repeatable roughly 5% or larger
  complete-contract gain at 512 resident environments with exact output.

The packet is not ready for a production edit until the callback inventory below identifies the
exact displaced owners and their consumers. Diagnostic-only instrumentation may be used to produce
that inventory without changing the production schedule.

### Chronological log

#### 2026-07-17 — packet opened

- **Scope:** establish the common/stage-specific scheduling boundary before measurement.
- **Hypothesis:** a material portion of the measured 9–10% stage-object owner consists of repeated
  JObj animation or presentation callbacks that publish no gameplay value.
- **Evidence:** `Ground_801C1CD0` unconditionally calls `HSD_JObjAnimAll`, increments
  `mpColl_804D64AC`, then invokes optional `Ground.x8_callback`; supported stage initialization
  separately installs `StageCallbacks.callback2` at priority 4. FD already replaces ten static
  common animation walks with one epoch publication.
- **Disposition:** open; no runtime code changed.
- **Next step:** record adjacent 512/256 production baselines, then split stage cost by stage and
  callback with the preserved unchanged-scheduler profiler.

#### 2026-07-17 — adjacent baseline recorded

- **Scope:** unchanged committed runtime, CPU 0, canonical 153-case-derived workload, 32,768
  complete frames, 512 then 256 resident environments.
- **Hypothesis:** committed HEAD still reproduces the retained performance/digest baseline before
  diagnostic instrumentation or candidate work.
- **Result/evidence:** 512 reached 76,634 complete / 78,381 step-only FPS with digest
  `3fb5823d90657775`; 256 reached 59,045 / 60,353 FPS with digest `bdc54107c51fa3d7`.
  Both production digests are exact. The Make target crossed ten seconds only because the first
  invocation refreshed its build/check path; the now-built benchmark itself takes under one second.
- **Disposition:** retained as this packet's adjacent baseline evidence.
- **Next step:** apply the previously preserved diagnostic-only unchanged-scheduler profiler and
  produce per-stage/per-callback cost evidence without altering production behavior.

#### 2026-07-17 — stage-profile experiment opened

- **Scope:** diagnostic owner timing only; production schedule, workload, and outputs remain exact.
- **Hypothesis:** per-stage owner attribution will separate common `Ground_801C1CD0` animation cost
  from stage-specific priority-4 callbacks and establish whether at least 5% of the complete
  workload has a credible removable boundary.
- **Result/evidence:** pending.
- **Disposition:** open.
- **Next step:** build the preserved diagnostic instrumentation, run one bounded supporting sample
  per stage, resolve owner addresses to symbols, and audit only owners large enough to matter.

#### 2026-07-17 — stage-profile experiment completed

- **Scope:** unchanged production schedule, mixed 512-resident canonical workload, owner timing
  attributed by internal stage and Ground map id.
- **Hypothesis:** common Ground animation plus static-stage collision publication contains a
  credible approximately 5% removable boundary.
- **Result/evidence:** exact digest `3fb5823d90657775`; timer overhead 43 cycles. The complete step
  owned 2,035,276,043 timed cycles. `Ground_801C1CD0` owned 112,241,868 cycles (5.52%). The exact
  audited common-animation cut below owns 94,362,000-class cycles (about 4.6%); frozen Stadium map
  5 `grStadium_801D1604` adds 23,486,815 cycles and static Battlefield map 6
  `grBattle_8021A174` adds 896,421 cycles. Together with the null `Ground_801C1D38` callbacks the
  credible boundary is about 5.9% of complete step time. Raw evidence is under ignored
  `reports/triage/packet1-stage-cull/`.
- **Disposition:** retained diagnostic evidence; profiling instrumentation remains temporary until
  the candidate is re-profiled, then will be removed from the production diff.
- **Next step:** implement the exact locked inventory below as a construction-time schedule cut.

### Callback inventory

The production edit is approved only for this inventory:

| Stage | Common `Ground_801C1CD0` animation disposition | Priority-4 disposition | Source/data basis |
|---|---|---|---|
| Final Destination | Existing one-epoch headless owner retained | Existing FD owner retained | Static extracted collision/model boundary already validated |
| Fountain of Dreams | Replace animation with epoch-only for maps 0, 1, and 3; retain map 4 | Retain map 4 `grIzumi_801CC358` and other source callbacks | Map 4 is the source moving-platform actor and extracted `platform_motion` owner; hosted map 3 reflection consumers are absent |
| Yoshi's Story | Replace maps 0 and 1; retain maps 2 and 3 | Retain Randall and Shy Guy/collision callbacks | Maps 2/3 own `Ground_801C2FE0`; extracted Randall `platform_paths` and item scheduling are live |
| Frozen Pokemon Stadium | Replace maps 0, 1, 2, and 5 | Remove map 5 `grStadium_801D1604` after its construction publication; retain jumbotron RNG projection | Hosted source disables the transformation loader; frozen geometry has no supported runtime transform event |
| Dream Land N64 | Replace maps 0, 1, 3, 4, 5, 6, and 8; retain maps 2 and 7 | Retain Whispy/wind state, animation-completion timing, transient cloud, and RNG callbacks | Map 7 owns the wind/device state and uses AObj completion; map 2 is transient and completion-owned; extracted collision is static |
| Battlefield | Replace maps 0, 1, and 6; retain map 3 | Remove map 6 `grBattle_8021A174` after its construction publication; retain map 3 background transition/RNG timing | Extracted collision has no dynamic path; map 3 uses AObj completion to preserve source RNG phase |

For every constructed supported Ground object, remove `Ground_801C1D38` only after asserting its
`xC_callback` is null. The supported source files assign no non-null `xC_callback`. Replaced common
animation procs still increment `mpColl_804D64AC` once at the same scheduler position and invoke a
non-null `x8_callback` defensively, although every supported owner currently initializes it null.

The schedule policy is construction-only data in the existing stage specification. It does not add
a hot stage-id branch or mutable compatibility flag. Later-created transient Ground objects keep
their source schedule unless explicitly present in this locked construction inventory.

#### 2026-07-17 — production candidate implemented

- **Scope:** construction-time animation-retention masks; epoch-only replacements for the locked
  common Ground owners; deletion of frozen Stadium map 5 and Battlefield map 6 priority-4 updates;
  deletion of asserted-null `Ground_801C1D38` procs.
- **Hypothesis:** the complete locked boundary removes about 5.9% of measured step work without
  changing any gameplay publication, callback timing signal, or RNG consumption.
- **Result/evidence:** native release build completed in 3.0 seconds. The first 153-case native
  output-lock gate did not reach comparison: three workers reported a sealed Match-arena 1,024-byte
  runtime allocation. This is a candidate correctness/lifetime failure, not a valid benchmark.
- **Disposition:** open; candidate retained in place for diagnosis. No displaced owner restored and
  no unrelated optimization started.
- **Next step:** isolate the failing stage/replay and trace which removed scheduled owner previously
  prevented or released the late 1,024-byte allocation.

#### 2026-07-17 — sealed-allocation failure isolated

- **Scope:** exact per-stage output locks, followed by one narrow Stadium schedule ablation.
- **Hypothesis:** removing frozen Stadium map 5's priority-4 static-collision callback caused the
  late allocator demand.
- **Result/evidence:** Battlefield's 26-case stage lock remained green (13 exact pass, 13 existing
  classified, 221,403 frames). Frozen Stadium alone reproduced the sealed Match-arena 1,024-byte
  allocation. Retaining Stadium map 5's priority-4 callback did not change that failure, so the
  static collision callback is not its cause. Merely replacing the common animation callback leaves
  its attached AObj/FObj graph live but no longer advancing; that lifetime change is now the leading
  source-backed explanation for exhausting a preallocated object/class pool.
- **Disposition:** rejected as a diagnosis; Stadium's priority-4 callback remains temporarily live
  only to keep the next experiment single-variable.
- **Next step:** identify the exact allocator owner from the abort stack, then make the second and
  final structural attempt for this unknown: delete the displaced presentation animation graph at
  construction when installing the epoch-only owner. If that does not clear the focused Stadium
  lock, stop and reconvene.

#### 2026-07-17 — second lifetime experiment failed; packet paused

- **Scope:** exact abort-stack attribution plus deletion of culled Ground AObj/FObj graphs when the
  epoch-only owner is installed. Stadium map 5's priority-4 callback remained live during the test
  so the experiment changed only the common-animation lifetime boundary.
- **Hypothesis:** the non-advancing presentation graph stranded allocator-owned state and caused the
  late sealed allocation.
- **Result/evidence:** the graph was explicitly removed with `HSD_JObjRemoveAnimAll` at construction,
  but the 22-case Stadium lock failed identically. The diagnostic stack resolves the 1,024-byte
  request to `hsdAllocMemPiece -> _hsdClassAlloc -> hsdNew -> HSD_JObjAlloc -> JObjLoadJointSub`:
  runtime gameplay is exhausting the pre-sealed JObj class pool. The focused run completed in 4.2
  seconds. Temporary stack instrumentation was removed immediately after attribution; the intended
  candidate schedule (including Stadium map 5 removal) is restored in the dirty tree.
- **Disposition:** open and paused. Removing the displaced animation graph remains part of the final
  deletion boundary, but it does not explain or fix the JObj exhaustion. No benchmark result is
  valid while this lock is red.
- **Next step:** reconvene before another implementation experiment. The next investigation must
  identify which replay/frame and JObj consumer exceeds the committed high-water, and whether that
  is an early gameplay divergence from culled stage transforms or a cross-reset class-pool lifetime
  change. Do not widen JObj reserves or restore displaced callbacks as a production fix.

#### 2026-07-17 — diagnostic continuation: allocation is worker-shape dependent

- **Scope:** diagnostic-only replay/process isolation; no production owner changed.
- **Hypothesis:** one Stadium replay locally diverges and exceeds the JObj high-water.
- **Result/evidence:** all 22 Stadium replays completed in independent fresh processes in 3.0
  seconds. Four bounded sequential groups, including all 22 cases, also completed in persistent
  one-worker servers. Ten consecutive resets of the same replay remained stable. Two candidate
  output-lock regressions are real and replay-local (`WingedGorgeousPanther`, first mismatch frame
  3864; `ScaryFrankPorcupine`, first mismatch frame 2), but neither exhausts JObjs alone or in its
  sequential group. Repeating the exact eight-worker stage suite reproduced the sealed 1,024-byte
  allocation in 3.6 seconds. Match reset recreates the Match-local class context, so ordinary
  within-process reset carry-over is not supported by this evidence.
- **Disposition:** replay-local JObj exhaustion rejected. The abort is specific to the parallel
  runner assignment/concurrency shape or to a non-contiguous replay transition within that shape;
  it must not drive a gameplay allocator change.
- **Next step:** temporarily log native server job identity to identify the exact crashed worker's
  replay sequence, then remove that instrumentation. In parallel with that diagnosis, use the two
  deterministic output-lock failures—not the allocator symptom—to locate the first stage-owned
  field changed by the cull.

#### 2026-07-17 — Stadium dynamic-owner correction opened

- **Scope:** retain frozen Stadium map 5's complete source owner: common JObj animation followed by
  `grStadium_801D1604 -> Ground_801C2FE0`. All other locked culls remain unchanged.
- **Hypothesis:** map 5 animation publishes the platform transform/velocity consumed by mpLib and
  grounded fighter motion even in the frozen profile. Culling animation while also culling its
  collision publication caused `ScaryFrankPorcupine`'s frame-2 grounded horizontal-carry change
  and `WingedGorgeousPanther`'s later air/ground velocity drift.
- **Evidence before experiment:** `grStadium_801D1570` binds map 5 into collision through
  `Ground_801C2ED0`, `Ground_801C3214`, and `mpJointListAdd(4)`, attaches its animation, and schedules
  `Ground_801C2FE0`. The first changed Scary fields are `speed_air_x_self`,
  `speed_ground_x_self`, and `pos_x`; this is a gameplay collision-transform consumer, not a
  presentation symptom. The unchanged-scheduler profile assigns about 0.515M cycles to map 5's
  common animation and 23.487M cycles to its collision callback.
- **Disposition:** open.
- **Next step:** run the two deterministic exact replay locks with map 5 retained. If green, update
  the callback inventory and measured removable budget; if red, do not broaden the retention mask
  without locating the next first stage-owned publication.

#### 2026-07-17 — Stadium map 5 retention rejected; construction order isolated

- **Scope:** retained map 5's animation and priority-4 collision callback, then ran the two exact
  failing replay locks.
- **Hypothesis:** map 5's per-frame transform publication caused both deterministic regressions.
- **Result/evidence:** both failures were byte-for-byte unchanged: `ScaryFrankPorcupine` still first
  diverged at frame 2 and `WingedGorgeousPanther` at frame 3864. Map 5 retention did not own either
  change.
- **Disposition:** rejected. Keep map 5 retention only during the next construction-order
  isolation so one variable changes at a time.
- **Next step:** stop deleting culled animation graphs during stage construction while retaining the
  callback schedule cut. `configure_headless_ground_schedule` currently runs before fighter
  construction; freeing stage AObj/FObj nodes there changes the shared source allocator free-list
  order seen by every fighter. If leaving the graphs attached restores both locks, move their final
  deletion after all source construction rather than weakening the cull.

#### 2026-07-17 — construction-order hypothesis rejected; Stadium map 2 opened

- **Scope:** left every culled animation graph attached while retaining the epoch-only callback
  replacement.
- **Hypothesis:** early AObj/FObj release changed fighter construction through allocator order.
- **Result/evidence:** both deterministic failures were again unchanged. Animation-graph deletion
  does not own the output drift; callback execution does.
- **Disposition:** rejected.
- **Next step:** retain map 2's common animation. Maps 0 and 1 have hosted initialization paths that
  attach no animation; map 2 alone calls `grAnime_801C8138`, publishes collision through
  `Ground_801C2ED0`, and schedules `Ground_801C2FE0`. Map 5 remains retained for isolation. Run the
  two exact locks before deciding whether map 5 can independently be culled.

#### 2026-07-17 — Stadium map 2 retention rejected; null proc isolated

- **Scope:** retained common animation for maps 2 and 5, leaving only maps 0 and 1 culled; animation
  graphs remained attached and map 5's priority-4 callback remained live.
- **Hypothesis:** map 2's collision-bound animation caused the regressions.
- **Result/evidence:** both exact failure fingerprints remained unchanged. The regression does not
  depend on suppressing map 2 or map 5 animation.
- **Disposition:** rejected.
- **Next step:** retain all four Stadium common animation callbacks while continuing to delete the
  asserted-null `Ground_801C1D38` procs. This isolates the only remaining candidate schedule change
  on Stadium without touching another stage or owner.

#### 2026-07-17 — null Ground proc deletion identified and rejected

- **Scope:** retained every Stadium common animation and map 5 collision callback, leaving deletion
  of asserted-null `Ground_801C1D38` procs as the sole Stadium candidate change.
- **Hypothesis:** the null proc deletion was behavior-neutral.
- **Result/evidence:** both exact failures remained unchanged under that isolation. Although
  `xC_callback` is null, deleting the proc during stage construction returns its GObjProc node to
  the shared source free list before fighter callbacks are allocated; the changed downstream
  allocation/scheduler shape is gameplay-visible. The empty invocation is not independently worth
  restructuring construction to preserve allocation order.
- **Disposition:** rejected from Packet 1. The null post procs remain source-shaped and scheduled.
- **Next step:** with all Stadium common animation still retained, disable only null-proc deletion
  and confirm both exact locks. Then restore the audited common-animation mask independently.

#### 2026-07-17 — diagnostic object contamination invalidates mismatch attribution

- **Scope:** audit the effective native build after the supposedly no-op Stadium schedule still
  reproduced both failures.
- **Hypothesis:** the candidate schedule continued to differ from committed behavior.
- **Result/evidence:** with every Stadium common animation retained, its static callback retained,
  and null-proc deletion disabled, the output still differed. The preserved subsystem profiler
  compiles `api.c`, `scalar.c`, `gobj.c`, `fighter.c`, and `ftanim.c` with target-specific macros in
  the ordinary native object directory, but `NATIVE_FLAGS_SIGNATURE` does not include those
  per-target macros. `make native` therefore left unchanged profile-instrumented gameplay objects
  in the production binary. Scalar rebuilt cleanly during candidate edits; the other instrumented
  objects did not. Profile-induced code-generation changes can alter strict float output, so all
  preceding conclusions about the two deterministic mismatches are invalid.
- **Disposition:** the map-5, map-2, construction-order, and null-proc mismatch attributions are
  withdrawn, not retained evidence. Their narrow experiments remain chronologically documented.
  The profiler/build-directory defect is tooling-only and must not alter Packet 1's gameplay
  boundary.
- **Next step:** delete only the five known contaminated native build objects and rebuild them
  without profiling macros. Confirm the current no-op Stadium schedule exactly matches both replay
  locks before restoring and testing the candidate inventory.

#### 2026-07-17 — manifest profile correction and true-HEAD check opened

- **Scope:** rebuild known profile-instrumented objects cleanly and rerun focused validation through
  canonical suite manifests rather than positional replay arguments.
- **Hypothesis:** the two apparent output regressions were candidate behavior.
- **Result/evidence:** positional replay arguments use the validator's default UCF profile and drop
  manifest overrides. Both investigated replays explicitly set
  `ucf_cardinals_1_0_enabled=false`, so every earlier positional mismatch was invalid evidence.
  Through `aggregate_recent.json`, `WingedGorgeousPanther` exactly reproduces its existing
  classification (9,915/10,625 matched, first excluded row 9793). Through `peach.json`,
  `ScaryFrankPorcupine` reaches the sealed JObj allocation under its correct profile. This is the
  same replay assigned last to the crashed parallel worker. The current Stadium schedule performs
  no candidate action, so candidate ownership is not yet proven.
- **Disposition:** every positional mismatch attribution is withdrawn. The allocator symptom is
  now narrowed to `ScaryFrankPorcupine` under its manifest UCF profile.
- **Next step:** preserve the complete dirty candidate in a named stash, build and run the same
  manifest-scoped replay from exact committed HEAD, then reapply the stash. This decides whether the
  allocation is a pre-existing retained-baseline defect or a no-op-codegen/initialization effect
  introduced by Packet 1 before any further gameplay edit.

#### 2026-07-17 — true HEAD reproduces sealed JObj allocation

- **Scope:** named-stash preservation of the complete dirty packet; exact committed HEAD rebuild;
  one-case `peach.json` validation with the replay's manifest UCF profile; reapplication of the
  untouched candidate stash.
- **Hypothesis:** Packet 1 caused the `ScaryFrankPorcupine` JObj exhaustion.
- **Result/evidence:** exact HEAD `e3c700c5` reproduced the identical sealed Match-arena 1,024-byte
  allocation (`used=956020`, `allocations=1199`) in 2.6 seconds including rebuild. The candidate was
  preserved as stash `173e5a7af7baf14f5ffcaff4d4bf302d867c97c4` and reapplied without dropping
  the stash. The allocator failure predates Packet 1.
- **Disposition:** rejected as Packet 1 evidence. Do not widen the JObj pool or encode a validation
  ordering workaround in this packet. The focused correctness gate must compare candidate and HEAD
  only on canonical shapes that are green on HEAD; the full aggregate gate remains authoritative
  for whether the pre-existing order-sensitive defect blocks final retention.
- **Next step:** restore the exact candidate callback inventory, run manifest-scoped per-stage locks
  with normal clean objects, and compare every result to an adjacent HEAD run where necessary.

#### 2026-07-17 — exact candidate inventory restored

- **Scope:** restore the locked masks, including frozen Stadium map 5 and Battlefield map 6 static
  callbacks; replace the audited common animation owners; delete their now-displaced AObj/FObj
  graphs; remove asserted-null Ground post procs. No diagnostic profiling macro is active in the
  production build.
- **Hypothesis:** the original source/data audit is exact; prior mismatch evidence was entirely due
  to positional validation under the wrong UCF profile, while the JObj allocation is independently
  reproduced by HEAD.
- **Disposition:** open.
- **Next step:** build normal native once, then run canonical manifest-scoped stage locks. Any
  candidate-only classification/output-lock change is a real failure; a failure identical on exact
  HEAD is baseline evidence and must not be patched inside Packet 1.

#### 2026-07-17 — focused canonical locks and candidate re-profile opened

- **Scope:** canonical aggregate output locks by stage group, followed by one bounded candidate
  owner profile.
- **Hypothesis:** the candidate preserves every existing exact/classified output and completely
  removes the locked scheduled owners without shifting their work.
- **Result/evidence so far:** FD plus Battlefield completed all 67 cases exactly under their existing
  output locks/classifications (40 pass, 27 classified, no fail/error) in 4.3 seconds including the
  candidate rebuild. The 64-case FoD/Yoshi/Dream group stopped on two sealed JObj allocations;
  exact HEAD independently exhibits this baseline first-match/order-sensitive allocator defect, so
  the abort is not candidate correctness evidence. No classification drift was reported before the
  worker failures.
- **Disposition:** focused correctness remains open because the baseline allocator defect prevents
  a clean isolated all-stage gate. Static-stage output locks are retained evidence.
- **Next step:** run the representative 512-resident owner profile once. Verify that every named
  animation/static callback disappears and no substitute timed owner appears before benchmarking.

#### 2026-07-17 — candidate owner profile complete; digest drift isolated

- **Scope:** 512 resident environments, 64 representative ticks, unchanged profiling boundary.
- **Hypothesis:** named owners disappear without shifting work or changing the production digest.
- **Result/evidence:** all locked construction-time common animation owners are absent; only the
  retained dynamic maps and explicitly later-created transient maps remain. Frozen Stadium map 5
  `grStadium_801D1604` and Battlefield map 6 `grBattle_8021A174` are absent. No replacement stage
  owner appears. The profiling build took longer than ten seconds because it created a fresh
  instrumented release object set; the measured run itself took 0.542 seconds. However, the
  candidate digest is `f3f76e2500bf0da5`, not the adjacent unchanged-scheduler digest
  `3fb5823d90657775`; its 60,488 profiled FPS is therefore not retention evidence.
- **Disposition:** deletion boundary proven, correctness open.
- **Next step:** isolate the digest change before benchmarking. First leave displaced animation
  graphs attached while keeping the exact schedule cut. If the digest returns, move graph deletion
  after source construction; if not, separate common animation, static callback, and null-proc
  schedule cuts without changing the profiler/workload.

#### 2026-07-17 — attached-graph isolation rejected; null-proc cut opened

- **Scope:** exact candidate schedule with culled AObj/FObj graphs left attached.
- **Hypothesis:** deleting the graphs changed later source allocation order and therefore output.
- **Result/evidence:** the representative digest remained `f3f76e2500bf0da5`; leaving the graphs
  attached does not own the drift. The bounded run produced 60,118 profiled FPS and exact same
  candidate digest.
- **Disposition:** rejected as the digest cause. Final graph deletion remains required if the
  schedule candidate survives.
- **Next step:** retain all asserted-null `Ground_801C1D38` procs while keeping common-animation and
  audited static callback cuts. This tests whether construction-time GObjProc free-list changes,
  rather than skipped stage work, own the digest.

#### 2026-07-17 — null-proc isolation rejected; static callbacks opened

- **Scope:** retain all null Ground post procs; keep common-animation cuts and the two static
  priority-4 callback cuts.
- **Hypothesis:** construction-time GObjProc free-list changes caused the digest drift.
- **Result/evidence:** digest remained `f3f76e2500bf0da5`; null-proc deletion is not the cause.
  The run measured 60,006 profiled FPS.
- **Disposition:** rejected as the digest cause. Null-proc deletion remains excluded for now because
  it is independently negligible and avoiding it simplifies the production cut.
- **Next step:** retain frozen Stadium map 5 and Battlefield map 6 priority-4 callbacks while
  keeping only the common-animation replacement. If the digest remains changed, the common cut
  itself owns the behavior difference; if it returns, separate the two static callbacks.

#### 2026-07-17 — static callbacks rejected as digest cause; common cut isolated

- **Scope:** retain both static priority-4 callbacks and every null post proc; replace only the
  audited common animation owners.
- **Hypothesis:** one of the static collision callbacks changed gameplay output.
- **Result/evidence:** digest remained `f3f76e2500bf0da5`; static callback removal does not own the
  drift. The profile run measured 59,073 production / 60,410 step-only FPS.
- **Disposition:** rejected as the digest cause. The common-animation cut is the sole active
  candidate behavior in this isolation.
- **Next step:** run the canonical Fox/Falco suite across all six stages. It exercises every stage
  without the pre-existing Peach first-match JObj high-water and retains manifest UCF/event
  metadata. Use its first exact output-lock drift to identify the common stage owner before any
  further mask ablation.

#### 2026-07-17 — Fox/Falco all-stage locks green; Stadium-only ablation opened

- **Scope:** canonical 33-case Fox/Falco aggregate across all six supported stages with only the
  common-animation candidate active.
- **Hypothesis:** a gameplay-observed common stage owner will alter at least one exact output lock.
- **Result/evidence:** all 33 cases retained their exact outputs/classifications (22 pass, 11
  existing classified, no fail/error) over 301,249 frames in 4.2 seconds. The representative digest
  drift is therefore outside this broad two-character lock or arises from free-running snapshot
  state not challenged by replay-authoritative inputs/events.
- **Disposition:** retained correctness evidence; digest cause remains open.
- **Next step:** retain every common owner on FoD, Yoshi, Dream Land, and Battlefield, leaving only
  frozen Stadium's audited common cut active. The representative digest decides whether Stadium is
  in the causal set before another stage group is tested.

#### 2026-07-17 — Stadium common cut owns digest drift

- **Scope:** frozen Stadium common-animation replacement only; every other stage common owner and
  both static callbacks retained.
- **Hypothesis:** Stadium is outside the digest's causal set.
- **Result/evidence:** the Stadium-only candidate produced the same changed digest
  `f3f76e2500bf0da5`. It measured 55,766 profiled FPS, as expected after restoring the other removed
  work. At least one Stadium common JObj walk therefore publishes free-running state not challenged
  by the current replay-authoritative locks. Treating all four as presentation-only is disproven.
- **Disposition:** rejected as a source-complete cut. Retain all four frozen Stadium common owners;
  do not replay-fit a narrower map mask in this packet. Their combined common-walk cost is only
  about 6M of 2.035B profiled cycles, so this conservative boundary preserves nearly all common
  animation budget.
- **Next step:** restore the audited FoD/Yoshi/Dream/Battlefield common cuts with all Stadium common
  owners live. Confirm the baseline digest returns. Then test the Stadium and Battlefield static
  priority-4 callback cuts independently of common Stadium animation.

#### 2026-07-17 — non-Stadium common cuts share digest drift; AObj wrapper opened

- **Scope:** all audited FoD/Yoshi/Dream/Battlefield common cuts active, every Stadium common owner
  retained, both static callbacks retained.
- **Hypothesis:** retaining Stadium would restore the baseline digest.
- **Result/evidence:** digest remained `f3f76e2500bf0da5`; the other common cuts independently reach
  the same downstream output. Any one stage-specific presentation transform is therefore not a
  sufficient explanation.
- **Disposition:** the preceding claim that Stadium animation output itself owns the drift is
  narrowed: Stadium is in one causal set, but non-Stadium cuts form another.
- **Next step:** preserve the source's shared AObj wrapper semantics in every epoch-only owner.
  `HSD_JObjAnimAll` unconditionally calls `HSD_AObjInitEndCallBack`, traverses the JObjs, then calls
  `HSD_AObjInvokeCallBacks`; AObj end/active accounting is Match-global. The replacement must perform
  the empty begin/end boundary before publishing the collision epoch. Test this with Stadium still
  retained before reconsidering any stage-specific mask.

#### 2026-07-17 — empty AObj wrapper rejected; no-op control opened

- **Scope:** add the source AObj begin/invoke calls without traversing a culled graph.
- **Hypothesis:** Match-global end-callback accounting, rather than JObj animation output, caused the
  digest drift.
- **Result/evidence:** digest remained `f3f76e2500bf0da5`; empty wrapper calls are insufficient and
  add work without proving the required state.
- **Disposition:** rejected and removed.
- **Next step:** run a no-op schedule control: retain every non-FD common callback, both static
  callbacks, every null proc, and attached graphs. The committed FD epoch path remains the only
  existing cull. This distinguishes candidate behavior from benchmark-manifest/seed drift.

#### 2026-07-17 — no-op control proves benchmark artifact refresh

- **Scope:** exact no-op non-FD schedule under the same 512-resident representative profile.
- **Hypothesis:** Packet 1 caused the digest change from the earlier `3fb5823d90657775` evidence.
- **Result/evidence:** the no-op control also produced `f3f76e2500bf0da5`. The benchmark manifest's
  mtime is 22:12:52, immediately before the first candidate profile and after the 21:36 baseline
  profile; `benchmark-prepare` regenerated the packed artifact. Every candidate/no-op ablation used
  the same refreshed 153-case manifest and produced the same digest. The old and new digest are not
  an A/B pair.
- **Disposition:** Packet 1 digest drift rejected. All stage-specific and AObj causal claims based
  solely on comparison to `3fb5823d90657775` are withdrawn. The canonical Fox/Falco output locks
  remain independent correctness evidence.
- **Next step:** create an adjacent non-instrumented no-op baseline from the current packed manifest
  at 512 and 256, preserve its executable, restore the complete original candidate inventory, and
  alternate baseline/candidate measurements using the same manifest and digest.

#### 2026-07-17 — workload correction and adjacent no-op baseline complete

- **Scope:** clean non-instrumented release objects; preserved no-op executable; canonical 512/256
  resident benchmark.
- **Hypothesis:** the packed manifest refresh changed the benchmark digest.
- **Result/evidence:** false. The clean canonical workload exactly restores retained digests:
  512 reached 74,062 production / 75,875 step-only FPS with `3fb5823d90657775`; 256 reached
  58,463 / 60,014 FPS with `bdc54107c51fa3d7`. The `f3f76e2500bf0da5` diagnostic came from
  `MSL_CORE_PROFILE_REPRESENTATIVE_ONLY`, which maps only the 32 seed-bank cases across all slots;
  it is a different supporting workload. The no-op executable is preserved at ignored
  `reports/triage/packet1-stage-cull/noop-baseline-replay-bench`, SHA-256
  `9d33fe1aa115a4b08a87ee7de708ac55f3ab3cf15185e621dfdc444f7ffd59bc`.
- **Disposition:** retained adjacent baseline. All digest-based stage/AObj causal claims made under
  the representative-only workload are withdrawn; the ablations still show that all candidate
  variants were internally equivalent on that supporting workload.
- **Next step:** restore the complete original candidate inventory and build normal release without
  diagnostic macros. Measure canonical candidate 512/256, then rerun the preserved no-op executable
  for alternating A/B evidence.

#### 2026-07-17 — candidate earns benchmark retention

- **Scope:** full original candidate inventory; clean non-instrumented release; preserved-executable
  A/B pairs on CPU 0 at 512 and 256; representative-only supporting profile.
- **Hypothesis:** the completed measured deletion boundary produces a repeatable material gain with
  exact production outputs.
- **Result/evidence:** all canonical digests are exact. At 512, paired no-op/candidate results were
  74,062/80,723 FPS (+9.0%) and 76,080/82,379 (+8.3%); paired means are 75,071 versus 81,551
  (+8.6%). At 256, pairs were 58,463/61,441 (+5.1%) and 59,269/61,780 (+4.2%); means are 58,866
  versus 61,611 (+4.7%). The representative-only profiled supporting workload retained digest
  `f3f76e2500bf0da5` and moved from 55,918 no-op to 60,488 candidate FPS (+8.2%). Candidate
  executable SHA-256 is `d0be998fc5e4b84042e256ef2ce80848786cef2a75b4e72e19ea920b8fcc6769`.
- **Disposition:** retained performance candidate. The 512 acceptance checkpoint materially exceeds
  the roughly 5% threshold; 256 independently improves.
- **Next step:** take one canonical all-case owner profile without the representative-only mapping,
  confirm exact digest and named-owner deletion, then remove all temporary profiler source from the
  production diff. Run the complete correctness/API/allocation/Wasm gates only after that evidence
  is clean.

#### 2026-07-17 — canonical owner profile complete; profiler removed

- **Scope:** full 153-case slot mapping at 512 resident environments with the completed candidate,
  followed by removal of all diagnostic profiler source and build toggles from the production diff.
- **Hypothesis:** the canonical workload retains its exact digest, every construction-time owner in
  the locked inventory is absent, and no substitute callback inherits the work.
- **Result/evidence:** digest remained `3fb5823d90657775`. Culled construction maps no longer invoke
  `Ground_801C1CD0`; only the explicitly retained dynamic maps and later transient Ground objects do.
  Frozen Stadium map 5 `grStadium_801D1604` and Battlefield map 6 `grBattle_8021A174` are absent,
  while the cheap epoch owner appears only on culled construction maps. No shifted stage owner was
  observed. The diagnostic run itself measured 72,471 FPS, but profiler overhead makes that a
  deletion-boundary check rather than retention evidence. Raw output is
  `reports/triage/packet1-stage-cull/candidate-canonical-profile.txt`.
- **Disposition:** retained deletion-boundary evidence. Temporary profiler code is removed; the
  production diff again contains only the packet implementation and documentation.
- **Next step:** rebuild ordinary native/release artifacts without diagnostic macros, then run the
  complete correctness, API/save-restore, allocation, and Wasm/viewer gates.

#### 2026-07-17 — production gates complete; full-gate HEAD control opened

- **Scope:** normal native/source smokes, sealed-runtime census, 256-environment lifecycle and
  arbitrary-index restore, Wasm/native parity, live-viewer, and browser checks; one complete
  153-replay candidate gate followed by the same command on exact committed HEAD.
- **Hypothesis:** production contracts remain green; any complete-suite startup failure is the
  pre-existing first-match/order-sensitive JObj high-water already reproduced on exact HEAD.
- **Result/evidence so far:** source sync, native API/context/batch/save-restore smokes, runtime
  allocation lock, 256-environment lifecycle/restore, Wasm parity (`cef96ff32edfe898`), viewer
  (`1b43d24b47c228f2`), and browser smoke all pass. The candidate's full gate stopped before replay
  comparison when three workers requested a 1,024-byte JObj slab from sealed Match arenas. No
  output-lock or classification mismatch was reported.
- **Disposition:** production contracts retained; complete replay gate baseline attribution open.
- **Next step:** preserve the complete candidate in a named stash, run the identical full gate on
  exact `e3c700c5`, and reapply the stash before recording the final gate disposition.

#### 2026-07-17 — exact-HEAD full-gate control reproduces blocker

- **Scope:** identical 153-replay command, 16 workers, normal native build from exact committed
  `e3c700c5`, with the candidate safely preserved as named stash
  `6b987ab89689e708d82f3ff4fd498c35ce7c298c` and reapplied afterward.
- **Hypothesis:** exact HEAD reproduces the candidate's three sealed-JObj failures before replay
  comparison.
- **Result/evidence:** exact HEAD stopped on the same three 1,024-byte sealed JObj slab requests,
  at arena uses 887,704, 956,020, and 899,952 bytes. The candidate stopped at the corresponding
  lower uses 885,628, 955,640, and 897,876 bytes. Neither run reported an output-lock,
  classification, XPASS, or replay comparison failure before the baseline allocator defect aborted
  the suite.
- **Disposition:** complete replay gate is baseline-blocked, not a Packet 1 regression. The cull is
  retained on its exact canonical benchmark, focused output locks, deletion-boundary profile, and
  all unaffected production gates; it is not eligible for a correctness-green commit until the
  pre-existing JObj high-water defect is closed in a separately scoped packet.
- **Next step:** record the final result in `PERFORMANCE.md`, preserve the complete uncommitted
  candidate as a named stash and ordered ignored binary patch, and leave the candidate applied.

#### 2026-07-17 — retained candidate preserved; packet closed

- **Scope:** preserve the complete implementation and evidence without committing, then reapply it
  to the worktree.
- **Result/evidence:** named stash
  `884641e660af80095740015af64f050344ccc415` (`perf packet 01 retained supported-stage headless
  cull`) contains the candidate and untracked active log. Ordered ignored patch
  `reports/triage/perf_candidates/001-supported-stage-headless-cull.patch` has SHA-256
  `e53c6605ebc6b5877c7def2bd21d66b0ffa3f60304bc51f15235c65416d6e2e5`. The stash remains and the
  candidate is reapplied uncommitted.
- **Disposition:** retained uncommitted performance candidate; correctness-green commit remains
  blocked only by the independently reproduced baseline JObj high-water defect. Packet 2 has not
  started.
- **Next step:** report the measured result and disposition to the user; await a separately scoped
  decision on the baseline allocator gate before any commit or further optimization packet.

#### 2026-07-17 — correctness recovery reopened by user

- **Scope:** close the complete-suite JObj allocation failure as part of Packet 1, retain or improve
  the measured cull gain, rerun every production gate, and commit the finished result. The previous
  instruction to stop at the allocator owner is superseded for this recovery.
- **Hypothesis:** `hsdPreallocateMemPieces(64)` leaves too little free 192-byte source-class storage
  for a supported transient JObj graph after construction. The sealed 1,024-byte request is the
  source allocator trying to split a new class slab after that reserve is exhausted. The correct
  fix belongs to initialization-only source allocator sizing and adds no frame-path work.
- **Evidence:** candidate and exact `e3c700c5` fail on the same three slab requests. Current runtime
  census reports the 192-byte class at 473 active / 47 free after its representative construction;
  the supported replay corpus exercises larger transient graphs than that census scenario.
- **Disposition:** open. Prior claim that the packet was complete is withdrawn.
- **Next step:** reproduce one failing manifest-scoped replay, measure the smallest supported-domain
  JObj reserve that clears it and the full suite, then encode that initialization bound without
  changing gameplay scheduling or hot-path state.

#### 2026-07-17 — reached-class reserve 128 closes complete suite

- **Scope:** raise only the initialization-time reached-class reserve ceiling and request from 64
  to 128; retain the stage cull unchanged. Run the one-replay manifest reproducer, complete
  aggregate gate, and runtime census.
- **Hypothesis:** bounded initialization headroom clears every supported transient graph without
  runtime allocation or frame-path cost.
- **Result/evidence:** `ScaryFrankPorcupine` completes all 26,844 frames with its exact existing
  classification. The complete suite is green: 63 exact pass, 90 exact existing classifications,
  zero XPASS/fail/error, and 1,415,476 compared frames in 4.825 seconds. Runtime allocation remains
  sealed and stable. Representative arena/savestate payload rises from 907,420 to 921,656 bytes
  (+14,236 bytes); maximum supported construction rises from 1,252,240 to 1,266,476 bytes. The
  reached 192-byte class has 111 free pieces after construction instead of 47.
- **Disposition:** retained correctness fix. It changes initialization storage only and adds no
  gameplay callback, hot branch, or per-frame work.
- **Next step:** build a corrected no-cull baseline with the same 128-piece reserve, then alternate
  512/256 canonical A/B runs against the complete candidate. The cull must retain exact digests and
  a material gain after the correctness cost is normalized.

#### 2026-07-17 — normalized corrected A/B retains material gain

- **Scope:** build two clean release executables that both include the 128-piece reached-class
  reserve; only the candidate includes the supported-stage cull. Alternate uncontended CPU-0 runs
  at 512 and 256 resident environments on the canonical complete-output workload.
- **Hypothesis:** after normalizing the correctness reserve, the cull remains materially faster
  with exact production digests.
- **Result/evidence:** at 512, baseline runs were 78,609 and 78,398 FPS; candidate runs were 83,747
  and 83,927 FPS. Paired means are 78,504 versus 83,837 FPS (**+6.8%**) with digest
  `3fb5823d90657775`. At 256, baseline runs were 60,098 and 59,707 FPS; candidate runs were 63,158
  and 63,255 FPS. Means are 59,903 versus 63,207 FPS (**+5.5%**) with digest
  `bdc54107c51fa3d7`. Corrected no-cull and candidate executable SHA-256 values are
  `13b48edb7a535927183fe8b5a20e5657111398faffa88d88937853fd77a8aa0c` and
  `cfa974e86afc5a675a11686156be410587aaa351a49d4db53dcd097d3c41012d`.
- **Disposition:** retained final performance result. One accidentally overlapping diagnostic
  launch was terminated and all of its timing discarded; the recorded runs above were executed
  singly to completion on CPU 0.
- **Next step:** update durable evidence, rebuild the applied candidate, run source/API/allocation,
  Wasm/viewer, and formatting gates, then commit only the clean final implementation and evidence.

#### 2026-07-17 — final production gates complete

- **Scope:** source sync; native data/model/map/scheduler/API/context/batch/save-restore; sealed
  runtime allocation; 256-environment lifecycle and arbitrary-index restore; PPC scalar/data/model/
  map/scheduler; Wasm/native parity; live viewer; browser viewer; and diff hygiene.
- **Result/evidence:** every gate is green. Fresh PPC compilation exposed a pre-existing guard error
  from `4383ba3e`: `mpisland.h` selected the native direct-TLS owner for hosted PPC even though that
  pointer is declared only for native builds. Restricting the direct path to native and retaining
  the existing accessor for PPC closes the compile boundary without changing native execution.
  Wasm state/viewer digests remain `cef96ff32edfe898` and `1b43d24b47c228f2`.
- **Disposition:** Packet 1 is correctness-green, materially faster, and ready for its authorized
  commit. The PPC compile fix is isolated as a preceding focused commit.
- **Next step:** commit the PPC guard fix separately, commit the retained stage cull plus allocator
  bound and evidence, verify the clean worktree, and report the remaining measured performance queue.
