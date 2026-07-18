# Melee core performance worklog

This is the durable evidence log for the maximum-throughput program in `README.md`. Benchmark
results are accepted only when they use the production observation/terminal contract, retain the
same digest for the same workload, and pass the complete correctness gates without new or widened
classifications.

## Phase 7 baseline — 2026-07-16

### Workload and commands

`tools/melee_core/prepare_replay_benchmark.py` decodes the complete aggregate suite once through
Peppi, then asks the existing native Arrow preprocessor to write a packed match config and
controller-input tape. Cached cases are under ignored `build/melee_core/benchmark/`; checking a
warm cache takes about 0.7 seconds. The current corpus is 153 cases and 1,415,629 input frames.
Python and Arrow are absent from the timed workload.

`tests/melee_core/replay_bench.c` mmaps every tape before timing, cycles them over logical
environments, runs ordinary free-running gameplay, and writes `MslCoreObservation` plus
`MslCoreTerminal` into a caller-owned 128-frame ring. It does not use validation comparison,
per-frame replay RNG authority, stage-event teacher forcing, or viewer output. The original Phase 7
workload used one near-end checkpoint per shard; the representative Phase 9 contract below
supersedes that opening-heavy workload. A second pass reports step-only throughput. The
observation/terminal ring is hashed after timing.

Build or refresh the cache:

```bash
make -f src/melee_core/Makefile benchmark-prepare
```

Run one bounded baseline at a time; each target pins one physical core and completes in less than
ten seconds on the baseline host:

```bash
make -f src/melee_core/Makefile benchmark-9950x3d-frequency-256
make -f src/melee_core/Makefile benchmark-9950x3d-frequency-512
make -f src/melee_core/Makefile benchmark-9950x3d-vcache-256
make -f src/melee_core/Makefile benchmark-9950x3d-vcache-512
```

The default timed sample is 32,768 complete logical environment-frames with eight warmup ticks.
The ring writes 996 bytes/environment-frame: a 980-byte observation and 16-byte terminal result.
At history 128 this is 127,488 bytes/environment, 31.12 MiB for 256, and 62.25 MiB for 512.

### Host and release profile

- AMD Ryzen 9 9950X3D, Linux 6.17.0-14-generic, SMT siblings excluded.
- CPU 0 is a physical core in the 96 MiB V-cache L3 domain; CPU 8 is a physical core in the
  32 MiB frequency L3 domain.
- `amd-pstate-epp` with the `performance` governor on both measured cores.
- GCC 13.3.0. Source-shaped gameplay TUs retain the bit-exact reference flags; audited host/API,
  observation, relocation, savestate, wire, generated relocation, and benchmark TUs use `-O3
  -march=native -mtune=native`. Section garbage collection remains enabled. Unsafe floating-point
  flags are rejected by `native-release`.

The accepted measurements are:

| Core/domain | Logical envs | Complete FPS | Step-only FPS | Resets | Digest |
|---|---:|---:|---:|---:|---|
| CPU 8 / 32 MiB frequency | 256 | 16,722 | 16,568 | 16 | `7f31cdd8104cfc62` |
| CPU 8 / 32 MiB frequency | 512 | 20,305 | 20,435 | 32 | `cf13aa808327311f` |
| CPU 0 / 96 MiB V-cache | 256 | 16,412 | 16,593 | 16 | `7f31cdd8104cfc62` |
| CPU 0 / 96 MiB V-cache | 512 | 20,202 | 20,304 | 32 | `cf13aa808327311f` |

CPU 8 is the current official single-core target. Production and step-only differences are within
roughly one percent at this sample duration; observation/terminal packing is not the dominant
current cost.

These are explicitly **sharded logical-batch measurements**, not resident public batches. Each
`MslCoreMatch` currently reserves a 32 MiB `MAP_32BIT` arena, so the low 2 GiB mapping window cannot
construct 256 or 512 resident matches. The benchmark reuses 16 resident public matches in explicit
logical shards and reports `mode=sharded`, `logical_matches`, and `resident_matches` separately.
This preserves the replay mix, output-ring traffic, resets, and complete-frame accounting, but does
not claim cache behavior equivalent to the future resident batch. Phase 8 must remove the low-address
and per-match arena constraints before a true 256/512 baseline can replace these numbers.

Hardware performance counters are unavailable on this host because
`/proc/sys/kernel/perf_event_paranoid` is 4 and the session lacks `CAP_PERFMON`; consequently Phase 7
does not invent cycles/environment-frame or cache-miss estimates from nominal clocks. After counter
access is enabled, rerun the same pinned commands under `perf stat` for cycles, instructions,
branches, and cache events and append the results here.

### Compiler candidates

| Candidate | Result | Decision |
|---|---|---|
| Global `-O3 -march=native` plus LTO | Cross-TU decomp type UB caused a native crash; LTO also exposed incompatible declarations | Rejected; no flags retained |
| Global `-O3 -march=native`, no LTO | First trig drift was fixed by blocking builtin trig substitution, but the full suite still had 26 severe/control-flow failures | Rejected; no partial workaround retained |
| Global `-O1` | Crashed on a long aggregate replay | Rejected |
| O3 host allowlist on a changed global base profile | Ten aggregate failures remained | Rejected |
| Reference gameplay profile plus audited O3 host/API allowlist | Full 153-case gate: 63 exact pass, 90 existing classified, zero XPASS/fail/error, 1,415,476 compared frames | Retained baseline |

The conservative allowlist is intentional evidence, not the optimization ceiling. Phase 8 should
close optimizer-sensitive source ownership and type boundaries one bounded owner at a time; each
allowlist expansion needs the full output-lock gate.

### Runtime and reachability census

Run:

```bash
make -f src/melee_core/Makefile runtime-census
```

The census initializes all eight currently admitted characters over all six supported legal stages,
then runs a Peach/FD item scenario. Its high-water and allocation-lock results are:

- `MslCoreMatch`: 781,768 bytes; embedded relocation registry: 524,296 bytes; embedded memory
  allocation ledger: 196,648 bytes; source match state: 34,808 bytes.
- Per-match arena: 32 MiB reserved, 17,122,944 bytes maximum touched, 3,090 initial allocations,
  and 11,435 relocation records in the measured maximum configuration.
- A representative active match used 16,565,472 arena bytes and 2,321 allocations both before and
  after 306 gameplay steps, confirming no gameplay-path arena growth.
- Representative savestate: 17,347,368 bytes = 128-byte header + 781,768-byte match value +
  16,565,472 touched arena bytes.
- Shared `GameData`: 568,296-byte value, 64 MiB archive arena with 42,117,736 bytes used, and a
  256 MiB native-DAT address arena with 51,030,048 bytes used. These are shared rather than copied
  into each match.
- Representative scheduler: 15 live/peak GObjs and 53 live/peak GObjProcs. The general object
  pools still preallocate 256 or more entries per type; reached fighter and item pools contain two
  fighters and one Peach article in this scenario.
- At the measured high-water, the current struct plus arena costs about 4.27 GiB touched / 8.19 GiB
  reserved for 256 matches and 8.54 GiB touched / 16.37 GiB reserved for 512, before observation
  history. The 32-bit mapping window fails first.

The release build compiles 349 objects (318 source-shaped gameplay C files plus local/generated
runtime code). Section garbage collection reduces the replay benchmark to about 1.82 MiB text,
99 KiB initialized data, and 11 KiB BSS, but still retains roughly 6,895 text and 1,710 data symbols.
Large retained owners include fighter motion-state tables, item tables, fighter collision/update,
camera, stage, collision, and common animation code. Compilation alone is not treated as runtime
cost.

The reached scheduler callback set includes the source fighter phase chain, item phase chain,
`Fighter_ProcessHit_8006D1EC`, `mpLib_800587FC`, the camera callback, ten
`headless_ground_anim_proc` instances on FD, and FD background rotation. The scalar step also always
builds the 1,022-byte forensic `MslCoreCompare`, walks visible fighter/item JObj trees to publish lazy
render matrices, and publishes camera visibility. Some matrix publication is observed by later
hit/hurt capsules and cannot simply be deleted; the forensic row and genuinely presentation-only
scheduled work are prime Phase 8 reachability/cost boundaries.

### Phase 8 boundary implied by the evidence

1. Remove the 32-bit pointer/address assumption and replace the 32 MiB arena with bounded measured
   storage. Compact or externalize the 524 KiB relocation registry and 197 KiB allocation ledger;
   size fixed pools from observed/source-backed maxima rather than uniform reserves.
2. Stop producing the forensic compare row on the production observation path. Keep validation as
   an explicit projection and make the compact observation read canonical state directly.
