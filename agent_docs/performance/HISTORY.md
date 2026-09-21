# Detailed performance history

This is the detailed retained and negative-result ledger inherited from the current decomp-port
line. The current benchmark contract, concise retained summary, imported branch journals, and
architectural attempt records are indexed in [`README.md`](README.md). Forensic artifacts remain
under ignored `reports/triage/`.

## Final code recovery — 2026-09-21

The requested 120,000 FPS threshold is met at both resident batch sizes. Cleanup
is committed at `83356f34`; **Optimize exact pose and animation hot paths** includes
the subsequent code, tests and performance evidence. The final candidate retains
compact pose dirty/root walks, direct input and GObj field access, exact Gekko seed
and quaternion-weight
math, canonical field-order SRT samples, independent sample ranks with contiguous
loads, and masked affine translation publication. No compiler, benchmark, host
setting, input tape, gameplay semantic, Match layout or snapshot-format change
is retained. The sample array has 64 additional initialized bytes for its wide read.

Eight adjacent pairs per batch alternate the first arm across four ABBA groups.
Every pair improves; all 32 runs are included, including the three candidate-512
samples below 120,000 FPS. The threshold claim is the median, not a minimum.
All task builds, tests and profiling finished before this timing series.

| Batch | Control median FPS | Candidate median FPS | Median paired gain | Control / candidate cycles per frame |
| ---: | ---: | ---: | ---: | ---: |
| 256 | 115,097 | 124,141.5 | +8.07% | 37,289.95 / 34,573.10 |
| 512 | 111,239 | 120,606.5 | +8.62% | 38,583.70 / 35,586.40 |

| Batch | Arm | All FPS samples, in collection order |
| ---: | --- | --- |
| 256 | Control | 113476, 113395, 115451, 114314, 115196, 115007, 115251, 115187 |
| 256 | Candidate | 124627, 121221, 121295, 123928, 124355, 123574, 124684, 124881 |
| 512 | Control | 111659, 110426, 107865, 110462, 113888, 111694, 110819, 113566 |
| 512 | Candidate | 120779, 120434, 121188, 121932, 117711, 118281, 121942, 118578 |

The comparison uses the original 508 human recordings / 4,684,452 input frames,
262,144 timed match-frames per run, eight warmup ticks, 128-frame observation and
terminal history, CPU 0, and the unchanged GNU 13.3.0 native-release profile.
Both digests remain `64020ea131e1b3a7` / `8ae6f774c68378ea`, with 15 / 19 resets
in every run. All 508 tape hashes match the original inventory; ordered identity
is `3b34be0c4f050c2ced70de120ad7468af2efbefc99fdbf932b4f865c8ad87277`.

The exact final source passes source/native/PPC/API/sealed-allocation/copy/save
checks, 127 Python tests, native and release 512-recording gates (4,721,700
transitions each, zero diagnostics/failures/errors), Wasm parity, and viewer/browser
smoke. Focused source and production-object proofs below cover dirty traversal,
stationary root constraints, every unit-float quaternion input, SRT publication,
complete immutable sample tables, and affine matrix arithmetic. Gated text matches
the screened executable and the code used for the final sample-publication proof.

Match size remains 63,848 bytes; the ordinary arena remains 1,757,132 bytes with
1,349 allocations before/after gameplay, and its snapshot remains 1,821,108 bytes.
The construction peak is 2,119,308 arena bytes. GameData arena usage remains
114,515,944 bytes and native DAT usage remains 151,551,424 bytes. Compiled sample
values are a separate shared initialization allocation: 101,434,248 logical bytes
plus the 64-byte read tail. There is no new gameplay allocation or per-Match cache.

Final benchmark SHA-256:
`01ea4049da2add19201b29a3c4d8299dcec01304efbb4fa655412a8f1252d964`.
Final release runtime SHA-256:
`a0424114a4ba01d62f719fb34d83cb9246cdc6f9d703411bf2ce3601212f3554`.
The original committed control remains
`1861b873b4e77575c135ec7e14cbd082becc8768760b66aa356c7a40863d5f51`.
Frozen binaries, all raw logs, per-tape hashes, source patch, gate results and
machine-readable metrics are under `reports/triage/perf_120k_20260921/final-load/`.
The earlier below-target checkpoints and rejected candidates remain recorded
below; none was rerun unchanged to select a more favorable final result.

## Exact pose and math recovery — 2026-09-21

The next uncommitted code packet retains the preceding compact dirty owner and
adds these deletions under the same canonical state and workload:

- Root translation uses the existing pose preorder and subtree sizes instead of
  an ephemeral 1,024-byte affected map and per-node ancestor lookups. The same
  custom-matrix, independent-parent and path barriers prune descendants. Stationary
  constraint invalidation and every matrix operation remain unchanged.
- Consecutive sampled rotation/translation components use fixed-size bit copies
  from immutable GameData into canonical JObj SRT. No values, layout or filtering
  change. Native GObj accessors express their single field read directly, and pose
  ownership checks consume the canonical animation tag without an outlined joint
  accessor. Arguments still evaluate once and nullable callers retain their checks.
- Scalar acos constructs the rounded binary32 Gekko estimate from the existing
  32-entry tables. Its positive radicand is normal and lies in [2^-23, 1], so the
  general double classifier and float/double conversions are unnecessary. The
  three Newton steps, atan polynomial, exceptional path and output remain exact.
- The existing MSL vector owner evaluates quaternion acos in groups. The general
  blend predicate proves each input is finite and strictly between -1 and 1.
  Register table permutations produce the same Gekko seeds with no per-lane
  emulator calls. The existing angle scratch buffer holds cosines, then angles,
  and expands backward into the three sine inputs. No additional scratch array,
  persistent state, allocation or table copy is introduced.

The July 23 contiguous-clear rejection still applies: the recent fused compact
clear/dirty walk was also slower. The August 3 packed-copy and wide-acos findings
now have new controlled evidence. Fixed tuple copies improve every adjacent pair
in this workload; the new acos kernel deletes both the former per-lane seed calls
and its additional gather arrays. Its two four-pair screens improve seven of eight
pairs, with median adjacent gains of 0.42% at 256 and 0.38% at 512. All samples,
including the first losing 512 pair, remain in the evidence.

### Fully validated intermediate checkpoint

This checkpoint is short of the requested 120,000 FPS at resident 512. Further
code work continues; the measurements are retained rather than rerun for a more
favorable absolute result. Both arms use the original 508-recording,
4,684,452-input-frame tapes, 262,144 timed match-frames, eight warmup ticks and
128-frame observation/terminal history on CPU 0 of the 9950X3D. Compiler, host and
benchmark settings are unchanged. No build, tests or profiling ran during timing.

Eight adjacent pairs per size alternate which arm runs first. Every pair wins.

| Batch | Committed control median FPS | Candidate median FPS | Median paired gain | Control / candidate cycles per frame |
| ---: | ---: | ---: | ---: | ---: |
| 256 | 115,535 | 121,085.5 | +4.44% | 37,148.65 / 35,445.45 |
| 512 | 113,890.5 | 119,420 | +4.74% | 37,685.0 / 35,939.9 |

All wall-FPS samples, in collection order within each arm:

| Batch | Arm | FPS samples |
| ---: | --- | --- |
| 256 | Control | 116251, 116217, 115618, 115452, 115352, 115202, 116216, 115184 |
| 256 | Candidate | 121405, 121389, 121044, 120455, 120283, 121456, 121127, 120889 |
| 512 | Control | 112681, 113879, 113586, 114098, 114341, 112937, 114225, 113902 |
| 512 | Candidate | 118077, 119255, 119319, 119521, 119535, 119760, 119620, 119009 |

Candidate benchmark SHA-256:
`bf36226e41fb8ba73fe219aa63fa31c4a145159889e5283ccea198258ade43a5`.
Release runtime SHA-256:
`3a1ec032f6fe3a2c403ef5a716c5b2da32ea091dd0ab25f1e3e7338c5b18178e`.
Control remains the verified `83356f34` ELF
`1861b873b4e77575c135ec7e14cbd082becc8768760b66aa356c7a40863d5f51`.
Digests remain `64020ea131e1b3a7` / `8ae6f774c68378ea`, with 15 / 19 resets.
All 508 tape hashes still match the original inventory; ordered identity is
`3b34be0c4f050c2ced70de120ad7468af2efbefc99fdbf932b4f865c8ad87277`.

Native and release each pass 512 recordings / 4,721,700 transitions with zero
failures, diagnostics or errors. Source/native/PPC/API checks, 127 Python tests,
Wasm parity and viewer/browser smoke pass. The added root oracle compares 4,096
seeded trees, including stationary positions and constraints. Seed verification
covers all 254 normal exponents and every 15-bit interpolation cell, including
both discarded-byte endpoints: 16,646,144 comparisons against the original
emulator. A separate check links the actual release math objects and covers every
binary32 encoding strictly between -1 and 1, including signed zero and subnormals.
Together with in-place and tail checks, all 2,131,754,764 comparisons match the
scalar result. The gated executable has the identical text section to that proof's
screened executable.

State, allocation reserves and snapshot format are unchanged. The stepped arena
remains 1,757,132 bytes / 1,349 allocations, and the snapshot is 1,821,108 bytes.
Shared GameData remains 114,515,944 bytes. Logs, frozen binaries, patches, proofs,
tape hashes and all timing samples are in
`reports/triage/perf_120k_20260921/`; this intermediate checkpoint is in `final/`.

### Rejected follow-up experiments

All rejected code is removed. These screens use frozen immediate parents, the
same tapes and complete benchmark contract; they are not comparisons with older
403-recording README figures.

