# Performance history index

This directory is the single durable search root for performance work. Before opening a packet,
search it by subsystem, source owner, symbol, representation, and proposed optimization; for
example:

```sh
rg -n -i 'fighter animation|Fighter_8006A360|pose' agent_docs/performance
```

Retained changes belong in
[`RETAINED.md`](RETAINED.md), the current comparison contract belongs in
[`BASELINE.md`](BASELINE.md), and detailed historical measurements remain in
[`HISTORY.md`](HISTORY.md). The `attempts/` directory records architectural attempts whose code was
not retained, including enough context to avoid rebuilding the same failure under a new name.

An attempt record should state the exact parent and workload, intended owner/deletion boundary,
implementation shape, correctness result, controlled performance result, profile attribution,
why it was rejected, what (if anything) was salvaged, and the evidence required to justify a
revisit. Record failed hypotheses as evidence, not prohibitions: a materially different owner or
representation may justify revisiting the same broad subsystem.

## Files

- [Stationary root constraints and pose index capacity](HISTORY.md#stationary-fighter-roots-retain-constraint-invalidation--2026-09-07): Bowser capture dependency correction within the flat pose owner.

- [Bowser acos estimate counterexample](HISTORY.md#bowser-disproves-the-x86-acos-estimate-equivalence--2026-09-07): expanded gameplay disproves the retained x86 seed substitution; canonical Gekko seed restored.

- [Mixed training groups and natural 2v1 starts](HISTORY.md#mixed-training-groups-and-natural-2v1-starts--2026-09-07): normal three-player teams, exact event restores, mixed PPO and recording/warmup overhead measurements.
- [`TARGET_ARCHITECTURE.md`](TARGET_ARCHITECTURE.md): final canonical batch-native state,
  execution shape, hosted-runtime deletion boundary, throughput case, and cutover discipline for
  the 500k campaign.
- [`../PERFORMANCE.md`](../PERFORMANCE.md): concise current checkpoint result and gate status
  required by the repository performance-commit contract.
- [`BASELINE.md`](BASELINE.md): current production benchmark contract, binary provenance, digests,
  correctness gates, memory contract, and owner-selection profile.
- [`RETAINED.md`](RETAINED.md): concise commit-oriented ledger of performance work still present in
  the runtime.
- [`HISTORY.md`](HISTORY.md): detailed retained evidence and the legacy bounded negative-result
  ledger.
- [Python release library and item capacity correction](HISTORY.md#python-release-library-and-item-capacity-correction--2026-09-05): strict PIC release profiles,
  rejected partial Peach reservations, source-bounded item pools, and full-team
  RL validation.
- [`JOURNAL_2026-07-23.md`](JOURNAL_2026-07-23.md): cumulative scalar-owner optimization journal
  imported from the historical `melee-sim-light-decompport` checkout, including its retained cuts,
  rejected cuts, controlled measurements, and owner profile.
- [`JOURNAL_2026-07-24.md`](JOURNAL_2026-07-24.md): the same checkout's previously stranded
  dynamics, quaternion, ECB, callback, and compiler experiment log. Any `open` label in it is
  archival rather than an active task.
- [`JOURNAL_2026-07-21_TO_23_SYSTEM_REWRITES.md`](JOURNAL_2026-07-21_TO_23_SYSTEM_REWRITES.md):
  historical compiled-pose/system-rewrite checkpoints imported from
  `experiments/decomp-port/system-rewrites`; these are evidence, not current runtime state.
- [`JOURNAL_2026-08-03.md`](JOURNAL_2026-08-03.md): the first 150k-throughput campaign checkpoint,
  including the complete retained and rejected owner experiments between `5fac340b` and the exact
  wide-pose/Dream Land checkpoint.
- [`JOURNAL_2026-08-03_CHECKPOINT_2.md`](JOURNAL_2026-08-03_CHECKPOINT_2.md): the second
  150k-throughput checkpoint, including all retained and rejected experiments between `ee32fbc1`
  and the frame-local pose/motion/collision checkpoint.
- [`PACKETS_2026-08-02.md`](PACKETS_2026-08-02.md): completed owner/deletion packet worklogs from
  the final push to the 100k baseline.
- [`attempts/`](attempts/): one record per rejected architectural direction, including its revisit
  criteria and any salvage.

## Consolidation provenance

- The current branch's former `CURRENT_BASELINE.md`, `RETAINED_PERFORMANCE.md`, and
  `PERFORMANCE.md` are now `BASELINE.md`, `RETAINED.md`, and `HISTORY.md` here.
- Completed performance packets formerly mixed into `agent_docs/ACTIVE_WORK.md` are preserved in
  `PACKETS_2026-08-02.md`; `ACTIVE_WORK.md` no longer carries those closed packet logs.
- The unique cumulative journal and stale active experiment log from
  `/mnt/nvme0/projects/melee-sim-light-decompport` are preserved in the July 23 and July 24
  journals.
- The unique compiled-pose checkpoints from `experiments/decomp-port/system-rewrites` are preserved
  in its dated journal. Distinct committed performance-doc snapshots on `decomp-port-arm64-ppc`,
  interface cleanup, optimizations, system rewrites, and `perf/decomp-throughput` were audited;
  their remaining experiment headings are already present in this directory.
- Superseded baseline and retained-summary snapshots were not copied a second time. The July 20
  general operation-owner rewrite has only the recovered summary below and in the compiled-motion
  attempt record; no more detailed durable document was found in the audited checkouts.

## Architectural attempts

- [Tiled fighter transforms and SIMD
  publication](attempts/2026-08-04-tiled-fighter-transforms.md): the real
  eight-lane kernel was neutral at resident 256 and recovered only 1.25% at
  resident 512 inside a complete candidate still roughly 20-25% slower than
  the committed baseline. Do not revisit tile widths, scheduler splits,
  context consolidation, or gather/scatter publication; a future batch-native
  layout must eliminate those boundaries outright.
- [Compiled fighter motion to gameplay
  geometry](attempts/2026-08-03-compiled-fighter-motion-geometry.md): exact, but less than 1% faster for
  roughly 8k changed lines. A per-fighter scalar product cache does not remove enough scheduler,
  map, contact, or source-state cost. A revisit needs a batch-native owner and deletion boundary,
  not more consumer wrappers.
- [Gameplay-live compiled Figa
  samples](attempts/2026-08-04-gameplay-live-figa-samples.md): construction-time motion/part
  closure removed 27.8% of the exact immutable sample stream, but was 0.90% slower at resident 256
  and only 0.30% faster at 512 on the canonical V-cache CCD. Cold-byte deletion is not pose-work
  deletion; revisit only with a representation that also removes demanded publication or proves a
  materially different cache target.
- [Shared Figa program
  clock](attempts/2026-08-04-shared-figa-program-clock.md): a singular exact clock for direct
  looping and non-looping fighter programs was 3.9--4.2% slower at resident 256 and 0.5--0.6%
  slower at 512. Per-node clock math is already cheap; a revisit must delete traversal or demanded
  publication too.
- [Resident step/output
  fusion](attempts/2026-08-04-resident-step-output-fusion.md): one per-Match
  `step -> observation -> terminal` sweep preserved exact output but regressed 0.5--1.8% at
  resident 256 and 3.0% at 512. A full scalar frame does not leave enough useful Match state hot;
  revisit only after a materially smaller canonical representation.
- [Unchanged direct-pose
  publication](attempts/2026-08-04-unchanged-direct-pose-publication.md): construction-owned
  exclusion of dynamics-authored matrices made repeated-row suppression exact, but hot SRT
  comparisons were about 10.0% slower at resident 512. Revisit only by removing the scalar
  publication/consumer boundary itself, not by adding dirty-detection work.
- [Configuration-local resident
  order](attempts/2026-08-04-configuration-local-resident-order.md): stable physical sorting was
  exact and helped one resident-512 screen, but its isolated resident-256 symmetric center was
  about 1.9% slower. Revisit only with a smaller canonical Match hot state or a public-row layout
  that does not pay the inverse permutation.
- [Compact resident Match arena
  stride](attempts/2026-08-04-compact-resident-arena-stride.md): reducing unused per-lane virtual
  reserve from 3 MiB to 1.25 MiB was exact but about 2% slower at resident 512. Revisit only with
  actual live-state repacking, not another capacity or page-layout choice.
- [Canonical native JObj
  SRT](attempts/2026-08-04-canonical-jobj-srt.md): moving the singular 40-byte SRT into the compact
  pose node or a dense fixed pool preserved exact output but did not lower the 18% pose owner; the
  better form was below 1%, and the pool form regressed resident 256. Revisit only as part of a
  wider producer/consumer deletion, not another layout.
- General operation-owner rewrite (2026-07-20; summarized in the compiled-motion record): about
  94k lines and 3.7–6.0x slower. Operation-granular scheduling and generic ownership machinery
  overwhelm the work being optimized.
- [`experiments/decomp-port/system-rewrites` proof
  branch](JOURNAL_2026-07-21_TO_23_SYSTEM_REWRITES.md): exact semantic seams and large isolated
  owner speedups, but mostly noise-bound whole-runtime throughput. Proof scaffolding, handoffs, and
  synchronized source machinery are not a production representation.

## Imported scalar-owner journal

The [July 23 journal](JOURNAL_2026-07-23.md) records the retained direct animation spans,
authored-node shield blending, and Dream Land callback deletion, plus rejected work on scheduler
grouping, pose flags and traversal, hurt ownership, compiler closure, stage epochs, Figa layout and
deduplication, dirty checks, AObj accounting, observation export, input preprocessing, collision,
and contact broad phases. The [July 24 journal](JOURNAL_2026-07-24.md) continues with dynamics and
quaternion compiler boundaries, TLS pose lookup, ECB gather shape, Battlefield callback pruning,
and the next hurt-ownership hypothesis. Search both before proposing another narrow optimization
in those owners.

## Existing bounded negative results

The older detailed ledger remains in [`HISTORY.md`](HISTORY.md). Its rejected
experiments are grouped here by the lesson they test:

- **Pose, animation, and ECB demand:** [level-ordered ECB
  kernel](HISTORY.md#rejected-level-ordered-ecb-matrix-kernel), [compact pose
  schedule](HISTORY.md#rejected-compact-pose-animation-work-schedule), [dense pose
  interpreter](HISTORY.md#rejected-direct-dense-pose-interpreter), [immutable ECB
  trig](HISTORY.md#rejected-immutable-dense-ecb-trig), [dependency
  walk](HISTORY.md#rejected-compact-pose-dependency-walk), [direct tree
  iteration](HISTORY.md#rejected-direct-compact-pose-tree-iteration), [demand-owned
  publication](HISTORY.md#rejected-demand-owned-ordinary-pose-publication), [compiled
  spans](HISTORY.md#rejected-compiled-pose-publication-operation-spans), [native batch ECB
  seam](HISTORY.md#rejected-native-batch-ecb-publication-seam), [eager ECB
  program](HISTORY.md#rejected-eager-exact-ecb-matrix-program), [dense pose
  publication](HISTORY.md#rejected-dense-ordinary-pose-publication), and [scalar ECB
  follow-up](HISTORY.md#rejected-scalar-ecb-publication-follow-up). Extra admission,
  gathering, or publication seams repeatedly lose unless they delete a larger canonical owner.
- **Scheduling, layout, and context:** [split compact pose
  state](HISTORY.md#rejected-split-compact-pose-hot-state), [native stage
  access](HISTORY.md#rejected-native-stage-scalar-access), [scheduled-fighter compiler
  boundary](HISTORY.md#rejected-exact-scheduled-fighter-compiler-boundary), [tile-resident
  scheduler](HISTORY.md#rejected-tile-resident-scalar-scheduler), [process-global
  context](HISTORY.md#rejected-process-global-hosted-context-pointers), [cache-line JObj
  layout](HISTORY.md#rejected-cache-line-native-jobj-hot-layout), and [fixed fighter
  scheduler](HISTORY.md#rejected-fixed-hosted-fighter-scheduler). Layout alone does not
  help while consumers still chase the same source objects and callbacks.
- **Compiler and math boundaries:** [hot-closure
  LTO](HISTORY.md#rejected-exact-hot-closure-lto), [three-axis SIMD
  trig](HISTORY.md#rejected-exact-three-axis-simd-trig), [zero-Euler
  shortcuts](HISTORY.md#rejected-exact-zero-euler-shortcuts), [isolated affine
  concat](HISTORY.md#rejected-isolated-exact-affine-concat), [whole matrix compiler
  admission](HISTORY.md#rejected-whole-hsd-matrix-compiler-admission), and [release LTO
  partition](HISTORY.md#rejected-exact-release-lto-partition). Revisit only with an exact
  owner boundary and whole-contract evidence.
- **Collision, headless work, and specialization:** [stage AABB
  admission](HISTORY.md#rejected-exact-stage-line-aabb-admission), [result-stat
  deletion](HISTORY.md#rejected-headless-transient-result-stat-production), [guard overlay
  fusion](HISTORY.md#rejected-fused-guard-overlay-publication), [human-input
  specialization](HISTORY.md#rejected-supported-human-input-specialization), [dynamics
  specialization](HISTORY.md#rejected-fighter-dynamics-branch-specialization), [second map
  broad phase](HISTORY.md#rejected-second-fighter-map-pass-broad-phase), [camera
  closure](HISTORY.md#rejected-additional-headless-camera-closure), and [hosted input
  deletion](HISTORY.md#rejected-hosted-human-input-owner-deletion). These were exact but
  neutral/regressive because they duplicated an already cheap predicate or removed too little
  reached work.

When a new result supersedes one of these conclusions, update the relevant attempt record and this
index in the same change.

- [Replay curriculum retired](HISTORY.md#replay-curriculum-retired--2026-09-07): negative training result; experiment stashed, general relocation fixes retained.
