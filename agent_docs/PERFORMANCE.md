# Retained performance evidence

Only production-contract, correctness-green results belong here. Historical experiments remain in
Git history and ignored triage artifacts, not in this active evidence file.

## Canonical embedded stage-line topology — 2026-07-19

Native stage collision now owns each mutable `MapLine` topology record directly inside its
source-facing `CollLine`. Enabled/hidden runtime state occupies two source-unused `hi_flags` bits;
the separate per-Match topology allocation, copied array, pointer slot, and relocation graph are
deleted. Extracted topology remains immutable construction input, while PPC preserves the retail
pointer layout. All supported extracted stages use only `hi_flags` values 0, 1, 2, 4, 8, and 17.

The final 16-byte record is throughput-neutral at both resident sizes and preserves digests
`6f91f23e3553a090` / `8ef126a41244d514`; no speed claim is made. Ordinary arena use falls from
633,432 to 631,820 bytes and initialization allocations from 825 to 824. Ordinary savestate falls
from 695,048 to 693,972 bytes, and the reached four-player maximum falls from 961,900 to 960,000
bytes. This saves about 0.79 MiB at 512 environments and 25.2 MiB at 16,384.

Debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED / zero
XPASS/fail/error across 1,415,476 frames. Native source/API/copy/save-restore and sealed allocation,
maximum construction, PPC, Wasm parity, viewer/browser, pytest, source-sync, and formatting gates
pass.

### Rejected level-ordered ECB matrix kernel

The exact ECB origin publisher encoded ancestor depth once, built fixed per-depth worklists without
adding Match state, and evaluated ready ordinary Euler siblings through a shared AVX2/FMA local-SRT
kernel before canonical world-matrix publication. The production digest remained
`6f91f23e3553a090`. Exact scalar level scheduling already cost 44,644.0 cycles/frame against the
44,422-cycle retained control; vectorizing every width raised cost to 45,816.4, and restricting SIMD
to widths four through seven still cost 45,472.8. The retained per-matrix kernel already uses three
compact SSE/FMA row concatenations; gathering narrow scattered sibling groups and republishing them
costs more than their nine scalar local-SRT values. All topology metadata, worklists, and SIMD code
were removed. Cross-environment or contiguous state would be required before revisiting this math.

### Rejected exact stage-line AABB admission

All ordinary/remapped floor, ceiling, left-wall, and right-wall query families received one exact
source-coordinate AABB admission before narrow phase, without changing candidate iteration or
adding state. Axis-aligned source tolerance required a 0.125-unit envelope; ordinary floor/ceiling
endpoint extension used a conservative two-unit envelope. Digest `6f91f23e3553a090` remained exact,
but candidate costs were 44,657.9 cycles/frame without the early extension cull and 44,611.9 with it,
against the roughly 44,422 retained control. The optimized `mplib.c` owner already performs the same
bounding comparisons at narrow-phase entry; duplicating them at the callsite merely trades where
the checks run. The shared predicate and all eight gates were removed.

### Rejected headless transient result-stat production

The complete hosted producer closure for `pl_040D`'s `pl_x5EC_t` hit/result bonus record was
removed: sparse hit/trick producers plus the per-fighter `pl_800411C4` and grounded
`pl_80041280` six-record clears. Digest `6f91f23e3553a090` remained exact and all native symbols in
that result-only family left the release binary. The stable adjacent 512 pair was 44,264.3 control
versus 44,301.0 candidate cycles/frame; a second control was externally disturbed while its
candidate remained 44,365.2. This is throughput-neutral, and deleting the embedded record would save
only 816 bytes per Match while widening the Player layout delta, so the whole candidate was removed.

### Rejected compact pose animation work schedule

A complete 512 census found 2,534,669 of 6,488,168 retained-tree visits (39.1%) had no live
animation, RObj, or dependency publication; 98.4% of those empty visits were structurally
unattached. A fixed 128-byte per-Match topology bitset preserved the digest but raised cost from
44,422.8/44,324.7 to 44,929.6/45,013.5 cycles per frame. A state-free direct canonical admission
likewise cost 45,023.8. The remaining live nodes still require scattered JObj loads, while the
retained contiguous loop makes its empty shell cheaper than either extra admission form. All
schedule state, predicates, and census instrumentation were removed.

### Rejected native inline wire access

Native header-owned little-endian loads/stores removed every scalar accessor call from optimized
observation, compare, item, and viewer projection while preserving both digests. Adjacent 512 pairs
were 44,143.1/44,020.5 and 44,118.5/44,206.6 cycles per frame, a neutral median; 256 moved less than
one percent. The existing local calls are well predicted and the demanded scattered state reads and
980-byte output writes dominate this 2.49% owner. The inline implementation was removed; future
observation work needs a different public data-layout boundary rather than leaf call tuning.

## Fighter contact empty-producer cull — 2026-07-19

The native fighter-v-fighter contact owner now rejects an attacker before team, thrown-hitbox,
clank, shield, and hurtbox enumeration when all four canonical authored hit capsules are disabled.
Any live capsule enters the complete source body in its original order; the predicate adds no state,
cache, geometry approximation, action/character dispatch, or alternate combat representation.

