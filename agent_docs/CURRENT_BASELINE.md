# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Runtime: the committed compact fighter pose/gameplay-geometry owner, including the fighter
  wall-pass broad phase and staggered production workload
- Date: 2026-07-19
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

| Batch | Prior-commit median | Current median | Raw-median delta | Median paired delta |
|---:|---:|---:|---:|---:|
| 256 | 48,234 FPS | 58,192 FPS | +20.65% | +21.87% |
| 512 | 43,660 FPS | 54,202 FPS | +24.15% | +25.11% |

The exact 512 pairs were 43,660/54,624, 43,152/54,202, and 44,884/53,226 FPS. The exact 256 pairs
were 47,748/58,192, 48,282/57,478, and 48,234/59,070 FPS. Each pair is the preceding committed
runtime followed by the current candidate.

## Current memory contract

The compact pose owner is initialized before gameplay and participates in typed relocation,
arbitrary-index copy, and save/restore. The allocation lock remains exactly 676,440 arena bytes and
825 allocations before and after gameplay. Four supported Peach instances reach 976 compact pose
nodes inside the fixed 1,024-node capacity.

| Measure | Current |
|---|---:|
| Ordinary stepped arena | 676,440 B |
| Ordinary savestate | 738,056 B |
| Initialization allocations | 825 |
| Maximum reached compact pose nodes | 976 / 1,024 |

The public 128-frame observation history remains 127,488 bytes per environment and is caller-owned.

## Current corrected profile

On the committed staggered 512 workload, fighter map/stage collision is 19.84% of the instrumented
contract and compact pose animation is 17.88%. Action animation callbacks are 7.65%, input/action
callbacks are 5.13%, and `Fighter_ProcessHit_8006D1EC` is 4.26%. The profiler's 48,992 FPS is
diagnostic overhead, not a throughput baseline; nested callback rows are attribution drill-down and
must not be added to their enclosing phase shares.
