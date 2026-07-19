# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Runtime: compact fighter pose/gameplay geometry, shared exact ordinary Figa samples, direct hosted
  scheduler dispatch, demand-owned hurt capsules, exact O1 fighter map collision, the supported
  64-node hosted dynamics pool, exact paired three-axis matrix trig, and fused ordinary JObj world
  matrix publication, with release-only CET/frame-chain/unwind deletion
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

The retained comparison medians are 85,355 FPS at 256 and 80,041 FPS at 512. For the latest native
release-layout packet, adjacent 512 control/candidate medians are 76,948/80,041 FPS (+4.02%) with
the same digest. Final 256 samples have an 85,355 median (+4.41% over 81,753).

## Current memory contract

The compact pose owner is initialized before gameplay and participates in typed relocation,
arbitrary-index copy, and save/restore. The allocation lock remains exactly 633,432 arena bytes and
825 allocations before and after gameplay. Four supported Peach instances reach 976 compact pose
nodes inside the fixed 1,024-node capacity.

| Measure | Current |
|---|---:|
| Ordinary stepped arena | 633,432 B |
| Ordinary savestate | 695,048 B |
| Initialization allocations | 825 |
| Maximum reached compact pose nodes | 976 / 1,024 |
| Hosted fighter-dynamics pool | 10,752 B (64 nodes) |

The public 128-frame observation history remains 127,488 bytes per environment and is caller-owned.
Shared GameData additionally owns 10,721,046 compiled Figa values (40.90 MiB plus validity bits),
paid once per process rather than once per environment.

## Current corrected profile

On the staggered 512 workload after release-layout deletion, the scheduler owns 93.75% of
instrumented time. Its largest inclusive callback owners are hosted fighter maintenance
(`Fighter_8006A360`, 28.74%), fighter map collision (`Fighter_procMap`, 16.28%), fighter dynamics
(`Fighter_8006D9AC`, 7.76%), Spaghetti input/IASA (7.29%), and camera (3.96%). Cross-cutting phase
attribution assigns 17.13% to fighter animation, 15.37% to pose animation, 15.40% to stage collision,
8.84% to action animation callbacks, and 5.00% to input/action callbacks. The profiler's 66,519 FPS
is diagnostic overhead, not a throughput baseline; nested rows must not be added to their enclosing
phase shares.
