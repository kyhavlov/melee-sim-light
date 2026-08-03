# System-rewrite performance journal — 2026-07-21 to 2026-07-23

Imported from branch `experiments/decomp-port/system-rewrites`. These are historical disconnected
or branch-local checkpoints, not changes retained in the current runtime. The detail is preserved
here because it records the ownership boundaries, proof results, memory costs, and whole-workload
limits of the prior compiled-pose representation. No source code or runtime artifacts were
imported.


## Direct compact action-transition ownership — 2026-07-23

Structurally ordinary compact-to-compact action changes now preserve compact ownership from
`Fighter_ChangeMotionState` through the destination `ftAnim_8006EBE8` bind. Admission is derived
from MotionState, DAT/Figa capability, decoded script features, inherited-channel equality, and
procedural-state stability. It is not keyed by action, fighter, replay, or motion lists. Once the
direct transition begins, source demand is an invariant failure rather than a fallback.

The retained closure removes 334 outgoing local-pose exports, source Figa binds, compact imports,
and materializations on the fixed 65,536-frame/512-resident proof workload. Production handoffs
fall from `2,649/2,466` to `2,315/2,132`. It compares 594,140/594,140 action-entry, timeline,
local/world, hit, hurt, ECB, camera, dynamics, grounded-IK, and handoff records exactly, passes 360
arbitrary-index copy/restore checks, performs zero runtime allocations, and preserves digest
`6f91f23e3553a090`.

Equivalent-boundary source/direct cycle pairs are `4,945,731/2,197,816`,
`5,162,580/2,251,050`, and `4,667,736/2,167,114`, a **2.25x** ratio of paired medians including
preflight, export/direct transition, and destination binding. The full 153-replay gate remains 63
PASS / 90 unchanged CLASSIFIED / zero failures over 1,415,476 frames. All 38 tests and source,
native, PPC, Wasm, and viewer smokes pass; Wasm state/viewer digests remain
`cef96ff32edfe898` / `1b43d24b47c228f2`.

The capability additions occupy existing program padding. Shared immutable and mutable storage are
unchanged: 3,040 bytes/fighter, 12,160 bytes/match, and 83,605,680 logical shared bytes once per
process. Retained C/header growth is 356 additions and 21 deletions.

Five short standard-binary guards against `7abb724d` were deliberately not treated as cumulative
evidence: resident-256 and resident-512 paired medians were -1.42% and -0.95%, respectively, with
large alternating positive and negative pairs. No whole-simulator gain or regression is claimed
from that noisy series. The authoritative cumulative legacy comparison is recorded separately
after this checkpoint.

## Direct compact fractional animation-rate ownership — 2026-07-22

The `ftAnim_8006F0FC` / `ftAnim_SetAnimRate` rate writer and `ftAnim_8006E9B4` Figa advancement
form one compiled-owned rate-to-sample lifecycle. Admitted fractional segments keep the existing
compact `frame` and `rate` canonical and retain one source-shaped recurrence cursor for each
distinct Figa timing schedule. They do not export compact pose, reconstruct JObj/AObj controllers,
recurse through `HSD_AObjSetRate`, or run source Figa. All 449 historical fractional-rate exits are
gone. The ordinary integer-frame sampled program and hot runtime layout are unchanged.

Exact source behavior requires at most 136 independent recurrence cursors per fighter. Each cursor
contains one f32 local time and one u16 segment index, occupying 816 bytes/fighter. The existing
ordinary overflow grows from 172 to 176 bytes for its fractional-active tag. Complete mutable pose
state is therefore 3,040 bytes/fighter and 12,160 bytes/match, an 820-byte/fighter increase over the
secondary-pose checkpoint. There is no AObj, JObj, pointer, sampled pose, or second animation
controller in this state.

Cold compilation emits 1,506 fractional programs, 236,481 tracks, and 73,011 cursor mappings.
Exact bytewise deduplication reduces immutable spans from 1,294,314 to 1,039,527, removing 254,787
duplicate spans. Initialization then shrinks all completed arrays to their final counts. Fractional
storage is **32,494,158 bytes logical and reserved**, down from 39,628,194 logical and 62,676,992
reserved before retention cleanup: 7,134,036 logical bytes and 30,182,834 reserved bytes removed.
Shrinking the paired ordinary/fractional program arrays also removes 1,645,512 bytes of ordinary
program-capacity slack. Complete compiled-pose shared storage is consequently 83,605,680 logical
bytes and approximately 83,622,812 allocator-reserved bytes once per process. Compaction uses
temporary cold initialization storage only; runtime performs no decoding or extra lookup.

