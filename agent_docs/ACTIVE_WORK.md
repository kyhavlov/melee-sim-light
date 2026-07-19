# Active performance packet — compact fighter pose and gameplay geometry

## Outcome

Replace the live fighter AObj/FObj/JObj pose and matrix-publication boundary with one compact native
evaluator that directly serves gameplay geometry. The retained candidate must improve the true
resident 512-environment benchmark by at least 10%, preserve exact replay/API/save-restore/Wasm
behavior, and contain no dual pose, source fallback, generated animation artifact, or temporary
instrumentation.

## Final boundary

- Final owner: `runtime/fighter_pose.{c,h}` for native fighter motion tracks, contiguous joint
  topology, animation-phase invalidation, root/world placement, and gameplay point publication.
  Retained fighter `HSD_JObj` handles contain the representation's sole local SRT and world matrix so
  existing procedural source owners can mutate canonical state in scheduler order. Exact source HSD
  matrix construction remains the arithmetic primitive for dirty/special joints; it is not a second
  pose or fallback representation. Source `ftanim`, ground IK, capture, dynamics, and character
  callbacks remain semantic phase owners.
- Immutable input: the already translated, sealed `FigaTree`, `HSD_AnimJoint`, costume-joint,
  fighter-part, dynamics, and hit/hurt/contact descriptors loaded from DAT during GameData
  initialization. Compact track/topology programs are compiled from those C graphs before the
  initialization seal; there is no Python/legacy extractor or new packaged gameplay asset.
- Canonical mutable state: per-fighter compact track cursors and clocks; the retained joint handles'
  local SRT and parent-first world transforms; procedural-owner state; and
  hit/hurt/shield/reflect/absorb/grab/anchor geometry. The compact owner and its joint handles are
  Match-owned, fixed-capacity, relocatable, and included by arbitrary-index save/restore.
- Direct consumers: move hitboxes and grab boxes; BODY hurt capsules; shield, reflector, and absorber
  volumes; held-item/article and character-special attachment anchors; ground IK; capture/throw
  positioning; and retained dynamic-bone chains. ECB remains position/source-collision owned, but any
  pose-joint query it reaches consumes the compact transform.
- Displaced state/code: fighter `HSD_AObj`/`HSD_FObj` lifecycle storage, global/ancestry-scanning
  fighter animation traversal, animation-driven recursive descendant invalidation, generic fighter
  point dispatch, repeated root-driven ordinary matrix rebuilds, and duplicate hurt-endpoint setup.
  Retained JObj local/world fields are canonical storage used directly by the compact owner.
- Deletion boundary: supported native fighters have one pose/geometry representation and one
  animation/topology owner. All fighter point consumers enter its type route; hit/hurt/shield,
  attachment, IK, capture, and dynamics therefore read its canonical matrix directly. No synchronized
  pose, setter interception, lazy source materialization, fighter fallback dispatch, compatibility
  flag, legacy extractor, or dead fixed-pool implementation remains. Non-fighter objects and the
  source/PPC build keep their source HSD owners.

## Execution gates

1. Attribute track interpretation, tree dispatch, dirty propagation, matrix construction, procedural
   pose, and geometry publication on the exact committed 512 workload.
2. Prove the final evaluator shape in an ignored isolated harness before broad production edits. The
   proof must use live translated DAT programs and parent-first transforms and demonstrate a credible
   removal of at least 10% of total 512 frame time; otherwise stop before the cutover grows.
3. Install the final compact state and program compiler, then cut all named consumers over and
   complete the deletion boundary before replay-parity repair or throughput tuning.
4. Recover exact correctness, ordering, arbitrary-index copy/save/restore, doubles, and Wasm inside
   the compact representation. Never restore displaced source machinery to make an intermediate
   failure green.
5. Remove all proof/profiling code. Require unchanged 256/512 digests; 63 PASS / 90 unchanged
   CLASSIFIED / zero failures; native source/API/copy/save-restore and sealed-allocation gates; PPC,
   Wasm/viewer, and format gates. Benchmark adjacent CPU-0 control/candidate pairs at 512 and 256.
6. Update retained evidence only after every gate passes. Leave the complete result uncommitted.

## Baseline

- Runtime: `71586c18`
- 256 digest: `8ef126a41244d514`; retained raw median 48,273 FPS
- 512 digest: `6f91f23e3553a090`; retained raw median 45,638 FPS
- Correctness: 63 PASS / 90 CLASSIFIED / zero XPASS/fail/error

## Log

