# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Revision: second 150k-campaign checkpoint candidate based on `ee32fbc1`
- Release benchmark SHA-256:
  `d5de6ab2f3b66415f1cf15b129ea8d773098e2b5693015044feced5e3c6665e0`
- Date: 2026-08-03
- Host: AMD Ryzen 9 9950X3D, CPU 0 (V-Cache CCD), Linux 7.0 x86-64
- Compiler: GCC 13.3.0, strict native release profile, `-march=native -mtune=native`
- Runtime: source-shaped per-Match scheduler with compact gameplay-live fighter pose, exact dense
  ordinary Figa samples, direct canonical fighter-animation spans, lazy mutable Figa decoder
  ownership, construction-bound dynamic-hurt membership, fused exact JObj/dynamics transforms,
  exact quaternion compiler boundaries, exact O1 fighter map collision, direct hosted scheduler
  dispatch, source-defined Ice Climbers item projection, immutable authored-joint quaternions,
  exact batched fighter-pose blends, frame-major compiled Figa samples, direct native motion-program
  binding, register-generated exact wide-trig quadrant factors, duplicate input-predicate deletion,
  exact negative-wall-query culling, a handshake-preserving Dream Land background-animation cut,
  and the 16-character merged runtime
- Correctness: 366/366 accepted (`310 PASS`, `56 CLASSIFIED`, zero XPASS/fail/error) across
  3,501,461 validated frames. Twenty-five obsolete raw-pointer/pool-residue classifications are
  replaced by one genuine Whispy RNG-phase classification, a net reduction of 24, without changing
  gameplay.
- Source synchronization, native API/copy/save-restore and sealed-allocation smoke, optimized
  release validation, PPC smoke, and Wasm parity are green on this exact candidate.

## Benchmark contract

The benchmark packs all 366 aggregate replay cases and assigns resident lanes proportionally
across the complete ordered corpus. Each assigned unique replay is pre-rolled by one of eight
deterministic offsets from 200 through 900 frames; repeated batch slots copy those initialized
Matches through the production copy API. Timed play then advances 262,144 match-frames with
ordinary per-environment replay looping, a true resident 256/512 batch, and a caller-owned
128-frame observation/terminal history.

```sh
make benchmark-9950x3d-vcache-256 BENCHMARK_MATCH_FRAMES=262144
make benchmark-9950x3d-vcache-512 BENCHMARK_MATCH_FRAMES=262144
```

The final exact candidate samples are:

| Batch | Samples | Median cycles/frame | Median FPS | Digest |
|---:|---|---:|---:|---:|
| 256 | 3 | 36,908.0 | 116,288 | `bdff41cf74a54850` |
| 512 | 3 | 35,267.5 | 121,697 | `ee9d93c545aa3ef9` |

Raw candidate 256 cycles/frame were `37,104.3`, `36,891.6`, and `36,908.0`; corresponding wall
throughput was `115,673`, `116,339`, and `116,288` FPS. Raw candidate 512 cycles/frame were
`35,267.5`, `35,327.1`, and `35,249.0`; corresponding wall throughput was `121,697`, `121,492`,
and `121,761` FPS. Three alternating pairs against frozen committed control `ee32fbc1` yield
median paired throughput gains of 5.33% at resident 256 and 6.30% at resident 512. Ratios of raw
medians are +5.33% and +6.24%. Both digests are unchanged. Cycles/frame is the primary comparison
because wall FPS also reflects frequency and machine contention. The prior prefix-assigned
benchmark is a different workload and is not an A/B performance control for these numbers.

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
that immutable/process-wide cost is not replicated per Match. Native DAT translation appends
302,848 immutable bytes of authored-joint quaternions. The compiled-pose program descriptors add
26,472 process-wide bytes (eight bytes across 3,309 programs) for frame-row ownership; compiled
sample count is unchanged. Match size, savestate size, and gameplay allocation counts are
unchanged.

## Owner-selection profile

The final bounded 32,768-frame resident-512 subsystem profile reports 45,307.3 diagnostic
cycles/frame with digest `588be8489df1a62d`. It assigns 14.23% to pose animation, 12.75% to stage
collision, 7.25% to `Fighter_8006D9AC`, 6.71% to input/action, 4.32% to camera, 2.32% to hit
processing, and 0.70% to contact publication. This instrumented profile is for owner selection,
not throughput; inclusive and nested rows must not be added.