The 512 census finds 109,740 of 143,822 owner entries (76.3%) have no live fighter hit capsule.
Three adjacent resident-512 control/candidate median costs are 44,873.7/44,505.4 cycles per frame
(-0.82%), with wall throughput rising 95,645 to 96,436 FPS. Two resident-256 median costs are
42,247.1/41,804.5 (-1.05%), with wall throughput rising 101,597 to 102,667 FPS. Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`.

Debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED / zero
XPASS/fail/error across 1,415,476 frames. Native source/API/copy/save-restore and sealed allocation,
PPC, Wasm parity, viewer/browser, Python, source-sync, and formatting gates pass. Persistent and
shared memory are unchanged.

### Rejected fused guard overlay publication

Target attribution found `ftCo_GuardOn_Anim` and `ftCo_Guard_Anim` own 4.09% of the complete
contract through their shared dynamic shield-pose pipeline. A complete hosted evaluator traversed
the extracted default, animation-38, and neutral-guard sources once, evaluated exact Figa tracks
into a local JObj, and published the same two source blends directly to canonical main-pose SRT.
Digest `6f91f23e3553a090` remained exact, but whole-frame cost rose from 44,511.4 to 45,691.1 cycles
per frame. Target profiling likewise measured the fused callbacks at 148.5M cycles versus 146.1M
for the retained source. Exact guard rotation requires the same two ordered quaternion blends; the
candidate merely traded fixed compact attachment/traversal for stack mapping and fresh dynamic
track decoding without deleting the dominant math. A dense-sample variant was slightly faster but
not exact. All source, sampling, and attribution code was removed.

### Rejected direct dense pose interpreter

A production-shaped exact fast evaluator admitted the measured 89.0% dense active-joint population
before the mutable decoder and directly advanced loop/first-play state, published the existing
immutable sample, and updated canonical AObj callback counts. Digest `6f91f23e3553a090` remained
exact, but resident-512 cost rose from the retained 44,511.4 to 45,210.3 cycles per frame. The
existing interpreter already branches into the same dense publication after a small shared frame
advance; duplicating that control path adds admission and instruction pressure without deleting
sample or SRT work. Together with the earlier neutral direct-tree experiment, scalar range/wrapper
reorganization is closed; further pose gains require a different execution/data boundary. All
instrumentation and source changes were removed.

### Rejected immutable dense ECB trig

A complete exact GameData stream precomputed six sin/cos floats for every full-rotation dense Figa
sample and selected it only when live JObj rotation remained bit-identical to the source row. A
512 census found 3,114,789 exact hits among 5,884,181 direct dirty nodes (52.9%) and 2,342,902
eligible shared samples. Despite unchanged digest `6f91f23e3553a090`, the 53.62 MiB cold stream plus
live checks and cached/dynamic lane scatters raised cost to 45,844.8 cycles per frame from the
44,491.0 retained median. The exact AVX-512 runtime evaluator is cheaper than cold lookup across a
varied resident batch, so all table, descriptor, selection, and census changes were removed.

## Fused hosted dynamics transforms — 2026-07-19

The hosted exact fighter dynamics solver now publishes its demanded world bases, child directions,
inverse-parent axis, and tail position through one fused transform evaluator. It preserves the
source paired-single/FMA rounding sequence and canonical DynamicsData/JObj rotation, position, and
angular-velocity state while deleting per-link translation/scale matrices, general 3x4
concatenations, redundant origins, and an unobserved tail rotation/scale result. PPC retains the
upstream matrix sequence. There is no alternate dynamics state, approximation, fallback, or
character/action dispatch.

Three adjacent resident-512 control/candidate median costs are 45,022.1/44,491.0 cycles per frame
(-1.18%), with wall throughput rising 95,330 to 96,468 FPS. Resident-256 medians are
42,537.4/42,115.5 (-0.99%), with wall throughput rising 100,898 to 101,909 FPS. Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`. In the corrected profile, inclusive dynamics cost falls
from 316.8M parent cycles to 277.6M (-12.4%).

Debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED / zero
XPASS/fail/error across 1,415,476 frames. Native source/API/copy/save-restore and sealed allocation,
PPC, Wasm parity, viewer/browser, pytest, source-sync, and formatting gates pass. Persistent and
shared memory are unchanged.

### Rejected supported human input specialization

The hosted fighter input owner was cut directly to the source-backed human local-versus path,
deleting CPU stick/button synthesis, repeated constant-false match-mode calls, and the scheduled
CPU-command process. Strict O1 admission of the resulting function remained exact and was the best
compiler level; O2/O3 regressed. Resident-512 control/candidate cycle medians were
46,108.7/45,753.8 (-0.77%), while the two 256 pairs were neutral then slower. The deletion does not
materially change whole-frame cost and would add hosted source divergence for no reliable gain, so
all source/build changes were removed before the correctness gate.

## Optimized PPC-exact square-root owner — 2026-07-19

The process-wide hosted `sqrtf` definition now lives outside the quaternion translation unit that
must remain O0 for exact interpolation. Its optimized native owner preserves the source-authored
PPC `frsqrte` seed, three double-precision Newton steps, final volatile float store, and non-positive
behavior while deleting O0 stack traffic from every gameplay caller. PPC retains the upstream-shaped
definition. There is no approximation, dispatch, mutable state, or new platform-visible behavior.

