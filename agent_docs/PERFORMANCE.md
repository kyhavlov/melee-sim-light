# Retained performance evidence

Only production-contract, correctness-green results belong here. Historical experiments remain in
Git history and ignored triage artifacts, not in this active evidence file.

## Benchmark contract

- Host: AMD Ryzen 9 9950X3D; CPU 0 is the 96 MiB V-cache domain.
- Workload: all 153 supported validation replays packed once into native input tapes.
- Timed work: each unique replay is pre-rolled once to one of eight 200–900-frame offsets and
  copied to repeated batch slots through the production API. Timed free-running gameplay covers
  65,536 match-frames across a true resident batch plus a caller-owned 128-frame observation and
  terminal ring.
- Checkpoint: 512 environments on one CPU core; 256 is the cache-pressure secondary result.
- Acceptance requires unchanged workload digests and the complete replay/API/save-restore/Wasm
  gates with no new or widened classification.

```bash
make benchmark-prepare
make benchmark-9950x3d-vcache-256
make benchmark-9950x3d-vcache-512
```

## Demand-owned hurt-capsule publication — 2026-07-19

Hosted `Fighter_ProcessHit_8006D1EC` no longer transforms every hurt capsule unconditionally.
Existing `lbColl` contact primitives publish ordinary capsules exactly once on first demand through
their source `skip_update_pos` owner. Capsules attached to surviving gameplay-live dynamic chains
remain eager because the next frame's solver consumes their published JObj matrices; the hosted
owner discovers those capsules from the canonical dynamics graph rather than a character/action
list. This adds no mask, cache, state, allocation, or second geometry representation.

Four adjacent CPU-0 control/candidate binary pairs preserve digest `6f91f23e3553a090` at 512.
Controls are 65,122, 65,213, 64,942, and 63,839 FPS; candidates are 66,930, 66,976, 67,045, and
65,753 FPS. Raw medians improve 65,032 to 66,953 FPS (+2.95%); median paired improvement is +2.89%.
At 256, three adjacent pairs preserve digest `8ef126a41244d514`: controls 68,433/68,518/68,623 and
candidates 70,847/69,903/71,335 FPS. Raw medians improve 68,518 to 70,847 FPS (+3.40%); median
paired improvement is +3.53%.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed-allocation, PPC, Wasm parity,
viewer, and formatting gates pass. The first all-lazy proof exposed seven dynamic-chain misses;
retaining demand at that exact source owner restored every output lock without restoring the dead
ordinary work.

## Direct hosted GObj scheduler — 2026-07-19

The hosted production scheduler now executes the decomp-shaped priority/process walk directly from
the already-bound Match GObj context. This deletes the four out-of-line resumable-scheduler calls,
their repeated context recovery, and duplicated scheduler-state publication from every production
dispatch. The resumable functions remain a diagnostic interface only; there is no static schedule,
copied process list, callback specialization, alternate state, or changed mutation order.

Three adjacent CPU-0 control/candidate pairs preserve digest `6f91f23e3553a090` at 512. Results are
63,609/65,445, 63,464/65,652, and 63,544/65,668 FPS. Raw medians improve 63,544 to 65,652 FPS
(+3.32%); median paired improvement is +3.34%. The candidate's three 256-environment samples are
68,850, 68,589, and 68,891 FPS, a 68,850 median (+3.12% over the retained 66,767 baseline), with
unchanged digest `8ef126a41244d514`.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed-allocation, PPC, Wasm parity,
viewer, and formatting gates pass. The packet adds no state, allocation, API, or memory cost.

## Shared compiled fighter animation samples — 2026-07-19

GameData now enumerates every translated fighter Figa tree and compiles its exact ordinary integer
samples once during initialization. Match-owned joints bind by compact program/range identity and
publish unit-rate integer samples directly from immutable shared data. The existing exact mutable
decoder remains canonical only for legal fractional/non-unit-rate evaluation and source-ordered
resynchronization; there is no per-Match pose cache, dual pose, fallback dispatch, gameplay
allocation, or external/legacy extractor.