- 2026-07-18 — `open`
  Scope: source map for `ftAnim_8006EBA4`, HSD AObj/FObj/JObj evaluation, fixed fighter animation
  storage, DAT initialization sealing, and downstream transform consumers.
  Hypothesis: flat compact track state plus parent-first transforms and direct geometry can remove
  the linked generic animation/tree/matrix boundary rather than cache one of its outputs.
  Evidence: the committed subsystem profile attributes 22.56% of the instrumented contract to live
  pose animation; additional direct consumers are separately present in contact publication,
  dynamics, IK, capture, and attachment callbacks. Native initialization already preloads every
  supported motion archive before sealing translated DAT graphs.
  Disposition: final owner and deletion boundary named above. First experiment is temporary nested
  attribution followed by an ignored final-shape evaluator harness; no production representation is
  admitted before that proof.
  Next: measure track/tree/matrix shares and construct the bounded viability proof.
- 2026-07-18 — `rejected`
  Scope: unmodified native-release symbol sampling with Linux `perf` on the exact 512 workload.
  Hypothesis: call-stack samples can split FObj interpretation, JObj traversal, matrix construction,
  and geometry without instrumenting the runtime.
  Evidence: the host denies performance counters and observability at `perf_event_paranoid=4`; no
  benchmark process ran and no runtime evidence was produced.
  Disposition: sampling is unavailable. Use bounded compile-time cycle buckets inside the named
  owners, then remove them completely.
  Next: add nested animation/transform attribution to the existing diagnostic profiler.
- 2026-07-18 — `open`
  Scope: temporary nested cycle/count attribution in AObj/FObj interpretation, JObj dependency and
  matrix setup, RObj animation, and `lb_8000B1CC` point transforms.
  Hypothesis: the final cut must remove both animation dispatch and consumer-side lazy matrices;
  either owner alone is too narrow.
  Evidence: exact-digest 512 diagnostic counts 4,238,227 active AObj/FObj joint evaluations,
  6,979,165 dirty matrix builds, and 4,926,923 point transforms across 65,536 match-frames. FObj work
  is the majority of active AObj time; RObj animation is negligible. High-frequency timers inflate
  wall time, so their cycle totals are directional only, while counts are exact.
  Disposition: retain the owner map, not the instrumentation. Measure a disposable no-pose upper
  bound, then benchmark a live-data flat evaluator/world-transform proof.
  Next: quantify the removable whole-frame ceiling and final evaluator cost.
- 2026-07-18 — `retained`
  Scope: ignored 512-resident viability proof over live translated DAT-driven fighter states; no
  production representation is installed.
  Hypothesis: sparse/direct component publication plus flat parent-first world transforms is cheap
  enough to recover at least 10% overall even after exact exceptional-track support.
  Evidence: the disposable no-pose upper bound reduces a contemporaneous 1.499 s / 43,719 FPS run to
  0.752 s / 87,117 FPS, identifying roughly 0.747 s of animation plus downstream dirty-matrix work.
  The final-shape common kernel covers 1,150 live fighters, 84,951 DAT-derived SRT tracks, and 47,553
  retained joints; at equivalent 128-tick work its measured cost scales to roughly 0.322 s. Its
  parent-first matrices match source bits on all 46,700 ordinary eligible joints. There are 2,494
  non-unit-rate active joints (10.0% of animated joints) and zero live non-SRT tracks in the staggered
  snapshot; the final compact track interpreter must model the former, not exclude it. The projected
  whole-frame gain is approximately 25–30% before direct geometry savings, leaving ample margin over
  the 10% acceptance threshold.
  Disposition: viability gate passed. Remove the proof and attribution code, then install the one
  compact production representation and complete the named consumer/deletion boundary.
  Next: implement compact topology/track/local/world state and cut native `ftanim` to it.
- 2026-07-18 — `open`
  Scope: production representation cut for native supported-fighter tracks, flat topology, transform
  evaluation, and joint-backed gameplay queries.
  Hypothesis: retaining fighter joint handles as the compact owner's sole local/world storage avoids
  rewriting procedural mechanics into synchronized side state while still deleting the expensive
  generic AObj/FObj animation and recursive matrix owners. A final fighter/non-fighter type route is
  unconditional for fighter joints and is not a source fallback.
  Evidence: gameplay and character source owners mutate joint SRT directly across dozens of files;
  the live-data proof already established bit-exact parent-first output using those same canonical
  values. The current `aobj` slot can become the fighter node-owner link without growing JObj state;
  non-fighter objects remain exclusively HSD-owned.
  Disposition: install the compact track state and evaluator first, then flat transform/query
  ownership; never create side local/world arrays or synchronize two representations.
  Next: replace the native fighter animation pool and `ftanim` lifecycle with compact tracks.
