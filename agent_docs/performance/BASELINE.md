# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Revision: third 150k-campaign checkpoint candidate based on `02cfe013`
- Release benchmark SHA-256:
  `45b02500e300c850250385bf9c0dd6e34edd0bcb808f6019b47eebc81ede3318`
- Date: 2026-08-04
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
  compact lossless pose samples, exact native general matrix concat, direct dynamics direction
  products, exact scalar tangent sharing, direct fighter-part membership, exact stage-intersection
  rejection, direct output publication, native exact square-root owners, a conservative ceiling
  broad phase, a native x86 `acosf` estimate seed (retired after Bowser counterexamples; see HISTORY.md), and the 16-character merged runtime
- Correctness: 366/366 accepted (`310 PASS`, `56 CLASSIFIED`, zero XPASS/fail/error) across
  3,501,461 validated frames. Twenty-five obsolete raw-pointer/pool-residue classifications are
  replaced by one genuine Whispy RNG-phase classification, a net reduction of 24, without changing
  gameplay.
- Source synchronization, native API/copy/save-restore and sealed-allocation smoke, optimized
  release validation, PPC smoke, and Wasm parity are green on this exact candidate.

## Benchmark contract

The historical tapes below predate the
[shield-drop capability export correction](HISTORY.md#replay-benchmark-capability-export--2026-09-20).
Regenerate both arms with the corrected exporter for new comparisons; these
historical digests are not identities for regenerated workloads.

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
| 256 | 3 | 38,234.8 | 112,253 | `bdff41cf74a54850` |
| 512 | 3 | 40,117.3 | 106,985 | `ee9d93c545aa3ef9` |

Raw candidate 256 cycles/frame were `38,846.5`, `38,234.8`, and `38,060.3`; corresponding wall
throughput was `110,485`, `112,253`, and `112,767` FPS. Raw candidate 512 cycles/frame were
`40,383.1`, `40,117.3`, and `38,966.4`; corresponding wall throughput was `106,281`, `106,985`,
and `110,145` FPS. Three alternating pairs against frozen committed control `02cfe013` yield
median paired throughput gains of 9.80% at resident 256 and 8.23% at resident 512. The complete
paired gains were +3.36%/+9.80%/+11.62% and +8.23%/+3.41%/+10.37%, respectively; no arm is
filtered. Both digests are unchanged.

Absolute FPS is lower than the preceding checkpoint's recorded host window, but the adjacent
frozen-parent runs are lower by the same frequency/load effect: parent FPS spans 101,029--106,893
at 256 and 98,199--103,457 at 512, and every final candidate beats its paired parent. The
alternating ratios are therefore the checkpoint comparison; the observed candidate FPS supports
direction but does not claim that the 150k campaign target has been reached.

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

The final bounded 32,768-frame resident-512 subsystem profile reports 53,544.0 diagnostic
cycles/frame with digest `588be8489df1a62d`. It assigns 16.93% to pose animation, 11.46% to stage
collision, 5.48% to `Fighter_8006D9AC`, 4.32% to input/action, 3.36% to camera, 2.28% to hit
processing, and 0.93% to contact publication. This instrumented profile is for owner selection,
not throughput; inclusive and nested rows must not be added, and its absolute cycle count is not
comparable to the production benchmark.