Three adjacent CPU-0 control/candidate pairs preserve digest `6f91f23e3553a090` at 512. Results are
57,292/63,409, 57,460/63,110, and 57,082/62,818 FPS. Raw medians improve 57,292 to 63,110 FPS
(+10.15%); median paired improvement is +10.05%. At 256, digest `8ef126a41244d514` is unchanged
across 60,571/66,869, 59,961/66,767, and 59,373/66,194 FPS. Raw medians improve 59,961 to 66,767
FPS (+11.35%).

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore, sealed allocation, PPC, Wasm parity, viewer,
and formatting gates pass. The shared table holds 10,721,046 values (40.90 MiB plus validity bits)
for 1,683 unique Figa trees. Program identity and decoder mode fit the existing compact joint;
native arena/savestate remain 676,440/738,056 bytes and the Wasm snapshot remains 547,712 bytes.

## Compact fighter pose and gameplay geometry — 2026-07-19

The retained candidate replaces native fighter AObj/FObj animation ownership with fixed Match-owned
track state and contiguous joint/subtree topology. Tree lifecycle and animation operations no longer
scan the global pose pool or walk parent ancestry; SRT invalidation propagates in the existing exact
parent-first scheduler. Fighter root placement skips bit-identical publication and republishes only
the translation column of clean ordinary descendants. All fighter gameplay-point consumers enter the
compact type route, with paired hurt-capsule endpoints sharing one bone setup. Canonical JObj SRT and
matrix storage remains singular; exact HSD matrix arithmetic handles dirty and procedural-special
joints without a side representation or fighter fallback.

Three adjacent CPU-0 control/candidate pairs preserve digest `6f91f23e3553a090` at 512. Results are
43,660/54,624, 43,152/54,202, and 44,884/53,226 FPS. Median paired improvement is +25.11%; raw medians
improve 43,660 to 54,202 FPS (+24.15%). At 256, digest `8ef126a41244d514` is unchanged across pairs
47,748/58,192, 48,282/57,478, and 48,234/59,070. Median paired improvement is +21.87%; raw medians
improve 48,234 to 58,192 FPS (+20.65%).

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore, sealed allocation, PPC, Wasm parity, viewer,
and formatting gates pass. The supported construction census reaches 976 compact nodes for four
Peach instances inside the fixed 1,024-node owner; ordinary stepped arena use remains sealed at
676,440 bytes with 825 initialization allocations, and the corresponding savestate is 738,056 bytes.

### Rejected scalar ECB publication follow-up

A post-commit `Fighter_procMap` drill-down found that `mpColl_LoadECB_JObj` owns 11.90% of the
instrumented contract and its six exact bone-origin matrix queries own 11.48%. Action callbacks,
stage-line scans, and traversal shape were not the hidden cost: direct compact-pose queries, full
tree publication, and a sparse six-joint ancestor closure all preserved exact output but measured
between neutral and slower. The final direct-query A/B was +0.39% by raw median and +0.51% paired,
inside host variance, so every candidate was removed. ECB publication remains in the scalar source
owner until pose/collision arithmetic can be transformed across environments.

## Fighter wall-pass broad phase — 2026-07-18

The retained candidate performs one conservative, data-driven wall-line AABB test before each
high-level left/right wall pass. An empty pass now skips the source callback's repeated ECB
segment and swept-quad queries; any plausible static line and every transformed/remapped joint
continues through the exact source narrow phase in its original order. The broad phase owns no
cache or persistent state and adds no allocation.

Attribution over the 65,536-frame workload falls from 3,655,507 to 196,752 wall queries (-94.6%)
and from 23,233,780 to 3,061,555 exact intersection calls (-86.8%). Three adjacent CPU-0 control/
candidate pairs preserve digest `6f91f23e3553a090` at 512: 42,408/45,638, 41,592/44,998, and
42,463/45,976 FPS. Median paired improvement is +8.19%; raw medians improve 42,408 to 45,638 FPS
(+7.62%).