3. Separate gameplay-observed lazy transform publication from DObj/GX/presentation traversal, then
   remove scheduled callbacks proven irrelevant to headless gameplay. Preserve source ordering and
   every transform actually consumed by collision/hit/hurt logic.
4. Once hundreds of matches are genuinely resident, replace this sharded baseline with resident
   256/512 measurements, add 4,096/16,384 memory gates, and profile the canonical batch by source
   scheduler phase before choosing SoA/AoSoA fields.

## Phase 8 memory substrate — 2026-07-16

The first retained Phase 8 packet removed the native Match graph from Linux's low-2-GiB mapping
window. Raw archive construction remains low-address because the imported `lbFile` APIs still carry
source-width words; mutable Match allocations now use ordinary native mappings. The uniform
256-object free-list floor was replaced with a bounded byte budget plus the existing explicit
AObj/FObj/ID reserves. The census covers singles and doubles for all eight admitted characters on
all six legal stages.

The large relocation registry and allocation ledger are no longer embedded in every Match. A
thread-local construction registry emits an exactly sized runtime relocation image plus 512 spare
records for the source class allocator's bounded runtime splitting. Its address index is the next
power of two above twice that record capacity. This remains mutable so source pool alloc/free
transitions can change an object's exact relocation type without scanning raw/stale words. Match
allocation provenance similarly uses thread-local construction scratch and is discarded when the
bump arena seals; immutable shared GameData retains its own ledger. These are representation-only
changes: arbitrary-index save/restore, cross-match copy, runtime pool reuse, and the no-allocation
step contract remain covered by the native smokes.

Current census results:

- `MslCoreMatch`: 61,224 bytes, down from 781,768; `MslMemoryContext`: 56 bytes, down from
  196,648. The maximum configuration's exactly sized relocation image is 165,856 bytes, versus the
  former 524,296-byte embedded registry. Shared `MslCoreGameData` also fell from 568,296 to 371,728
  bytes.
- Match arena: 3 MiB virtual reserve, down from 32 MiB. The supported singles/doubles maximum is
  2,743,864 bytes used with 3,312 construction allocations and 10,482 live relocation records. The
  maximum configuration is four-player frozen Stadium with Sheik.
- The representative Peach/FD arena is 1,927,968 bytes and remains exactly stable over gameplay.
  Its savestate is 1,989,320 bytes = 128-byte header + 61,224-byte Match value + 1,927,968-byte
  arena, versus the 17,347,368-byte Phase 7 representative.
- Maximum source object-pool residency across the census is 1,374,804 bytes. Large cold object
  families use the public 15-item capacity while replay-proven small animation, identity,
  scheduler, matrix/vector, and Sheik-chain pools retain explicit bounded reserves. The largest
  remaining pools are the 32,768-byte fighter scratch family (589,824 bytes), fighters (173,400
  bytes), FObjs (94,784 bytes), items (86,656 bytes), and Sheik-chain links (80,936 bytes).
- At the measured maximum, runtime state plus a 128-frame observation ring is about 0.70 GiB for
  256, 1.40 GiB for 512, 11.19 GiB for 4,096, and 44.75 GiB for 16,384 environments. The
  corresponding 3 MiB arena virtual reservations plus Match values and rings are 0.79, 1.59,
  12.72, and 50.88 GiB. These deliberately conservative figures use the largest reached supported
  configuration for every environment; the mixed-domain resident measurement below is materially
  smaller.

Correctness gate: the complete 153-replay native suite retained 63 exact passes, 90 existing exact
classifications, zero XPASS/fail/error, and all full-output locks over 1,415,476 compared frames.
No classification or output lock changed.

### Production projection boundary

The scalar step no longer builds the complete 1,022-byte replay-forensic row on every production
frame. Policy observations now read fighter, stage, item, and terminal owners directly. A compact
per-match snapshot retains only post-gameplay/pre-render values that the following headless render
publication can mutate (offline DeadUp position and camera visibility), so lazy forensic output
continues to represent Slippi's recorder boundary exactly. The benchmark setup also verifies a
bounded original-versus-restored replay continuation after destroying the source Match; this makes
destination-independent savestate relocation a prerequisite for every accepted timing run.

Slippi's four generic item-variable bytes sometimes expose presentation pointers, padding, or
fixed-pool residue rather than gameplay. The existing source-backed validation ownership policy is
now shared with production projection. Forensic rows remain raw; policy observations and viewer
output zero only the lanes already excluded from exact output fingerprints. This removed native
address dependence from production digests without adding or widening a classification.

On CPU 8 with the still-sharded 256-logical/16-resident workload, the retained result is 16,997
complete FPS and 17,307 step-only FPS with digest `7cdab0237948d61f`. A repeat produced the same
digest. This is only a small improvement over the 16,722 FPS Phase 7 baseline, as expected: the
forensic projection was measurable but not the dominant cost. The complete 153-replay gate remains
63 exact passes, 90 existing exact classifications, zero XPASS/fail/error, and unchanged output
locks over 1,415,476 compared frames.

### Resident batches and tiled source scheduling

One contiguous virtual arena mapping now backs every Match in a batch. Match roots remain compact
separate values and each memory context binds one fixed 3 MiB slice, avoiding thousands of mapping
syscalls without first-touching unused arena capacity. Batch reset constructs each distinct packed
configuration once and relocates that canonical image into same-config environments. Newly split
HSD class-allocator free-list tails are registered as intrusive pointer owners at their source
creation sites; this closes arbitrary-index copy/restore for every allocator subobject without
raw-word pointer scanning.

The hosted source scheduler is decomposed at its existing priority and callback invocation
boundaries. The canonical batch path prepares a two-Match cache tile, preserves each Match's exact
priority/proc order, groups neighboring runnable callbacks by the imported `on_invoke` function
pointer, and finishes both Matches. Tile widths 1, 2, 4, and 8 were measured on the resident replay
workload; two was the best of that bounded sweep. This is Phase 9 execution substrate rather than a
current throughput win: the source-shaped per-callback context bindings and pointer-rich state
still dominate.

True resident CPU-8 results for 32,768 complete environment-frames are:

| Resident environments | Complete FPS | Step-only FPS | Digest |
|---:|---:|---:|---|
| 256 | 13,270 | 13,824 | `7579e5fc270dd660` |
| 512 | 15,881 | 17,090 | `0bd4fdd9cfdec765` |

The new `large-batch-smoke` gate creates one GameData and batch, resets a supported-domain mix,
physically touches a caller-owned 128-frame observation/terminal ring, saves Match 0, executes one
complete step/output pass over every environment, restores the snapshot into the last arbitrary
index, checks exact output equality, and destroys the batch. At 4,096 environments it measured:

```bash
# Safe routine smoke; defaults to 256 environments.
make -f src/melee_core/Makefile large-batch-smoke

# Explicit high-memory release gates.
make -f src/melee_core/Makefile large-batch-smoke LARGE_BATCH_MATCHES=4096
make -f src/melee_core/Makefile large-batch-smoke LARGE_BATCH_MATCHES=16384
```

- 12.55 GiB virtual and 7.97 GiB resident/private after reset;
- 13.04 GiB virtual and 8.46 GiB resident after touching the 0.49 GiB output ring;
- 0.49 GiB resident after destroying GameData and the batch while retaining the caller's ring;
- 0.218 s create, 9.140 s reset, and 0.302 s complete step/output (13,545 FPS).

The same complete gate at 16,384 environments measured 49.25 GiB virtual and 31.62 GiB resident
after reset, then 51.20 GiB virtual and 33.57 GiB resident after physically touching the 1.95 GiB
output ring. Create took 0.233 s, reset took 31.454 s, and one complete step/output pass took 1.198 s
(13,676 FPS). Destruction returned resident memory to the caller-owned 1.95 GiB ring. The measured
31.46 GiB initialized-state component agrees with the 4,096 slope and remains below the
conservative all-maximum 44.75 GiB touched bound. Because this materially pressures host memory and
exceeds the normal command-time budget, the 16,384 gate must remain an explicit supervised release
check rather than a routine background command.

The complete 153-replay native suite after scheduler cutover retained 63 exact passes, 90 existing
classifications, zero XPASS/fail/error, and 1,415,476 compared frames. Native and PPC smokes,
native/Wasm API parity, and the browser viewer smoke also remained green.

Rejected candidates are recorded rather than retained: transparent huge-page advice made the
256-environment reset about five times slower, and a file-backed copy-on-write template did not
reduce proportional/private memory because relocation necessarily dirtied the pointer-bearing
pages. Both experiments were removed.

## Phase 9 pre-SoA profiling and compiler closure — 2026-07-16