The independent production proof compares 594,140/594,140 rate, animation, hit, hurt, ECB, camera,
dynamics, grounded-IK, and handoff records exactly. It passes 360 arbitrary-index copy/restore
checks, performs zero runtime allocations, and preserves digest `6f91f23e3553a090`. The complete
owner boundary covers 423 identical rate segments. Source/direct cycle pairs remain
`58,336,595/16,963,113`, `57,318,140/16,206,915`, and `57,554,425/16,544,078`, a **3.48x** ratio
of medians including rate mutation, main sampling, secondary/capture work, and displaced or
terminal handoff. Retention changes only initialization storage and immutable addresses, not this
runtime boundary.

The full 153-replay gate remains 63 PASS / 90 unchanged CLASSIFIED / zero failures over 1,415,476
frames. All 38 tests and source, native, PPC, Wasm, and viewer smokes pass; Wasm state/viewer
digests remain `cef96ff32edfe898` / `1b43d24b47c228f2`. Standard no-option guards against clean
`d90a991b` were noise-bound: resident-256 paired median -0.77% with one positive pair and one severe
system outlier, and resident-512 median +0.38%. No whole-simulator gain is claimed, and there is no
repeatable material regression.

Retained C/header growth is 765 additions and 44 deletions. The memory-retention pass adds no hot
state, runtime branch, lossy packing, decoder, fallback, or new representation.

## Direct compact secondary-pose ownership — 2026-07-22

The complete five-slot `ftAnim_800707B0` lifecycle now publishes partial-pose animation directly
into the canonical compact pose at fighter proc 4, after main animation and action scripts and
before capture, dynamics, grounded IK, and geometry consumers. The existing `Fighter::x8B0`
fields remain the sole timing, weight, animation-id, and default-selection state; `flags_b5`
remains the part mask. Admitted segments do not attach or run `x4_jobj2`, call
`lb_8000C490`/`lbCopyJObjSRT` on live JObjs, or materialize/re-import pose at this owner. All 93
historical secondary-caused ownership exits are deleted; 2,073 reached callbacks remain
compact-owned until a later unsupported owner.

Timed blending requires one relocatable 44-byte compact local per fighter: 12 bytes rotation,
4 bytes quaternion W, 12 bytes scale, 12 bytes translation, and four one-byte
part/slot/valid/quaternion fields. Total compiled-pose mutable state is therefore 2,220
bytes/fighter and 8,880 bytes/match. Cold compilation adds 95 secondary program descriptors and
734 node descriptors. The incremental immutable footprint is 31,820 logical bytes and 43,816
allocator-capacity/BSS bytes, making the complete retained totals 51,111,522 and 52,774,166 bytes
respectively.

The normal production proof compares 420,078/420,078 records exactly, including 109,140
secondary local/state records, with 360 arbitrary-index copy/restore checks, zero runtime
allocations, and digest `6f91f23e3553a090`. A focused source-backed timed lifecycle compares
356,357/356,357 records exactly, including 18,900 secondary records, with digest
`9c3a74dcb61b47a6`. It confirms the source lifetime rule that main animation clears quaternion
interpretation before each secondary blend while preserving the existing XYZ rotation values.
The full 153-replay gate remains 63 PASS / 90 unchanged CLASSIFIED / zero failures over 1,415,476
frames. All 38 tests and source, native, PPC, Wasm, and viewer smokes pass; Wasm state/viewer
digests remain `cef96ff32edfe898` / `1b43d24b47c228f2`.

The complete owner boundary contains 2,323 equivalent operations: 2,073 callbacks and 250
request/cancel operations, including the 93 displaced one-time exports. Raw source/direct cycle
pairs are `2,141,529/262,386`, `2,057,550/262,171`, and `2,352,444/266,127`, for a **7.959x**
ratio of medians. The final evaluator preserves the ordinary compact-SRT stack and carries the one
possible timed quaternion separately, avoiding a global per-node tax for this rare owner.

