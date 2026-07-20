# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Commit: fighter contact empty-producer cull (this commit)
- Runtime: compact fighter pose/gameplay geometry, node-indexed dense exact ordinary Figa samples,
  direct hosted
  scheduler dispatch, demand-owned hurt capsules, exact O1 fighter map collision, the supported
  64-node hosted dynamics pool, exact paired three-axis matrix trig, and fused ordinary JObj world
  matrix publication, optimized PPC-exact hosted square root, fused exact dynamics transforms, with release-only
  CET/frame-chain/unwind deletion and a strict optimized native source closure
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

The retained comparison medians are 102,667 FPS at 256 and 96,436 FPS at 512. For the latest fighter
contact packet, adjacent 512 control/candidate median costs are 44,873.7/44,505.4 cycles/frame
(-0.82%) and adjacent 256 costs are 42,247.1/41,804.5 (-1.05%), with the same digests.

## Current memory contract

The compact pose owner is initialized before gameplay and participates in typed relocation,
arbitrary-index copy, and save/restore. The allocation lock remains exactly 633,432 arena bytes and
825 allocations before and after gameplay. Four supported Peach instances reach 976 compact pose
nodes inside the fixed 1,000-node capacity.

| Measure | Current |
|---|---:|
| Ordinary stepped arena | 633,432 B |
| Ordinary savestate | 695,048 B |
| Initialization allocations | 825 |
| Maximum reached compact pose nodes | 976 / 1,000 |
| Hosted fighter-dynamics pool | 10,752 B (64 nodes) |

The public 128-frame observation history remains 127,488 bytes per environment and is caller-owned.
Shared GameData additionally owns 10,718,160 compiled Figa values, 131,653 immutable node
descriptors, and direct binding tables (43.44 MiB total), paid once per process rather than once per
environment. The dense publication and direct-binding layouts add 1.23 MiB of process-global data
while deleting sparse validity bits; per-Match memory is unchanged.

## Current corrected profile

On the staggered 512 workload at this commit, the scheduler owns 93.90% of instrumented time. Its
largest inclusive callback owners are hosted fighter maintenance (`Fighter_8006A360`, 29.12%),
fighter map collision (`Fighter_procMap`, 13.77%), fighter dynamics (`Fighter_8006D9AC`, 7.94%),
Spaghetti input/IASA (7.50%), and camera (3.79%). Cross-cutting phase attribution assigns 16.84% to
fighter animation, 14.75% to pose animation, 12.92% to stage collision, 9.64% to action animation
callbacks, and 5.08% to input/action callbacks. The profiler's 80,330 FPS is diagnostic overhead,
not a throughput baseline; nested rows must not be added to their enclosing phase shares.
