# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Revision: local `perf/decomp-throughput` candidate based on
  `4a344d568df0d338819ab25bacd3c44da1419227`
- Release benchmark SHA-256:
  `3ef66625839ca254b2448f43e2c541858526a185c3811b681c8beda8faec2637`
- Date: 2026-08-02
- Host: AMD Ryzen 9 9950X3D, CPU 0 (V-Cache CCD), Linux 7.0 x86-64
- Compiler: GCC 13.3.0, strict native release profile, `-march=native -mtune=native`
- Runtime: source-shaped per-Match scheduler with compact gameplay-live fighter pose, exact dense
  ordinary Figa samples, direct canonical fighter-animation spans, lazy mutable Figa decoder
  ownership, construction-bound dynamic-hurt membership, fused exact JObj/dynamics transforms,
  exact quaternion compiler boundaries, exact O1 fighter map collision, direct hosted scheduler
  dispatch, and the 16-character merged runtime
- Correctness: 366/366 accepted (`286 PASS`, `80 CLASSIFIED`, zero XPASS/fail/error) across
  3,501,461 validated frames, with no new or widened classifications
- Native source/API/copy/save-restore, sealed-allocation census, 45 Python tests, PPC, Wasm parity,
  live viewer/browser, lifecycle, and formatting gates are green on this exact candidate

## Benchmark contract

The benchmark packs all 366 aggregate replay cases, starts each unique replay once, and pre-rolls
it by one of eight deterministic offsets from 200 through 900 frames. Repeated batch slots copy
those initialized Matches through the production copy API. Timed play then advances 262,144
match-frames with ordinary per-environment replay looping, a true resident 256/512 batch, and a
caller-owned 128-frame observation/terminal history.

```sh
make benchmark-9950x3d-vcache-256 BENCHMARK_MATCH_FRAMES=262144
make benchmark-9950x3d-vcache-512 BENCHMARK_MATCH_FRAMES=262144
```

The final exact candidate samples are:

| Batch | Samples | Median cycles/frame | Median FPS | Digest |
|---:|---|---:|---:|---:|
| 256 | 3 | 41,700.7 | 102,923 | `5bd0cb90236b720d` |
| 512 | 5 | 42,310.7 | 101,439 | `932bcfbacb888ac4` |

Raw 256 cycles/frame were `42,083.7`, `41,700.7`, and `41,614.7`. Raw 512 cycles/frame were
`42,939.1`, `42,259.9`, `42,346.9`, `42,305.3`, and `42,310.7`; corresponding wall throughput was
`99,954`, `101,561`, `101,352`, `101,452`, and `101,439` FPS. Cycles/frame is the primary
comparison because wall FPS also reflects frequency and machine contention.

## Current memory contract

The runtime census seals the ordinary stepped Match at 608,864 arena bytes and 843 allocations;
both counts remain identical before and after gameplay. Its complete relocatable savestate is
671,664 bytes. The simpler two-player lifecycle benchmark produces a 611,420-byte snapshot.

| Measure | Current |
|---|---:|
| Ordinary stepped arena | 608,864 B |
| Runtime-census savestate | 671,664 B |
| Initialization allocations | 843 |
| Maximum reached arena | 973,328 B |
| Maximum reached compact pose nodes | 1,016 / 1,024 |
| Hosted fighter-dynamics pool | 10,752 B (64 nodes) |

The public 128-frame observation history remains 127,488 bytes per environment and is
caller-owned. Shared GameData owns 72,404,776 initialized arena bytes plus the native DAT arena;
that immutable/process-wide cost is not replicated per Match.

## Owner-selection profile

The last bounded resident-512 subsystem profile, after canonical animation spans and quaternion
cuts but before lazy decoder construction and the two small final deletions, assigns 14.99% to pose
animation, 12.98% to stage collision, 8.10% to input/action, 6.83% to dynamics, and 3.93% to
camera. A 4,096-frame portable-ISA instruction window assigns 9.74% self to `interpret_joint` and
2.27% to eager Figa attachment/allocation/compaction, which selected lazy decoder ownership. This
profile is for owner selection rather than final throughput; its nested rows must not be added.