Standard no-option binaries use identical release flags, the 153-case aggregate manifest,
524,288 match-frames, eight warmup ticks, and CPU 0. Against clean baseline `57bdbb1b`, the four
resident-256 source/current cycle pairs are `22555305742/22648551242`,
`22663796720/22621664761`, `22965381305/23533251302`, and
`22712182986/22790314373`, a conservative paired median of **-0.378%**. Resident-512 pairs are
`24214424837/24001817916`, `24583452170/23787142695`,
`23994881801/24174039753`, and `24871040040/24933377484`, a paired median of **+0.318%**.
Every digest matches. These are retained as noise-bound regression guards, not as a claimed
whole-simulator throughput improvement.

Retained C/header growth is 871 additions and 14 deletions. The exact addition itemization is:
237 lines of cold compiler/immutable-data support (including the 40-line DAT/Figa sampler), 529
lines of production runtime, lifecycle seam, state, and declarations, and 105 lines of reusable
field-exact proof support. No census, forced-input, trace, timing, rejected-kernel, or alternate
runtime machinery remains.

## Direct compact dynamic-bone ownership — 2026-07-22

The fighter proc-`0x10` dynamic-bone owner (`ftCo_8009DD94` / `lb_8001044C`) now writes its
source-backed procedural result directly into the canonical compact pose. `DynamicsData` remains
the sole procedural history. Cold-compiled topology binds each procedural node to its dynamics
set/index, and runtime publication occurs before grounded IK and downstream hit, hurt, ECB, and
camera consumers. All 1,021 dynamics-caused exits at `ftCo_8009E0A8` are gone: admitted frames do
not materialize a JObj pose, run the JObj solver, re-import pose, maintain shadow dynamics state,
or use a fallback. Source JObj dynamics remains only for source-owned pose segments.

The retained cache is 2,048 hot bytes/fighter. Two overflow products/terms and previous
source-demand masks occupy an initialization-owned, relocatable 128-byte/fighter sidecar, for
2,176 bytes/fighter and 8,704 bytes/match total. `MslCoreMatch` grows by one eight-byte pointer;
the sidecar contributes 512 bytes/match outside the hot match stride. There is no duplicate
pose/cache representation. Immutable data is 51,079,702 logical bytes: 45,541,870 bytes of sampled
Figa data plus 5,537,832 bytes of program/topology/capability data, an increase of 24,192 bytes over
`6be7d024`. Allocator capacity is 52,730,350 bytes total, an increase of 24,576 bytes.

The final proof is field-exact for 297,696/297,696 real records, including 13,468 records both
before and after the dynamics owner. It passes 360 arbitrary-index copy/restore checks, performs
zero runtime allocations, and preserves digest `6f91f23e3553a090`. The full suite remains 63 PASS /
90 unchanged CLASSIFIED / zero failures or errors over 1,415,476 frames. All 38 tests and the
source, native, PPC, Wasm, and viewer smoke gates pass; Wasm state/viewer digests are
`cef96ff32edfe898` / `1b43d24b47c228f2`, with 508,140-byte snapshots.

The complete affected-boundary proof uses 12,891 dynamics calls and the same digest in both modes.
Fresh instrumentation-free source-equivalent/direct cycle pairs are
`20,642,107/2,864,445` (7.206x), `18,346,380/3,120,553` (5.879x), and
`17,886,925/2,874,679` (6.222x). The final implementation avoids redundant unchanged-prefix pose
sampling during incremental rebuilds while preserving the closure, operation order, and full-
rebuild path.

### Standard-binary A/B provenance

`A` is clean commit `6be7d0249061981f9fd8c07a3b03c5b6dd86718b`; `B` is this retained dynamics
checkpoint. Both are normal no-option strict-release binaries from the same Makefile profile,
pinned to CPU 0 and run against the 153-case `melee_core_aggregate` benchmark manifest (SHA-256
`1541068539f860f143045e4fd7626fab2a1e0fc814405aeca6874a70e3072403`). Resident and logical
match counts are equal, each run executes 524,288 match-frames after eight warmup ticks, and the
observation/terminal digests match. Reproduce with `make benchmark-prepare`, `make native-release`,
then the generated `replay-bench --matches N --resident-matches N --match-frames 524288
--warmup-ticks 8` under `taskset -c 0`, building `A` from the named commit and `B` from this
checkpoint.

At resident 256, clean/current cycle pairs are:

