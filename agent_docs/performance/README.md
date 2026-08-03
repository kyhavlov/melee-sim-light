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

- [`BASELINE.md`](BASELINE.md): current production benchmark contract, binary provenance, digests,
  correctness gates, memory contract, and owner-selection profile.
- [`RETAINED.md`](RETAINED.md): concise commit-oriented ledger of performance work still present in
  the runtime.
- [`HISTORY.md`](HISTORY.md): detailed retained evidence and the legacy bounded negative-result
  ledger.
- [`JOURNAL_2026-07-23.md`](JOURNAL_2026-07-23.md): cumulative scalar-owner optimization journal
  imported from the historical `melee-sim-light-decompport` checkout, including its retained cuts,
  rejected cuts, controlled measurements, and owner profile.
- [`JOURNAL_2026-07-24.md`](JOURNAL_2026-07-24.md): the same checkout's previously stranded
  dynamics, quaternion, ECB, callback, and compiler experiment log. Any `open` label in it is
  archival rather than an active task.
- [`JOURNAL_2026-07-21_TO_23_SYSTEM_REWRITES.md`](JOURNAL_2026-07-21_TO_23_SYSTEM_REWRITES.md):
  historical compiled-pose/system-rewrite checkpoints imported from
  `experiments/decomp-port/system-rewrites`; these are evidence, not current runtime state.
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

- [Compiled fighter motion to gameplay
  geometry](attempts/2026-08-03-compiled-fighter-motion-geometry.md): exact, but less than 1% faster for
  roughly 8k changed lines. A per-fighter scalar product cache does not remove enough scheduler,
  map, contact, or source-state cost. A revisit needs a batch-native owner and deletion boundary,
  not more consumer wrappers.
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