Hardware counters remain unavailable at `perf_event_paranoid=4`. An opt-in flat `gprof` build now
disables sampling through GameData/Match setup and samples only the complete production pass; the
normal benchmark binary and timed workload are unchanged. A true resident 256-environment sample
identified hosted matrix concatenation as the largest individual source owner (7.8%), followed by
strict square-root/trig helpers, JObj/FObj animation, GObj next-owner dispatch, collision/vector
math, and camera/context accessors. This is sufficient owner evidence for the bounded compiler
packet; instruction/cache counters remain deferred until the host permits `perf`.

The first apparent candidate, adding `runtime/context.c` to the strict `-O3` allowlist, was rejected.
Its initial noisy measurements looked positive and the full release replay gate passed, but an
adjacent baseline/candidate alternation measured 14,552 versus 12,905 complete FPS. Optimizing the
small accessor bodies does not remove the profiled cross-TU call boundary, so the allowlist change
was removed.

The first retained candidate adds only `platform/dolphin_mtx.c` to the existing strict `-O3
-march=native -mtune=native` allowlist. Its hosted SDK matrix functions already name the retail
paired-single `fmaf` boundaries explicitly, and the release profile continues to reject fast-math
and unsafe contraction. Adjacent true-resident CPU-8 measurements were:

| Environments | Baseline complete FPS | Candidate complete FPS | Change | Digest |
|---:|---:|---:|---:|---|
| 256 | 13,581 | 16,337 | +20.3% | `7579e5fc270dd660` |
| 512 | 16,273 | 19,663 | +20.8% | `0bd4fdd9cfdec765` |

The candidate's second 256 run reached 16,848 complete FPS with the same digest. The complete
release-binary 153-replay gate retained 63 exact passes, 90 existing classifications, zero
XPASS/fail/error, and all output locks over 1,415,476 frames. The release validation wall time fell
from 8.303 seconds for the rejected context candidate to 6.752 seconds for the retained matrix
candidate under the same 16-worker command; this is supporting evidence rather than the production
throughput metric.

Desktop activity on the documented CPU-8 target made short wall-clock A/B samples vary by more
than the candidate effects. The benchmark now reports `CLOCK_THREAD_CPUTIME_ID` seconds and FPS
beside the existing monotonic wall-clock figures. This adds two clock reads per timed pass, does not
change the production loop or digest, and distinguishes scheduler preemption from actual execution
time. It does not normalize boost-clock variation, so experimental owner sweeps use a 64-tick
warmup and reversed adjacent ordering on an otherwise idle physical core. The published production
contract and ordinary targets retain their eight-tick warmup and wall FPS as the primary metric.

Three more whole-TU compiler candidates were bounded and removed:

- `platform/native_dat.c` affected reset-owned archive translation but alternated from about 5%
  ahead to 3% behind at 512 environments while the supposedly unaffected step-only pass moved by
  similar amounts. This was boost/load noise, not a retained runtime win.
- `sysdolphin/baselib/gobj.c` was 4.6% slower for complete frames and 4.7% slower for step-only on
  the clean adjacent 512-environment pair. The scheduler translation remains on its strict source
  profile.
- `runtime/context.c` remains rejected as described above; its hot accessor cost requires an
  ownership/call-boundary change rather than a higher optimization level on the callee TU.

The second retained candidate adds `sysdolphin/baselib/fobj.c` to the strict `-O3 -march=native
-mtune=native` allowlist. This is the source-owned FObj interpreter identified by the production
profile. No fast-math, unsafe contraction, LTO, or source rewrite is involved. Stabilized,
reverse-ordered true-resident CPU-0 comparisons against the matrix-only binary were:

| Environments | Matrix-only complete / step FPS | FObj complete / step FPS | Complete / step change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 20,221 / 21,027 | 21,777 / 22,448 | +7.7% / +6.8% | `7579e5fc270dd660` |
| 512 | 28,286 / 29,127 | 29,026 / 29,969 | +2.6% / +2.9% | `0bd4fdd9cfdec765` |

The complete release-binary gate again retained 63 exact passes, 90 existing classifications, zero
XPASS/fail/error, and all output locks across 1,415,476 frames. Its 16-worker wall time was 6.229
seconds. Release-profile native API/data/scheduler/batch smokes also passed.

The flat `gprof` sample is useful for locating substantial source owners, but it is not trustworthy
for ranking tiny leaf accessors: `-pg` inserts per-function instrumentation and consequently
overstates their cost. A direct thread-local source/camera context experiment, motivated by those
samples, proved this in the production workload. It was 4.3% slower for complete frames and 2.5%
slower for step-only at 512 environments and was removed in full; the promoted context API and
binding representation remain unchanged.

The third retained candidate adds `sysdolphin/baselib/jobj.c` to the same strict host-optimization
allowlist. JObj owns source model hierarchy traversal, dirty propagation, and animation matrix
setup. This is a compiler-only change to that complete owner TU, with the paired-single/FMA matrix
boundary still owned by the separately audited `dolphin_mtx.c`. Stabilized true-resident CPU-0
comparisons against the FObj baseline were:

| Environments | FObj complete / step FPS | JObj complete / step FPS | Complete / step change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 21,433 / 23,363 | 25,278 / 24,618 | +17.9% / +5.4% | `7579e5fc270dd660` |
| 512 | 28,567 / 27,923 | 28,947 / 30,260 | +1.3% / +8.4% | `0bd4fdd9cfdec765` |

The complete release-binary gate retained the same 63 exact passes, 90 existing classifications,
zero XPASS/fail/error, and all output locks across 1,415,476 frames. Its 16-worker wall time was
5.694 seconds, and release-profile native smokes passed.

The fourth retained candidate adds the complete source `melee/lb/lbvector.c` owner to the strict
allowlist. This TU owns shared vector length, normalization, angle, projection, and rotation
helpers used across gameplay and collision. The release profile still preserves source expression
order with `-ffp-contract=off`, disables strict-alias assumptions, and forbids fast-math; this does
not substitute approximate square roots or trigonometry. Stabilized true-resident CPU-0
comparisons against the JObj baseline were:

| Environments | JObj complete / step FPS | lbVector complete / step FPS | Complete / step change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 25,403 / 24,931 | 28,573 / 29,003 | +12.5% / +16.3% | `7579e5fc270dd660` |
| 512 | 31,931 / 27,666 | 36,222 / 37,948 | +13.4% / +37.2% | `0bd4fdd9cfdec765` |

The complete release-binary gate retained 63 exact passes, 90 existing classifications, zero
XPASS/fail/error, and all output locks across 1,415,476 frames. Its 16-worker wall time was 4.998
seconds, and release-profile native smokes passed.

### Bounded packet result

The retained pre-SoA release profile optimizes four complete, audited owners: hosted SDK matrix
math, FObj interpretation, JObj hierarchy/matrix traversal, and shared lbVector math. All other
experiments were removed. With the ordinary eight-tick warmup, the final true-resident results are:

| Core/domain | Environments | Complete FPS | Step-only FPS | Digest |
|---|---:|---:|---:|---:|
| CPU 0 / 96 MiB V-cache | 256 | 27,831 | 28,584 | `7579e5fc270dd660` |
| CPU 0 / 96 MiB V-cache | 512 | 38,086 | 38,412 | `0bd4fdd9cfdec765` |
| CPU 8 / 32 MiB frequency | 256 | 21,227 | 19,957 | `7579e5fc270dd660` |
| CPU 8 / 32 MiB frequency | 512 | 24,026 | 23,356 | `0bd4fdd9cfdec765` |

Relative to the Phase 8 true-resident CPU-8 baseline, complete throughput is up 60.0% at 256 and
51.3% at 512. True residency reverses the earlier sharded cache-domain result: CPU 0 is now the
faster repeatable local core and becomes the primary target for the following hot/cold and
SoA/AoSoA packet. The best current result is 38,086 complete FPS, so the 500k goal still requires
the planned representation and cross-environment execution work rather than more blanket compiler
flags.

Final preservation evidence comprises the complete release-binary 153-replay gate; native and PPC
API/data/model/map/scheduler/batch smokes; native/Wasm state, viewer, and savestate parity; browser
viewer smoke; the runtime allocation lock; the ordinary 256-environment large-batch lifecycle and
arbitrary-index restore gate; formatting; and the focused benchmark unit test. The compiler-only
packet did not rerun the supervised 4,096/16,384 high-memory lifecycle measurements: their storage,
reset, relocation, observation-ring, and API code is unchanged, while the safe 256 gate exercises
the same contract. The repository-wide legacy benchmark report was refreshed as required, but it
is not used as evidence for this new core.

## Phase 9 headless dynamic-owner cut — 2026-07-16