- `22,491,630,095/22,453,701,988` (+0.169%)
- `22,735,566,730/22,293,101,202` (+1.985%)
- `22,408,700,811/22,560,070,916` (-0.671%)
- `22,378,672,664/22,133,407,673` (+1.108%)
- `22,312,110,900/22,108,015,528` (+0.923%)

The conservative paired median is **+0.923%** with digest `ec0faefa8c71adde`.

At resident 512, clean/current cycle pairs are:

- `23,704,085,443/23,724,981,938` (-0.088%)
- `24,133,496,257/23,616,171,556` (+2.191%)
- `23,751,165,971/23,704,898,487` (+0.195%)
- `24,055,320,537/23,330,336,581` (+3.107%)
- `23,684,003,841/23,987,898,945` (-1.267%)

The conservative paired median is **+0.195%** with digest `dadef5c5e5aadd94`.

The separate direct resident-512 recovery series compares the frozen pre-recovery candidate to
the retained tree; it is not combined with the clean-baseline series above. Frozen/recovered cycle
pairs are `24,129,859,446/23,488,878,871` (+2.729%),
`23,925,391,995/23,467,109,089` (+1.953%),
`24,121,642,060/23,560,708,780` (+2.381%),
`23,662,996,062/23,898,092,241` (-0.984%), and
`23,718,051,628/23,681,432,656` (+0.155%). Its paired median is **+1.953%**.

## Compact ordinary pose with direct grounded IK — 2026-07-22

The full structurally admitted ordinary class uses compact sampled DAT/Figa programs and one
match-owned product cache. At the existing proc-7 scheduler cut, grounded slope and two-leg IK now
publish directly into that canonical compact pose. The source `ft_80089B08` JObj path remains only
for source-owned segments; an admitted segment performs no pose export, source IK execution, or
re-import at this owner. Later unsupported consumers receive one exact ownership export from the
same compact pose and cached proc-7 products.

The final owner proof compares 125,216/125,216 records exactly: 2,581 bind, 12,422 animation,
10,964 hit, 14,190 hurt, 11,191 ECB, and 73,868 grounded-owner local/world records. It performs 360
arbitrary-index copies, reports zero runtime allocations, and preserves digest
`6f91f23e3553a090`. The full 153-replay gate remains 63 PASS / 90 unchanged CLASSIFIED / zero
failures over 1,415,476 frames, and all 38 tests plus source/native/PPC/Wasm/viewer smoke gates
pass. Mutable pose state is 2,048 bytes/fighter and 8,192 bytes/match.

The complete equivalent grounded-IK boundary uses the same 2,444 calls and digest in both modes.
Export-plus-source IK costs 26,951,841 cycles (11,027.76/call), while direct compact publication
costs 7,680,789 cycles (3,142.71/call), a 3.509x speedup. All 749 historical proc-7 ownership exits
are deleted. Total materializing exits fall from 2,765 to 2,616 because many segments now continue
until a later unsupported owner; entry/exit cost is 3,180,065 / 25,272,691 cycles, or 434.15
combined cycles per 65,536-frame workload frame, 20.97% below the frozen compact tree.

The old 1,315-exit attribution label was overbroad, not an overlapping-exit count: it was exactly
566 dynamic-bone plus 749 grounded-IK transitions. The 120 secondary-pose transitions had been
counted in the old partial-animation/blend bucket, making 1,435 unique, mutually exclusive
ownership exits across the three call sites. A disposable final census, removed afterward, also
measured source-trigger incidence on the retained path. Across 10,869 scheduler frames that began
or became compiled-owned before the three cuts, the exclusive trigger masks were: none 6,656;
secondary only 68; ground only 2,809; secondary+ground 25; dynamics only 569; ground+dynamics 742;
and zero secondary+dynamics or three-way cases. Thus trigger incidence is 93 secondary, 3,576
ground, and 1,311 dynamics across 4,213 trigger-bearing frames; 767 frames contain overlap. These
are trigger occurrences, not ownership exits. In the retained path grounded IK causes zero exits;
the measured current exit incidences at these cuts are 93 secondary and 1,021 dynamics.

Immutable payload is 45,541,870 sampled-Figa bytes plus 5,513,640 bytes of compiled program,
topology, and capability metadata, or 51,055,510 bytes (48.690 MiB). Allocator capacity raises the
physical compiled-metadata reservation to 7,163,904 bytes, making 52,705,774 bytes (50.264 MiB)
including sampled Figa. The fixed animation-id admission keys add 202,752 bytes while preserving
deduplication at 1,506 physical programs for 9,106 logical costume programs.

