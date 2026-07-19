# Active performance packet — fixed fighter animation lifecycle

## Outcome

Replace fighter motion-transition AObj/FObj pool churn with fixed, initialization-owned Match
storage. Preserve the source animation state machine and exact output while targeting at least
+5% throughput at 512 environments on the staggered active-gameplay benchmark.

## Packet boundary

- Final owner: fighter animation attachment/removal in `melee/ft/ftanim.c`, with fixed mutable
  storage owned by `MslCoreMatch`. Generic stage/item animation remains in `melee/lb/lbanim.c` and
  baselib.
- Canonical state: one fixed-capacity Match pool of fighter-owned `HSD_AObj`/`HSD_FObj` nodes,
  initialized before gameplay and relocated as persistent typed state. Source `FigaTree` data is
  immutable and shared. Motion state, animation frame, interpolation, and scheduler order remain
  authoritative.
- Consumers: every full and partial fighter Figa attachment/removal path in `ftanim.c`, including
  ordinary motion changes, blends, wait-animation swaps, and part animation. Construction-time
  model AObjs displaced by the first Figa attachment are removed in `ftparts.c`.
- Benchmark owner: `tests/melee_core/replay_bench.c`. Its final workload starts every logical
  environment from its real replay, advances eight deterministic startup groups by 200–900
  untimed frames, and then times continued gameplay with ordinary per-environment replay looping.
  The displaced partial representative-snapshot bank is deleted completely.
- Temporary measurement owners: `runtime/subsystem_profile.*`, the two dispatch sites in
  `melee/ft/fighter.c`, `melee/ft/ft_0C31.c`, `melee/ft/ftanim.c`, and the profiler report
  tooling/target. These may change only behind `MSL_SUBSYSTEM_PROFILE` and must be restored before
  retention.
- Displaced code/state: fighter use of generic HSD AObj/FObj allocation, free-list mutation,
  runtime relocation-record type mutation, and whole-skeleton removal traversal during animation
  changes.
- Deletion boundary: every hosted fighter Figa path attaches, replaces, and detaches through the
  fixed Match owner. No generic-allocation fallback, dual representation, lazy migration, or
  replay-derived capacity is retained. Temporary attribution code is deleted before retention.

## Execution

1. Establish source-backed fixed capacities from the existing complete supported-domain HSD pool
   contract and verify all fighter attachment variants fit them.
2. Cut hosted fighter attachment/removal over to the fixed owner in one path. Keep generic HSD
   animation ownership only for non-fighter consumers and the non-hosted source build.
3. Preserve typed relocation, arbitrary-index copy/save/restore, and exact animation semantics.
   Diagnose correctness inside the fixed representation; do not add a fallback.
4. Use interleaved CPU-0 control/candidate medians at 512 and 256 because host throughput varies
   materially between runs. Retention requires both production
   digests, the complete 153-replay gate, native/API/save-restore tests, Wasm parity, source sync,
   and no post-initialization allocation.
5. Remove all attribution instrumentation. Update `CURRENT_BASELINE.md`, `PERFORMANCE.md`, and
   `RETAINED_PERFORMANCE.md`; commit only atomic correctness-green improvements with evidence.

## Baseline

- Runtime: staggered benchmark control `cc83fb42`
- 256: 42,399 FPS, digest `8ef126a41244d514`
- 512: 35,623–39,495 FPS, digest `6f91f23e3553a090`
- Correctness: 63 PASS / 90 CLASSIFIED / 0 XPASS/fail/error

## Log

- 2026-07-18: Opened the packet from clean `a0f7c8bf`. First experiment is profiler-only callback
  attribution; no production representation change is authorized until the target distribution is
  measured.
- 2026-07-18: Profiler-only attribution retained as an open diagnostic. Action-animation callbacks
  are 7.36% and input callbacks 1.61% of the contract. `ftCo_EntryStart_Anim` and
  `ftCo_EntryEnd_Anim` alone own 60.1% of animation-callback cycles and 4.43% of the full contract;
  input cost is diffuse. Named `melee/ft/ft_0C31.c` and its shared motion-state transition call
  graph as the first optimization owner. Next: separate steady entry ticks from transition ticks
  and attribute the transition cost before changing production code.
- 2026-07-18: Corrected attribution to capture callback identity before callbacks can change motion
  state. Entry-to-start and entry-start-to-end transitions consume 4.35% of the measured contract;
  `Fighter_ChangeMotionState` consumes 6.58%, almost entirely animation setup. Track attachment is
  2.12% and old animation removal 1.02%. Disposition remains diagnostic: no production
  optimization was attempted.
- 2026-07-18: Stopped on uncertain benchmark provenance. The workload constructs only 32
  representative snapshots but cycles 512 logical slots across all 153 cases; cases without a
  selected snapshot start from frame zero. Roughly 405/512 slots therefore measure opening entry
  states during the 64-tick window, explaining the artificial entry-transition concentration.
  Next decision: make every measured slot restore one of the bounded representative midgame seeds
  (repeated across the batch), re-establish the baseline/profile, then resume callback ownership.