- 2026-07-18 — `open`
  Scope: first production compact-track cut using fixed Match-owned joint/track arrays and direct SRT
  publication from live Figa/AnimJoint streams.
  Hypothesis: exact source track state with direct common-component publication can replace fighter
  AObj/FObj allocation and linked callback traversal independently of the subsequent flat transform
  cut.
  Evidence: native construction, API, copy/save-restore, and gameplay-parts smoke pass after deleting
  the fixed generic fighter animation owner. A 1,200-frame replay sweep is not yet correct: 24/153
  cases pass and the dominant symptom is a two-frame delayed action transition; the bounded Peach
  demo reproduces it only on frames 34–60. Registering otherwise unattached topology nodes was ruled
  out, and FObj TYPE_JOBJ ordering plus IK-hint publication now match source.
  Disposition: representation retained as open; isolate the shared animation-liveness/end-callback
  discrepancy before any matrix/geometry edits.
  Next: compare source and compact per-joint active/end accounting around the bounded transition.
- 2026-07-18 — `open`
  Scope: global native fighter animation entry routing and the first full supported-domain gate for
  the compact track owner.
  Hypothesis: routing every supported-fighter `HSD_JObjAnim` entry through the compact owner closes
  the shared two-frame transition delay without changing source scheduler order.
  Evidence: native/API/save-restore smoke and the bounded Peach replay became exact; the 1,200-frame
  sweep improved from 24/153 to 112/153. The complete 1,415,476-frame gate still regresses to 43
  PASS / 49 CLASSIFIED / 61 FAIL, with many failures appearing only after thousands of frames and
  disproportionately affecting doubles. Compact track runs are currently relocated in joint
  registration order rather than source-address order, so an in-place leftward move can overwrite
  a later run that has not yet been copied.
  Disposition: entry routing is necessary but the compact storage lifecycle remains open. Do not
  begin the transform/geometry cut until long-run compact-track relocation is exact.
  Next: make compaction preserve ascending old track-run order, then retest one exact singles, one
  previously late-diverging classified replay, and one doubles replay before repeating the full
  domain gate.
- 2026-07-18 — `retained`
  Scope: compact track-run relocation during fighter animation transitions.
  Hypothesis: compacting live runs in ascending old-address order prevents an earlier move from
  overwriting a run whose owning joint was registered earlier but whose current tracks were
  allocated later.
  Evidence: the exact 10,174-frame singles replay passes; the previously early-diverging 9,328-frame
  classified replay now has only its unchanged one-row camera classification; and the 9,560-frame
  doubles replay reproduces its exact committed signed-zero fingerprint with no new mismatch.
  Disposition: retain address-ordered relocation as canonical compact storage behavior.
  Next: repeat the complete supported-domain gate before beginning the transform/geometry cut.
- 2026-07-18 — `retained`
  Scope: full-domain correctness checkpoint for compact fighter track ownership.
  Hypothesis: address-ordered relocation was the sole long-run storage corruption after global
  fighter animation entry routing.
  Evidence: the complete native gate is restored to 63 PASS / 90 unchanged CLASSIFIED / zero
  XPASS/fail/error across all 1,415,476 frames, including every doubles replay and output lock.
  Native construction, API, copy/save-restore, and gameplay-parts smoke remain green.
  Disposition: compact AObj/FObj replacement is exact and becomes the sole native fighter animation
  owner. Continue directly to flat transforms and named geometry consumers; do not retain the
  generic fighter matrix/query boundary.
  Next: compile parent-first topology and replace recursive/lazy fighter matrix publication plus
  generic fighter point queries.
- 2026-07-18 — `rejected`
  Scope: first production owned-joint matrix cut using per-node local/world revisions and direct
  compact point transformation.
  Hypothesis: revision-based parent setup would delete recursive dirty-subtree propagation while
  retaining demand-driven source matrix arithmetic.
  Evidence: the 1,200-frame suite preserves the existing 135 exact rows plus 18 truncated known
  classifications, and the 512 workload preserves digest `6f91f23e3553a090`; throughput falls from
  the retained 45,638 FPS median to 37,723 FPS (-17.3%). Per-query ancestry/revision work is more
  expensive than the displaced recursive owner.
  Disposition: reject the per-query revision evaluator as an intermediate shape.
  Next: test one parent-first tree publication per dirty tree, with direct compact point queries.