An opt-in cycle profile now measures only the timed production pass, after GameData construction,
Match reset, warmup, and savestate restore. The same boundary is used by the opt-in gprof and
Callgrind controls, so setup no longer contaminates owner rankings. No profiling code is present in
ordinary release objects. A one-Match late-game/reset probe was added to the owner analysis because
the ordinary 32,768-frame resident benchmark executes only 128 ticks at 256 Matches and 64 ticks at
512 Matches. Apart from its one deliberately late savestate lane, that benchmark therefore
overweights the opening. Its output/digest lock remains useful for adjacent performance checks, but
the next benchmark-contract packet must stage representative aggregate gameplay states before the
500k result can be accepted.

The late-game probe made the first hot/cold owner boundary unambiguous. The priority-16 fighter
dynamics owner consumed about half of timed scheduler cycles for the selected Peach-heavy state.
Peach, Zelda, and Marth construct source dynamic JObj chains for hair, cloth, and cape motion, but
none of those chains owns a supported-domain hurt capsule. The raw-data audit additionally found no
supported move-script HitCapsule, normal guard sphere, or special shield/reflector sphere on a
rejected chain. Fox's tail chain is retained because part 18 owns both a hurt capsule and move
hitboxes; Puff's body chain is retained because parts 7 and 8 own hurt capsules. The decision is
made by live source JObj/hurt-capsule ownership rather than character id.

Hosted fighter construction now returns presentation-only dynamics descriptors to the existing
fixed source pool after `ftColl_8007B320` has published every hurt-capsule bone. The source
`dynamics_num` and descriptor slots remain intact, so movescript bone-physics callbacks and
savestate layout do not gain a second representation. The priority-16 source loop sees empty cold
descriptors; collision-owned Fox/Puff chains continue through the exact `lb_8001044C` solver. The
complete `lbspdisplay.c` owner is compiled under the already locked strict `-O3 -march=native`
profile; adding `ftdynamics.c` itself to that profile was neutral/negative and was removed.

Retained CPU-0 results, using the unchanged ordinary benchmark workload and digests, are:

| Environments | Pre-cut complete FPS | Retained complete FPS | Change | Digest |
|---:|---:|---:|---:|---|
| 256 | 27,831 | 31,139 | +11.9% | `7579e5fc270dd660` |
| 512 | 38,086 | 46,947 | +23.3% | `0bd4fdd9cfdec765` |

The one-Match late-game/reset probe improved from about 21,100 to 39,200 complete FPS with digest
`c3162f853421cb0b` and three ordinary resets over 1,024 frames. Removing strict optimization from
the still-live `lbspdisplay.c` owner reduced the 512 result to 40,299 FPS, so that compiler boundary
is retained with the source-owner cut rather than as unused scaffolding.

Correctness evidence is the exact release binary used above: the complete 153-replay native gate
retained 63 exact passes, 90 existing exact classifications, zero XPASS/fail/error, all output
locks, and 1,415,476 compared frames. The cut adds no runtime allocation, new classification,
character-id dispatch, or alternate gameplay state.

## Phase 9 representative benchmark contract — 2026-07-16

The official resident workload now assigns all 153 aggregate controller streams cyclically while
staging a bounded bank of 32 free-running source states before timing. Seed selection first covers
every stage and character present in the manifest, then fills the bank with evenly spaced aggregate
cases. Those matches are advanced normally from match start to deterministic quarter-, half-, and
three-quarter-game offsets; one seed begins 32 frames before replay end so even the 64-tick
512-environment sample includes ordinary masked reset/restart. No replay row state, validation RNG,
teacher forcing, or serialized generated artifact enters staging. Unseeded aggregate cases begin at
ordinary match start, so the timed owner mix deliberately contains both openings and mature match
states. The untimed bank contains 154,102 free-running environment-frames and keeps each complete
command below ten seconds on the current baseline.

This workload exposed a real savestate gap before producing a number. Source
`zeroStageInfoArrays` initializes 64 ground-object slots, and Dream Land reads slot 7, but the decomp
declaration exposed only four pointers followed by opaque padding. It happened to preserve the
retail 32-bit byte extent, but hid the remaining live pointers from generated native/Wasm relocation.
`StageInfo.x180` now expresses the source-used 64-pointer owner array. A native batch smoke saves a
live Dream Land match, restores it into an independent environment, advances the Whispy/ground
owners, and requires the same continuation. The full 153-replay native gate remains 63 exact passes,
90 existing exact classifications, and zero XPASS/fail/error; native and Wasm savestate smokes pass.

The representative CPU-0 baseline replacing the opening-heavy Phase 9 numbers is:

| Environments | Complete FPS | Step-only FPS | Resets | Digest |
|---:|---:|---:|---:|---|
| 256 | 32,848 | 32,944 | 2 | `bdc54107c51fa3d7` |
| 512 | 41,567 | 39,096 | 4 | `3fb5823d90657775` |

The seed-coverage locks are stage mask `000000019000010c` and character mask
`00000000004c8286`. These results are the comparison point for the scalar hot/cold storage cut;
older ordinary-workload digests remain useful only for adjacent forensic experiments.

## Phase 9 consecutive source-owner runs — 2026-07-16

The canonical tiled scheduler previously rebound the complete Match TLS owner bundle between every
individual GObj process, even when a Match's next two or four fighter processes had the same source
callback. Cross-Match grouping therefore alternated bindings after each fighter. Matches share only
immutable `GameData`; mutable callback, scheduler, arena, player, stage, and RNG state is explicitly
Match-owned. The scheduler now consumes each consecutive same-callback run in source process order
under one Match binding, then returns the already-discovered next owner to the tile dispatcher.
Lane order and every per-Match GObj order remain deterministic.

This is a measured execution change rather than dormant scheduler substrate. On the representative
CPU-0 workload, 256 environments improved from 32,848/32,944 complete/step-only FPS to
34,411/34,819 (+4.8%/+5.7%) with digest `bdc54107c51fa3d7`. A directly adjacent 512 baseline/candidate
comparison improved from 42,261/42,445 to a repeated 42,995/43,142 (+1.7%/+1.6%) with digest
`3fb5823d90657775`; a first candidate repeat was 42,164/42,985. The complete native gate retained 63
exact passes, 90 existing exact classifications, and zero XPASS/fail/error over 1,415,476 frames.

## Phase 9 extracted gameplay-pose closure — 2026-07-16

The first scalar hot/cold representation cut now consumes the existing generated `MSLPART1`
artifact in immutable `GameData`. Its part set is extracted from animation skeletons and the full
gameplay consumer closure: hit and hurt capsules, ECB owners, capture/throw joints, article anchors,
special descriptors, and every required ancestor. Hosted fighter construction marks the complete
main and interpolation JObj trees cold, promotes the extracted live joints and their observed
ancestors, removes already-created animation state from the remaining cold subtrees, and prevents
later Figa/JObj attachment, interpretation, and render-matrix publication there. A defensive
bottom-up promotion means incomplete metadata can only retain extra source work; it cannot hide a
live descendant. A character without generated metadata uses the complete source graph unchanged,
which is the current Peach/Puff path and the future-character admission fallback.

This is the first retained data-backed representation cut, not a replay-specific omission or a
character-id gameplay branch. `MSLPART1` is generated once by the established ISO extraction
pipeline and remains outside Git. Its compact masks are included in the immutable game-data
fingerprint. The retail/PPC graph is unchanged, and native/Wasm gameplay remains output-equivalent;
Wasm currently takes the complete-graph fallback until the generated compact pose assets are added
to its preload contract.

On the representative CPU-0 workload, stabilized results are:

| Environments | Previous complete/step FPS | Retained complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---|
| 256 | 34,411 / 34,819 | 40,384 / 40,925 | +17.4% | `bdc54107c51fa3d7` |
| 512 | 42,995 / 43,142 | 51,671 / 52,412 | +20.2% | `3fb5823d90657775` |

The full native suite retains 63 exact passes, 90 existing exact classifications, zero
XPASS/fail/error, and all 1,415,476 output locks. Native and Wasm smokes pass. The next pose packet
should keep this extracted closure as the immutable admission table, but replace the surviving
pointer-linked FObj/JObj traversal with a compact per-motion pose program: direct cached matrices
for proven exact integer/unblended rows and a compact track/blend/dynamics evaluator for the cases
that need live state. This is the first credible path to a step-change beyond repeated source-loop
micro-optimization.

## Phase 9 animation-owner compiler closure — 2026-07-16

The first compact-pose experiment was intentionally rejected. It memory-mapped the existing
ISO-extracted `SSANIM01`/`SSANIML1`/`SSANIMT1` artifacts and replaced source FObj decoding on
zero-start, unit-rate, unblended integer frames with indexed values and source-derived per-frame
publication masks. The complete Fox control replay remained exact, but the representative 256
workload improved only from 40,384 to about 41,500 FPS (+2.8%). It retained the pointer JObj walk,
RObj publication, dirty propagation, and matrix construction, while adding roughly one dense pose
cache per supported character. That cost/complexity ratio is not a useful precursor to the later
direct-matrix or SoA design, so the experiment was removed completely.