| Owner / proposed deletion | Controlled result and disposition |
| --- | --- |
| Pose and scheduler prefetch | Mixed or slower at both sizes; no prefetch remains. |
| Empty line-range guard | About +0.3% at 256, mixed/slower at 512; removed. |
| Signed animation word assembly | Fewer loads but all four pairs lose; restored both parsers. |
| Construction-time scale clamping | All four pairs lose roughly 0.8–1.1%; restored original table values and runtime clamps. |
| Fused compact quaternion clear / dirty walk | Exact source oracle and native checks, but neutral at 256 and slower/mixed at 512; removed the helper, map and caller change. |
| Scheduler current-object field deletion | Exact self/object deletion checks; smaller context but all pairs lose 0.6–0.8%. Original field, writers and snapshot layout restored. |
| Retained sample-index local | All pairs slower; original reload/conversion restored. |
| Outer null-animation guard | All pairs lose 1.2–2.0%; removed. |
| Conditional unchanged flag store | All four pairs lose; unconditional source store restored. |
| Last-child clear iteration | +0.4–0.9% at 256 but -0.4–1.2% at 512; original recursion restored. |
| Deferred-only scheduler cursor publication | Mutation checks include a destructor deleting the next object at the same priority. All four pairs lose 0.6–1.4%; original stores and test fixture restored. |
| Scheduler bit-test expression | Generated bit-test instruction, but mixed/neutral; restored. |
| Rotation tuple before mask switch | -0.2–0.4% at 256, +0.23% at 512; original dispatch restored. |
| Scheduler epoch local | No gameplay callback writes the epoch, but 256 loses 0.4–0.8% and 512 is nearly neutral; restored. |
| Conditional atan zero-offset removal | All 2^32 input patterns match, but the additional branch loses all four pairs; restored. |
| Atan range-specific return paths | All 2^32 input patterns match; the larger generated body loses three of four pairs. Original atan and polynomial restored. |
| Vector acos empty-middle range | All 2,131,754,764 unit-float checks match, but 256 loses both pairs; restored the single reduction path. |
| Plain 16-bit last sampled frame | Same descriptor size and source/native/release clock/flag oracle pass; three of four pairs lose. Restored packed frame, tests and original snapshot layout hash. |
| Direct physics vector clears | 256 -0.36% / +0.69%, 512 -1.17% / +0.02%; pointer/zero scaffold removal is inconclusive and removed. |
| Integer-unit double-seed exponent | 137,577,130 exact normal/subnormal/exception comparisons pass; two of four pairs lose. Original full-width exponent arithmetic restored. |
| Runtime packed-sample permutation | All 511 channel masks remain exact, but contiguous load plus per-lane popcount ranks/permutation loses all four pairs. Original masked expand restored. |
| Wide fused SRT concat | 3,145,728 matrices / 37,748,736 float lanes match old production objects, including aliases. All pairs lose: 256 candidate119110/120162 vs121364/120405;512 candidate116634/116737 vs118849/116947. Original three-row fused kernel restored. |
| Sparse rotation-only 128-bit publication | All channel/filter/edgefloat checks pass, but all four pairs lose:256 candidate121283/122183 vs122241/122260;512 candidate118037/116138 vs119751/119058. Removed branch and restored uniform vector path. |


The later packed-load/register-expand publisher also failed to establish a gain:
256 candidates 120630/121160 versus 121440/119582, and 512 candidates
112915/116754 versus 116467/115391. Its separate load mask and register expansion
are removed. The original memory expand remains the sole vector publisher.

A cold layout trial put each program's constant tuples beside its frame rows,
replacing the globally separated constant suffix. All 380,066 node records retain
every non-offset byte and all 25,358,562 sample words match through their declared
node/frame/channel addresses. Allocation sizes and all runtime instructions were
unchanged. Nevertheless, all four comparisons lose: 256 candidates 117120/118370
versus 118665/119280; 512 candidates 115207/112994 versus 115533/115939.
The original compactor and offsets are restored. Evidence is in
`sample-local-constants/` under the September 21 campaign. Revisit requires a
working-set change beyond bringing these existing constant tuples closer to
frame rows; this packing alone does not improve locality in the measured workload.

The subsequent full-gate seed-conversion checkpoint retained all 32 samples in
`final-convert/`: medians 120,485.5 / 118,464.5 FPS and adjacent gains +4.64% /
+4.55% against the same committed control. All 16 pairs win, but 512 still misses
the target. Its benchmark is
`53d6c1d830e0161563558b58801ed2697f27c3da057a5b7bf2427fa72e9a311b`;
all source/native/PPC, 127 Python, 512 native/release and Wasm/viewer gates pass.