### Standard-binary A/B provenance

`A` is clean committed baseline `3666724c5881bbad1bc169c25263a7b905a6f970`; `B` is this
unstaged tree. Both are normal no-option strict-release binaries from the same Makefile profile,
run on CPU 0 against the 153-case `melee_core_aggregate` benchmark manifest (SHA-256
`1541068539f860f143045e4fd7626fab2a1e0fc814405aeca6874a70e3072403`), with resident and logical
match counts equal, 524,288 match-frames, eight warmup ticks, and identical observation/terminal
digests. Reproduction uses `make benchmark-prepare`, `make native-release`, then the generated
`replay-bench` with `--matches N --resident-matches N --match-frames 524288 --warmup-ticks 8`,
pinned with `taskset -c 0`; build `A` from the named commit and `B` from the reviewed tree.

At resident 256, `A` cycles are `22145633562`, `22102711220`, `22135167663`, `22361914919`, and
`22380064660` (median `22145633562`); `B` cycles are `21635941963`, `21795922001`, and
`21742766347` (median `21742766347`). The cycle-median ratio is a +1.853% whole-simulator
throughput gain with digest `ec0faefa8c71adde` (`B` median is about 103,493 FPS).

At resident 512, `A` cycles are `23501300883`, `24185167895`, `23885100995`, `24311170666`, and
`24238609026` (median `24185167895`); `B` cycles are `22859776530`, `22935999233`, and
`22922337746` (median `22922337746`). The cycle-median ratio is a +5.509% whole-simulator
throughput gain with digest `dadef5c5e5aadd94` (`B` median is about 98,167 FPS). Against the
immediately preceding frozen compact tree, the corresponding gains are +3.796% at 256 and +6.335%
at 512.

## Unconditional production compiled-pose seam — 2026-07-21

Admitted ordinary programs now take the compiled timeline/pose/geometry path unconditionally in
normal native and Python builds. Admission is a cold-built `(fighter kind, motion)` capability
lookup; no production build flag, environment mode, candidate toggle, or source/candidate dispatch
remains. During an admitted segment the compiled record is the sole pose owner. Source Figa/JObj
owns unsupported segments and receives pose state only at a one-time demanded exit.

Production stores exactly one 112-byte record per fighter. Its ownership tag occupies former tail
padding, so the complete ordinary match block is 448 bytes and has no parallel tag/cache. A normal
`MslCoreMatch` is 62,472 bytes. The 5,644-byte/fighter timeline state, proof records, modes, counters,
source control, and boundary instrumentation exist only in `MSL_COMPILED_POSE_TEST` builds.

### Correctness and ownership

- The separate production-proof build compares 16,779/16,779 real bind, animation, hit, hurt, and
  ECB records field-exactly with zero mismatches. It performs 360 arbitrary-index copies, reports
  zero runtime allocations, and preserves digest `6f91f23e3553a090`.
- The no-option 153-replay gate is 63 PASS / 90 unchanged CLASSIFIED / zero XPASS, failure, or error
  across 1,415,476 frames. `make test` builds native and PPC before all 38 tests pass.
- Frozen transition chain 1 is exact across 70,768 fields with digest `c12ba69d4078dbc5`;
  chain 2 is exact across 23,776 fields with digest `81863fdbf5f07405`. Both retain exact
  arbitrary-index copy/save/restore and zero allocations.
- The resident-512 proof window contains 199 entries, 210 exits, and 35 materializing exits. It
  replaces 3,669 animation calls, 2,229 hit publications, 2,527 hurt transforms, 3,783 ECB
  publications, and 3,591 camera transforms. Retained profile accounting measures 413,789 handoff
  cycles, or 6.31 cycles/frame, in that window.

### Boundary and whole-workload timing

The final ordinary boundary is `10,764,190/2,641,576` source/compiled cycles, or 4.075x. The frozen
transition boundaries are `39,000,742/10,785,260` (3.616x) and
`16,856,731/7,596,380` (2.219x), or `55,857,473/18,381,640` (3.039x) combined.
All accounting includes candidate-only work at the real cuts.

The production comparison uses clean standard binaries rather than the instrumented test build:
legacy commit `a5f699e7` and the current no-option tree, built with the same strict release flags and
run in alternating legacy/current order on CPU 0. Every run uses the same 153-replay manifest,
524,288 match-frames, eight warmup ticks, and matching final digests.