The much smaller retained closure compiles the complete source `melee/ft/ftanim.c` owner under the
existing strict `-O3 -march=native -mtune=native` profile. FObj, JObj, hosted matrix, and vector
owners were already audited at that boundary; this removes the remaining unoptimized tree/control
overhead without introducing a second animation representation. An adjacent CPU-0 comparison at
256 environments measured 40,508/41,040 versus 42,385/42,973 complete/step-only FPS (+4.6%/+4.7%)
with digest `bdc54107c51fa3d7`. The 512 result is 54,159/55,650 versus the prior retained
51,671/52,412 (+4.8%/+6.2%) with digest `3fb5823d90657775`.

Adding the much broader `fighter.c` TU changed the 256 production digest to
`d4376876c81b0602` and was removed before validation. The retained `ftanim.c` release binary passed
the complete 153-replay gate: 63 exact passes, 90 existing classifications, zero XPASS/fail/error,
and all 1,415,476 output locks in 3.807 seconds. The next representation packet targets matrix/pose
publication or map/collision state directly; further small cache or whole-TU sweeps are not the
implementation queue.

## Phase 9 whole-owner collision and bonus-stat closure — 2026-07-16

The representative phase profile moved the optimization unit back to complete frame owners. Of
the timed source-callback work, `Fighter_procMap` owned about 29%, the main fighter action owner
18%, `Fighter_ProcessHit` 11%, the gameplay camera 7%, and fighter animation 6%. A proposed
per-line endpoint cache and exact-sized mp pools were removed after adjacent measurements were
neutral or slower. A second direct-pose experiment flattened eligible integer-pose matrices but
improved the complete 256 workload by only about 1% while requiring large shared pose files; it
was likewise removed completely.

The retained collision change places the complete source `melee/mp/mplib.c` owner under the
existing strict `-O3 -march=native -mtune=native` profile. This is the 7k-line directional stage
query, projection, remap, and line-traversal owner identified by both the phase and flat profiles;
the floating-point contract remains strict. Extending that profile to `mpcoll.c` changed a
Peach/Puff item-position output lock and was removed. Extending it to `ftcoll.c` changed the
production digest and was also removed.

The retained whole-system cull removes `pl_8003FAA8` from the hosted per-frame fighter schedule.
That function and its four large children collect movement, action, magnifier, item-hold, and bonus
statistics for post-match result scoring. They do not publish gameplay state. Source player
position and facing publication remain in their original priority-22 owner, and retail/PPC builds
retain the complete statistics path.

On the representative CPU-0 workload, the combined retained result is:

| Environments | Previous complete/step FPS | Retained complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 42,385 / 42,973 | 47,474 / 48,382 | +12.0% | `bdc54107c51fa3d7` |
| 512 | 54,159 / 55,650 | 61,362 / 62,381 | +13.3% | `3fb5823d90657775` |

The exact release binary passes all 153 replays: 63 exact passes, 90 existing classifications,
zero XPASS/fail/error, and all 1,415,476 output locks. This packet is still scalar cleanup, not the
500k architecture. The next large unit is the complete pointer-heavy map-collision kernel:
compact extracted stage-line arrays plus cross-environment collision execution, preserving the
source admission/publication order and dynamic-platform fallback. Presentation camera state is the
next whole-owner cull candidate; leaf caches and isolated arithmetic rewrites are not the queue.

## Phase 9 large replicated-state cull — 2026-07-16

The optimization unit is now explicitly split between state residency and frame time. Native
fighter animation translation already parses immutable `PlFxAJ.dat` subarchives directly, so the
two retail 32 KiB ARAM/file scratch buffers allocated for every fighter had no supported hosted
consumer. The native owner is removed completely; the PPC path and Nana's unsupported scratch-copy
path remain source-shaped. Hosted `mpLibLoad` also sizes its dense `CollVtx`, `CollLine`, and
`CollJoint` arrays from the selected stage's DAT counts instead of duplicating retail's maximum-disc-
stage byte ceilings in every Match. Final Destination publishes one collision epoch without ten
presentation-only stage-model animation walks.

The ordinary singles arena/savestate payload falls from 1,927,968 to 1,656,588 bytes, a 271,380-byte
(14.1%) reduction. The reached four-player maximum falls from 2,743,864 to 2,080,864 bytes, a
663,000-byte (24.2%) reduction; the removed fighter scratch pool alone eliminates 589,824 bytes at
that census maximum. The selected FD collision graph now owns 320 bytes of `CollJoint` records and
544 bytes of `CollLine` records instead of the former 13,280/12,288-byte maxima.

This is intentionally not reported as a throughput win: the current representative workload still
shards through sixteen resident Matches, so those cold bytes were not in its timed working set. Its
digests remain `bdc54107c51fa3d7` and `3fb5823d90657775`, with adjacent CPU-0 results of 47,887/48,534
FPS at 256 and 61,534/62,955 FPS at 512. All 153 replay locks remain unchanged (63 exact passes, 90
existing exact classifications, zero XPASS/fail/error), and native smoke/save-restore gates pass.
This cut is retained because true 256/512 residency and the 4k–16k memory target cannot tolerate
replicated cold retail ceilings. It does not defer the required frame-time architecture: the next
throughput packet must replace a dominant complete owner with canonical cross-environment execution.

After this cut, the representative state-bank workload was re-run with every logical environment
actually resident. It reaches 44,395/44,979 complete/step FPS at 256 and 55,248/56,192 at 512 on
CPU 0 with the same digests. The standard benchmark now defaults `resident_matches` to
`BENCHMARK_MATCHES`; a deliberately sharded forensic run must opt into a smaller value. These true-
resident results replace the 16-resident 47,887/61,534 figures as the acceptance baseline for all
following layout and execution packets. At 512 the remaining distance to 500k is 9.05x, so scalar
owner deletion alone is not a credible completion path.

## Phase 9 scalar residency and renderer-dispatch closure — 2026-07-16

The generic two-Match callback interleaver was a net cost before any owner had a real batch kernel.
It repeatedly evicted the current Match and republished the complete hosted context merely to call
the same scalar source owner in its neighbor. The canonical production path now keeps one Match
resident for its complete source `HSD_GObj_80390CFC` scheduler. It enters that scheduler directly
after prepare instead of re-entering match-binding wrappers at every priority/proc seam. Future
cross-environment kernels must cut in at explicit complete-owner boundaries; they must not restore
generic scalar callback interleaving as batching theater.

The same closure removes the hosted `DObj/MObj/PObj` animation dispatch from `HSD_JObjAnim` and
avoids entering generic AObj/RObj dispatchers for null owners. DObj is renderable geometry/material
state; fighter pose, constraints, hit/hurt primitives, ECBs, and attachments remain owned by the
retained JObj/AObj/RObj graph. This is an owner-level renderer exclusion, not a gameplay-joint
exclusion.

On the true-resident CPU-0 workload, 256 environments improve from 44,395/44,979 to
46,739/47,434 complete/step-only FPS. Two adjacent 512 runs reach 59,493/60,681 and
59,962/61,179 versus the 55,248/56,192 baseline, a 7.7–8.5% complete improvement. Digests remain
`bdc54107c51fa3d7` and `3fb5823d90657775`. The complete release gate remains 63 exact passes, 90
existing exact classifications, zero XPASS/fail/error, and all 1,415,476 output locks.

This closes generic scheduler/cache and null-dispatch work; it does not materially change the
500k plan. At 59,962 FPS the remaining gap is still 8.34x. Linked but unreachable translation units
are already removed by function/data sections and linker GC, so binary-source pruning is a build-
size concern unless an owner is actually scheduled or its state is touched. The remaining phase
therefore replaces complete live representations: compact fighter pose programs, compact
stage-collision topology/overlays, and cross-environment physics/combat/collision execution.

## Phase 9 construction-time fighter-pose compaction — 2026-07-16

The earlier MSLPART1 packet stopped runtime animation/publication below cold pose roots but still
constructed both complete source JObj trees and their renderer geometry for every fighter. Hosted
fighter construction now filters the main and interpolation descriptors before allocation. It
retains the extracted ancestor-closed gameplay joints, AObj/RObj constraints, source part/depth
indices, and one detached sentinel for direct source operations on omitted parts; it never creates
the omitted JObjs or any DObj/MObj/PObj graph. The source FigaTree-to-FighterBone walks were kept in
their original index domain, with the one physical-tree walk explicitly skipping sentinel entries.

