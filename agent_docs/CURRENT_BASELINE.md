# Current performance baseline

This is the comparison point for production performance work. Refresh it only with the complete
resident replay workload below, unchanged digests, and the complete correctness/API/Wasm gates.
The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Commit: canonical embedded stage-line topology (this commit)
- Runtime: compact fighter pose/gameplay geometry, node-indexed dense exact ordinary Figa samples,
  direct hosted
  scheduler dispatch, demand-owned hurt capsules, exact O1 fighter map collision, the supported
  64-node hosted dynamics pool, exact paired three-axis matrix trig, and fused ordinary JObj world
  matrix publication, optimized PPC-exact hosted square root, fused exact dynamics transforms, with release-only
  CET/frame-chain/unwind deletion, a strict optimized native source closure, and singular embedded
  mutable stage-line topology
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

Standing as of 2026-08-26 (not a refresh): the throughput contract above is still the 2026-07-19
9950X3D evidence and has not been re-measured on comparable hardware since. Three things a reader
must know before comparing against it. (1) The default `VALIDATION_CHARACTERS` now admits twenty
fighters, so the unqualified benchmark targets pack **492** cases (4,876,790 frames; digest
`af9b25381779d47f` at `c86bc762`), not the 153 above; pass
`VALIDATION_CHARACTERS='Fox,Falco,Marth,Captain Falcon,Sheik,Zelda,Jigglypuff,Peach'` to reproduce
the 153-replay selection. (2) On that 153 selection the production digest at `c86bc762` is
`474828382690a770`: gameplay fixes since 07-19 (re-recorded locks) moved it, and the per-fighter
reserve packet itself is digest-neutral (control `d59b65ba` and candidate `3cd7ead3` agree on every
run) and within run-to-run spread on cost (+2.0% median over three alternating samples). (3) The
host these were taken on is a shared Ryzen 9 3950X, where the 153-workload 256 batch runs at about
27k FPS / 130k cycles/frame; those figures are host-bound and are not evidence of a code regression
against the 102,667 FPS above, but a same-host comparison to the 07-19 commit was not possible
(its data-check rejects the current extracted-data profile). Refreshing this contract needs the
9950X3D host or a fresh baseline recorded on the new one.

## Current memory contract

The compact pose and stage-line owners are initialized before gameplay and participate in typed
relocation, arbitrary-index copy, and save/restore. Refreshed 2026-08-26 at `c86bc762` from
`make runtime-census` (20 characters x 6 stages x 2/4 players) after the per-fighter reserve
packet (`agent_docs/ACTIVE_WORK.md`, "per-fighter sealed-arena reserves"): every pool reserve is
now a sum of per-fighter terms plus a Yoshi's Story item term, the pose arenas are 256 joints and
384 tracks per player, and the 192-byte JObj mem-piece class has a 128 + 128/port floor. The
ordinary two-player allocation lock is exactly 608,100 arena bytes and 868 allocations before and
after gameplay. Four Sheiks reach 1,016 pose joints inside the 1,024-joint four-player capacity;
the four-player maximum arena is four Sheik on Yoshi's Story.

| Measure | Current | 2026-07-19 |
|---|---:|---:|
| Ordinary stepped arena | 608,100 B | 631,820 B |
| Ordinary savestate | 671,140 B | 693,972 B |
| Initialization allocations | 868 | 824 |
| Maximum reached arena | 1,311,096 B of 3,145,728 | 960,000 B |
| Maximum relocation records | 5,371 of 16,384 | — |
| Maximum reached pose joints | 1,016 / 1,024 | 976 / 1,000 |
| Hosted fighter-dynamics pool | 10,752 B (64 nodes) | 10,752 B (64 nodes) |

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