At 256, three pairs preserve digest `8ef126a41244d514`: 43,008/48,273, 44,559/48,292, and
44,001/48,158 FPS. Median paired improvement is +9.45%; raw medians improve 44,001 to 48,273 FPS
(+9.71%). The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error
across 1,415,476 frames. Native API/copy/save-restore, source sync, sealed allocation, PPC, Wasm
parity, viewer, and formatting gates pass.

## Fixed fighter animation lifecycle — 2026-07-18

The retained runtime replaces fighter JObj AObj/FObj use of the generic HSD object pools with
fixed, typed Match storage initialized before gameplay. Every hosted full/partial Figa and fighter
`HSD_AnimJoint` JObj path uses this owner; non-fighter animation and RObj animation retain their
source owners. Motion state, animation state, track interpretation, scheduler order, and the
non-hosted source build are unchanged. There is no generic fighter fallback.

Three adjacent CPU-0 pairs preserve digest `6f91f23e3553a090` at 512. The control/candidate results
were 40,381/42,276, 40,562/42,666, and 36,356/40,999 FPS. The median paired improvement is +5.19%;
the separate throughput medians are 40,381 and 42,276 FPS (+4.69%) because host throughput moved
substantially during the final sequence. At 256 the median paired improvement is +5.25% and the
raw-median improvement is +4.67%, digest `8ef126a41244d514`.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native API/copy/save-restore, source sync, sealed allocation, Wasm parity, viewer,
and formatting gates pass. Ordinary arena/savestate bytes fall from 685,888/747,480 to
658,148/719,788; maximum arena use falls 997,972 to 986,616; and maximum relocation records fall
6,127 to 4,530.

## Retained pre-cutover baseline

The latest committed runtime before the canonical repository cutover retained:

| Environments | Complete FPS | Digest |
|---:|---:|---:|
| 256 | 63,315 | `bdc54107c51fa3d7` |
| 512 | 85,156 | `3fb5823d90657775` |

The full correctness gate was 63 exact passes, 90 unchanged exact classifications, zero
XPASS/fail/error, and 1,415,476 compared frames. The canonical-cutover result is recorded below.

## Canonical-cutover A/B

An alternating three-sample same-host comparison used exact committed HEAD as the control and the
uncommitted canonical layout as the candidate. Both ran the same extracted data, packed 153-replay
workload, release flags, CPU 0, and resident observation history.

| Environments | HEAD median FPS | Cutover median FPS | Delta | Digest |
|---:|---:|---:|---:|---:|
| 256 | 62,151 | 61,850 | -0.48% | `bdc54107c51fa3d7` |
| 512 | 83,445 | 83,004 | -0.53% | `3fb5823d90657775` |

The candidate is performance-neutral within ordinary run variance. The native correctness gate
remains 63 exact, 90 unchanged classified, zero XPASS/fail/error, and 1,415,476 compared frames.

Current maximum supported construction uses roughly 1.27 MiB of per-match arena at the reached
four-player high-water. Ordinary singles use about 0.92 MiB. Shared game-data archive/native-DAT
storage is paid once per process. The public observation history costs 127,488 bytes per environment
at 128 frames.

## Gameplay-pose admission ownership A/B

An interleaved three-sample same-host comparison used exact `4a3340b0` plus its extracted pose
artifact as the control and the immutable `ftparts.c` admission table as the candidate. Both used
the same raw archives, packed 153-replay workload, release flags, CPU 0, and resident observation
history.

| Environments | HEAD median FPS | Immutable-table median FPS | Delta | Digest |
|---:|---:|---:|---:|---:|
| 256 | 60,030 | 60,911 | +1.47% | `bdc54107c51fa3d7` |
| 512 | 80,045 | 80,780 | +0.92% | `3fb5823d90657775` |