The first exactness gate exposed a real extraction-contract hole rather than a need for replay
exceptions. Direct Peach/Puff extraction did not materialize `data/hurtcaps/<character>.json`, so
Puff BODY bones 12 and 26 were absent from the compact closure and back-air contact changed. Direct
animation extraction now reads the same ISO-backed `ftHurtboxInit` owner when that intermediate is
absent. Puff retains 34 of 50 physical nodes and the complete 153-replay output lock returns green.

The supported-domain runtime census changes as follows:

- ordinary singles arena/savestate payload: 1,656,588 to 1,593,188 bytes (-63,400, -3.8%);
- reached four-player maximum arena: 2,080,864 to 1,955,308 bytes (-125,556, -6.0%);
- census HSD_JOBJ relocation storage: 1,546 records / 284,464 bytes to 1,130 / 207,920
  (-416 records, -76,544 bytes, -26.9%);
- ordinary active 192-byte source-class objects: 569 to 473 (-16.9%).

True-resident CPU-0 throughput retains a smaller but repeatable frame-time improvement:

| Environments | Previous complete/step FPS | Retained complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 46,739 / 47,434 | 48,516 / 48,429 | +3.8% | `bdc54107c51fa3d7` |
| 512 | 59,962 / 61,179 | 61,871 / 63,270 | +3.2% | `3fb5823d90657775` |

The release gate remains 63 exact passes, 90 unchanged exact classifications, zero
XPASS/fail/error, and all 1,415,476 output locks. This packet is retained primarily because it
deletes a large live representation and improves per-environment memory; it does not pretend that a
3-4% scalar gain closes the remaining 8x throughput gap. The next unit remains compact collision
topology/overlay state followed by complete-owner cross-environment execution.

## Phase 9 reached dead-owner deletion — 2026-07-16

The compact hosted fighter graph exposed a complete stale runtime owner: `msl_core_match_step_finish`
still walked every fighter JObj tree once for each of retail's OPA, XLU, and TEXEDGE display passes.
Hosted fighter construction now creates no DObj/MObj/PObj graph, and the source display path only
publishes a JObj matrix after reaching a DObj in the selected pass. All three fighter traversals are
therefore empty and are deleted; item display publication remains source-shaped because item graphs
still own gameplay-observed matrices.

Two adjacent dead consumers are removed at the same source boundary. Slippi's
`FreezeDeadUpFallPhysics` capability eliminates the sole gameplay consumer of the separately
smoothed DeadUp render-camera transform, so hosted standard camera updates no longer run that
duplicate bounds and smoothing pipeline. The camera result-screen running average is also omitted
because its sole linked consumer is already excluded bonus-stat output. Finally, hosted matches
construct every participant as a human controller, so hurt-capsule publication retains its exact
matrix work while omitting the following CPU-AI-only targeting-box reduction.

On the true-resident CPU-0 workload, the retained results are:

| Environments | Previous complete/step FPS | Retained complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 48,516 / 48,429 | 50,833 / 52,488 | +4.8% | `bdc54107c51fa3d7` |
| 512 | 61,871 / 63,270 | 66,552 / 67,851 | +7.6% | `3fb5823d90657775` |

The complete native gate remains 63 exact passes, 90 unchanged exact classifications, zero
XPASS/fail/error, and all 1,415,476 output locks. Native API/save-restore and Wasm smokes pass. A
bounded attempt to phase-band the still-scalar map owner across sixteen Matches was exact but fell
to 60,535 complete FPS at 512 and was removed: an owner leaves the resident scheduler only together
with its compact cross-environment replacement. This packet closes reached presentation/CPU dead
work; further scalar leaf cleanup is not the 500k path.

## Phase 9 hosted context-owner cut — 2026-07-16

The imported gameplay code was still paying an out-of-line compatibility call whenever it read a
retail global that had been promoted into `GameData` or `Match`. A short gprof census found roughly
61.4 million `msl_core_source_match_state`, 27.8 million `msl_core_gobj_context`, 13.0 million
`msl_core_stage_info`, 5.1 million `msl_core_native_dat_context`, 4.5 million
`msl_core_source_game_data`, and 4.4 million `msl_core_try_active_match` calls in the sampled
resident workload, plus the same pattern for object pools, controller state, fighter registries,
items, and collision state. These calls did not implement gameplay; they repeatedly rediscovered
owner pointers that `msl_core_bind_match` had already published.

Native imported headers now read those already-bound thread-local owner pointers directly. The
compatibility accessors remain available at the runtime/API boundary, the PPC source-global path is
unchanged, and a Match is still bound once for its complete scalar scheduler. The strict release
allowlist also optimizes the complete `mpcoll.c` translation unit; isolated `fighter.c` and
`ftcoll.c` additions changed the production digest and were removed instead of broadening the
compiler exception.

True-resident CPU-0 results are:

| Environments | Previous complete/step FPS | Retained complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---:|
| 256 | 50,833 / 52,488 | 60,242 / 61,548 | +18.5% | `bdc54107c51fa3d7` |
| 512 | 66,552 / 67,851 | 78,472 / 80,974 | +17.9% | `3fb5823d90657775` |

All 153 native replays remain green: 63 exact passes, 90 unchanged classifications, zero
XPASS/fail/error, and all 1,415,476 output locks. Native API/save-restore, Wasm state/viewer digest
and cross-match restore, formatting, and the no-runtime-allocation smoke also pass.

Two bounded negative results define the next cut. Treating the left/right wall remap helpers as
dynamic-platform-only was exact on the representative digest and faster, but failed thirteen
Battlefield replays: the source owner also projects static wall vertices through the fighter's
swept ECB. That cull was removed. Optimizing `fighter.c`/`ftcoll.c` was likewise rejected because it
changed arithmetic output without a commensurate architectural gain. The retained packet removes a
large port-shim layer, but 78,472 FPS still leaves a 6.37x gap to 500k. The queue now returns to
complete live representations: compact collision topology/overlays and compact pose evaluation,
then cross-environment kernels over those canonical arrays. More isolated leaf tuning is closed.

## Phase 9 reached class-storage cut — 2026-07-16

Every hosted Match still called the source size-class allocator's bootstrap for all 32 generic
classes from 32 through 1,024 bytes. That policy created free slabs even when construction had
never produced an object of that size. The slabs were sealed into the Match arena and replicated
across every resident environment; linker garbage collection cannot remove runtime-owned storage.

Native bootstrap now reserves headroom only for classes proven reached by live construction state
or the hosted source-class high-water counters. It also stops constructing metadata entries merely
by iterating over empty class slots. The surviving JObj class retains 64 free objects because the
compact construction graph no longer donates renderer JObjs to the free list and Peach's transient
article graph crosses the former 32-object reserve. Allocation still uses the ordinary source free
list, and the sealed-arena failure remains the proof against an omitted runtime class.

The supported-domain runtime census changes as follows:

- ordinary singles arena/savestate payload: 1,593,188 to 907,420 bytes (-685,768, -43.0%);
- reached four-player maximum arena: 1,955,308 to 1,269,516 bytes (-685,792, -35.1%);
- replicated `HSD_MEMORY_ENTRY` records: 32 / 1,024 bytes to 6 / 192 bytes;
- reserved source-class slabs: all 32 generic size slots to the reached 64- and 192-byte owners.

True-resident throughput is intentionally reported as neutral: 60,131 complete FPS at 256 and
78,370 at 512 versus 60,242 and 78,472 immediately before the cut, with unchanged digests
`bdc54107c51fa3d7` and `3fb5823d90657775`. The full validation, native API/save-restore, Wasm, and
runtime-allocation gates remain green. This packet is retained for its large per-environment memory
deletion; it is not counted as progress toward the 500k frame-time target. The next speed unit is a
complete compact pose/collision owner, not more size-class or scalar-leaf tuning.

## Phase 9 clean-HEAD profile and 500k experiment queue — 2026-07-17

The failed broad compact-pose experiment established that representation work must follow measured
complete owners rather than an assumed renderer-shaped bottleneck. Its complete dirty tree is
preserved outside the branch as named stash
`fad684cec36b130020c8badd08d9ddd841fd514b`; it is evidence and a salvage source, not the active
runtime. The temporary profiling instrumentation is independently preserved as
`0d17defb5841f04e80924af05698747f3ee38ef0`. Neither stash is a retained performance result.

The committed HEAD was rebuilt and remeasured on CPU 0 before profiling. It produced the expected
digests and reached 58,289 complete FPS at 256 and 75,363 at 512. These are 3.1% and 3.8% below the
recorded retained results of 60,131 and 78,370 under contemporaneous host conditions, so the
retained implementation is intact. Future decisions use adjacent alternating baseline/candidate
runs rather than comparing a new sample with the best historical sample.

