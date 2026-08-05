# Retained performance changes

This is the concise ledger of performance and memory work still represented in the canonical
decomp-based runtime. Each entry identifies the commit that introduced the retained boundary.
Correctness/source-completion patches and rejected experiments do not belong here. Rejected work
is indexed in `README.md`; legacy measurements remain in `HISTORY.md`.

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

- `f8b3e325` **Fuse ordinary JObj world matrices** fuses exact Euler SRT construction and parent concat for every hosted non-root,
  non-quaternion JObj, directly publishing the singular `jobj->mtx` world matrix. It deletes the
  complete intermediate local matrix round trip, general alias handling, and separate call
  boundaries with no alternate state or fallback. Adjacent 512 raw medians improve +4.02%
  (+4.17% paired), and final 256 throughput improves +4.01%. Both digests and the complete
  replay/allocation/API/save-restore/PPC/Wasm/viewer gates remain exact.

## Native release control-flow/layout deletion

- `a8431342` **Trim native release control-flow overhead** removes CET landing pads, frame chains, and unwind tables from the native release
  profile while leaving gameplay source, floating-point generation, debug builds, PPC, and Wasm
  unchanged. Release text falls 13.4%; adjacent 512 medians improve +4.02% and final 256 throughput
  improves +4.41%. Both digests and the complete replay/allocation/API/save-restore/PPC/Wasm/viewer
  gates remain exact.

## Optimized native source closure

- `3d0ecbb0` **Optimize strict native source closure** makes strict O1 the native release default while preserving measured lower source
  profiles for float-sensitive fighter/item/quaternion closures and stronger profiles for exact
  admitted owners. It also preserves separate fixed-ECB trig calls and makes full optimized-release
  replay validation a permanent gate. Adjacent medians improve +5.54% at 512 and +6.41% at 256;
  both digests and the complete debug/release replay, API/save-restore/PPC/Wasm/viewer gates remain
  exact.

## Compact fighter ECB matrix publication

- `d05e385c` **Batch fighter ECB matrix publication** binds each fighter ECB's six-origin ancestor union at initialization, evaluates its
  ordinary matrices through exact AVX-512 paired trig in topology order, and publishes only the
  canonical JObj matrices consumed by source collision. Dynamic special nodes remain source-owned;
  the generic fighter path and repeated per-origin traversal are deleted. Adjacent raw medians
  improve +5.13% at 512 and +6.14% at 256, both production digests and the complete gate remain
  exact, and tighter censused pose capacity makes lifecycle snapshots 800 bytes smaller per
  environment.

## Dense pose publication and direct transition binding

- `90078dfb` **Publish dense fighter pose samples** replaces sparse program-wide validity/value
  storage and per-frame source-track scans with immutable per-node exact SRT streams. Adjacent
  cycle cost falls 3.71% at 512 and 3.70% at 256 with no per-Match state growth.
- `9885c446` **Bind fighter animation nodes directly** maps Figa trees and track starts directly to dense node descriptors, deleting the
  binary program search and linear node scan from every animation attachment. Adjacent cycle cost
  falls 2.16% at 512 and 3.32% at 256; 544,046 bytes are added only to process-global GameData.

## Optimized PPC-exact square root

- `94d764b0` **Optimize exact hosted square root** moves the singular hosted PPC-exact `sqrtf` implementation out of the O0
  quaternion translation unit and compiles it with the strict optimized profile. The source
  `frsqrte`/Newton/store sequence is unchanged; adjacent cycle cost falls 1.10% at 512 and 0.90% at
  256 with no state or memory change.

## Fused hosted dynamics transforms

- `cabbcf65` **Fuse exact fighter dynamics transforms** replaces general temporary-matrix construction in the exact dynamics solver with
  one fused hosted evaluator for the demanded bases, directions, inverse axis, and tail position.
  Dynamics profile cycles fall 12.4%; adjacent whole-frame cycle cost falls 1.18% at 512 and 0.99%
  at 256 with unchanged state and memory.

## Fighter contact empty-producer cull

- `13e26c2d` **Cull empty fighter contact producers** rejects each attacker inside the canonical
  fighter-v-fighter contact owner when all four authored hit capsules are disabled, before team,
  throw, clank, shield, and hurt enumeration. The census removes 76.3% of owner entries; adjacent
  cycle medians improve 0.82% at 512 and 1.05% at 256 with both digests unchanged and no state.

## Canonical embedded stage-line topology

- `This commit` **Embed canonical mutable stage-line topology** replaces each native
  `CollLine -> MapLine` pointer plus separate copied topology array with one 16-byte embedded owner,
  aliasing runtime enabled/hidden state into source-unused topology bits. Throughput is neutral;
  ordinary arena/savestate fall 1,612/1,076 bytes, initialization loses one allocation, and all
  correctness, API, snapshot, PPC, Wasm, and viewer gates remain green.