Resident-256 legacy/current cycles per frame are `42777.5/41707.2` (+2.566%),
`42345.9/42112.1` (+0.555%), `42199.7/42300.1` (-0.237%), `42274.4/41401.2` (+2.109%), and
`42629.6/42193.2` (+1.034%), for a +1.034% paired median and digest `ec0faefa8c71adde`.
The first legacy run was descheduled for about 47 ms, but it is reported and retained in the
unfiltered median.

Resident-512 pairs are `45808.4/44554.9` (+2.813%), `45497.4/44897.7` (+1.336%),
`44086.0/44946.9` (-1.915%), `44981.6/45857.2` (-1.910%), and `45434.6/45384.1`
(+0.111%), for a +0.111% paired median and digest `dadef5c5e5aadd94`. The sub-percent signal remains
noisy, but both standard-binary medians are positive without compiled-pose options.

The promoted central implementation has 1,288 active nonblank source lines in a normal preprocessed
build versus 3,315 in the former opt-in build. Thus production C/header code is materially
net-negative even though the source file still contains the separately compiled proof/timeline
support. Twenty-four deduplicated ordinary programs use 1,982,048 bytes and four topology
descriptors use 528 bytes. `source-check`, native/Python/PPC tests, `wasm-smoke`, and
`git diff --check` pass; Wasm retains state digest `cef96ff32edfe898`, viewer digest
`1b43d24b47c228f2`, and
508,140-byte snapshots.

## Disconnected compiled pose/geometry checkpoint — 2026-07-21

This is a commit-ready disconnected experiment, not a production integration or production
baseline change. It retains the generic cold-compiled ordinary evaluator, the closed compact
timeline-to-geometry path for two frozen Captain Falcon transition chains, and only the proof and
timing seam needed to reproduce the accepted gates. Full-corpus census/trace/rehydration machinery,
dynamic admission, semantic-blocker reporting, rejected kernels, source-pose mirrors, redundant
proof modes, and temporary filters are deleted.

The cleanup started from `HEAD` `9a036cf2b154df17ef961517c0d75786ebef442f`, staged diff SHA-256
`65f52db3b3e2f73fc4eccb6d242681cc5dd222077cd60573b82c251843860424`, and unstaged diff
SHA-256 `1a911e37714aac246cff62bca1dc785a09f51b692bd44970229f7883bec4aa61`. Its initial unstaged
C/header delta was 2,425 added / 196 deleted, net +2,229. The minimized tree's final hashes and
diff counts are recorded in the final review report because hashing this evidence file into itself
would not be stable.

The initial combined diff SHA-256 was
`fbc5f9f2b6e14b4a38fb0b62675725af6b54fcfc6904e96011e887d938cdbbc6`. The evaluator was 3,662
lines (`411474ac64157dd54c591749f7cf36996f338ef8f163e0daa79a4d1999708dde`), its header 82 lines
(`651befeb89a3ad8d7a8fb85a7c0688b741800497a274301211b83d728bb0a74d`), `scalar.h` 222 lines
(`524a7f67da405dff58f57986f6ca7c7da804d131946aed1a36b153f9e6126507`), and the replay harness
636 lines (`cf73909be55e72381c3502dfbd19239f08b78f5b5c2f580b9573f284642efdb8`). The stash-list display
snapshot hashed to `4c263ff83311076bd5d714c3eff754928fef6b7658ca1c236ce965c053ae226e`; no stash entry content was
inspected and no stash ref was changed during this packet.

### Retained implementation

- **Runtime:** one match-owned ordinary geometry state and one compact timeline state per fighter;
  typed timeline advancement; exact blend/previous-pose lifetime; one cached world closure; and
  direct hit, hurt, and ECB publication at the existing scheduler cuts. No source pose or cache is
  mirrored.
- **Cold compiler:** DAT/Figa-derived typed tracks, consumer ancestor closures, hit products,
  actual-sized program storage, program deduplication, and separately deduplicated 132-byte
  immutable topology descriptors. Runtime track evaluation uses cold-compiled channel bytes rather
  than scanning unused channel types.
- **Proof:** independent source-oracle/candidate comparison for the ordinary set; source-shadow
  comparison at every frozen timeline cut; target-attributed complete-boundary timing using the
  already measured intervals; zero-allocation accounting; final workload digests; and
  arbitrary-index copy/save/restore.

