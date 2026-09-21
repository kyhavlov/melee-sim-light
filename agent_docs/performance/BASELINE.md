# Current performance baseline

This is the production comparison point after strict replay admission and the
retained pose, input and exact-math code optimizations. Historical workloads
remain in [HISTORY.md](HISTORY.md); their absolute FPS is not a controlled
comparison with this corpus.

## Provenance

- Revision: **Optimize exact pose and animation hot paths**, following `83356f34`.
- Date: 2026-09-21.
- Host: AMD Ryzen 9 9950X3D, CPU 0, 96 MiB V-cache CCD, Linux 7.0.0-31 x86-64.
- Compiler: GCC 13.3.0, unchanged strict root native-release profile.
- Governor: existing `performance` setting; host settings were not changed.
- Candidate benchmark SHA-256: `01ea4049da2add19201b29a3c4d8299dcec01304efbb4fa655412a8f1252d964`.
- Candidate release runtime SHA-256: `a0424114a4ba01d62f719fb34d83cb9246cdc6f9d703411bf2ce3601212f3554`.
- Control benchmark SHA-256:
  `1861b873b4e77575c135ec7e14cbd082becc8768760b66aa356c7a40863d5f51`.
  A clean rebuild of committed source reproduced this entire ELF byte for byte.

## Benchmark contract

Both arms use the same ordered 508-human-recording manifest containing 4,684,452
input frames. Four CPU recordings are excluded by the existing controller-only
tape format. All 508 tape-content hashes match the pre-optimization inventory;
ordered replay-path/tape-hash identity is
`3b34be0c4f050c2ced70de120ad7468af2efbefc99fdbf932b4f865c8ad87277`.

Each run uses one single-threaded resident batch, 262,144 timed match-frames,
eight warmup ticks, and caller-owned 128-frame observation/terminal history.
Logical and resident sizes are equal at 256 or 512. Replay assignment, pre-roll,
copy initialization, reset behavior, input profiles, compiler flags and benchmark
implementation are unchanged. All task builds, tests and profiling finished
before the final timing series.

```sh
make benchmark-9950x3d-vcache-256 BENCHMARK_MATCH_FRAMES=262144
make benchmark-9950x3d-vcache-512 BENCHMARK_MATCH_FRAMES=262144
```

Eight adjacent control/candidate pairs per size alternate which arm runs first.
Every pair improves; all 32 samples are retained. Paired gain is the median of
the eight adjacent ratios. Both candidate medians exceed 120,000 FPS; three of
the eight candidate-512 samples are below that threshold. These are measured
medians, not guaranteed minimum throughput. The root README rounds to whole FPS.

| Batch | Control median FPS | Candidate median FPS | Median paired gain | Control / candidate cycles per frame |
| ---: | ---: | ---: | ---: | ---: |
| 256 | 115,097 | 124,141.5 | +8.07% | 37,289.95 / 34,573.10 |
| 512 | 111,239 | 120,606.5 | +8.62% | 38,583.70 / 35,586.40 |

Digests are `64020ea131e1b3a7` at 256 and `8ae6f774c68378ea` at 512, with 15 and
19 resets respectively in every run. Older 403-recording README figures use a
different workload; the paired current-corpus results establish this code gain.

## Correctness and memory

Native and optimized-release validation each pass all 512 recordings and
4,721,700 transitions, with zero diagnostics, failures or errors. Source/native/
PPC checks, 127 Python tests, copy/save/restore and sealed-allocation checks,
Wasm parity and browser viewer smoke pass. Differential source and actual
production-object proofs cover tree pruning, stationary root constraints,
quaternion math, every sampled channel mask, the complete immutable value table,
and affine matrix composition; their counts and scope are in the detailed evidence.

No Match layout, reserve or snapshot-format change is retained. The ordinary
stepped Match remains at 1,757,132 arena bytes and 1,349 allocations before and
after gameplay. Its snapshot is 1,821,108 bytes: 128-byte header, 63,848-byte
Match and the arena. The construction census peaks at 2,119,308 arena bytes.
GameData arena usage is 114,515,944 of 134,217,728 reserved bytes; native DAT storage
uses 151,551,424 of 268,435,456 reserved bytes. Compiled sample values are a separate
shared initialization allocation of 101,434,248 logical bytes plus 64 initialized
tail bytes required by the new wide read. No gameplay allocation is added.

The retained code reuses canonical pose topology for dirty/root propagation,
expresses trivial field access directly, evaluates source-exact quaternion math
in vector blocks, publishes packed SRT fields through independent ranks and a
contiguous sample read, and keeps affine translation in its vector register.
Generic source traversal, stationary constraint invalidation, all callbacks and
float operation boundaries remain intact. There is no new per-Match cache or
alternate gameplay representation.

[Detailed evidence](HISTORY.md#final-code-recovery--2026-09-21) contains all FPS
samples, focused proofs and rejected-trial/build-audit notes. Frozen binaries,
source patch, provenance, full gate logs and per-tape hashes are under
`reports/triage/perf_120k_20260921/final-load/`.