- 2026-07-18 — `rejected`
  Scope: parent-first dirty-tree publication using contiguous registered topology, non-recursive
  dirty marking, and direct compact point transformation.
  Hypothesis: one selective parent-first scan per dirty tree amortizes matrix publication across all
  gameplay consumers and realizes the isolated flat-kernel result.
  Evidence: the same 1,200-frame suite and 512 digest remain unchanged, but throughput falls further
  to 33,412 FPS (-26.8%). The production scheduler can dirty and query a fighter tree multiple times
  across animation, action, IK, capture, dynamics, attachment, and contact phases, so tree-wide
  publication repeats excess scanning/matrix work instead of matching the isolated one-pass model.
  Disposition: reject the demand-triggered dirty-tree evaluator. This is the second failed structural
  experiment on the same architectural unknown; stop implementation and reconvene as required.
  Next: preserve the exact compact-track owner separately; reshape matrix/geometry publication around
  explicit scheduler phase boundaries and consumer-owned compact geometry rather than another lazy
  JObj setup variant.

## Stop-point salvage inventory

- Durable: the compact Match-owned joint/track program, direct SRT publication, global fighter
  animation routing, address-ordered track relocation, relocation/save-state contract, and exact
  63/90/0 full-domain checkpoint before matrix edits.
- Reshape: the current contiguous topology/root metadata and direct point API are useful inputs, but
  the demand-triggered tree evaluator is not a retained design. The next proposal must name the
  scheduler publication seams and which geometry is materialized at each seam before editing.
- Proposed removal after approval: revision/tree-dirty fields and owned `HSD_JObjSetupMatrix`
  dispatch introduced by the two rejected experiments. Do not remove or restore substantial dirty
  work without explicit user direction.
- Still incomplete: direct hit/hurt/shield/attachment/IK/capture/dynamics cutover, deletion of generic
  fighter matrix ownership and dead fixed-pool code, complete gates, and the required >=10% retained
  512 result.

## Resumed sequence

- 2026-07-18 — `open`
  Scope: isolate the full-domain-exact compact animation owner from both rejected matrix evaluators.
  Hypothesis: measuring the compact track owner alone is required before choosing the scheduler-aligned
  geometry cut; its cost cannot be inferred from either regressed combined candidate.
  Evidence: the last exact checkpoint preceded all owned-matrix dispatch, while both later benchmarks
  combined compact track interpretation with rejected demand-triggered matrix machinery.
  Disposition: surgically remove only revision/tree-dirty state and owned matrix/point dispatch. Keep
  the compact animation program, address-ordered storage, native animation entry routing, path-track
  semantics, relocation contract, and O3 owner intact.
  Next: rerun native smoke, the complete replay gate, and the 512 checkpoint on the isolated compact
  animation owner; use that result to budget the final scheduler-aligned geometry publication cut.
- 2026-07-18 — `retained`
  Scope: isolated compact animation-owner correctness and adjacent 512 measurement.
  Hypothesis: compact track ownership is a durable exact substrate even if it does not independently
  deliver the matrix/geometry packet's throughput target.
  Evidence: the complete gate is 63 PASS / 90 unchanged CLASSIFIED / zero failures. An adjacent clean
  `71586c18` control is 45,649 FPS and the compact-track candidate is 44,591 FPS with identical digest
  `6f91f23e3553a090`, a -2.32% cost before geometry replacement.
  Disposition: retain the exact compact owner as the required final representation; the geometry cut
  must recover this small interpreter cost and remove enough matrix/publication work to exceed +10%.
  Next: count point queries, matrix builds, and dirty publications by exact scheduler owner on the
  resident workload, then name explicit publication seams from those counts.
- 2026-07-18 — `open`
  Scope: temporary scheduler-owner attribution for fighter point queries, matrix builds, and dirty
  publications on the exact resident workload.
  Hypothesis: most costly geometry demand clusters at a small number of source process priorities;
  publishing consumer-owned geometry at those seams avoids both rejected demand-triggered designs.
  Evidence: current subsystem timing identifies broad phase owners but does not attribute matrix
  demand or invalidation frequency to the active GObj process.
  Disposition: add profile-build-only counters keyed by source process callback, collect one resident
  run, then remove all counters before production edits.
  Next: resolve callback addresses to source owners and write the final phase/deletion map.