The former `MSL_CORE_PHASE_PROFILE` result is rejected as optimization evidence because it changed
the production scalar schedule into a two-Match interleaving path. The replacement diagnostic timed
the unchanged complete production contract, used RDTSCP with measured timer overhead removed, and
kept the production digest exact. The canonical suite-derived and midgame-weighted samples agree on
the major owners:

| Exclusive complete-contract owner | Canonical | Midgame-weighted |
|---|---:|---:|
| Fighter map/stage collision callback | 18.15% | 18.10% |
| Live pose animation evaluation | 13.34% | 16.21% |
| Hit/damage resolution | 13.20% | 10.80% |
| Stage object animation/callbacks | 9.34% | 10.03% |
| Action-state animation callback | 7.56% | 8.76% |
| Controller/input/IASA owner | 7.46% | 6.42% |
| Fighter dynamics | 6.68% | 5.07% |
| Global and fighter camera callbacks | 4.52% | 4.50% |
| Contact/hurt/hit/shield publication | 2.82% | 2.32% |
| Scheduler traversal/dispatch | 2.93% | 2.93% |
| Finish/publication | 2.31% | 2.31% |
| Observation | 1.95% | 1.95% |
| Combat collision detection | 1.61% | 1.27% |
| Ground IK | 1.19% | 1.20% |
| Items/articles | 0.83% | 1.34% |

Stage collision plus scheduled stage work owns about 28% of the frame. Hit resolution, collision
detection, and contact publication own about 14–18%. A generous pose/geometry grouping owns about
25%, which gives it only a 1.34x whole-runtime ceiling even if it were eliminated completely. The
500k checkpoint is therefore defined at **512 resident environments on one CPU core**. It requires
5.96x over the retained 83,837 FPS result; 256 remains a required secondary cache-pressure report,
not the official completion point.

### Confidence labels

Every experiment below is labeled before implementation:

- **Definite improvement** means the completed final boundary removes measured executed work or
  replaces it with strictly less equivalent work. A neutral or negative result is evidence that the
  implementation, deletion boundary, or benchmark is wrong; it is not grounds to relabel the idea
  as worthless. This expectation applies only after the complete named boundary is in place, not to
  temporary scaffolding.
- **Potential improvement** means the idea is source-correct and plausible, but locality, branch
  frequency, divergence, setup cost, or cache pressure can legitimately make a correct
  implementation neutral or slower. It is retained only on measured complete-contract evidence.

| Order | Optimization item | Confidence | Basis and expected role |
|---:|---|---|---|
| 1 | **Completed by Packet 1:** cull or construction-precompute supported-stage animation/callback work whose products are invariant or presentation-only | **Retained: +6.8% at 512** | Dynamic collision/platform/wind publication remains source-owned. The final audit, correctness gates, and normalized A/B evidence are recorded below. |
| 2 | **Rejected by Packet 2:** split `Fighter_ProcessHit_8006D1EC` into a minimal no-pending-event path and the exact event path | **Correctness-green, noise-level** | 96.0% of visible calls were idle, but preserving gameplay-required hurt-capsule publication left +0.7% at 512 and -0.4% at 256. The capsule consumer boundary remains queue item 6; no scalar guard was retained. |
| 3 | Reduce camera/magnify/visibility to the gameplay-observed headless owner | **Definite** for presentation-only branches; total size is potential | Camera callbacks are 4.5%, with additional finish visibility cost. Existing gameplay camera bounds, blast zones, offscreen damage, and visibility bits remain canonical. |
| 4 | Replace stage collision pointer topology with compact immutable line/normal/adjacency arrays and a bounded dynamic overlay | **Potential** | Map collision is 18%. Compact data is the final memory shape, but conversion alone may not beat hot scalar pointers until traversal is also replaced. |
| 5 | Add stage spatial candidate sets and swept-ECB broad-phase rejection before exact narrow phase | **Potential** | It can skip large line searches, especially across repeated static stages, but candidate maintenance and small-stage workloads may erase the gain. Exact admission, ordering, remap, platform, and ledge rules remain unchanged. |
| 6 | Replace live pose tree traversal with shared ordinary-frame pose data plus a compact evaluator for blends, fractional rates, dynamics, IK, and capture | **Potential** as a scalar replacement; **definite** only when it deletes the tree/matrix owner and feeds batched consumers directly | Pose evaluation is 13–16% and the broader geometry group about 25%. Prior partial caches failed because they retained the expensive consumer graph. No further partial cache qualifies. |
| 7 | Compile one complete hot owner at a time under the audited strict O3 profile | **Potential** | Prior matrix, animation, and mpLib closures won, while other TUs changed exact output or regressed. Compiler closure can improve a measured owner but cannot supply the multi-x architecture. |
| 8 | Promote compact hot mutable state into aligned AoSoA tiles, separating immutable shared and cold rare state | **Definite final-architecture improvement at 512**, but state extraction alone need not raise FPS | This is required to stop loading multi-megabyte per-Match pointer graphs and to make cross-environment kernels contiguous. The gain is judged after consumers use the new state and the displaced hot representation is deleted. |
| 9 | Replace generic linked-list scheduling at named complete-owner boundaries with fixed phase loops and callback/action-owner index queues | **Definite as part of the completed batched execution boundary**; direct dispatch savings alone are small | Traversal is only about 3%. The value is homogeneous work and stable locality. Scalar callback interleaving is already proven slower and must not return. |
| 10 | Implement masked AVX2/AVX-512 kernels for homogeneous stage collision, pose transforms, integration, contact, and other unconditional owners | **Definite for completed homogeneous kernels** | These kernels are the principal multi-x step. If a completed contiguous kernel cannot beat equivalent scalar work materially, its layout, lane utilization, or measurement is wrong. Divergent action logic uses compact owner queues rather than vectorizing function pointers. |
| 11 | Add active fighter/item/hitbox masks and pair broad phase for combat | **Potential** | It can remove empty and distant pair work, particularly in doubles and item-heavy states, but measured collision detection itself is only 1–2%; do it after the larger hit-resolution boundary. |

The plausible milestone model is 1.3–1.6x from deletion, precomputation, fast paths, and bounded
compiler closure; another 1.5–2x from compact hot state and homogeneous phase kernels; and 2–3x
from effective SIMD over those kernels. These factors are hypotheses to falsify, not gains to claim
in advance. No scalar patch pile is expected to reach 500k.

### Sequential optimization execution protocol

The next autonomous performance goal owns queue items 2–11 in order and continues from one item to
the next without waiting for routine approval. Keep one active packet at a time. Adjacent items may
be implemented as one packet when separating them would require a temporary bridge or a
representation that the next item immediately deletes; the queue is a priority order, not a demand
for artificial commit boundaries.

Before each packet, start from the rolling clean committed baseline and make `ACTIVE_WORK.md` name
the final owner, canonical state, consumers, displaced state/code, deletion boundary, confidence
label, and early proof plan. Profile the actual production owner before choosing the implementation.
Record each material experiment and disposition in the chronological log. Benchmark adjacent
alternating CPU-0 A/B runs at 512 and 256 resident environments on the canonical complete-output
workload, with exact digests; 512 decides retention and 256 reports cache pressure.

For a large representation or execution refactor, prove the target shape as early as credibly
possible with an isolated ignored kernel/prototype or bounded end-to-end slice. Such a proof is
evidence, not a production compatibility path. Once the final design is supported, cut the owning
runtime over directly, delete the displaced representation/consumer boundary, and recover
correctness inside the final representation. Intermediate validation may be red while the cut is
incomplete. Do not add or commit synchronized dual state, setter hooks, lazy materialization,
fallback dispatch, compatibility flags, or adapters whose only purpose is keeping an intermediate
tree green. Do not tune scaffolding or code the packet intends to delete, and do not restore the old
owner merely because an incomplete final cut initially benchmarks slower.

A definite item that is neutral or slower after its complete final deletion boundary is in place
must be diagnosed as an implementation, locality, or measurement failure; it is not rejected on
the first number. A potential item may be rejected after a complete correctness-equivalent ablation
shows no useful gain. Preserve rejected evidence under ignored `reports/triage/`, remove the
rejected production code cleanly, record the result, and continue to the next queue item. A
structural prerequisite is not a standalone retained checkpoint merely because later work needs it:
keep it in the same dirty packet until the dependent final consumer and performance proof exist.