- 2026-07-18: User selected the simpler final benchmark contract: delete snapshots, pre-roll all
  153 replay streams in eight 100-frame-spaced groups, begin timing only after every group is in
  gameplay, and keep ordinary replay-end looping. Open implementation uses 200–900 frame offsets
  plus the existing eight all-environment warmup ticks.
- 2026-07-18: Rejected independently pre-rolling all 512 repeated copies: even a 32,768-frame timed
  window exceeded ten seconds. The final open benchmark pre-rolls each of the 153 unique replay
  streams once, copies that initialized Match into its repeated batch slots through the production
  copy API, and deletes the redundant step-only pass. At 65,536 timed match-frames it completes in
  5.7s at 256 and 7.5s at 512, including preparation checks. New results are 42,399 FPS / digest
  `8ef126a41244d514` at 256 and 39,495 FPS / digest `6f91f23e3553a090` at 512.
- 2026-07-18: Corrected 512 profile matches digest `6f91f23e3553a090`. Entry transitions fall to
  effectively zero. Action-animation callbacks are 8.72% and input callbacks 7.11%. The dominant
  shared descendant is `Fighter_ChangeMotionState` at 11.36%: animation setup owns 11.02%, with
  attachment 7.48%, old-animation removal 4.46%, and new track attachment 1.88%. Outside callbacks,
  live pose evaluation is 17.14% and fighter map collision is 21.81%. Input attribution is diffuse
  across Wait, Dash, Jump, Turn, and Landing; action attribution concentrates in GuardOn, Guard,
  Fall, KneeBend, and Landing families. Next: choose a durable common transition/animation-lifecycle
  cut rather than leaf-specializing individual callbacks.
- 2026-07-18: Opened the fixed lifecycle implementation. The existing Match already reserves 704
  AObjs and 1,280 FObjs before sealing, so gameplay does not reach the system heap; the removable
  work is generic free-list churn, per-object relocation-record mutation, and broad skeleton
  traversal. The final candidate will move the same bounded fighter state into a persistent typed
  Match owner while leaving non-fighter HSD animation generic. Adjacent corrected 512 controls are
  digest-stable at 35,623 and 38,915 FPS, requiring interleaved medians for final attribution.
- 2026-07-18: The first fixed-pool native smoke stopped on a construction AObj attached by the
  compact costume JObj loader. Retained direction: strip those model AObjs once in `ftparts.c`,
  matching the source Figa attacher's immediate replacement, rather than adding a local/generic
  runtime fallback. Next: complete that construction boundary and rerun native smoke.
- 2026-07-18: Fixed ownership now covers ordinary Figa attachments and fighter part
  `HSD_AnimJoint` attachments; compact model AObjs are stripped once at construction. Native/API/
  copy/save-restore smoke and all 153 replays are green at 63 PASS / 90 unchanged CLASSIFIED /
  zero failures. Candidate samples are 44,971 FPS at 256 and 42,779 FPS at 512 with exact
  staggered-workload digests. Worst-case arena use falls 997,972 to 986,616 bytes and relocation
  records fall 6,127 to 4,530. Next: build an exact-HEAD control with only the staggered benchmark
  change and collect interleaved medians before cleanup.
- 2026-07-18: Completed the source teardown audit. Fighter removal passes flag `1`, while RObj
  animation removal is owned by flag `0x80`; therefore RObj state must remain untouched here.
  Rejected a separate RObj active list and the per-AObj no-op RObj call it was intended to avoid.
  The final owner contains only fixed fighter JObj AObjs/FObjs and preserves generic RObj ownership.
- 2026-07-18: Final exact-code A/B at 512 produced adjacent pairs 40,381/42,276,
  40,562/42,666, and 36,356/40,999 FPS (control/candidate), all with digest
  `6f91f23e3553a090`. The median paired improvement is +5.19%; comparing the two raw medians gives
  +4.69% under the same substantial host drift. At 256 the corresponding median paired improvement
  is +5.25% and the raw-median improvement is +4.67%, digest `8ef126a41244d514`.
- 2026-07-18: Rejected removing fixed-pool ownership checks after it lowered the adjacent 512
  raw-median improvement to about +3.4%. Restored the checked implementation. The final census is
  allocation-locked at 658,148 arena bytes / 828 allocations before and after gameplay; ordinary
  savestate size is 719,788 bytes, maximum construction is 986,616 bytes, and maximum relocation
  records fall from 6,127 to 4,530.
- 2026-07-18: Final production tree is correctness-green: 63 PASS / 90 unchanged CLASSIFIED / zero
  XPASS/fail/error over 1,415,476 frames; native API/copy/save-restore, source sync, formatting,
  Wasm parity, viewer schema/gameplay/browser smoke, and allocation census pass. Temporary
  transition attribution was removed. Retained evidence is refreshed with the implementation in
  this commit.