This is retained as performance-neutral: the packet removes initialization/data ownership rather
than frame work, and the small positive delta is within ordinary layout/run variance. Shared
`MslCoreGameData` shrank from 380,216 to 371,728 bytes (-8,488 bytes); the admitted table is one
read-only executable constant rather than mutable data copied into every GameData owner.

## Measured frame-time owners

| Exclusive owner | Canonical share | Midgame share |
|---|---:|---:|
| Fighter map/stage collision callback | 18.15% | 18.10% |
| Live pose animation evaluation | 13.34% | 16.21% |
| Hit/damage resolution | 13.20% | 10.80% |
| Stage object animation/callbacks | 9.34% | 10.03% |
| Action-state animation callback | 7.56% | 8.76% |
| Controller/input/IASA | 7.46% | 6.42% |
| Fighter dynamics | 6.68% | 5.07% |
| Camera callbacks | 4.52% | 4.50% |
| Contact/hurt/hit/shield publication | 2.82% | 2.32% |
| Scheduler traversal/dispatch | 2.93% | 2.93% |
| Finish/publication | 2.31% | 2.31% |
| Observation | 1.95% | 1.95% |
| Combat collision detection | 1.61% | 1.27% |
| Ground IK | 1.19% | 1.20% |
| Items/articles | 0.83% | 1.34% |

## Retained architectural results

- True resident 256/512 batches and arbitrary-index save/restore.
- Compact construction-time allocation/relocation metadata and reached source-class storage.
- Direct-bound Match/GameData owners rather than repeated compatibility accessor calls.
- Audited strict-O3 allowlist for portable whole owners.
- Supported-stage invariant/presentation callback deletion: +5.5% at 256, +6.8% at 512.
- Gameplay-observed headless camera cut: +0.3% at 256, +1.5% at 512.

The 500k path remains complete compact pose/geometry ownership, aligned AoSoA hot state, fixed
homogeneous phase loops, and AVX2/AVX-512 kernels. Scalar compatibility bridges, partial caches,
and isolated leaf tuning are not acceptable substitutes for those deletion boundaries.

## Exact compiler and source-pool closure — 2026-07-18

The current source was re-audited at Og/O1/O2 rather than assuming the former O3-only sweep was
complete. Blanket Og/O1 changed both benchmark digests and O1 crashed a full-suite worker; blanket
O2 exposed an unresolved unsupported-Ness reference. Measured O1/O2 variants of `fighter.c`,
`ftdynamics.c`, `gobj.c`, camera, `ftaction.c`, and supported stage owners were exact only when
neutral/slower. Replay-trained PGO over the existing exact allowlist preserved digests but
alternated within noise at 512 and was neutral at 256, so no profile artifact or workflow remains.

`lb_00B0.c` remained 5–9% faster in isolation at O1/O2, but release validation revealed that its
apparent `ExpertWorthlessFinch` failure was byte-for-byte identical to the committed rebuilt
release binary. Bisecting the existing allowlist identified `mpcoll.c -O3` as that stale
release-only owner. The retained replacement restores `mpcoll.c` to the source profile and compiles
`lb_00B0.c` at O2. The release gate changes from 63 PASS / 89 CLASSIFIED / 1 FAIL to 63 / 90 / 0,
while adjacent A/B improves about +2.6% at 256 and +3.0% at 512 with unchanged digests.

A temporary full-suite pool census then measured construction and replay high water and was
removed. The uniform `HSD_ObjAllocPreallocateAll` floor reserved fighter construction pools, unused
list and Rvalue owners, and oversized small pools in every Match. Explicit runtime-owner reserves
delete that state while retaining conservative animation, matrix, process, item, and Sheik-chain
bounds. Ordinary singles falls 921,656 to 685,888 arena bytes (-25.6%); maximum supported
construction falls 1,266,476 to 997,972 (-21.2%); maximum pool storage falls 757,540 to 542,072
(-28.4%). Alternating 512 measurements are neutral (-0.27%, +0.33%), so this is retained as a
memory-capacity result.