Three adjacent resident-512 control costs are 45,724.9/45,669.2/46,068.3 cycles per frame and
candidates are 44,997.0/45,222.2/45,387.6, reducing the median from 45,724.9 to 45,222.2 (-1.10%).
Resident-256 controls are 44,150.4/43,122.9/42,771.7 and candidates are
42,913.3/42,736.7/42,694.1, reducing the median from 43,122.9 to 42,736.7 (-0.90%). Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`; wall medians are 94,908 FPS at 512 and 100,428 FPS at 256.

Debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED / zero
XPASS/fail/error across 1,415,476 frames. Native source/API/copy/save-restore and sealed allocation,
PPC, Wasm parity, viewer/browser, pytest, source-sync, and formatting gates pass. Persistent and
shared memory are unchanged.

### Rejected split compact pose hot state

The retained 56-byte combined node was tested against three singular final layouts: a 32-byte hot
animation stream plus 16-byte path/topology metadata, the same split with a bit-packed hot track
range, and a 40-byte hot stream with an explicit range. A profiler census found 27.76M ownership
checks, 13.62M initial metadata resolutions, and 7.97M dense publication hits per 65,536 match-frames;
hot null-path/attachedness summaries cut metadata resolutions to 8.44M. Despite lower isolated pose
time, the best 32-byte whole-frame result only tied its adjacent control at 46,427.4/46,427.5 cycles
per frame. Packed and 40-byte variants cost 47,305.5 and 47,040.4. All measured variants preserved
digest `6f91f23e3553a090`, but none produced a production gain, so no representation or instrumentation
remains. The candidates are preserved in named stash
`rejected-split-compact-pose-hot-state-20260719`.

### Rejected native stage scalar access

Every native camera/blast scalar accessor was moved to an exact always-inlined `StageInfo` read,
deleting the external ABI and repeated bound-stage TLS lookup from more than 12.8 million calls in
the 131,072-frame diagnostic. Digest `6f91f23e3553a090` remained exact, but the candidate measured
46,311.3 cycles per frame against the adjacent 46,346.4 retained median, well inside noise. The
calls overlap demanded camera arithmetic and are not a material owner boundary. No copied stage
state or source change remains; the candidate is in named stash
`rejected-native-stage-scalar-access-20260719`.

### Rejected compact pose dependency walk

The compact pose owner classified ordinary parent-only JObj dependency behavior once at joint
registration and directly iterated the typed full-tree interval, removing the generic dependency
dispatcher and exported single-joint wrapper from the ordinary path. Both 512 samples preserved
digest `6f91f23e3553a090`, but candidate costs were 46,760.6/46,453.0 cycles per frame against
46,341.9/46,350.9 controls: every adjacent pair regressed and the median cost rose 0.56%. The
existing generic branch is well predicted and the extra per-node classification branch does not
delete demanded animation work. No source or per-Match state remains; the exact candidate is in
named stash `rejected-compact-pose-dependency-walk-20260719`.

### Rejected exact scheduled-fighter compiler boundary

Per-function O1--O3 admission tested the dominant O0 scheduled owners in `fighter.c` without
admitting the known non-exact translation unit. `Fighter_8006A360`,
`Fighter_Spaghetti_8006AD10`, `Fighter_8006CB94`, and `Fighter_ProcessHit_8006D1EC` each preserved
the production digest but remained neutral or slower against a 47,571.8 cycles/frame resident-512
control. `Fighter_procUpdate` changed the digest at both O1 and O3. The inclusive callback shares
are therefore demanded callee work rather than O0 shell overhead; no compiler attribute remains.

### Rejected tile-resident scalar scheduler

An exact diagnostic interleaved the existing source scheduler by priority within bounded groups of
2, 4, and 8 Matches. Resident-512 costs rose from a 47,571.8 cycles/frame control to 49,964.1,
50,028.5, and 50,484.8 respectively, with digest `6f91f23e3553a090` unchanged. Even the two-Match
tile's 5.0% state/context tax exceeds the previously measured 3.3% maximum from deleting resumable
wrapper dispatch. Cross-environment kernels therefore need an owner-specific final state boundary;
generic priority interleaving and its diagnostic code were removed.

### Rejected direct compact pose tree iteration

The compact full-tree owner directly invoked dependency, interpretation, and RObj work from its
typed node interval, deleting 6.5 million redundant singular-entry calls and `joint->aobj` ownership
checks per production workload. The digest stayed exact, but candidate cycles/frame were
47,511.1/47,590.8/47,884.5 against the 47,571.8 control median: neutral. GCC's optimized caller and
the demanded per-node work hide this boundary cost, so the direct duplicate was removed.

### Rejected process-global hosted context pointers

Replacing the native transient Match/GameData owner pointers with ordinary process globals preserved
the production digest and matched the single-threaded API contract, but resident-512 cost rose to
48,714.7 cycles/frame against the 47,571.8 control (+2.4%). The native local-exec TLS model is
already cheaper than interposable external globals in the separated source closure; all context
pointers were restored to TLS and no API contract changed.

### Rejected exact hot-closure LTO

The prior LTO link failure was narrowed to dead CObj/LObj/GX/rumble functions in four mixed
presentation objects. Excluding those complete objects produced an exact pose/JObj/platform/runtime
partition with both production digests unchanged. Balanced and single-partition LTO measured about
48,295--48,306 cycles/frame at resident 512 against the 47,571.8 control (+1.5% cost). Cross-TU
inlining inflated or rearranged this already optimized scalar closure without deleting demanded
math; all LTO flags and artifacts were removed, and no headless stubs were added.

### Rejected cache-line-native JObj hot layout

The hosted JObj was reordered into a 192-byte layout with its 48-byte matrix at offset 64 and its
actual arena allocation aligned to 64 bytes. The candidate was exact, but the alignment guarantee
raised lifecycle snapshot size from 633,432 to 644,440 bytes per environment. Adjacent binary pairs
improved resident-512 cycles/frame by only 0.25--0.57%; most of that small effect reproduced from
alignment alone. The roughly 11 KiB per-environment cost is not justified by the sub-percent gain,
so the original layout and 32-byte arena contract were restored.

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

## Dense ordinary fighter pose samples — 2026-07-19

Shared fighter animation data now describes each Figa node once and stores only its supported,
present SRT channels in an exact dense sample stream. Each live pose joint binds that immutable node
descriptor at animation attachment. The ordinary integral, unit-rate publication path writes the
canonical JObj SRT fields directly; it no longer scans source tracks twice, probes sparse validity
bits, redispatches component types, or stores absent samples. Fractional rates, paths, duplicate or
unsupported channels, and other dynamic cases continue through the exact mutable decoder. There is
no second mutable pose, compatibility mode, legacy extractor, or per-Match state.

Three adjacent resident-512 controls are 47,623.8/47,902.1/47,868.8 cycles/frame and candidates are
46,030.4/46,124.2/46,090.4, reducing the median cost from 47,868.8 to 46,090.4 (-3.71%). Adjacent
resident-256 controls are 44,892.6/45,184.1/45,181.3 and candidates are
43,510.7/43,266.4/43,658.5, reducing the median from 45,181.3 to 43,510.7 (-3.70%). Median wall
throughput is 93,120 FPS at 512 and 98,641 FPS at 256. Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`.

