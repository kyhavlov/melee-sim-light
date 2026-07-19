# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Runtime: the committed fighter wall-pass broad phase, including the fixed fighter animation
  lifecycle and staggered production workload
- Date: 2026-07-18
- Host: AMD Ryzen 9 9950X3D, CPU 0 (V-Cache CCD), Linux 6.17 x86-64
- Compiler: GCC 13.3.0, strict native release profile, `-march=native -mtune=native`
- Correctness: 153/153 accepted (`63 PASS`, `90 CLASSIFIED`, zero XPASS/fail/error), with no new
  or widened classifications
- Native source/API/copy/save-restore, sealed-allocation census, Wasm parity, viewer, and formatting
  gates: green

## Benchmark contract

The benchmark packs all 153 supported-domain input tapes, starts each unique replay once, and
pre-rolls it by one of eight deterministic offsets from 200 through 900 frames. Repeated batch
slots copy those initialized Matches through the production copy API. Timed play then advances
65,536 match-frames with ordinary per-environment replay looping, a true resident 256/512 batch,
and a caller-owned 128-frame observation/terminal history.

```sh
make benchmark-9950x3d-vcache-256
make benchmark-9950x3d-vcache-512
```

| Batch | Digest |
|---:|---:|
| 256 | `8ef126a41244d514` |
| 512 | `6f91f23e3553a090` |

Host throughput varied materially during the packet, so retention uses three adjacent CPU-0
control/candidate pairs and reports both the median paired delta and the raw throughput medians.

| Batch | Control median | Candidate median | Raw-median delta | Median paired delta |
|---:|---:|---:|---:|---:|
| 256 | 44,001 FPS | 48,273 FPS | +9.71% | +9.45% |
| 512 | 42,408 FPS | 45,638 FPS | +7.62% | +8.19% |

The exact 512 pairs were 42,408/45,638, 41,592/44,998, and 42,463/45,976 FPS. The exact 256 pairs
were 43,008/48,273, 44,559/48,292, and 44,001/48,158 FPS. Each pair is the preceding committed
runtime followed by the current candidate.

## Current memory contract

The fixed fighter animation owner is initialized before gameplay and participates in typed
relocation, arbitrary-index copy, and save/restore. The allocation lock remains exactly 658,148
arena bytes and 828 allocations before and after gameplay.

| Measure | Committed control | Candidate | Delta |
|---|---:|---:|---:|
| Ordinary singles arena | 685,888 B | 658,148 B | -27,740 B (-4.0%) |
| Ordinary savestate | 747,480 B | 719,788 B | -27,692 B (-3.7%) |
| Maximum supported arena | 997,972 B | 986,616 B | -11,356 B (-1.1%) |
| Maximum relocation records | 6,127 | 4,530 | -1,597 (-26.1%) |

The public 128-frame observation history remains 127,488 bytes per environment and is caller-owned.

## Current corrected profile

On the committed staggered 512 workload, live pose evaluation is now the dominant measured owner
at 22.56% of the instrumented contract. Fighter map collision follows at 16.20%,
`Fighter_ProcessHit_8006D1EC` at 8.38%, action animation callbacks at 6.90%, and input/action
callbacks at 4.73%. The profiler's 36,111 FPS is diagnostic overhead, not a throughput baseline.
