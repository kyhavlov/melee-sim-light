# Retained performance changes

This is the concise ledger of performance and memory work still represented in the canonical
decomp-based runtime. Each entry identifies the commit that introduced the retained boundary.
Correctness/source-completion patches and rejected experiments do not belong here; detailed
negative results remain in `PERFORMANCE.md`.

Every throughput result uses the production release path and was accepted with the then-current
complete replay gate and unchanged output digests. Absolute results from older commits are not
directly comparable when the benchmark contract changed; their marginal A/B results remain useful.

## Retained history

### Compact resident match substrate

- `01c30553` **Compact melee core match storage** — moved relocation/allocation metadata out of
  large fixed fields and cut `MslCoreMatch` from 781,768 to about 61 KiB.
- `6036c1bc` **Separate production melee core projection** — separated the compact public output
  contract from source-state forensic bytes.
- `3b0c1847` **Tighten melee core resident memory budget** — reduced the native Match arena reserve
  from 32 MiB to 3 MiB and the reached maximum payload from roughly 17 MiB to 2.74 MiB.
- `24c6fd31` **Establish resident tiled melee core batches** — made each environment independently
  resident with fixed arena slices, arbitrary-index copy/save/restore, and no gameplay allocation.

These commits established the current memory/runtime contract rather than a clean comparable FPS
step. The first production baseline at `ec6a9b55` was 16,412 FPS at 256 and 20,202 FPS at 512.

### Exact-safe optimized source owners

- `6786edcb` **Optimize strict hosted matrix math**
- `6c89323b` **Optimize strict FObj interpretation**
- `92124554` **Optimize strict JObj traversal**
- `896bfc7d` **Optimize strict gameplay vector math**

These four complete owners entered the strict `-O3 -march=native` allowlist without fast-math or
output drift. The cumulative resident result was 27,831 FPS at 256 and 38,086 FPS at 512, about
+60%/+51% versus the preceding Phase 8 CPU-domain baseline.

### Headless owner and scheduler cuts

- `0ca3ee39` **Prune presentation-only fighter dynamics** — removed dynamics chains with no
  supported gameplay capsule consumer and retained strict `lbspdisplay.c`; measured +11.9% at 256
  and +23.3% at 512 on the then-current workload.
- `6202627f` **Run consecutive scheduler owners per match** — amortized Match binding across
  consecutive same-owner source callbacks while preserving per-Match order; +4.8% at 256 and
  +1.7% at 512.
- `86e67502` **Optimize source animation owner** — admitted complete `ftanim.c` to the strict
  compiler profile; +4.6% at 256 and +4.8% at 512.
- `7573e83e` **Cull headless bonus stats and optimize stage queries** — deleted result-screen bonus
  accounting and optimized complete `mpLib`; combined +12.0% at 256 and +13.3% at 512.
- `045d0cfa` **Restore scalar match residency** — removed the slower generic cross-Match callback
  interleaver and kept one Match bound through its source scheduler; together with null/render
  dispatch deletion, +5.3% at 256 and 7.7–8.5% at 512.
- `70ae1a3a` **Cull reached headless display and CPU owners** — deleted empty fighter display
  traversals, unused camera/result state, and CPU-only hurtbox reduction; +4.8% at 256 and +7.6% at
  512.
- `4383ba3e` **Eliminate hosted context accessor overhead** — imported native code reads the
  already-bound Match/GameData owners directly. The original packet (which also admitted
  `mpcoll.c -O3`) measured +18.5% at 256 and +17.9% at 512; the context cut remains, while the
  stale non-exact compiler admission was removed by `a46cc202` below.
- `0129f75a` **Cull supported-stage headless callbacks** — construction now schedules only stage
  callbacks that publish gameplay state; +5.5% at 256 and +6.8% at 512.
- `0d14e97c` **Cull headless camera presentation work** — reduced camera/magnify/visibility to the
  gameplay-observed owner; +0.3% at 256 and +1.5% at 512.

### Replicated-state deletion

- `bf92388d` **Prune extracted cold fighter pose** and `316be83e` **Compact hosted fighter pose
  construction** — omitted gameplay-unobserved pose subtrees and renderer graphs. The latter cut
  ordinary singles by 63,400 bytes and the four-player maximum by 125,556 bytes while improving
  throughput +3.8% at 256 and +3.2% at 512.
- `4600a762` **Cull replicated cold match state** — removed per-fighter retail scratch buffers and
  sized stage collision storage to the selected stage: -14.1% ordinary singles and -24.2% at the
  reached four-player maximum.
- `e3c700c5` **Cull replicated source class storage** — reserved only reached hosted size classes:
  ordinary singles -685,768 bytes (-43.0%), four-player maximum -685,792 bytes (-35.1%), with
  throughput neutral.
- `78b63f0a` **Replace extracted gameplay pose metadata** — replaced mutable extracted admission
  metadata with one immutable table, shrinking shared GameData by 8,488 bytes with neutral
  throughput.

### Measurement contract

- `24085f02` **Stage representative replay benchmark states** — changed the benchmark from short
  openings to 32 deterministic stage/character/late-game seeds and established the current output
  digests.
- `e7efaf44` **Make throughput benchmarks true resident** — made every logical environment a real
  Match rather than measuring a smaller sharded resident set.
- `6399f75b` **Add production-path subsystem profiler** — added opt-in RDTSCP ownership accounting
  without changing the production scheduler or normal objects.

## Profiler-introduction baseline