A native output-transport trial serialized each unchanged 980-byte observation
into a stack record, streamed its complete 64-byte cache lines, copied boundary
bytes ordinarily, and fenced before return. The strategy follows the cache and
ordering requirements in [Intel's optimization manual, section 9.4.1](https://cdrdv2-public.intel.com/821612/248966-Optimization-Reference-Manual-V1-050.pdf).
It passes immediate complete-byte comparisons at every alignment, masked writes
and padded strides. Both digests match, but adjacent results are +0.23% / +0.95%
at 256 and -0.28% / -0.11% at 512. The extra record copy does not pay for cache
allocation avoidance. The helper, temporary, intrinsics and test extension are
removed; no streaming-store policy remains. Revisit requires evidence that output
traffic dominates a changed producer/consumer workload.

The direct GObj accessors initially had mixed marginal evidence. Ablating them
from the complete validated candidate loses all four adjacent pairs: roughly
1.6% at 256 and 0.8–1.1% at 512. They remain part of the candidate. No scheduler
field, cursor, epoch, representation or snapshot-version change remains.

The source input `SET_STICKS` macro now assigns the same two fields directly.
It removes 48 pointer-address/spill/load instructions across eight callsites and
reduces the protected function's stack frame from 184 to 56 bytes. CPU function
calls and x-before-y stores remain in source order. Two adjacent-pair screens
against `final-convert` improve seven of eight comparisons:

| Screen | Batch | Control FPS | Candidate FPS |
| --- | ---: | --- | --- |
| 1 | 256 | 120495, 119340 | 121323, 121539 |
| 1 | 512 | 117925, 119146 | 119426, 118592 |
| 2 | 256 | 120575, 119491 | 121009, 121267 |
| 2 | 512 | 117615, 118276 | 119772, 119401 |

Both digests match. This local assignment simplification keeps all human and CPU
input processing; the earlier whole-input specialization remains rejected.

Packed SRT publication now expands its existing mask into vector registers and
writes only the authored, unfiltered JObj fields. The initial channel-order form
wins seven of eight adjacent pairs against direct stick stores (all four 512
pairs win). The losing 256 pair contains 74 ms of descheduling; it remains in the
raw result. A refinement changes the singular immutable mask and packed values
to JObj field order, deleting runtime mask remapping and permutation. Its first
four pairs all improve. No source track format, Match state, table size, callback
or compiler setting changes.

The field-level oracle checks all 511 nonempty masks, both filtering modes,
publish/no-update and 32 edge/random float trials: 65,408 full-JObj comparisons
pass in scalar native and actual production-release builds. Read-only debugger
dumps from the frozen production executables additionally compare all 380,066
initialized nodes and 25,358,562 binary32 values: every non-mask metadata byte is
identical; masks have exactly the declared mapping, and values have only the
corresponding permutation. All allocation offsets, strides and counts match.

Moving scale clamps after compaction was also screened against the new vector
owner. Unlike the earlier pre-compaction trial, it preserved constant detection
and row placement while deleting vector abs/compare/select. Nevertheless, three
of four pairs lose: -0.55% / -1.50% at 256 and +0.22% / -1.95% at 512. It is
removed entirely. The retained publisher keeps the exact runtime scale clamp.

The field-order repeat improves three of four pairs, for seven of eight total.
A later mask-only admission trial was mixed at 512 (-2.06% / +0.08%) and removed.
The rotation-only tuple bypass also loses all four pairs; it duplicates admission
before the vector path and remains absent. Scalar value-count admission and the
single vector publication path are restored before the complete gate.

The complete SRT checkpoint passes all gates again: 127 Python tests, 512 native
and 512 release recordings / 4,721,700 transitions per build, PPC/API/sealed
allocation/copy/save, Wasm and browser. All 16 adjacent comparisons improve, but
its 32-sample medians are 122,658.5 FPS at 256 and 119,851.5 at 512, still short
of the requested target. Controls are 114,809.5 / 113,172; paired gains are +6.78%
/ +5.90%. All raw samples and provenance remain in `final-srt/`; benchmark
SHA-256 is `70cdb3117d8ac1d37607c763f77fa1e5dc2bf8bbe3b2ccc06bf056d449223dd5`.
The full text section matches the field-order executable used for the complete
table proof. Every original tape hash and all memory sizes remain unchanged.
Further code work continues without repeating this unchanged final series.


### Combined quaternion weights

The general quaternion blend now evaluates its angle and both source weights in
one vector-block loop. It reuses the existing acos and sincos arithmetic owners;
the shared sincos helper still produces both results. The caller reuses the cosine
scratch as first weights and keeps one second-weight array, deleting 3,920 bytes
of stack arrays, the triplet expansion pass and scalar division pass. Quaternion
sign selection, exceptional branches, fused output products and flag publication
remain source-shaped. No persistent state or allocation changes.

Two direct screens against `final-srt` improve six of eight adjacent comparisons
(three of four at 512); the losing samples remain included:

| Screen | Batch | Control FPS | Candidate FPS |
| --- | ---: | --- | --- |
| 1 | 256 | 120829, 120707 | 120909, 120491 |
| 1 | 512 | 116607, 116150 | 116854, 117910 |
| 2 | 256 | 122154, 120974 | 122986, 121916 |
| 2 | 512 | 118466, 117420 | 116605, 117698 |

The production-object oracle compares every binary32 cosine strictly inside
(-1, 1), including signed zero and subnormals, at weight 0.375 against the prior
vector pipeline. Random weights and all tails add coverage: 4,261,674,944 weight
comparisons match. Native and release smoke also compare separate/in-place
outputs, padding, six fixed weights and complete live-JObj blends against the
scalar source function.

Returning only sine from the shared helper with an optional cosine pointer
passes the same proof but loses all four pairs (-1.83% / -0.89% at 256,
-1.04% / -0.27% at 512). It is removed. A final quaternion temporary allowed the
existing compiler to emit one packed multiply/FMA/store instead of four scalar
sequences, but three of four whole-runtime pairs lose amid wider host variation.
That production edit is also removed; its public-blend source oracle remains to
validate the combined weight owner. The full gate passes 127 Python tests, all 512 native and release recordings
(4,721,700 transitions each), source/PPC/API/sealed/copy/save and Wasm/viewer.
An additional 10,747,922 comparisons verify both shared sine/cosine outputs,
including quadrant-adjacent encodings. Every tape, allocation size and state
layout remains unchanged. All 16 final pairs improve, but the medians are
120,082 / 114,820.5 FPS against controls 112,757.5 / 109,707; paired gains are
+7.09% / +4.63%. The target is still missed. All 32 samples and gate results
remain in `final-slerp/`; benchmark SHA-256 is
`7996ed6343a96cb4732f1d40d1b0cb4e576cc6e8c585f46b29678ca16158b08c`.
Its full text section matches the executable used for the exhaustive weight proof.
Further code work continues without repeating this unchanged final series.


### Sample admission and affine translation

The compiled node's existing `sample_count` now means the number of valid stored
sample frames, including zero for nodes with no compiled values. Construction
sets that count once; publication uses its existing bounds check and removes the
separate `value_count` admission. No descriptor size, clock, table allocation or
decoder behavior changes. Read-only production-executable dumps verify that all
380,066 records are identical except the count becoming zero on 92,230 empty
nodes, and all 25,358,562 sample words remain identical. The source oracle also
checks empty, out-of-range and no-update cases. Its isolated first screen was
mixed, so no independent throughput gain is attributed to this admission change.

The fused native SRT matrix kernel keeps the fourth lane in its vector register.
One masked FMA replaces extracting translation, performing its scalar FMA and
reinserting it. The other three lanes pass through unchanged; local SRT math,
parent-scale compensation and operation order are unchanged. Existing ISA build
settings select the instruction. The ordinary non-AVX512VL implementation keeps
its existing scalar translation sequence.

Actual before/after production objects match across 3,145,728 matrices and
37,748,736 binary32 lanes, including signed zero, subnormals, infinities, NaNs,
parent-scale compensation and aliases. A durable native/release smoke fixture
additionally compares 4,096 Euler inputs through both fused entry points against
independent `HSD_MtxSRT` followed by the scalar source concat formula. That fixture
uses the production contract's distinct parent/output matrices; an initial extra
alias case exposed the pre-existing non-FMA scalar implementation's lack of alias
support. The initial failure log is retained, and alias coverage remains in the
actual release-object proof.

Two direct screens against `sample-count` improve five of eight comparisons,
including three of four at 512. No independent 256 gain is claimed:

| Screen | Batch | Control FPS | Candidate FPS |
| --- | ---: | --- | --- |
| 1 | 256 | 122704, 122287 | 123105, 122497 |
| 1 | 512 | 120206, 118648 | 120633, 120269 |
| 2 | 256 | 122617, 122913 | 122209, 122857 |
| 2 | 512 | 119642, 116901 | 120336, 116023 |

The combined admission/matrix candidate against `final-slerp` gives 256 candidates
122453/122796 versus 123954/122614, and 512 candidates 119297/118644 versus
118048/115963. All samples remain included. A fresh completed-owner ablation also
keeps the combined quaternion weights: the earlier `final-srt` pipeline loses
three of four comparisons, including both 512 pairs (117571/107495 versus
119422/118610). The 107495 sample is retained; it is not used to claim an isolated
marginal gain.

The `final-matrix` candidate again passes all source/native/PPC/API checks,
127 Python tests, both full 512-recording gates (4,721,700 transitions each,
zero diagnostics/failures/errors), and Wasm/viewer/browser smoke. All memory
sizes, the original 508 tape hashes and committed control ELF remain unchanged.
Its gated text matches the frozen executable used for the matrix proof and
screens. Benchmark SHA-256 is
`bb0dfc11403a8946e621eebd48fae2c81747dfc82865b34b77982d2a0806caab`;
release runtime SHA-256 is
`1f2c700a4d30ff22ac2be4372d709199aa23b8296fde395b1c14e37f968811a3`.
The predeclared final series uses eight adjacent pairs per batch against the
original `83356f34` control, alternating the first arm across four ABBA groups.
It finishes below target again: medians 122,579.5 /
116,317.5 FPS against 113,885.0 / 110,297.0; median adjacent gains
are +7.21% / +5.90%. Thirteen of sixteen pairs improve. The three losing
pairs and all other samples remain in `final-matrix/throughput/samples.json`;
no sample is excluded and the unchanged candidate is not rerun as a new final.
Further code optimization continues.


The subsequent [compact collision vertex packet](attempts/2026-09-21-compact-collision-vertices.md)
removed immutable local-coordinate duplication and cut each mutable record from
24 to 16 bytes. Native/release source oracles and API/save checks passed, but
three of four comparisons lost; the 128-byte ordinary Match saving did not improve
throughput. Its entire representation, snapshot hash change and fixture are
removed. The record preserves the owner audit, all measurements and revisit criteria.


### Independent sample ranks and contiguous loads

The sampled SRT publisher expands constant indices 0..15 into authored field
positions, then permutes a contiguous sample read using those ranks. Rank
computation no longer depends on sampled memory. The generated code uses
`vpexpandd` on the shared constant identity vector and `vpermps` on the sample
operand. It removes the original memory-form sample expansion without the earlier
rejected route's per-lane popcounts or masked-load count/shift work. The exact
scale clamp, filtered field stores, dirty flags and scalar backend stay unchanged.

The immutable allocation has sixteen initialized tail floats so every actual
64-byte sample read, including the final row, stays within its allocation. Logical
counts, offsets, strides and all sample words remain unchanged. This is required
load storage; no alignment, arena coloring, compiler or benchmark setting changes.
The separate shared sample allocation grows by 64 bytes; the GameData arena,
Match state and gameplay allocation counts are unchanged.
The field-level fixture supplies the same readable tail.

Actual release smoke passes all 511 channel masks, filtering/no-update modes and
special/random float inputs. Read-only production-executable dumps additionally
verify all 380,066 node records and 25,358,562 logical float words byte for byte
against `sample-count`; all 64 appended bytes are zero.

Two fixed-contract screens against `final-matrix` improve seven of eight pairs,
including all four at batch 512. The losing 256 pair remains included:

| Screen | Batch | Control FPS | Candidate FPS |
| --- | ---: | --- | --- |
| 1 | 256 | 124730, 123345 | 125222, 124616 |
| 1 | 512 | 120475, 120529 | 122780, 121321 |
| 2 | 256 | 125054, 123401 | 124490, 123978 |
| 2 | 512 | 118546, 121236 | 123199, 122196 |

The complete gate and final comparison pass; both medians exceed 120,000 FPS
as recorded above. Raw source, objects, table proof and direct paired logs are
under `sample-expand-index/`; accepted final evidence is under `final-load/`.

## Code performance recovery — 2026-09-21

The uncommitted candidate after `83356f34` moves registered fighter dirty
propagation into the existing canonical pose preorder/tree-count owner.
`HSD_JObjSetMtxDirtySub` retains its source traversal for generic roots. The
compact path preserves unconditional root marking, instance boundaries,
independent-parent boundaries and the exact custom-matrix-aware dirty predicate.
Already-dirty children prune their entire subtree even when descendants are clean.

The existing ftParts cold-domain proof remains the boundary: omitted costume
parts are absent/detached, and inserted presentation-only accessories have no
gameplay matrix consumer. They already stay outside canonical pose animation and
root publication, and their unused dirty bits are not published by the live pose
walk. Explicit cold-root calls retain the generic source owner. No topology
cache, extra state, allocation, layout change or snapshot-format change remains.

The same 508-recording / 4,684,452-input-frame workload runs on CPU 0 of the
9950X3D using unchanged GCC 13.3 strict release flags, resident/logical 256 or 512,
262,144 timed match-frames, eight warmup ticks and 128-frame output history.
All tapes match the original content inventory. Eight adjacent pairs per size
alternate first arm; all 32 samples remain in the result and every pair improves.

| Batch | Control median FPS | Candidate median FPS | Median paired gain | Median control / candidate cycles/frame |
| ---: | ---: | ---: | ---: | ---: |
| 256 | 115,082 | 116,461.0 | +1.11% | 37,294.6 / 36,853.2 |
| 512 | 113,623 | 115,036.5 | +1.06% | 37,773.6 / 37,309.5 |

All wall-FPS samples, in collection order within each arm:

| Batch | Arm | FPS samples |
| ---: | --- | --- |
| 256 | Control | 115301, 115435, 114259, 115062, 114898, 115102, 114982, 115255 |
| 256 | Candidate | 116487, 116052, 116727, 116435, 117160, 116161, 116893, 116392 |
| 512 | Control | 113637, 114193, 113609, 113976, 113591, 113736, 113524, 113332 |
| 512 | Candidate | 114819, 115266, 114765, 115214, 115302, 114859, 115385, 114712 |

Digests remain `64020ea131e1b3a7` / `8ae6f774c68378ea`, with 15 / 19 resets.
README medians round to 116,461 / 115,037 FPS. These improve the initial refresh
below; its previous 403-recording figures are not a same-workload baseline.
Only the adjacent paired gains are attributed to this code change.

Native and release gates each pass 512 recordings / 4,721,700 transitions with
zero diagnostics/failures/errors. Source/native/PPC checks, 127 Python tests,
API copy/save/restore and sealed-allocation checks, Wasm parity and browser viewer
smoke pass. A literal source-recursion oracle covers 90,112 generic and registered
subtree cases, including custom matrices, independent parents, shared instance
children, dirty descendants, cold roots and outside-tree sentinels. All live flags
match. Stepped arena usage stays at 1,757,132 bytes / 1,349 allocations; storage
and snapshot format are unchanged.

Candidate benchmark SHA-256: `36b15c7c3c3f16a392c0a9bb831d7caef0519e6c4b531848e383a0e670b8dc53`.
Release runtime SHA-256: `741798bd1cb629f3af9f5cade5602dc3b465e3cd12148b0d654e50236b8f61f2`.
Control benchmark SHA-256:
`1861b873b4e77575c135ec7e14cbd082becc8768760b66aa356c7a40863d5f51`.
A clean rebuild of committed source reproduced the complete control ELF exactly.
Ordered tape-content identity:
`3b34be0c4f050c2ced70de120ad7468af2efbefc99fdbf932b4f865c8ad87277`.
All logs, frozen binaries, source patches and provenance are under
`reports/triage/perf_recovery_20260921/`; final accepted evidence is in `final/`.

The initial PC profile attributed 9.14% of samples to the pose interpreter and its
inlined work, 4.07% to scheduler dispatch and 3.51% to matrix invalidation.
Diagnostic timings are excluded from throughput. A build audit invalidated early
trial attribution: restoring source backups with preserved timestamps retained
stale objects. The apparent epoch-only gain also contained a compact dirty
prototype with an incorrect raw-bit test for custom matrices. Those binaries and
comparisons are not acceptance evidence. The old build was preserved, the release
rebuilt from an empty directory, and subsequent source restores refreshed timestamps.

Fresh screens rejected epoch-byte storage alone (neutral), duplicate pose-admission
removal (mixed), dirty tail recursion (slower), generic dirty child-list traversal
(mixed), and mask-safe flag/dirty fusion (+0.59% at 256, -0.07% at 512). The final
correct compact traversal was also compared with/without epoch-byte storage:
its extra layout change helped 256 by about 0.42% but cost 512 about 0.14%, so it
and its snapshot-version bump were removed. Other unretained trials covered pose
decoder metadata, root ancestry traversal, scheduler masks and field grouping;
the forensic worklog preserves their source and attribution caveats. No rejected
candidate or experimental mode remains in production.

## README benchmark refresh — 2026-09-21

Measured `5e036b4a` with the local admission/corpus cleanup, using strict native
release GCC 13.3 on the Ryzen 9 9950X3D, CPU 0 (96 MiB V-cache CCD), performance
governor. No runtime or benchmark implementation changed for this measurement.
Root targets `benchmark-9950x3d-vcache-{256,512}` used
`BENCHMARK_MATCH_FRAMES=262144`, eight warmup ticks, true resident batches and
128-frame observation/terminal history. Three samples per size ran sequentially
in alternating 256/512 order; all samples are retained.

| Batch | FPS samples | Median FPS | Digest | Resets |
| ---: | --- | ---: | --- | ---: |
| 256 | 110415, 113458, 114135 | 113458 | `64020ea131e1b3a7` | 15 |
| 512 | 109982, 111556, 107099 | 109982 | `8ae6f774c68378ea` | 19 |

The workload contains 508 human recordings / 4,684,452 input frames; four CPU
recordings are skipped by the controller-only tape format. These medians formed
the initial README refresh, superseded by the code-recovery measurements above.
Its previous values used 403 recordings, so the difference is not a controlled
runtime comparison. Digests and reset
counts are identical within each size. `make validation-release-supported-domain`
passes all 512 recordings / 4,721,700 transitions with zero diagnostic/fail/error.

Release benchmark SHA-256:
`1861b873b4e77575c135ec7e14cbd082becc8768760b66aa356c7a40863d5f51`.
Ordered tape-content identity (replay path, NUL, tape SHA-256, newline):
`3b34be0c4f050c2ced70de120ad7468af2efbefc99fdbf932b4f865c8ad87277`.
Raw samples, host details and per-tape hashes are under
`reports/triage/readme_benchmark_20260921/`.

## Strict replay admission workload — 2026-09-20

The local admission cleanup on `5e036b4a` changes the replay workload, not the
runtime. It removes 133 ineligible recordings from all including suites, adds
seven source-checked corpus replacements and gives three previously traced
captures explicit legacy arithmetic profiles. The complete native gate is now
512 exact recordings / 4,721,700 transitions with no classified-success path.
All 502 other retained output-lock records are unchanged; the three profile
recoveries and seven new captures have measured native/PPC locks.

Benchmark preparation applies admission even to cached tapes and increments
its cache revision to 3. The resulting controller workload has 508 recordings
and 4,684,452 input frames; four CPU recordings are explicitly skipped because
the controller-only tape cannot represent their processed AI inputs. Bounded
release playback and the subsystem profile both pass at resident/logical 16,
4,096 match-frames, eight warmup ticks and 128 observation history, producing
`09644ea1039ba5af` in both builds. This is a compatibility check, not an A/B
throughput result.

Historical digests and FPS remain evidence for their recorded workloads. Future
performance comparisons must freeze the same admitted manifest, profiles and
exported config/input bytes for both arms. Do not interpret changed corpus
selection as a speedup. Scratch evidence is under
`reports/triage/pr_sequence_20260920/replay_cleanup/`.

## Kirby integration memory and profile compatibility — 2026-09-20

PR #29 integrated onto `53ebe390` adds the final supported fighter's copy
archives, costume hats, items and capture callbacks. The submitted 192 MiB
GameData reserve is unnecessary: the combined 26-fighter preload uses
114,515,944 / 134,217,728 bytes, so retain main's 128 MiB reserve. Native DAT
uses 151,551,424 / 268,435,456 bytes. Construction's maximum Match arena remains
2,119,308 / 3,145,728 bytes (four Peaches, Yoshi's Story), with 7,966 relocation
records. The ordinary census step remains sealed at 1,757,132 bytes and 1,349
allocations before and after gameplay. These are expanded-domain memory
measurements, not a replacement throughput baseline.

A valid two-Kirby/two-opponent control with different Kirby costumes exhausted
the submitted flat HSD_ID allowance after repeated hat replacement. Source hat
loaders register descriptor keys against fighter parts; those keys persist
across hat removal. Count the preloaded costume/accessory descriptor trees per
actual Kirby costume at initialization, adding that bound to the existing
transient allowance. Repeated Mewtwo/alternate hats (Puff, Falco, Game & Watch,
DK), clone continuation and snapshot restore now pass without runtime allocation.
The four-Kirby special smoke reaches FObj 65/1,337 and AObj 29/410.

Review also repaired a profiled-build compile error: the new hat matrix loop
closed the timing variable's scope before its use. Keep item and hat matrix
publication in the existing timed block. Both release benchmark and subsystem
profile now run the same 634-human-recording manifest (four CPU captures
explicitly skipped), resident/logical 16, 4,096 match-frames, eight warmup ticks,
and produce digest `d732d9c97d9299c3`. This bounded run proves compatibility;
there is no controlled throughput comparison or claimed speedup.

Fresh extraction, native/PPC smoke, full native 638-case gate (609 exact,
29 existing classifications, 6,028,589 transitions), Python tests and Wasm/viewer
checks accompany the implementation. All 595 pre-Kirby output locks and 28
classification records are unchanged. Scratch evidence is under
`reports/triage/pr_sequence_20260920/pr29/`.

## Replay benchmark capability export — 2026-09-20

PR #26 review found that `prepare_replay_benchmark.py` included both UCF
shield-drop capabilities in its cache key but omitted them when calling
`write_benchmark_case`. The writer defaulted both to true. In the 553-case
aggregate, 16 recordings disable the classic 0.84 capability, including three
that also disable extended shield drop. Their benchmark configuration therefore
did not match validation. Forward both fields and increment the export cache
revision; wire layout and production runtime remain unchanged.

A before/after export with both flags disabled changes only config bytes 31/32
(tape bytes 55/56), from 1 to 0. Regression tests also cover the mixed settings
and explicit arithmetic profiles. The full aggregate remains lock-exact with
527 exact / 26 classified cases; all 549 pre-existing output locks are unchanged.
The rebuilt benchmark manifest contains 551 human recordings and reports the
two unsupported CPU tapes as exclusions.

Historical paired performance measurements remain measurements of their frozen
tapes, not proof that those tapes preserved all declared capture capabilities.
Future A/B comparisons must export both workloads with the corrected preparer
and verify config/input equality. Do not compare a regenerated workload's
digest or throughput against the old tapes as though computation were equal.
This is an export correctness correction, with no claimed runtime speedup.

## Correctness completion — 2026-09-08

Checkpoint is `5f097b2a`, committed after the preceding correctness/performance
verification. The new packet completes FD actors, fog and color lifetimes, Dream
Land temporal state and Slippi direction voting, plus camera-pan, aerial-yaw and
side-special momentum arithmetic. Both benchmark arms use the same 403 recordings
and 3,685,488 input frames from the canonical suite. Candidate wire v2 adds one
Whispy capability byte; checkpoint v1 tapes remove only that byte and normalize
the version/header size. Every other config/input byte matches the frozen tapes.

GNU/Linux amd64 GCC 13.3 strict release, 9950X3D CPU 0, resident 256/512,
262,144 match-frames, eight warmup ticks and 128 observation history. Eight
adjacent alternating pairs per size, with all samples retained:

| Resident | Checkpoint FPS | Initial candidate FPS | Median change |
| ---: | --- | --- | ---: |
| 256 | 118924, 120063, 120297, 121122, 121159, 121016, 121140, 120180 | 108754, 108749, 110536, 110706, 110511, 110633, 110574, 110673 | -8.37% |
| 512 | 124772, 125574, 125668, 124652, 125100, 125243, 125379, 125179 | 114644, 114257, 114449, 114468, 114125, 114099, 114444, 113867 | -8.67% |

Digests remain `f07121ff2d154a20` (256, 25 resets) and `4424fd866178964a`
(512, 22 resets). The 8–9% throughput cost is real and is not accepted as
timing noise. Current callback profiling attributes only 2.83% to full Ground
animation and 0.50% to the headless epoch proc, so a checkpoint profile is
required before selecting an optimization. Source lifecycle ownership remains
the final representation; no displaced controller is restored.

Scratch evidence: `reports/triage/correctness_done_20260908/` contains
`performance-samples.json`, all 32 `performance-*.log` samples, tape equivalence,
the frozen binaries and source/checkpoint profiles. The recovery and final result are recorded below.

### Retained stationary transformed-line queries

Source FD now binds the actual collision JObj and publishes `CollJoint_B8` even
while the endpoints stay fixed. The old manual constructor did not bind that
transform. This disabled the previously retained wall-pass AABB rejection, which
conservatively admitted every transformed joint. Profiled collision cost rose
356M→459M cycles on the 65,536-frame workload, versus 38M→88M for Ground animation.

Keep the completed binding, flags and epoch semantics. Extend the existing wall/
ceiling predicate to unchanged nondegenerate endpoints; their remap displacement
is zero. Moving and degenerate lines retain the exact source queries. The remap
leaf also skips zero-displacement arithmetic for finite nonzero query coordinates;
zero coordinates retain source evaluation to preserve cancellation signs. No
cache, spatial index, stage-id branch or mirrored state is added.

Every one of the 529 full replay output fingerprints is unchanged. The source
geometry smoke checks distant/overlapping stationary transformed walls and keeps
moving/degenerate walls admitted. A four-million finite-input differential corpus
has 2,041,343 fast-path admissions and equal raw output words throughout (seed
`8e53a49b`, digest `35b808398d47909a`). A preceding arbitrary-bit probe exposed
compiler-dependent NaN payload selection in the unchanged fallback; NaN gameplay
inputs are outside the finite-domain identity claim, and no scoring policy changes.

The isolated remap leaf screen recovers 0.46%/0.57% at 256/512. Adding stationary
wall admission gives these combined reverse-order screens against the complete
source-stage control:

| Resident | Complete-stage FPS | Query-optimized FPS | Median change |
| ---: | --- | --- | ---: |
| 256 | 111847, 112071 | 116873, 116704 | +4.31% |
| 512 | 116291, 116409 | 120885, 120734 | +3.83% |

Both digests and reset counts are unchanged. This recovers part of the initial
cost, not a throughput gain over the committed checkpoint.

### Rejected stage/AObj compiler admission

Strict O3/native ISA for grLast, Ground, grMaterial and HSD AObj clock dispatch
preserves all 529 output locks, but loses versus the query-optimized control:

| Resident | Control FPS | O3 candidate FPS | Median change |
| ---: | --- | --- | ---: |
| 256 | 117086, 115931 | 115766, 115936 | -0.56% |
| 512 | 120515, 121021 | 119681, 119421 | -1.01% |

No compiler admission is retained. Revisit requires a separately attributed hot
owner or materially different generated code; the broad stronger profile costs
more than it saves. Scratch-only objects and sampling binaries are not production
paths. The final checkpoint comparison follows.

### Final checkpoint comparison

All builds, replay gates, PPC arbitration and viewer/API checks finished before
this final sequence. Eight adjacent alternating pairs per size, same CPU 0 and
the same tape/config equivalence contract:

| Resident | Checkpoint FPS samples | Final FPS samples | Median FPS, checkpoint → final | Paired median change |
| ---: | --- | --- | --- | ---: |
| 256 | 120971, 120581, 121422, 120919, 121264, 121541, 121251, 120999 | 116499, 116523, 116294, 116117, 116715, 116672, 116707, 116865 | 121125 → 116598 | -3.75% |
| 512 | 126100, 126138, 126104, 125721, 125960, 126172, 125429, 125880 | 120699, 120560, 120435, 120945, 120367, 120679, 120147, 120676 | 126030 → 120618 | -4.32% |

The final complete lifecycle implementation remains **3.74%/4.29% slower by
median FPS** at resident 256/512 than `5f097b2a`. Every adjacent pair is slower;
this is a retained correctness cost, not measurement noise or a performance-neutral
claim. The earlier committed checkpoint remains covered by its separate neutral
comparison below. The stationary-query changes recover roughly half of this
campaign’s original 8–9% cost while keeping the completed source representation.

All 16 samples per size keep the same digest and reset count. Debug/release
each pass 503 exact / 26 classified / zero fail/error across 5,059,922 transitions.
The 38-case PPC audit passes 12 exact / 26 classified with identical snapshots.
Source/native, transformed-line edge tests, sealed stage lifecycle/copy/restore,
Wasm/viewer and 47 focused Python tests pass. No stronger compiler profile or
diagnostic mutation remains.

Binary SHA-256:

- Checkpoint release benchmark: `3ab204f344363aedd37cf57ba7a1bf21c48606cd73785a30f5284f102107f757`.
- Final release benchmark: `afca8a434f5b59b4eda088f27be14019bbd0f337d78d1f0ecec2b141c7ec3712`.
- Final strict release runtime: `ec0411e487b577ba0c6baf3a5233245c718e3494a7635f39aa998b0459984d5a`.

Final raw samples and complete logs are `final-throughput-*` in the campaign
triage directory. This bounded 403-case throughput check is not a replacement
for the longer `BASELINE.md` workload contract.

## Classified replay closure — 2026-09-08

Correctness packet against frozen merge `24643394`: source quaternion dot-product
order, SDK camera arithmetic, Fountain's existing timer/RNG machine before recorded
height publication, and source Wait-entry parasol cleanup. Also correct the old
Peach capture's UCF profile. No new state or production allocation. Search/review
covered quaternion, camera and stage owners in this history and the retained index;
this is source recovery, not a renewed transform/pose architecture experiment.

Controlled GNU/Linux x86-64 GCC 13.3 strict release, 9950X3D CPU 0, the same 403
ordinary-input recordings as the preceding merge comparison, resident 256/512,
262,144 match-frames, eight warmup ticks, 128 history. Prepare one input manifest
with the corrected classic Peach flags for both binaries, then run three alternating
samples per binary/size. Baseline executables were frozen before the first edit;
candidate is built with the root `native-release-benchmark` target. Each run uses
identical decoded tapes and lane order; PPC checks finished before timing began.

| Resident matches | Baseline samples, FPS | Candidate samples, FPS | Baseline median | Candidate median | Change |
| ---: | --- | --- | ---: | ---: | ---: |
| 256 | 120572, 118104, 119424 | 119336, 119790, 119077 | 119424 | 119336 | -0.07% |
| 512 | 124492, 123893, 124215 | 123816, 125084, 124554 | 124215 | 124554 | +0.27% |

All six samples at each size share the same digest: `f07121ff2d154a20` (256),
`4424fd866178964a` (512). This bounded throughput workload does not reach the
corrected validation episodes; matching benchmark digests do not replace the replay
gates. Differences are within these samples' run-to-run spread; no throughput gain
is claimed. The 256-match lifecycle smoke reports 0.58 GiB reset RSS, 0.61 GiB with
observation ring and 0.04 GiB after destruction. Native/Wasm parity passes.

Validation: full 529 native debug and release gates preserve the 470 original exact
locks and reach 493 exact / 36 classified. The former 59-exception PPC subset has
21 exact / 38 classified; two camera entries remain PPC-only. See the
[closure report](https://github.com/kyhavlov/melee-sim-light/blob/8d049aba187a38b7ace9961ba2eaf3c03f5487ee/agent_docs/CLASSIFIED_REPLAY_CLOSURE_2026-09-08.md) for source evidence,
remaining boundaries and final gate scope. Forensics:
`reports/triage/classified_closure_20260908/benchmark-{samples.json,*.log}`.

### Pre-commit verification and camera compiler recovery

Ten additional adjacent pre/post pairs (alternating order, same workload) detected
a small cost in the original packet: geometric paired throughput -0.335% at 256
(approximate t interval -0.606%..-0.065%) and +0.001% at 512
(-0.558%..+0.563%). A diagnostic-only hardware-sqrt ablation of the new runtime
camera helper recovers a median 0.55% at 256. The source Gekko arithmetic remains;
that ablation is discarded. Linux perf counters were unavailable under the host's
existing perf permissions, so this attribution uses isolated object relinking.

Retain strict O2/native ISA for **runtime/camera.c** in native release (and the
corresponding release Python object). This is a different owner from the rejected
August 2 **melee/cm/camera.c** admission. The isolated O2 object preserves all 529
replay output locks; six adjacent pairs versus the unoptimized correction give
median paired recovery +0.586% at 256 and +0.270% at 512. A fresh full root release
build with the final profile again passes 493 exact / 36 classified / zero failure.
Debug, PPC and Wasm arithmetic/profiles are unchanged by this compiler admission.

Final direct frozen-merge comparison, eight adjacent pairs per size:

| Resident | Baseline FPS samples | Final FPS samples | Median FPS, baseline → final | Paired median change |
| ---: | --- | --- | --- | ---: |
| 256 | 117252, 118397, 119480, 118455, 118834, 115236, 119000, 119508 | 119651, 120156, 118052, 109695, 119372, 119597, 119301, 119810 | 118644.5 → 119484.5 | +0.353% |
| 512 | 124351, 123970, 123259, 123361, 124479, 123873, 122855, 122031 | 124501, 124777, 123203, 123277, 123131, 124094, 121948, 122077 | 123617.0 → 123240.0 | -0.004% |

All digests still match the frozen merge. The final paired medians show no material
throughput regression. Keep all samples: the 256 run contains a 109695-FPS
candidate outlier and a 115236-FPS baseline outlier, so this is not proof of a
strict sub-percent bound on host timing. Geometric paired changes are -0.089%
(approximate t interval -2.887%..+2.790%) and -0.120% (-0.575%..+0.338%). No speedup
claim or removal of outliers is used for acceptance. Scratch samples/summaries are
`confirm-*`, `ablation-*`, `camera-o2-*`, and `final-perf-*` under the same triage
root. Implementation, full output-lock gates and these retained samples accompany
the authorized correctness checkpoint commit.

## Bowser disproves the x86 acos estimate equivalence — 2026-09-07

The Yoshi/Bowser support packet starts from `3a71888c`. Adding the matched
Bowser flame owner exercises `itKoopaFlame_Update_Angle`'s `lbVector_Angle`
consumer beyond the former 16-character corpus. Native x86 has a 19-frame
flame velocity/position island in `JuvenileRedGoshawk.slp` (first 3756) and a
four-frame island in `MustyGummyLemur.slp` (first 5569). The PPC reference
matches both original recordings exactly.

A bounded one-owner experiment replaces only `acosf`'s `rsqrtss` seed with
`__frsqrte`, preserving the radicand, three ordered f32 Newton refinements,
`atanf`, exceptional behavior, and every caller. Both native cases then match
all 24,837 frames, including the live normalized-direction item bytes. The
three rounded refinements do not guarantee a seed-independent result. The
former 3,501,461-frame gate was valid evidence for that corpus, but its
extrapolation to all supported gameplay is disproved.

Retain the source Gekko estimate as the singular seed on every backend. Remove
the native estimate substitution; no per-character path, approximate return,
new state, or fallback is introduced. The rest of the completed scalar math
and collision work remains. This is a correctness correction, with no claim
that it preserves the prior throughput gain. Revisit requires source-exact
results across the expanded domain and a general argument about the returned
bits, not only the former replay set. Scratch build/validation logs are under
`reports/triage/yoshi_bowser_port/acos-gekkoseed-*`.

## Stationary fighter roots retain constraint invalidation — 2026-09-07

The Bowser packet at parent `3a71888c` exposes a missing dependency in
`msl_fighter_pose_set_root_position`: identical root coordinates caused an
early return, although a capture RObj could point at the other fighter's
moving bone. Native/PPC traces at Cylindrical frame 855 show the native
victim's matrix staying clean and holding the previous attachment position;
the source setter dirties the subtree and PPC follows Bowser's TransN2.
The same defect appears for two rows of Sheik's capture in MildMurkyNewt.

Keep the flat canonical preorder and direct root matrix publication. Traverse
the affected descendants for constraint/IK invalidation even when the root
position repeats, while omitting ordinary redundant matrix products. No new
state or alternative traversal is added. The two full native fingerprints
then equal PPC: the capture-position differences disappear completely. A
separate diagnostic forcing compact decoder evaluation left both original
fingerprints unchanged, ruling out the immutable integer pose table.

Adding Yoshi and Bowser also grows the shared animation bank to 267,980 nodes,
beyond its old 18-bit token. Widen that one token to uint32 and place its flags
in the existing native trailing padding: the native pose joint stays 56 bytes;
the 32-bit layout grows from 44 to 48 bytes. This is source-completion capacity
work, with no claimed throughput gain. Full expanded-domain gates and the
prior exact output locks remain the acceptance criterion.

The completed character checkpoint passes `source-check`, native lifecycle and
costume/parts census, Python team and live-article cross-index restore checks,
and Wasm/live and production browser tests. Debug and strict release each preserve the expanded
404-replay output locks: 339 PASS, 65 CLASSIFIED, 0 FAIL/ERROR across 3,691,888
transitions. The 38 added replays contribute 190,427 transitions; all 20 real
microreplays are exact on both native and PPC. The charged-smash source fix
retires the old doubles damage classification and refreshes only that prior
output lock; the other 365 old locks are unchanged. See
[historical report](https://github.com/kyhavlov/melee-sim-light/blob/8d049aba187a38b7ace9961ba2eaf3c03f5487ee/agent_docs/YOSHI_BOWSER_SUPPORT.md) for the complete port and evidence limits.
The character-only commit tree also passes source/native lifecycle, focused
Python and both full debug/release gates in an isolated checkout without the
concurrent reward additions; all 404 retained outputs remain unchanged.

## Python release library and item capacity correction — 2026-09-05

Slippi-AI uses `perf/decomp-throughput` at `f3f0f299` (runtime through
`8cbd742c`). `python-release` now builds PIC objects in a separate directory
with the existing strict per-source native release profiles and unsafe-FP/LTO
rejection. No new compiler optimization was admitted.

A 512-environment Fox/Peach doubles RL run exposed two initialization capacity
errors. The first sealed-arena abort was a turnip JObj allocation after the
128-piece reserve was exhausted. Reserving 256 pieces alone was rejected: a
scripted 64-match pull/throw probe then exhausted the 15-object Item pool near
frame 6500. The observation's 15 slots are not the source gameplay limit:
ItCo.dat permits 80 category-8 character articles and 40 category-0 common items.

Item and DynamicBone reservations now use those source limits, including
common items when Peach is configured. Only the JObj size class gets extra
Peach article headroom: 17 joints per possible item, the maximum reached
Peach graph size, in addition to the existing runtime headroom. The arena stays
sealed and the 3 MiB per-match ceiling remains unchanged. This is a correctness
and capacity fix, not a claimed native throughput optimization.

On gigaserver, the release library passes 768,000 scripted Peach frames, all
nine Python API tests (including a 512,000-frame regression and four-player
construction across the full roster), and `native-smoke` including the
construction census and copy/save/restore/allocation checks. Logs live under
Slippi-AI `reports/triage/decomp_fp16_rl_20260905/`. End-to-end RL throughput is
reported separately in Slippi-AI `docs/decomp_fp16_rl.md`.

The completed-game audit also found premature doubles termination in
`runtime/match.c::gm_80167320`: the first eliminated player ended the match.
The handler now applies `gm_GetFFAOutcome` / `gm_GetTeamBattleOutcome`'s
surviving-side criterion. Native scalar regression covers singles and both
doubles eliminations; a full scripted Python game verifies that surviving
teammates continue playing. The earlier RL evaluation is superseded, and the
final Slippi-AI demonstration starts fresh with this correction.

## Ice Climbers item-output canonicalization — 2026-08-03

Ice, Blizzard, and Belay now publish only source-defined gameplay bytes in the four Slippi item
misc lanes. Ice's scale byte is read from its named source member; owner/link/JObj pointers and
bytes beyond each declared item-variable payload are excluded. This replaces 25 obsolete
raw-pointer or fixed-pool-residue classifications with one genuine Whispy RNG-phase classification,
a net reduction of 24, and refreshes 23 affected output locks without changing gameplay state or
adding runtime storage.

One candidate/parent/parent/candidate sequence against exact parent `de8879be`, using 262,144
frames and eight warmup ticks, measures:

| Resident matches | Parent median cycles/frame | Candidate median cycles/frame | Delta |
|---:|---:|---:|---:|
| 256 | 41,004.5 | 40,744.0 | -0.64% |
| 512 | 42,501.2 | 42,361.7 | -0.33% |

The 256 digest remains `5bd0cb90236b720d`. The 512 digest deliberately changes from parent
`932bcfbacb888ac4` to `a5f79aeaf1a03b84` because the benchmark includes the corrected item-output
projection. Both orders are within one percent; this is accepted as performance-neutral
correctness work, not as a throughput optimization. The complete optimized-release gate is
310 PASS / 56 unchanged CLASSIFIED / zero XPASS/fail/error over 366 replays and 3,501,461 frames;
source/native/PPC/Wasm gates pass. Release benchmark SHA-256:
`8949d99cf7270be2d778e89bb934f6d07d08c95d042ba22a7174f57d671ed29b`.

## Archived performance baseline — 2026-08-04

The former BASELINE.md checkpoint used 366 recordings, before the September
capability-export and strict-admission corrections. Its full contract and memory/
profile census remain in `83356f34:agent_docs/performance/BASELINE.md`. Preserve
its unique throughput evidence here when refreshing the current baseline.

Candidate based on `02cfe013`, benchmark SHA-256
`45b02500e300c850250385bf9c0dd6e34edd0bcb808f6019b47eebc81ede3318`, measured the same
262,144-frame/eight-warmup/128-history shape with strict GCC 13.3 on CPU 0.

| Batch | FPS samples | Cycles/frame samples | Median FPS | Digest |
| ---: | --- | --- | ---: | --- |
| 256 | 110485, 112253, 112767 | 38846.5, 38234.8, 38060.3 | 112253 | `bdff41cf74a54850` |
| 512 | 106281, 106985, 110145 | 40383.1, 40117.3, 38966.4 | 106985 | `ee9d93c545aa3ef9` |

Adjacent paired gains versus `02cfe013` were +3.36%/+9.80%/+11.62% at 256
(median +9.80%) and +8.23%/+3.41%/+10.37% at 512 (median +8.23%). All arms were
retained. Correctness was 310 PASS / 56 then-existing CLASSIFIED / zero errors
across 3,501,461 transitions. These are historical results, not current acceptance
rules or current-corpus performance identities.

## Canonical pose/dynamics throughput checkpoint — 2026-08-02

The retained native/Wasm fighter animation path now consumes the compact pose arena's canonical
preorder and construction-bound source-part identities directly. This deletes the repeated JObj
DFS, sparse `FighterBone` search, and JObj-to-pose lookup from `ftAnim_8006E7B8` without adding a
second schedule or growing the 56-byte Match-owned pose node. The track count and source-part index
share the former 16-bit track-count slot; all extracted Figa nodes fit the asserted 8-bit count.

Native Figa attachment no longer constructs mutable decoder tracks while the exact immutable
integer-sample program owns evaluation. The existing decoder state is materialized from source
tracks only when a fractional/non-table transition actually gives it ownership. Source identity is
held by a 16-bit program index in existing padding, so the shared program node remains 16 bytes and
per-Match state does not grow. Descriptor animation and PPC retain eager source decoders, and the
existing exact table-to-decoder resynchronization remains the only transition path.

Two exact compiler boundaries remove measured O0 quaternion overhead: the already optimized hosted
dynamics owner contains the source-authored axis-angle/Euler operation sequence, while only
`HSD_QuatLib_8037EF28` receives singular O2 admission. The rest of `quatlib.c`, PPC, and the
correctness-sensitive shared conversion paths remain unchanged. Finally, construction marks
retained dynamics-chain hurt owners in the existing hosted capsule bit, deleting the per-frame
identity search, and the existing inactive-animation predicate is evaluated before entering the
large interpreter. Neither cut adds state or changes publication order.

Paired 262,144-frame evidence for the two largest independently isolated changes is:

| Change | Resident 256 cycles/frame | Resident 512 cycles/frame | Digests |
|---|---:|---:|---|
| Canonical fighter pose span | `44,308.7 -> 42,785.8`; `44,617.9 -> 42,644.8` | `46,310.2 -> 45,506.8`; `46,683.9 -> 46,025.5` | exact |
| Exact quaternion boundaries | `43,787.5 -> 42,666.2`; `43,194.1 -> 42,266.4` | `46,597.2 -> 44,523.2`; `45,975.4 -> 44,838.6` | exact |

The final release benchmark SHA-256 is
`3ef66625839ca254b2448f43e2c541858526a185c3811b681c8beda8faec2637`. Three final resident-256
samples are `42,083.7`, `41,700.7`, and `41,614.7` cycles/frame, with median 102,923 FPS and digest
`5bd0cb90236b720d`. Five final resident-512 samples are `42,939.1`, `42,259.9`, `42,346.9`,
`42,305.3`, and `42,310.7` cycles/frame, with median 101,439 FPS and digest
`932bcfbacb888ac4`. The first wall sample was 99,954 FPS; the other four tightly span
101,352–101,561 FPS. Cycles/frame is the primary measure because wall throughput moves with host
frequency and contention.

The complete release validation remains 286 PASS / 80 unchanged CLASSIFIED / zero
XPASS/fail/error across 366 replays and 3,501,461 frames. Source synchronization, native
API/copy/save-restore and sealed allocation, 45 Python tests, PPC smoke, Wasm parity,
viewer/browser, runtime census, lifecycle, and formatting gates pass. The ordinary stepped Match
remains sealed at 608,864 arena bytes and 843 allocations; the relocatable savestate remains
671,664 bytes. Wasm reports state digest `a8a4eceda2e87198`, viewer digest
`5d40dda6e777c112`, and a 491,964-byte snapshot.

## Authored-node alternate-pose blending — 2026-08-02

`ftAnim_80070108` runs after the interpolation skeleton has been reset to its static authored pose
and its attached Figa has been evaluated. The native canonical path now performs the final
quaternion/SRT blend only for joints that actually carry attached animation. Unattached joints are
still exactly the static pose that the blend would reproduce. Attached nodes retain the source
traversal and `lb_8000C868` operation order; no action/character list, state, cache, allocation,
snapshot byte, compatibility path, or runtime mode is added.

The control/candidate release binaries are
`52f5c10dd956b11db0186fa7ad214b0054a1c6f6c18aba62a5fa1224666fb1f7` and
`00265ac015db0404e8ed27f7215d1ea5d4be7781cbb71db20696f664c03ac8ef`. Three alternating
262,144-frame resident-256 pairs preserve digest `5bd0cb90236b720d`. Control cycles/frame are
`46,107.4`, `46,342.1`, and `45,028.1`; candidates are `45,121.8`, `44,734.1`, and `45,157.7`.
Medians improve `46,107.4 -> 45,121.8` (-2.14% cycles, +2.18% FPS).

Three clean reverse-order resident-512 pairs preserve digest `932bcfbacb888ac4`. Candidate/control
cycles/frame are `49,179.6/50,464.9`, `50,109.5/51,353.8`, and `50,697.2/50,168.8`; the median paired
cycle change is -2.42%, while raw medians improve `50,464.9 -> 50,109.5` (-0.70%). The complete
native gate remains 286 PASS / 80 unchanged CLASSIFIED / zero XPASS/fail/error over 366 replays
and 3,501,461 frames. Source, native/API/copy/save-restore/allocation, ordinary tests, formatting,
PPC smoke, Wasm parity, and viewer/browser smoke pass. Wasm reports state digest
`a8a4eceda2e87198`, viewer digest `5d40dda6e777c112`, and a 491,964-byte snapshot.

## `decomp-port-arm64-ppc` merge polish — 2026-08-01

The expanded character branch no longer publishes the retained dynamics chains twice per fighter:
`Fighter_8006D9AC` is the sole post-solve/end-frame owner, and it consumes canonical JObj dirty
state rather than forcing every link dirty. Native release also emits the source-authored
`__fmadds` sites directly as the compiler intrinsic they represent; the debug, Python, Wasm, and
PPC profiles keep the existing out-of-line owner. A broader fmsubs/fnmsubs inline candidate was
measurably slower and is not retained.

Match initialization keeps the established ordinary FObj/class-piece reserves and selects the
larger measured reserves only when Samus is configured, because her grapple is their documented
sole consumer. Pose storage is likewise sized at 256 joints per configured player under the final
1,024-joint public ceiling. The ordinary two-player snapshot falls from the review head's 686,676
bytes to 611,420, below the comparison branch's 633,156; save/restore measure 0.504/0.661 ms versus
the comparison medians of 0.5265/0.691 ms. The complete census peaks safely at 1,016 pose joints
for four Sheiks and preserves the larger Samus pool high-water.

Final serialized common-domain samples use the same 153 replays, 65,536 match-frames, fixed CPUs,
and stable per-ref digests. At resident batch 256, comparison/candidate median costs are
63,765.7/62,011.2 cycles per frame (-2.75%). At batch 512 they are 48,921.1/49,625.9 (+1.44%).
The remaining spread is normal host contention; this closes the review head's material
+12.85%/+13.54% regression. Digests differ across refs only because the branch deliberately
changed hosted identities and the compare wire (`8ef126a41244d514`/`6f91f23e3553a090` comparison,
`474828382690a770`/`a1178bdf42eb555a` candidate).

The final native aggregate is 286 PASS / 80 unchanged CLASSIFIED / zero XPASS/fail/error over all
366 replays and 3,501,461 frames. Seven dynamics-sensitive exact locks remain byte-identical;
focused PPC Ice Climbers validation has only 187 established item-residue rows and no follower or
physics mismatch.

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

A 2026-08-04 bounded revisit removed the original candidate's concrete overheads: it dispatched
priority once instead of once per fighter, kept the winning generic hot loop in place, resumed its
saved later-p-link process in the ordinary case, and rescanned only when an existing insert/remove
owner marked the proc list mutated. It also admitted the direct path only for an all-fighter,
proc-free p-link-8 list, preserving the generic scheduler API. Both digests and native smoke stayed
exact. CPU-8 timings were invalidated by large frequency/contender drift; on the canonical CPU-0
V-Cache contract, adjacent resident-256 frozen-control/candidate costs were
`35,979.4/36,091.5` and `35,979.4/36,028.5` cycles/frame, a 0.14--0.31% regression. The entire
revisit was removed. This closes fixed fighter scheduling after both the source-shaped and
mutation-aware implementations; another dispatch rearrangement is not a justified revisit.

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

## Replay curriculum retired — 2026-09-07

The matched 1,000-update experiment found no measurable playing-strength benefit
and cost 20.0% more time per update. User requested stashing the replay/drill
implementation. Capture/seal optimization and the companion are no longer active;
the source-owned destructor/color-return relocation corrections remain, with the
context-smoke coverage. Per-capture hashing had previously cost 96.2% in the native
64-lane screen; admission-time sealing reduced that isolated overhead to 7.24%,
which did not remove the full-training cost. Future replay work needs a better
selection criterion before revisiting this implementation. Evidence and frozen
libraries remain under Slippi-AI reports/triage/replay_buffer_20260906/; previous
implementation and full experiment notes are in the 2026-09-07 curriculum stash.

## Mixed training groups and natural 2v1 starts — 2026-09-07

User requested mixed 1v1/2v1/2v2 training with normal three-player team starts
until natural endgames become available, and negligible overhead. Existing Match
construction now accepts three players; the wire ABI and source gameplay remain
unchanged. `EnvBatch.step_and_reset(step_mask=...)` holds saved native states
while the app warms current recurrence with recorded public inputs. The app owns
fixed player/worker slices and a shared 32-entry FIFO with uniform sampling;
only natural irreversible 4-to-3 transitions capture native saves. The original
characters/state survive restoration. No periodic native capture or TD ranking.

Native source-check, native-smoke and all ten Python API tests pass. The former
negative three-player API fixture now rejects one player; positive coverage
includes three/four-player teams across the supported roster. The app verifies
exact restored continuation, held warmup, controller/source mapping, rewards and
loss masks, and runs actual mixed PPO training. The 104-step learning run includes
100 policy updates, 93 admissions, 410 restored starts and 1,064 empty-bank fresh
starts. Warmup consumed 0.48% of scheduled environment frames on average, rising
to 1.65% in the final rollout; throughput excludes these held frames. This is
functional validation, not evidence of a playing-strength gain.

Initial four-process ABBA measurements (1,280 envs, 512/256/512 mode split,
80-frame rollouts, FP16, full PPO with zero learning rate, microbatch 1,280)
measured 2.53% recording overhead with exact matching final trajectories.
Packing replay metadata alone did not reliably improve whole-update throughput
(second ABBA mean-of-medians overhead 3.08%). The final implementation uses bulk
byte copies for native history and ordinary packed inference whenever recorded
actions are unused. Separate packed-JIT input layouts share the same policy/RNG;
packed FP16/FP32 regression checks verify exact recurrent state and samples
while switching ordinary/recorded signatures.

A more controlled final benchmark alternated off/record arms within one process,
with separate actors and recurrence but one frozen learner. Recording-only leaves
reset selection unchanged to keep computation/gameplay equivalent. Across 84
timed updates per arm after 20 startup updates, mean update time was
0.68568485/0.68708673 seconds (+0.20%); medians 0.66626718/0.67116268 (+0.73%).
Learner means differed by 0.04%. It captured 55 natural events; both final
game/action/reset hashes were
`3641a1a2daefef2d1412d359869979502162bc7acbf027f9e165d94f6339280c`.
An eight-update block bootstrap gives a wide -4.91% to +5.57% interval for mean
whole-update overhead: small observed overhead, not a certified sub-1% ceiling.
Restored-start warmup and changing the mix alter useful compute and must be
accounted for separately from recording overhead.

Evidence: isolated Slippi-AI `reports/triage/mixed_envs_20260907/`, including
before-state tarballs/patches, `bench_complete.json`, packed ABBA logs,
`paired_fast/{paired_timings.jsonl,paired_complete.json,summary.json}`, and
training/check logs. The original deployed checkout and frozen experiment
libraries remain untouched. No commits or long strength experiment requested.

Follow-up: fresh three-player starts now use one stock and independent uniform
integer percents in 0..100 on every fresh reset, as requested. Per-player seeded
draws live in the Slippi-AI adapter's next-reset configuration. Native
`Player_SetHUDDamage` seeds the player owner before `Fighter_Create`; no live
fighter mutation or per-frame randomization. Saved endgames keep their native
percents. The new `start_percent` byte expands public match config from 48 to 52
bytes and internal match config from 53 to 57; C/Python bindings and generated
viewer schema were updated together, and libraries must be rebuilt together.
Native release/source-check/native-smoke, ten API tests (including exact roster
starts at 0/37/100), eleven app environment checks and viewer-schema checks pass.
A final assertion confirms percents survive normal entry before automatic reset.
A 24-iteration mixed GPU run (four burn-in, twenty policy updates; 512/256/512
environments) exercised 783 fresh three-player starts with all metrics finite.
The 900-frame limit leaves the bank empty in this short run; exact-restore tests
and the preceding longer training run cover bank reuse. Evidence is under
Slippi-AI `reports/triage/mixed_envs_20260907/random_percents/`. This behavior change
was not treated as an equivalent-work throughput comparison.


## decomp-port-dev integration — 2026-09-08

Merged candidate: dev `152f703d` plus incoming `22136d7d`. Retain dev's compact
pose, source-sized Item pool and full Peach turnip reserve. Import the incoming
per-fighter AObj/FObj/GObj/ItemLink reserves and per-player pose-track capacity.
The single `msl_class_reserve_pieces` helper supplies both the per-port JObj floor
and Peach's full item-graph bound; the incoming capped class helper and empirical
per-fighter Item-count switch are displaced. Source `grStory_801E3418` admits at
most five Shy Guys in a wave, separately reserved on Yoshi's Story.

Link/Young Link sword insertion now joins the existing flat pose preorder during
OnLoad. It repairs shifted node pointers, parent spans and ECB indices, and binds
the Wasm part id. Native article-pool smoke and cross-index Python restores cover
this interaction; no hot-path traversal or duplicated part-flag state is restored.

Fresh native `make runtime-census` sweeps all 21 fighters on six stages with
2-player singles, 3-player teams and 4-player teams (378 configurations):

| Measure | Combined candidate |
|---|---:|
| Maximum construction arena | 2,069,028 / 3,145,728 bytes |
| Maximum-arena lineup | Four Peach, Yoshi's Story |
| Maximum allocations / relocation records | 2,174 / 7,052 |
| Maximum pose joints | 1,016 / 1,024 |
| Two-Peach FD runtime allocation lock | 1,745,572 bytes, 1,276 allocations before and after |
| Two-Peach FD saved state | 1,808,620 bytes |
| Shared game-data arena used | 92,399,848 bytes |
| Shared native DAT arena used | 118,277,920 bytes |
| Native archive-cache entries | 4,480 / 8,192 |

The 256-lane large-batch smoke and lifecycle benchmark pass. Their timing was
collected concurrently with verification and is not a controlled performance
comparison. No throughput equivalence or speedup is claimed. A fresh resident
256/512 comparison follows below; it does not replace the longer contract in
`BASELINE.md`.

The combined debug/release gate passes 529 replays and 5,059,922 transitions
(470 exact, 59 classified). Fresh extraction, source, native/Python, Wasm and
production-viewer checks pass. Full integration details are in
[historical report](https://github.com/kyhavlov/melee-sim-light/blob/8d049aba187a38b7ace9961ba2eaf3c03f5487ee/agent_docs/DECOMP_DEV_INTEGRATION.md); the incoming branch's distinct historical evidence
is preserved in `IMPORTED_DECOMP_RESERVES_2026-08-26.md`.

## Pre/post merge throughput — 2026-09-08

Compare frozen pre-merge dev `152f703d` with merge `b0601914`, using fresh
strict native release GCC 13.3.0 builds on the same Linux amd64 Ryzen 9 9950X3D
host, pinned to CPU 0 (V-cache CCD). No gameplay or benchmark code changed for
this measurement. The experiment was recorded in `../ACTIVE_WORK.md` before
execution. The user accepted the initial small differences; the corrected
profile below remains within that range. No performance repair is requested.

Use 403 shared recordings: the pre-merge 404-case suite minus retired unfrozen
Stadium recording `HummingDismalCat.slpz`. Both builds consume the same selection
and lane order. Compare all prepared tapes after normalizing the version 81/82
header and removing the added UCF 0.84 configuration byte: input/configuration
contents match across all 403 tapes, covering 3,685,488 available input frames.
Each build loads its own extracted game-data profile.

Run `make benchmark-native` sequentially in order pre/post/post/pre/pre/post for
each size. Parameters are `BENCHMARK_CPU=0`, `BENCHMARK_MATCHES=N`,
`BENCHMARK_RESIDENT_MATCHES=N`, `BENCHMARK_MATCH_FRAMES=262144`, and
`BENCHMARK_WARMUP_TICKS=8`, with `VALIDATION_SUITE` pointing to the shared
`reports/triage/decomp_merge_benchmark/common_suite.json`. Observation history is
128; startup preroll is outside production timing. Each timed sample lasts
roughly 2.1–2.2 seconds; this is a bounded throughput check, not a sustained RL
training benchmark or replacement for `BASELINE.md`.

| Resident lanes | Pre samples, match-frames/s | Post samples, match-frames/s | Pre median | Post median | Change |
|---|---|---|---:|---:|---:|
| 256 | 122297, 120895, 121806 | 119942, 120808, 119932 | 121806 | 119942 | -1.53% |
| 512 | 126009, 125740, 125520 | 125141, 124991, 125084 | 125740 | 125084 | -0.52% |

All six samples at 256 lanes have digest `f07121ff2d154a20` and 25 resets;
all six at 512 have digest `4424fd866178964a` and 22 resets. The observed
throughput differences remain small; three short samples do not
establish a precise regression size or attribute cost to a runtime owner.
The workload covers shared gameplay and does not measure the newly added
Ness/Link/Young Link population. Expanded-domain correctness gates remain those
recorded in [historical report](https://github.com/kyhavlov/melee-sim-light/blob/8d049aba187a38b7ace9961ba2eaf3c03f5487ee/agent_docs/DECOMP_DEV_INTEGRATION.md).

Forensic logs, tape comparison, runner and machine-readable samples are under
`reports/triage/decomp_merge_benchmark/` (`{size}-{run}-{pre|post}.log`,
`tape-equivalence.txt`, `run.py`, `samples.json`). The table above retains all
throughput samples independently of that ignored scratch directory.

### Temporary-profile correction

The initial manifest was flattened with `dataclasses.asdict`, leaving absent
per-replay settings as explicit nulls. `load_suite` interpreted optional UCF
nulls as false and null `played_on` as the string `None`. The initial comparison
therefore measured equivalent custom-profile work on both builds, not the
intended canonical replay profile. This was found while preparing the classified
replay audit. Omit absent keys, verify the intended suite defaults/entries, and
repeat the same twelve-sample protocol; the corrected results are above.
All 403 corrected tapes match after the documented wire-header normalization.

For durable provenance, the superseded initial samples were:

| Resident lanes | Pre samples, match-frames/s | Post samples, match-frames/s | Median change |
|---|---|---|---:|
| 256 | 120887, 120969, 119445 | 119855, 118761, 118970 | -1.59% |
| 512 | 124036, 125024, 125507 | 123819, 122013, 124577 | -0.96% |

Their digests were `d12cec3aa27c08da` (256) and `3b9cf47fe13ef1b3` (512),
equal across builds. Initial raw artifacts are preserved under
`reports/triage/decomp_merge_benchmark/initial-null-profile/`. These numbers are
not the canonical-profile result and should not be cited as such.