## Exact wide fighter-pose blending

- `This commit` **Batch exact fighter-pose blends and cull Dream Land background response** stores
  each immutable authored joint's exact source quaternion alongside the translated native DAT
  descriptor, evaluates live Euler inputs and general-slerp sine products through the existing
  bit-exact wide trig owner, and publishes otherwise unchanged SRT and dirty flags in source order.
  Dream Land object 1 still consumes and clears Whispy's gameplay-observed `xD0` handshake but no
  longer starts the associated presentation-only background animation on the headless native path.
- Three 262,144-frame alternating pairs against committed control `5fac340b` preserve both complete
  benchmark digests and improve median paired throughput by 6.70% at resident 256 and 6.70% at
  resident 512. Candidate medians are 38,882.2 / 37,318.4 cycles/frame and 110,383 / 115,009 FPS.
  Every pair exceeds the required 5% checkpoint threshold. Match state and gameplay allocation are
  unchanged; the sealed process-wide native DAT arena grows by 302,848 bytes.
- Source synchronization, native smoke, PPC smoke, the 3,501,461-frame supported-domain suite,
  public API/test, Wasm parity, and viewer smoke all pass. The full experiment and rejected-variant
  record is in `JOURNAL_2026-08-03.md`.

## Frame-local pose and collision work deletion

- `This commit` transposes the existing compiled Figa values to program-owned frame rows, replaces
  two exact AVX-512 quadrant gathers with register masks, binds each native fighter motion record
  directly to its compiled pose program before GameData is sealed, evaluates the repeated CPU
  input-owner predicate once, and skips the source collision driver's second left/right wall pass
  only when both first checks were negative and therefore changed no relevant state.
- Three 262,144-frame alternating pairs against `ee32fbc1` preserve both production digests and
  improve median paired throughput by 5.33% at resident 256 and 6.30% at resident 512. Candidate
  medians are 36,908.0 / 35,267.5 cycles/frame and 116,288 / 121,697 FPS. Match storage,
  savestate size, and gameplay allocation are unchanged; the frame-row descriptors add 26,472
  process-wide immutable bytes while the compiled sample count remains unchanged.
- Source synchronization, native smoke, PPC smoke, the 3,501,461-frame optimized-release suite,
  allocation/save-restore census, Wasm parity, and viewer smoke pass. The complete
  retained/rejected experiment record is in `JOURNAL_2026-08-03_CHECKPOINT_2.md`.

## Exact scalar, pose, geometry, and output work deletion

- `This commit` retains the completed exact aggregate since `02cfe013`: lossless compact pose
  samples; exact AVX-512 general matrix concat and wide-trig quadrant publication; direct fighter
  part membership; direct dynamics direction products; shared exact scalar tangent reduction;
  earlier rejection inside generic and horizontal stage-line intersection; direct canonical
  observation/item publication; and exact hosted square-root/arccosine boundaries.
- The final collision addition generalizes the existing conservative wall-range scan to ceiling
  ranges. Ordinary ceiling queries return early only when a static line AABB, expanded by the two
  units required for source endpoint extension, cannot overlap the swept ECB top. Remapped joints
  and wall-connected ceiling cases retain the source path.
- Native x86 `acosf` replaces only Gekko's software-emulated reciprocal-square-root estimate seed
  with `rsqrtss`; all three source-ordered binary32 Newton refinements and the unchanged `atanf`
  tail remain. The rejected completed-reciprocal shortcut changed quaternion-derived ECB bounds by
  one ULP and failed eight full-suite output locks. The final form is exact across all 3,501,461
  validation frames, shrinks `acosf` from 205 to 154 bytes, and removes its hot call to the
  345-byte general estimate leaf. Non-x86 behavior is unchanged.
- Three 262,144-frame alternating pairs against `02cfe013` preserve digests
  `bdff41cf74a54850` / `ee9d93c545aa3ef9`. Median paired throughput improves 9.80% at resident
  256 and 8.23% at resident 512. Candidate medians in the measured host window are 38,234.8 /
  40,117.3 cycles/frame and 112,253 / 106,985 FPS. Every candidate arm beats its paired parent;
  the campaign remains open because the observed medians are still below 150k FPS.
- Source synchronization, native API/copy/save-restore and sealed-allocation smoke, PPC smoke,
  complete optimized-release validation, Wasm parity, and viewer/browser smoke are green. Match
  storage, savestate size, and gameplay allocation counts remain unchanged.
