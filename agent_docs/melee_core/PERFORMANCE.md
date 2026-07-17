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
per-frame replay RNG authority, stage-event teacher forcing, or viewer output. One resident match
per shard starts from a real near-end savestate, so the timed region includes ordinary replay-end
masked reset and restart. A second pass reports step-only throughput. The observation/terminal ring
is hashed after timing.

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
