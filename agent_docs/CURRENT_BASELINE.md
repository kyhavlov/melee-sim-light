# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Runtime: committed staggered-workload control `cc83fb42` and the fixed fighter animation
  lifecycle retained with this evidence
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
| 256 | 42,434 FPS | 44,414 FPS | +4.67% | +5.25% |
| 512 | 40,381 FPS | 42,276 FPS | +4.69% | +5.19% |

The exact 512 pairs were 40,381/42,276, 40,562/42,666, and 36,356/40,999 FPS. The exact 256 pairs
were 42,628/44,866, 42,434/43,665, and 38,452/44,414 FPS. Each pair is control/candidate.

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

On the staggered 512 workload before the lifecycle cut, `Fighter_ChangeMotionState` owned 11.36%
of the contract: animation setup was 11.02%, including 4.46% old-animation removal and 1.88% new
track attachment. Other dominant owners were fighter map collision (21.81%) and live pose
evaluation (17.14%). This attribution, not the obsolete opening-state profile, selected the fixed
lifecycle packet.