The representation contains 10,718,160 exact values and 131,653 immutable node descriptors across
1,683 programs. It adds 0.71 MiB to process-global shared GameData while leaving the 633,432-byte
per-Match arena and every savestate field unchanged. Debug and optimized-release validation remain
63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames. Native source/API/
copy/save-restore and sealed allocation, PPC, Wasm parity, viewer/browser, pytest, source-sync, and
formatting gates pass.

## Direct fighter animation node binding — 2026-07-19

Shared pose programs now own a 261,195-entry track-start-to-relative-node map and a 4,096-slot
open-addressed Figa-tree hash. Each animation attachment resolves its program and dense node with
bounded direct lookups, deleting the binary search across 1,683 programs and the linear scan across
the selected program's nodes. The map is immutable GameData; it adds no mutable cursor, source-tree
field, per-Match state, fallback scan, character list, or transition shortcut.

Three adjacent resident-512 controls are 47,184.4/47,185.9/47,623.1 cycles/frame and candidates are
46,168.3/46,458.8/45,916.6, reducing the median from 47,185.9 to 46,168.3 (-2.16%). Adjacent
resident-256 controls are 44,781.4/45,816.6/44,986.5 and candidates are
43,561.5/43,367.1/43,492.5, reducing the median from 44,986.5 to 43,492.5 (-3.32%). Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`; candidate wall medians are 92,963 FPS at 512 and 98,682
FPS at 256.

The lookup owner adds 544,046 bytes to process-global shared GameData. The 633,432-byte per-Match
arena and all savestate fields remain unchanged. Debug and optimized-release validation remain
63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames. Native source/
API/copy/save-restore and sealed allocation, PPC, Wasm parity, viewer/browser, pytest, source-sync,
and formatting gates pass.

## Compact fighter ECB matrix publication — 2026-07-19

Fighter JObj-backed ECB publication now binds the union of its six origin-joint ancestor paths once
at fighter initialization. The hot source owner walks that compact topology once, evaluates the
ordinary Euler subset through one exact AVX-512 MSL trig batch, publishes directly to the canonical
`HSD_JObj::mtx` matrices in parent order, and reads the same six origins into the source ECB. Dynamic
quaternion, path, IK, RObj, user-matrix, and independent nodes remain in their exact source routines;
there is no second matrix representation, lazy cache, generic fighter fallback, character list, or
replay exception. Non-AVX-512 native and Wasm builds retain the exact scalar loop.

Supported bindings contain 18, 21, 25, or 28 joints. A 10,000-query census found 198,930 dirty
matrices, of which 186,096 (93.5%) use the direct Euler evaluator. Bypassing the compact boundary
while retaining exact wide trig falls to an 83,758 FPS median at 512, versus 89,094 for the complete
shape in its adjacent run, proving that the structural cut owns material work rather than riding the
shared trig improvement.

Final adjacent CPU-0 controls are 80,085/81,453/82,231 FPS at 512 and candidates are
85,633/86,880/76,512; raw medians improve 81,453 to 85,633 FPS (+5.13%) despite one externally
interrupted candidate. At 256, controls are 86,212/87,161/86,597 and candidates are
94,669/91,912/87,947; medians improve 86,597 to 91,912 FPS (+6.14%). Digests remain
`6f91f23e3553a090` / `8ef126a41244d514`.

The pose-joint pool is tightened from 1,024 to 1,000 against the measured supported maximum of 976,
offsetting all bound-topology state: lifecycle snapshot size falls from 634,232 to 633,432 bytes per
environment (-800 bytes). Debug and optimized-release validation remain 63 PASS / 90 unchanged
CLASSIFIED / zero failures across 1,415,476 frames. Native source/API/copy/save-restore and sealed
allocation, PPC, Wasm parity, viewer/browser, pytest, source-sync, and formatting gates pass.

## Optimized native source closure — 2026-07-19

The native release profile now uses strict O1 as its default instead of inheriting the decomp's O0
matching profile. Complete fighter-callback and item source closures plus quaternion interpolation
retain O0 because isolated O1 admission changes canonical outputs; stronger exact owner profiles
remain explicit. This compiles the rest of the native scheduler closure as optimized production
source without fast-math, a parallel runtime, per-action dispatch, or persistent state.

Release validation exposed a pre-existing hole in the retained O1 `mpcoll.c` admission: GCC fused
the source's separate `sinf` and `cosf` calls in `mpColl_LoadECB_Fixed` into `sincosf`, moving one
Peach turnip ECB by two ULPs. Narrow non-inlinable hosted wrappers preserve the retail call and
rounding boundary while the surrounding collision owner remains optimized. `test-full` now runs
the complete replay inventory against the optimized release binary as well as the development
binary, preventing future compiler changes from passing through an O0-only validation gate.

Three adjacent CPU-0 clean-HEAD/candidate samples preserve digest `6f91f23e3553a090` at 512.
Controls are 80,118/79,807/80,213 FPS and candidates are 84,720/83,758/84,556 FPS; raw medians
improve 80,118 to 84,556 FPS (+5.54%). At 256, controls are 85,001/84,597/84,513 and candidates are
89,148/90,017/90,169 FPS, preserving digest `8ef126a41244d514`; medians improve 84,597 to 90,017 FPS
(+6.41%).

The complete debug and optimized-release replay gates are each 63 PASS / 90 unchanged CLASSIFIED /
zero XPASS/fail/error across 1,415,476 frames. Native source/API/copy/save-restore and sealed
allocation, PPC, Wasm parity, viewer/browser, pytest, source-sync, and formatting gates pass.
Persistent and shared memory are unchanged.

## Native release control-flow/layout deletion — 2026-07-19

The native release profile now explicitly omits frame pointers, CET branch landing pads, and unwind
tables. None is consumed by the simulator, public API, or save/restore contract. Debug/native
development, PPC, and Wasm profiles remain unchanged; gameplay source and floating-point code
generation are untouched. Release text falls from 1,826,759 to 1,581,491 bytes (-13.4%), and the
linked binary no longer advertises IBT/SHSTK or emits ENDBR64 in gameplay functions.

Three adjacent CPU-0 controls/candidates at 512 preserve digest `6f91f23e3553a090`. Controls are
77,347/76,948/76,606 FPS and candidates are 80,375/80,041/79,899 FPS; raw medians improve 76,948 to
80,041 FPS (+4.02%). Final 256 samples are 85,381/84,790/85,355 FPS, an 85,355 median (+4.41% over
81,753), preserving digest `8ef126a41244d514`.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed allocation, PPC, Wasm parity,
viewer/browser, pytest, source-sync, and formatting gates pass. Persistent and shared memory are
unchanged.

### Rejected demand-owned Figa decoder construction

Moving exact mutable Figa track construction from every transition to first fractional/non-unit
demand reduced the diagnostic materializations by about 90%, but adjacent resident-512 throughput
was neutral. The exact candidate adds a deferred hot-path state branch without deleting material
production work and is preserved in `rejected-demand-owned-figa-decoder-20260719`.

### Rejected demand-owned ordinary pose publication

Deferring exact ordinary Figa samples until the common JObj matrix boundary moved work rather than
deleting it. The initial 512 result was 85,153 FPS with a changed digest against the retained 84,556
FPS. Instrumented pose animation fell from 16.41% to 11.72%, but stage collision rose from 16.19%
to 20.34% and action-animation callbacks from 9.09% to 10.43%. Common ECB and callback consumers
therefore demand nearly the same samples later in the frame; the lazy state and hook were removed
before correctness cleanup.

### Rejected exact three-axis SIMD trig

An exact three-lane AVX2/FMA `msl_sincosf3`, including the common all-small-angle exit, preserved the
512 digest but measured 77,085 FPS against an adjacent 77,324 control median. Mixed matrices make
the vector path execute both polynomial families and lane selection; the predictable scalar
small/even/odd paths are cheaper. The candidate is preserved in
`rejected-exact-simd-sincosf3-20260719`.

### Rejected exact zero-Euler shortcuts

The shared trig owner skipped range reduction/polynomials for exact zero axes, and a follow-up also
tested direct diagonal concat for all-zero rotations. Both preserved production digests. The
diagonal continuation was neutral; trig-only measured 85,174 FPS at 512 against an adjacent 83,077
control (+2.52%) but only 90,368 at 256 against the retained 90,017 (+0.39%), with a wide 512 sample
spread. The hot per-lane branch was removed rather than retaining an inconclusive scalar leaf.

### Rejected shared change-owned pose publication

Immutable per-sample transition bits alone were invalid because source systems can mutate animated
JObj components between samples. An exact live-SRT guard restored the digest, but reached only
75,871 FPS against the adjacent 77,324 control median: metadata reads and live admission cost more
than the matrix work avoided. The candidate is preserved in
`rejected-shared-change-owned-pose-publication-20260719`; scalar channel invalidation is closed.

## Fused ordinary JObj world matrix — 2026-07-19

Hosted non-root, non-quaternion JObjs now publish their final world matrix through one exact owner.
The evaluator preserves the source Euler SRT and paired-single concat operation boundaries, but
consumes the local components directly and writes only `jobj->mtx`. This deletes the complete local
matrix store/reload, general alias handling and temporary copy, and separate SRT/concat calls.
Roots and quaternion JObjs retain their explicit source owners; there is no second matrix state,
fallback flag, approximation, or persistent memory.

Three adjacent CPU-0 controls/candidates at 512 preserve digest `6f91f23e3553a090`. Controls are
74,332/74,623/73,679 FPS and candidates are 77,319/77,732/76,889 FPS; raw medians improve 74,332 to
77,319 FPS (+4.02%) and median paired change is +4.17%. Final 256 samples are
81,309/81,753/81,824 FPS, an 81,753 median (+4.01% over 78,599), preserving digest
`8ef126a41244d514`.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed allocation, PPC, Wasm parity,
viewer/browser, pytest, and formatting gates pass. Arena/savestate storage remains
633,432/695,048 bytes with 825 initialization allocations.

## Exact paired matrix trig — 2026-07-19

Hosted `HSD_MtxSRT` and `HSD_MkRotationMtx` now request all three Euler sine/cosine pairs through one
exact MSL owner. Each component performs the source sign/range/quadrant reduction once, evaluates
both unchanged MSL polynomials once, and publishes the same six scalar results. This deletes the
second identical reduction and five of six external call boundaries per matrix; it adds no cache,
table, approximation, mutable state, or alternate pose representation. PPC retains the matching
scalar source sequence.

Three adjacent CPU-0 control/candidate samples at 512 preserve digest `6f91f23e3553a090`.
Controls are 69,882/70,242/69,633 FPS and candidates are 74,405/74,669/74,322 FPS; raw medians
improve 69,882 to 74,405 FPS (+6.47%). A separate final candidate set is
74,780/74,340/74,039 FPS, a 74,340 median. At 256, final samples are
78,599/78,487/78,818 FPS, a 78,599 median (+5.55% over the preceding 74,466 baseline), with digest
`8ef126a41244d514` unchanged.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed allocation, PPC, Wasm parity,
viewer/browser, pytest, and formatting gates pass. Persistent and shared memory are unchanged.

### Rejected isolated exact affine concat

An x86 exact fixed-shape affine `PSMTXConcat` reduced the emitted kernel substantially but improved
the complete 512 workload only +1.27% by adjacent raw median. It would also be displaced by direct
source-boundary fusion, so the candidate was preserved in named stash
`rejected-exact-affine-concat-20260719` and removed.

### Rejected whole HSD matrix compiler admission

Complete `sysdolphin/baselib/mtx.c` O1/O2/O3 release admission preserved both production digests,
but every level remained around one whole-frame point at 512. The dominant work is demanded exact
arithmetic and cross-owner matrix traffic rather than O0 scaffolding; the candidate is preserved in
named stash `rejected-exact-hsd-matrix-compiler-20260719`.

## Supported hosted dynamics-pool capacity — 2026-07-19

Native and Wasm Matches now own 64 fighter-dynamics nodes instead of the retail all-roster pool of
320. The bound covers the largest supported transient construction plus all prior live gameplay
chains; pool exhaustion is a hard hosted contract failure rather than a fallback or silently
truncated chain. PPC preserves the retail layout, while solver data, order, and behavior are
unchanged.

Ordinary arena and savestate sizes fall from 676,440/738,056 to 633,432/695,048 bytes, exactly
43,008 bytes per environment. The reached four-player maximum falls from 1,004,908 to 961,900
bytes. This saves 21.0 MiB at 512 environments, 168.0 MiB at 4,096, and 672.0 MiB at 16,384.

Three valid adjacent 512 control/candidate pairs preserve digest `6f91f23e3553a090` at
69,765/70,102, 69,982/69,944, and 70,153/70,185 FPS. Raw medians improve 69,982 to 70,102 FPS
(+0.17%) and median paired change is +0.05%, establishing neutral throughput rather than a speed
claim. The 256 digest remains `8ef126a41244d514`. The complete gate remains 63 PASS / 90 unchanged
CLASSIFIED / zero XPASS/fail/error across 1,415,476 frames. Maximum construction, native
source/API/copy/save-restore and sealed-allocation, PPC, Wasm parity, viewer, and formatting gates
pass with the exhaustion assertion active.

## Exact O1 fighter map collision owner — 2026-07-19

The native release profile now compiles the complete `mpcoll.c` translation unit at O1. This is the
strongest exact measured level for the dominant fighter map-collision source owner: O2 preserves the
production digest but reaches only 68,414 FPS at 512, while the previously removed O3 admission
changes a validation replay. There is no collision source edit, function clone, action fast path,
runtime dispatch, or alternate state.

Three adjacent CPU-0 control/candidate pairs preserve digest `6f91f23e3553a090` at 512. Controls are
67,572, 67,255, and 67,627 FPS; candidates are 70,466, 70,510, and 70,468 FPS. Raw medians improve
67,572 to 70,468 FPS (+4.29%); median paired improvement is +4.28%. At 256, controls are
71,467/71,383/71,231 and candidates are 74,395/74,466/74,674 FPS, preserving digest
`8ef126a41244d514`; raw and paired medians improve +4.32%.

The complete gate remains 63 PASS / 90 unchanged CLASSIFIED / zero XPASS/fail/error across
1,415,476 frames. Native source/API/copy/save-restore and sealed-allocation, PPC, Wasm parity,
viewer, and formatting gates pass. The packet changes no persistent or shared memory.

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

### Rejected fighter-dynamics branch specialization

A profile-only census of every admitted fighter call found only three-node/no-collider and
four-node/collider chains, with no skipped prefixes or stiffness branch and one dominant axis.
That structural simplicity is not the cost: exact orientation and constraint math account for
about 67% of solver cycles, setup and floor work another 23%, and collider handling only 2.4%.
A branch-pruned clone therefore cannot meet a two-point whole-frame threshold; no production code
or instrumentation was retained. Future dynamics work must transform the exact math execution or
state layout rather than duplicate the generic solver.

### Rejected compiled pose-publication operation spans

Immutable per-node/filter operation spans replaced both hot scans of original Figa track metadata
while preserving exact publication order and digest. Three adjacent 512 pairs improved only
66,974 to 67,764 FPS by raw median (+1.18%, +1.21% paired). The candidate was preserved in a named
stash and removed: another shared metadata array is not justified for a sub-threshold scalar scan
cut. Further pose work must reduce sample/matrix execution itself rather than its small dispatch
surface.

### Rejected change-owned fighter SRT invalidation

Bit-exact direct-component comparisons preserved the 512 digest but measured 66,419 FPS against the
66,953 retained median. Reached parent animation/demanded geometry already invalidates enough of the
closure that per-track comparisons add more work than they remove. The candidate was preserved in a
named stash and removed without further leaf tuning.

### Rejected exact release LTO partition

Restricting GCC LTO to the already exact O2/O3 release allowlist does not link cleanly. LTO merges
complete translation units before section GC and retains presentation-only functions in mixed
owners such as `lbspdisplay`, `lbvector`, and `mpLib`; those functions correctly reference renderer/
GX symbols absent from the headless runtime. Ordinary LTO and whole-program internalization failed
the same boundary. No stubs or source-owner splits were added solely for LTO, and the Makefile
candidate was preserved in a named stash.

### Rejected dominant action-callback compiler closure

Profile-only target attribution resolved 594 actual animation, input/IASA, physics, and collision
callback targets. Ten common-action source files contained nearly every dominant target, but much of
their measured collision time belongs to the shared `mpColl` callee. Complete-owner O2 admission was
exact only after removing `ftCo_Damage`; the eight useful exact files reached 67,298 FPS at 512
against the retained 66,953 median (+0.52%). O3 remained exact but measured 67,194 FPS. The bounded
source set is below the three-point threshold, so no compiler list or target instrumentation remains;
both candidates are preserved in named stashes.

### Rejected second fighter map-pass broad phase

Profile-build-only function instrumentation attributed every `mpcoll.c` helper after exact O1
admission. No remaining floor, ceiling, or wall narrow-phase entry owns even one whole-frame point;
the only dominant source is `mpColl_LoadECB_JObj` at 143,594 calls and 14.31% of the instrumented
contract. The next common query owner is 1.59%, and the retained wall broad phase itself is 1.13%
under instrumentation. A second line-pass cull cannot meet the three-point bound, so no production
collision behavior changed and the diagnostic is preserved in a named stash.

### Rejected additional headless camera closure

Profile-build-only function attribution found `Camera_8002958C` is the only dominant remaining
camera helper. Its subject-bound result directly feeds the standard transform consumed by fighter
visibility and offline DeadUp publication. The second transform-copy invocation is only 23,554 of
89,090 calls and must retain history for future DeadUp entry; every other camera helper is below the
packet ceiling. No complete dead subpass can clear two whole-frame points, so the diagnostic was
preserved and removed without leaf arithmetic tuning or a changed camera approximation.

### Rejected native batch ECB publication seam

A temporary exact source-query publisher split every native Match once between scheduler priorities
5 and 6, published its six current ECB points, and resumed map callbacks from batch-owned rows. The
complete 153-replay suite remained 63 PASS / 90 unchanged CLASSIFIED / zero failures, proving the
phase ordering, but 512 throughput fell from 70,468 to 65,454 FPS (-7.12%) and the production digest
changed. The clean prior attribution assigns only about 11.5--12% of frame time to the removable
queries, so even a zero-cost matrix replacement could not reliably clear the five-point retention
threshold after this seam tax. No direct matrix kernel or runtime bridge was pursued; the entire
diagnostic implementation is preserved in named stash `rejected-native-batch-ecb-seam-20260719`.
Future batch geometry work must amortize its phase boundary across multiple dominant consumers and
use a canonical hot layout rather than interleave one scattered per-Match owner.

### Rejected eager exact ECB matrix program

An exact direct-index diagnostic proved that local Euler-matrix lookup could improve the retained
scalar workload about 4.3%, but its narrow 13,041-binding sample covered only values reached by the
probe workload. The production-shaped exhaustive program expanded to 34,834 bindings and 1,430,638
samples; full matrices cost about 68.7 MiB shared and measured between 57k and 71k FPS. Exact linear
3x3 dedup still retained 668,807 unique matrices (about 30 MiB) and reached only 68,074 FPS. Compact
ECB topology without the matrix table measured about 67.6k. The final eager design is therefore
slower than retained execution and carries excessive shared state; both diagnostic paths are
preserved in named stashes, and no production source remains.

### Rejected dense ordinary pose publication

A node-shaped shared Figa table directly published complete ordinary SRT records and deleted the
track-major validity/type-dispatch path for 261,169 of 261,195 extracted tracks. Both production
digests and the complete 63 PASS / 90 unchanged CLASSIFIED suite remained exact. Adjacent 512
controls were 70,676/70,694/70,073 FPS and candidates were 72,136/71,979/72,092 FPS, only +2.00% by
raw median and +2.07% paired. The shared node descriptors also offset the removed validity storage.
The final candidate is preserved in named stash `rejected-dense-ordinary-pose-publication-20260719`;
no larger mask switch or duplicated timing fast path was retained. Further pose work must change its
cross-environment execution/state layout rather than add another scalar shared-data representation.

### Rejected hosted human-input owner deletion

The hosted runtime removed its priority-2 CPU proc and every constant-false CPU branch from the
priority-3 human-input path. That hot deletion preserved the digest but measured 70,630 FPS against
the retained 70,468 FPS. Removing the now-unreachable 0x57C CPU state reduced Fighter from 11,560 to
10,104 bytes and ordinary arena/savestate from 676,440/738,056 to 673,344/734,960 bytes. Preserving
the CPU initializer's two source RNG draws restored the production digest, but the compacted layout
measured only 69,243 FPS. The small capacity saving does not justify a hot regression; the complete
experiment is preserved in named stash `rejected-hosted-human-input-owner-deletion-20260719`.

### Rejected fixed hosted fighter scheduler

Hosted Fighter construction omitted all 15 generic proc nodes per Fighter, and the source priority
walk directly invoked those fixed phases from the canonical p-link-8 fighter GObj list between
lower- and higher-p-link generic processes. The candidate preserved both production digests and
migrated the three reached scheduler-priority consumers without a copied fighter list, phase mask,
callback table, compatibility state, or fallback.

Three adjacent 512 control/candidate pairs were 70,344/68,919, 69,223/67,975, and 68,526/68,027
FPS; every pair regressed. The existing proc list's stable per-priority callback targets are already
well predicted, while the direct cut must splice and rescan the live dynamic list around p-link 8.
The complete exact candidate is preserved in named stash
`rejected-fixed-hosted-fighter-scheduler-20260719`; no unrolled code-size or leaf-dispatch variant
was pursued.

### Rejected unsupported casual fighter-status deletion

The hosted priority-0/1 Fighter owners removed every per-frame maintenance block for unsupported
Mushrooms, Bunny Hood, metal, flower, cloak/refract, gradual healing-item recovery, and Hammer,
including flower input escape work. The exact candidate preserves digest `6f91f23e3553a090` but
measures 70,146 FPS at 512 against the retained 70,468 FPS. The complete non-pose/non-action shell
inside `Fighter_8006A360` is only 4.25% of the instrumented contract, and the deleted blocks are a
minor subset; widening this into scattered cold checks across action files cannot clear the packet
threshold. The source candidate is preserved in named stash
`rejected-unsupported-casual-fighter-status-deletion-20260719`.

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

## macOS portability identity cleanup — 2026-07-31

The PR #11 cleanup replaces ASLR-derived low-32 pointer identities with deterministic GameData,
native-DAT, Match, and core-image tokens. Conversion occurs when source graphs are loaded or
resolved; true match pointer fields and the production step loops remain unchanged.

Six alternating same-host runs compared exact `experiment/decomp-port` (`a5f699e7`) with the final
candidate on a Ryzen 9 9950X3D in the performance governor, using the same extracted data, packed
153-replay manifest, release flags, resident observation history, and 65,536 measured match frames.
Lower cycles/frame is better.

| Environments/profile | Current median cycles/frame | Candidate median cycles/frame | Delta | Digest |
|---:|---:|---:|---:|---:|
| 256 / frequency core 8 | 47,900.1 | 47,326.3 | -1.20% | `8ef126a41244d514` |
| 512 / V-cache core 0 | 43,673.2 | 43,922.8 | +0.57% | `6f91f23e3553a090` |

The 512 delta is within the ordinary run/layout spread and no throughput gain is claimed. A
temporary 256-sample lifecycle probe measured candidate/current medians of 0.4775/0.4780 ms save
and 0.6595/0.6580 ms restore with the same 633,156-byte artifact. Sharing the ELF/Mach-O image walk
with savestate relocation instead measured a repeatable +2.4% restore cost, so that refactor was
rejected and the existing savestate implementation remains intact.

After the final typed archive-sentinel and range-check cleanup, exact-candidate release samples
measured 47,259.0 cycles/frame at resident 256 and 43,710.2 at resident 512. Both remain inside the
retained alternating-run envelope and preserve digests `8ef126a41244d514` and
`6f91f23e3553a090`, respectively.

The final merge-review reduction was rechecked with three adjacent pre-cleanup-PR/candidate pairs
under the same transient host load. Resident-256 medians move 52,347.3 to 51,354.5 cycles/frame
(-1.90%, 83,575 candidate FPS); resident-512 medians move 46,100.2 to 45,855.3 (-0.53%, 93,598
candidate FPS). Both digests remain unchanged, so the reduction is retained as performance-neutral.