At `6399f75b`, CPU 0 of the Ryzen 9 9950X3D produces about 61,830 FPS at 256 and 83,013 FPS at 512.
The production digests are `bdc54107c51fa3d7` and `3fb5823d90657775`. The ordinary native replay
gate accepts all 153 cases (`63 PASS`, `90 CLASSIFIED`); a clean rebuild of the committed release
profile exposes the one stale output lock corrected below.

## Latest retained changes

- `a46cc202` **Replace stale release compiler owner** — remove non-exact `mpcoll.c -O3` and compile
  complete `lb_00B0.c` at exact `-O2 -march=native`. Adjacent A/B improves about +2.6% at 256 and
  +3.0% at 512, keeps both production digests, and makes the release binary itself pass all 153
  output locks (the committed rebuilt release profile was 63 PASS / 89 CLASSIFIED / 1 FAIL).
- `90216884` **Compact sealed source pools** — delete the uniform all-pool free floor and reserve
  only runtime-reachable source owners. Ordinary singles arena/savestate bytes fall from 921,656
  to 685,888 (-25.6%); the four-player supported maximum falls from 1,266,476 to 997,972 (-21.2%);
  maximum source-pool storage falls from 757,540 to 542,072 (-28.4%). Throughput is neutral.

At that checkpoint the retained runtime produced about 66,474 FPS at 256 and 89,950 FPS at 512 on the current
host, with production digests `bdc54107c51fa3d7` and `3fb5823d90657775`.

## Fixed fighter animation lifecycle

- `be1d9d12` **Fix fighter animation lifecycle** moves hosted fighter JObj AObjs/FObjs from generic HSD
  transition-time pool/relocation churn to fixed typed Match storage, with no fighter fallback.
  The new staggered all-replay contract measures +5.19% median paired improvement at 512 and +5.25%
  at 256; raw throughput medians improve +4.69% and +4.67%. All correctness/API/save-restore/Wasm
  gates are green, and ordinary savestate size falls 747,480 to 719,788 bytes.

## Fighter wall-pass broad phase

- `71586c18` **Cull empty fighter wall passes** adds one conservative line-AABB rejection before each common
  left/right wall pass, removing 94.6% of repeated wall-query entries and 86.8% of exact
  intersection calls without replacing narrow phase. Adjacent A/B raw medians improve +7.62% at
  512 and +9.71% at 256; both digests and all correctness/API/PPC/Wasm gates remain exact.

## Compact fighter pose and gameplay geometry

- `0d2b44f2` **Compact fighter pose and geometry publication** replaces fighter AObj/FObj animation ownership with fixed Match-owned compact
  tracks and topology, publishes root/world placement in scheduler order, and directly serves
  gameplay-point consumers without a side pose representation or fallback. Adjacent A/B raw
  medians improve +24.15% at 512 and +20.65% at 256; median paired improvements are +25.11% and
  +21.87%. Both digests and the complete correctness/API/PPC/Wasm/viewer gates remain exact.

## Shared compiled fighter animation samples

- `edf724d4` **Precompute exact fighter animation samples** compiles all translated Figa trees into one immutable exact ordinary-sample table
  owned by GameData. Match joints publish integer unit-rate samples without repeated packed-stream
  decoding, while the exact mutable decoder owns only legal fractional/non-unit-rate samples. There
  is no per-Match cache or state growth. Adjacent A/B raw medians improve +10.15% at 512 and +11.35%
  at 256; both digests and the complete correctness/API/PPC/Wasm/viewer gates remain exact.

## Direct scheduler, demand geometry, and exact collision compilation

- `98c78d85` **Inline hosted GObj scheduler dispatch** executes the decomp-shaped priority/process
  walk directly from the bound Match and deletes repeated resumable-scheduler context publication;
  +3.32% raw median at 512 and +3.12% at 256.
- `4e275d65` **Publish fighter hurt capsules on demand** moves ordinary hurt-capsule transforms to
  their existing first-contact owner while keeping gameplay-live dynamics capsules eager; +2.95%
  raw median at 512 and +3.40% at 256.
- `fe25fec5` **Compile fighter map collision at exact O1** admits the complete dominant `mpcoll.c`
  owner at its strongest measured exact compiler level; +4.29% raw median at 512 and +4.32% at 256.

## Supported hosted dynamics capacity

- `3305b234` **Compact hosted fighter dynamics capacity** sizes the native/Wasm fighter-dynamics
  pool to its supported construction and gameplay bound of 64 nodes, with hard exhaustion
  enforcement and the retail PPC layout preserved.
  Ordinary arena/savestate storage falls by 43,008 bytes per environment; 512 adjacent A/B medians
  are throughput-neutral (+0.17% raw, +0.05% paired), and every construction, correctness,
  allocation, API, PPC, Wasm, and viewer gate is green.

## Exact paired matrix trig

- `9d54eedd` **Share exact matrix trig evaluation** replaces six scalar trig calls in each hosted
  three-axis HSD matrix constructor
  with one exact paired evaluator. It shares source range reduction while preserving both MSL
  polynomial results and adds no state or approximation. Adjacent 512 medians improve +6.47%; the
  final 256 result improves +5.55% over the preceding baseline. Both production digests and the
  complete replay/API/save-restore/PPC/Wasm/viewer gates remain exact.

## Fused ordinary JObj world matrix

- **This commit** fuses exact Euler SRT construction and parent concat for every hosted non-root,
  non-quaternion JObj, directly publishing the singular `jobj->mtx` world matrix. It deletes the
  complete intermediate local matrix round trip, general alias handling, and separate call
  boundaries with no alternate state or fallback. Adjacent 512 raw medians improve +4.02%
  (+4.17% paired), and final 256 throughput improves +4.01%. Both digests and the complete
  replay/allocation/API/save-restore/PPC/Wasm/viewer gates remain exact.