Every retained commit must be correctness-green: the complete 153-replay gate must finish with no
new or widened classification/output lock, and native API/save/restore, sealed-allocation,
arbitrary-index batch restore, relevant PPC, and Wasm/viewer gates must pass. Never commit a known
correctness regression or a red intermediate cut. Commit each retained packet or inseparable packet
group as a logical checkpoint, update the rolling retained baseline and cumulative/marginal evidence
in this file, archive its worklog, then continue automatically. The autonomous goal explicitly
authorizes those green performance commits. Send the Discord webhook for concrete retained wins and
major completed cutovers, not routine diagnostics.

Do not pause merely because an item is difficult, an intermediate structural cut is red, or a
routine design choice is required. Pause only for a genuine external blocker or source/data evidence
that invalidates the intended final architecture with no credible in-scope path. The goal finishes
after every remaining queue item has a retained green commit or a documented clean rejection, the
rolling final tree passes all production gates, and final 512/256 throughput and memory are reported.

### Packet 1 — supported-stage headless animation/callback cull

This is one bounded optimization item, not a general stage refactor and not the stage-collision
representation packet. Its objective is to remove scheduled per-frame stage work whose output is
presentation-only, or move gameplay-observed invariant output to construction/reset. The declared
confidence is **definite improvement for every completed removable owner**: if profiling proves the
old executed work is gone without equivalent replacement work and complete-contract throughput
does not improve, the implementation or measurement must be investigated rather than declaring the
idea invalid. The amount of removable work within the measured 9–10% stage-object budget remains an
open measurement; the packet is independently retained only for a repeatable improvement of about
5% or more at 512 environments.

The source boundary starts at `Ground_801C1CD0`, its common JObj animation work, and the scheduled
stage-specific callbacks reached on Final Destination, Battlefield, Fountain of Dreams, frozen
Pokemon Stadium, Yoshi's Story, and Dream Land N64. The audit must name every changed callback and
each value it publishes. Dynamic collision transforms, moving-platform state, stage hazards that
remain enabled in the supported profile, Dream Land wind, gameplay camera/blast-zone state, and any
other value consumed by fighter, item, collision, or terminal logic remain in their exact source
order. No callback is removable merely because its name or object graph looks renderer-related.

Process:

1. Create a concise `ACTIVE_WORK.md` packet map before runtime edits. Record the final owner,
   canonical invariant/dynamic state, consumers, exact displaced callbacks/traversals, deletion
   boundary, exclusions, confidence label, and chronological log. Do not restore the old broad pose
   candidate or profiling changes into the production path.
2. Measure adjacent CPU-0 baselines at 512 and 256 resident environments with exact production
   digests. The 512 result decides acceptance; 256 is the secondary cache-pressure result.
3. Use the preserved diagnostic profiler only to split stage-object cost by supported stage and
   callback. Keep production scheduling unchanged and keep each routine command within the normal
   time limit.
4. Trace the outputs and downstream consumers of every callback large enough to matter. Classify
   it as dynamic/gameplay-observed, invariant/gameplay-observed, or presentation-only/unobserved.
   Record source/data evidence for the classification before changing it.
5. Leave dynamic gameplay owners source-shaped. Publish invariant gameplay data once during the
   appropriate construction/reset owner. Delete presentation-only scheduled work completely. Do
   not add flags, lazy fallbacks, duplicate representations, compatibility dispatch, or a new
   per-frame substitute loop.
6. Run focused exact per-stage output locks and re-profile the candidate. Verify that each displaced
   callback has disappeared and that its work was not shifted to another timed owner before treating
   benchmark movement as evidence.
7. Run alternating baseline/candidate measurements at 512 and 256 plus the midgame-weighted
   supporting workload. Diagnose any neutral or negative result for an allegedly completed
   definite cut; do not immediately restore the deleted owner or pivot to unrelated tuning.
8. If the candidate earns retention, run the complete 153-replay native gate, native API and
   arbitrary-index save/restore checks, no-runtime-allocation smoke, and relevant Wasm/viewer smokes.
   No digest change or new/widened validation classification is allowed.
9. Preserve a successful result as both a named stash and an ordered ignored binary patch with
   SHA-256 and evidence, then reapply it so the accepted implementation remains uncommitted in the
   worktree. Preserve a bounded unsuccessful experiment before removal as well. Report the audit,
   displaced owners, standalone and marginal FPS, correctness gates, and disposition. Send the
   Discord webhook only for a concrete retained performance win. Commit only when the active user
   request authorizes it.

#### Packet 1 retained result — 2026-07-17

The supported-stage audit retained one construction-time deletion boundary. Culled common Ground
owners have their animation graphs removed and `Ground_801C1CD0` replaced by an epoch-only owner;
the source collision-cache epoch and any non-null Ground callback remain in the same scheduler
position. Dynamic common animation remains on Fountain map 4, Yoshi maps 2/3, Dream Land maps 2/7,
and Battlefield map 3. Frozen Stadium retains no common animation in the frozen supported profile.
The invariant Stadium map-5 and Battlefield map-6 priority-4 callbacks are removed after their
construction publication, as are asserted-null construction-time `Ground_801C1D38` callbacks.
Later transient Ground objects retain their source schedule. There is no runtime stage branch,
fallback, duplicate state, or compatibility dispatch.

Canonical CPU-0 A/B evidence uses the same packed manifest, output contract, and digest:

| Environments | Adjacent no-op mean | Candidate mean | Gain | Digest |
|---:|---:|---:|---:|---|
| 512 | 78,504 FPS | 83,837 FPS | **+6.8%** | `3fb5823d90657775` |
| 256 | 59,903 FPS | 63,207 FPS | **+5.5%** | `bdc54107c51fa3d7` |

Both sides of the final comparison include the completed 128-piece reached-class reserve. The two
individual 512 pairs improved 78,609 to 83,747 FPS and 78,398 to 83,927 FPS. The 256 pairs improved
60,098 to 63,158 FPS and 59,707 to 63,255 FPS. The representative-only supporting workload from the
earlier schedule audit improved 55,918 to 60,488 FPS (+8.2%) with unchanged digest
`f3f76e2500bf0da5`. A canonical all-case owner profile retained digest
`3fb5823d90657775`, removed every named construction-time owner, and showed no substitute callback.
The final corrected no-cull and candidate executables are preserved under ignored
`reports/triage/packet1-stage-cull/` with SHA-256 values
`13b48edb7a535927183fe8b5a20e5657111398faffa88d88937853fd77a8aa0c` and
`cfa974e86afc5a675a11686156be410587aaa351a49d4db53dcd097d3c41012d`.

The initial 64-piece reached-class reserve was insufficient for three supported transient JObj
graphs. Raising the initialization-only reserve to 128 closes the source allocator boundary with no
frame-path work. It adds 14,236 bytes to the representative per-environment arena/savestate payload
(921,656 bytes total) and raises maximum supported construction to 1,266,476 bytes, still inside the
3 MiB arena.

The complete 153-replay gate is green: 63 exact pass, 90 unchanged exact classifications, zero
XPASS/fail/error, and 1,415,476 compared frames. Source sync, native API/context/batch and
save/restore smokes, the sealed-runtime allocation census, 256-environment arbitrary-index restore,
PPC scalar/data/model/map/scheduler smokes, Wasm/native parity (`cef96ff32edfe898`), live-viewer
(`1b43d24b47c228f2`), and browser smokes pass. The final implementation is correctness-green and
retained for commit.

### Packet 2 — ProcessHit idle/event split rejected — 2026-07-17

A macro-gated census inside the unchanged callback found 329,584 conservative idle candidates
among 343,361 visible calls (96.0%) on the canonical 512-environment workload. The candidate used
only the existing source pending fields after exact shield and delayed-damage advancement. It kept
`ftCo_800A0DA4`, because its hurt-capsule publication is consumed by the following contact pass,
and introduced no second flag, queue, fallback, or compatibility path.

The correctness-equivalent path passed the complete 153-replay gate with 63 exact passes, 90
unchanged classifications, zero XPASS/fail/error, and all 1,415,476 output locks. Adjacent rebuilt
CPU-0 measurements were:

| Environments | Clean HEAD complete/step FPS | Candidate complete/step FPS | Complete change | Digest |
|---:|---:|---:|---:|---|
| 512 | 83,094 / 85,741 | 83,696 / 86,379 | +0.72% | `3fb5823d90657775` |
| 256 | 62,992 / 64,452 | 62,761 / 64,579 | -0.37% | `bdc54107c51fa3d7` |

This is a clean rejection: the measured 10.8–13.2% callback owner is dominated by the required
hurt-capsule projection, not by its idle resolution/reset branches. Retaining a scalar guard would
be noise-level intermediate machinery that queue item 6 later deletes. The production source is
restored exactly; the branch census, implementation boundary, full-gate evidence, and A/B are
preserved under ignored `reports/triage/perf_candidates/packet2_process_hit.md`.