- 2026-07-18 — `rejected`
  Scope: the hypothesis that geometry demand is concentrated at one scheduler publication seam.
  Hypothesis: one source process owns enough point demand and matrix invalidation to support a single
  bulk publication pass.
  Evidence: exact owner counts show two distinct dominant boundaries. Animation priority 1 causes
  2,818,984 dirty publications but only 5,899 immediate matrix builds. Map priority 6 causes
  3,265,366 dirty publications, 3,040,134 matrix builds, and 860,376 point queries. Post-map contact
  priority 9 contributes 688,199 builds. The later collision owners then dominate demand:
  `Fighter_8006CB94` has 417,544 queries / 311,669 builds and `Fighter_ProcessHit_8006D1EC` has
  3,139,727 queries / 2,368,740 builds despite only 12,248 local dirty publications.
  Disposition: reject a single demand-triggered or single-seam whole-tree pass. Remove all diagnostic
  counters. The final evaluator must separate local-pose publication from root/world placement and
  publish collision geometry before the priority 13/14 consumers.
  Next: prove the root-placement factorization and exact collision-geometry publication boundary;
  prioritize eliminating map-driven descendant rebuilds and repeated ProcessHit matrix demand.
- 2026-07-18 — `open`
  Scope: redundant fighter-root placement publication at map and accessory seams.
  Hypothesis: source publishes `cur_pos` before and after map collision and after each accessory
  callback even when the root already contains the identical three float bit patterns. Skipping only
  identical writes preserves pose semantics while avoiding recursive descendant invalidation; it is
  a durable subset of final root/world placement ownership.
  Evidence: priority 6 owns 3,265,366 dirty publications and 3,040,134 matrix builds; priorities 7/8
  add 49,939 dirty publications and 331,935 builds. No other state changes when the assigned SRT bits
  are identical.
  Disposition: test bitwise-equal root placement suppression at the four source scheduler seams. Keep
  only if the full replay contract and digest are unchanged and throughput materially improves.
  Next: native smoke, bounded replay check, then adjacent 512 measurement before the full gate.
- 2026-07-18 — `retained`
  Scope: bit-identical fighter-root placement suppression.
  Hypothesis: repeated identical SRT publication has no gameplay effect and only invalidates cached
  descendants.
  Evidence: native smoke and the complete 63/90/0 replay contract pass; digest remains
  `6f91f23e3553a090`. The 512 candidate improves from 44,591 to 46,310 FPS (+3.85%) and exceeds the
  adjacent clean control's 45,649 FPS by 1.45%.
  Disposition: retain as the first scheduler-aligned world-placement cut.
  Next: replace changed root-translation invalidation with exact translation-column propagation for
  already-clean ordinary descendants while leaving dirty/special source owners to rebuild normally.
- 2026-07-18 — `open`
  Scope: changed fighter-root translation publication over compact parent-first topology.
  Hypothesis: a pure root translation leaves every descendant basis column unchanged. For a clean
  ordinary descendant, recomputing only the world translation column with the exact `PSMTXConcat`
  operation order is bit-identical to a full SRT/matrix rebuild; dirty nodes already carry the new
  root value into their eventual source build, while IK/RObj/path/independent/user-matrix subtrees
  retain their semantic source handling.
  Evidence: priority 6 currently rebuilds 3,040,134 matrices after root placement. The source matrix
  formula writes local translation directly, then concatenates its translation column independently
  of the three basis columns.
  Disposition: install root-tree topology and a direct world-placement owner; do not add a second
  matrix representation or a generic setter hook. Retain only with exact replay/save-state behavior
  and a material 512 gain.
  Next: implement the root placement API, replace the four explicit fighter scheduler seams, then run
  bounded exactness and an early benchmark before the full gate.
- 2026-07-18 — `retained`
  Scope: exact changed-root translation propagation.
  Hypothesis: ordinary clean descendants need only their translation column republished when fighter
  world placement changes.
  Evidence: full 63/90/0 replay parity and digest `6f91f23e3553a090` remain exact. The early 512 result
  is 50,055 FPS, +9.65% over the adjacent 45,649 clean control and +9.68% over the retained 45,638
  median. Dirty and semantic special subtrees still execute their source matrix owner.
  Disposition: retain. This is canonical root/world placement ownership, not a cache or second pose.
  Next: route fighter point geometry directly through the compact owner and group paired hurt-capsule
  endpoints so the priority 13/14 collision consumers stop re-entering generic point dispatch.