The former 5,824-byte mutable fighter state consisted of a 168-byte geometry/source-mirror prefix,
a 5,648-byte timeline body, and 8 bytes of tail alignment. The prefix was 8 bytes frame/rate, 4
source-facing, 12 ECB root, 72 ECB origins, 12 pose/ECB epochs and hit mask, 48 source runtime SRT,
4 motion/program, 6 flags/padding, and 2 source motion. The old timeline body was 2,816 current and
blend-target SRT, 512 track times, 256 track elapsed values, 1,536 world matrices, 384 world scales,
12 world root, 96 transition/bone vectors, 20 timeline floats, 8 program/state bytes, and 4 scale
mask bytes.

The cleaned state is physically split so ordinary execution touches only its 112-byte geometry
record: 8 frame/rate, 12 ECB root, 72 ECB origins, 12 epochs/hit mask, 2 motion, 5 program/state
bytes, and 1 alignment byte. The 5,644-byte timeline record is 2,816 current/previous-or-target SRT,
512 track times, 256 track elapsed values, 1,536 world matrices, 384 world scales, 12 world root, 96
transition/bone vectors, 20 timeline floats, 8 program/topology/state bytes, and 4 scale-mask bytes.
Total mutable experiment state is therefore 5,756 bytes/fighter and 23,024 bytes/match, 68 bytes per
fighter below the former layout. Current and previous/blend-target poses have distinct required
lifetimes; world matrices are the derived dynamic closure; ECB origins are the canonical persistent
collision-consumer packet. None is a duplicate source or candidate pose/cache representation.

### Exactness and memory

The generalized ordinary proof compares 16,779/16,779 real records exactly: 199 bind, 4,385
animation, 4,430 hit, 3,193 demanded-hurt, and 4,572 ECB records, with zero mismatches. It performs
360 arbitrary-index copies, reports zero runtime allocations, and preserves digest
`6f91f23e3553a090`. The candidate has 24 deduplicated geometry programs occupying 1,913,104 bytes
plus four deduplicated topology descriptors occupying 528 bytes, versus 45 logical programs and
3,545,888 logical geometry bytes.

Frozen chain 1 compares 70,768 fields across 6 bind, 242 animation, 2 rate, 4 transition, and all
downstream hit/hurt/ECB cuts with zero mismatches. Frozen chain 2 compares 23,776 fields across 4
bind, 76 animation, 2 rate, 4 transition, and all downstream cuts with zero mismatches. Each
performs an arbitrary-index copy/save/restore with zero runtime allocations. Source and candidate
workload digests match at `1284c406cc4b5b25` for the replicated chain-1 workload and
`8840e19f87b90c25` for chain 2. Chain 1 immutable data is 150,448 geometry + 149,671 timeline + 132
topology = 300,251 bytes; chain 2 is 423,040 + 299,342 + 132 = 722,514 bytes.

### Complete-boundary timing

The ordinary contrast set excludes the original Falco-dair baseline and times the eight generalized
motions. Three adjacent source/candidate cycle pairs are `11453437/2948252` (3.885x),
`11299669/2732349` (4.136x), and `11549456/2828110` (4.084x), for a 4.084x paired median.

The transition timing repeats each frozen real chain across 64 independently copied matches to make
the very short windows reproducible while preserving each chain's call order. Chain 1 pairs are
`40372442/11750094` (3.436x), `40129449/12000397` (3.344x), and `39913933/12045289` (3.314x).
Chain 2 pairs are `17968539/8720271` (2.061x), `18537601/8604214` (2.154x), and
`17975806/9071280` (1.982x). Every pair is faster. Aggregate pairs are
`58340981/20470365` (2.850x), `58667050/20604611` (2.847x), and `57889739/21116569`
(2.741x), for a 2.847x paired median. Accounting includes real bind, animation, rate, transition,
hit, hurt, and ECB cuts plus candidate-only hit binding/unbinding and invalidation; measurement
bookkeeping runs after each measured interval.

Final `git diff --check`, `source-check`, `native-smoke`, and `wasm-smoke` gates pass. The Wasm smoke
digest is `cef96ff32edfe898`, viewer digest is `1b43d24b47c228f2`, and snapshot size is 508,140
bytes. The experiment remains disconnected and production behavior is unchanged when its build
flag is absent.
