# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Revision: benchmark-balancing change based on
  `b3ade65ac45c588e8d8e0958a4cd575380cf78c3`
- Release benchmark SHA-256:
  `a461e1301379b32f00ea5e8eb56e73d3c60c31f1be387ee0749ee98821fec0ff`
- Date: 2026-08-03
- Host: AMD Ryzen 9 9950X3D, CPU 0 (V-Cache CCD), Linux 7.0 x86-64
- Compiler: GCC 13.3.0, strict native release profile, `-march=native -mtune=native`
- Runtime: source-shaped per-Match scheduler with compact gameplay-live fighter pose, exact dense
  ordinary Figa samples, direct canonical fighter-animation spans, lazy mutable Figa decoder
  ownership, construction-bound dynamic-hurt membership, fused exact JObj/dynamics transforms,
  exact quaternion compiler boundaries, exact O1 fighter map collision, direct hosted scheduler
  dispatch, source-defined Ice Climbers item projection, and the 16-character merged runtime
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
| 256 | 3 | 42,429.7 | 101,155 | `bdff41cf74a54850` |
| 512 | 5 | 40,783.0 | 105,239 | `ee9d93c545aa3ef9` |

Raw 256 cycles/frame were `42,340.5`, `43,204.3`, and `42,429.7`; corresponding wall throughput
was `101,367`, `99,341`, and `101,155` FPS. Raw 512 cycles/frame were `40,626.5`, `41,423.8`,
`41,188.6`, `40,596.3`, and `40,783.0`; corresponding wall throughput was `105,644`, `103,611`,
`104,202`, `105,723`, and `105,239` FPS. Cycles/frame is the primary comparison because wall FPS
also reflects frequency and machine contention. The prior prefix-assigned benchmark is a different
workload and is not an A/B performance control for these numbers.

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