- 2026-07-18 — `open`
  Scope: direct compact point and paired-capsule publication for fighter geometry.
  Hypothesis: owned fighter joints already expose canonical matrices through the compact node;
  bypassing generic root/type dispatch and setting up a bone once for paired hurt endpoints removes
  repeated hot-call overhead without changing transform arithmetic or publication timing.
  Evidence: `Fighter_ProcessHit_8006D1EC` alone requests 3,139,727 fighter point transforms on the
  resident workload; hurt capsules publish two endpoints from the same bone.
  Disposition: add one owned point/pair API, route the native fighter point boundary unconditionally,
  and replace the repeated hurt endpoint sequences. No alternate representation or fallback.
  Next: bounded correctness and 512 checkpoint before broader consumer cleanup.
- 2026-07-18 — `open`
  Scope: animation-phase local-pose invalidation in the compact evaluator.
  Hypothesis: the compact parent-first animation walk can publish an animated joint's SRT and mark
  only that joint dirty. Each following joint already executes `HSD_JObjCheckDepend`, so parent
  invalidation propagates in walk order without recursively dirtying every descendant from the
  first animated ancestor.
  Evidence: animation priority owns 2,818,984 dirty publications while doing only 5,899 immediate
  matrix builds. The compact evaluator is now the sole SRT track owner and visits retained fighter
  joints parent-first; source setters add no semantic work beyond the SRT store and dirty call.
  Disposition: replace standard compact-track setter calls with direct canonical SRT publication and
  local dirty marking. Path/procedural updates keep their semantic source handling.
  Next: require the bounded replay shape and unchanged 512 digest before measuring the cut.
- 2026-07-19 — `retained`
  Scope: final compact topology, local-pose invalidation, and direct gameplay-point boundary.
  Hypothesis: a contiguous subtree range is the missing production equivalent of the viability
  proof: it removes global pool scans and ancestry walks without introducing phase-wide matrix work.
  Evidence: every registered node now records its parent and subtree range; animation, request,
  lifecycle, and query owners iterate only that range. Compact SRT publication marks the current
  node dirty and relies on the existing parent-first scheduler to propagate dependency, while root
  placement directly republishes exact translation columns for clean ordinary descendants. The
  native fighter point route directly serves all existing hitbox, shield, attachment, IK, capture,
  and dynamics callsites, and hurt capsules set up a shared bone once for both endpoints. The full
  replay gate is 63 PASS / 90 unchanged CLASSIFIED / zero failures across 1,415,476 frames; 256/512
  digests remain `8ef126a41244d514` / `6f91f23e3553a090`.
  Disposition: retain. Both rejected demand-triggered matrix designs and the dead fixed AObj/FObj
  implementation are absent; exact HSD matrix arithmetic remains the single canonical dirty-joint
  primitive rather than a fallback representation.
  Next: finish lifecycle capacity, platform, formatting, and final adjacent benchmark evidence.
- 2026-07-19 — `retained`
  Scope: supported-domain compact-pose capacity and sealed lifecycle.
  Hypothesis: capacity must cover structural joint ownership rather than the displaced active-AObj
  count, because compact topology also registers nonanimated ancestors.
  Evidence: the construction census reaches 976 nodes for four Peach instances. A fixed 1,024-node
  owner passes that maximum; the existing 1,024-track source-backed bound remains sufficient. The
  runtime allocation lock is unchanged at 676,440 bytes / 825 allocations through gameplay, and
  arbitrary-index copy/save/restore remains inside the typed relocation contract.
  Disposition: retain the measured fixed capacities and expose their high water in runtime census.
  Next: repeat the final complete gate and adjacent 256/512 benchmark pairs after all cleanup.
- 2026-07-19 — `retained`
  Scope: final packet acceptance.
  Hypothesis: the completed compact owner and deletion boundary retain at least 10% total throughput
  once correctness, four-player capacity, save-state relocation, and platform parity are included.
  Evidence: three adjacent 512 control/candidate pairs are 43,660/54,624, 43,152/54,202, and
  44,884/53,226 FPS, for +25.11% median paired improvement and +24.15% raw-median improvement. At
  256 the pairs are 47,748/58,192, 48,282/57,478, and 48,234/59,070, for +21.87% median paired and
  +20.65% raw-median improvement. Digests are unchanged. Final native correctness is 63/90/0 across
  1,415,476 frames; native source/API/copy/save-restore/allocation, PPC, Wasm/viewer, and formatting
  gates pass. No proof profiler, rejected matrix evaluator, dead fixed-pool code, dual pose, legacy
  extractor, or fighter fallback path remains.
  Disposition: packet complete and ready for uncommitted review.
  Next: user review; do not commit.
