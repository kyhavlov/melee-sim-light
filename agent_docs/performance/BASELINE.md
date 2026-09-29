# Performance baseline

Measured at `8cf34043` on 2026-09-21: AMD Ryzen 9 9950X3D, CPU 0 on the
96 MiB V-cache CCD, Linux x86-64, GCC 13.3.0, performance governor.
Use the root Makefile's strict native-release flags.

| Resident matches | Median match-frames/second |
|---|---:|
| 256 | 124,141.5 |
| 512 | 120,606.5 |

Each median uses eight runs. These are measured medians, not minimum throughput.

```sh
make benchmark-9950x3d-vcache-256 BENCHMARK_MATCH_FRAMES=262144
make benchmark-9950x3d-vcache-512 BENCHMARK_MATCH_FRAMES=262144
```

The workload is 508 human recordings with 4,684,452 input frames. Four CPU
recordings are excluded by the controller-only tape format. Each timed run uses
one single-threaded batch, 262,144 match-frames, eight warmup ticks, and a
128-frame observation/terminal history. Resident and logical batch sizes match.
Input tapes and initialization are outside the timed loop.

The recorded digests are `64020ea131e1b3a7` at 256 matches and `8ae6f774c68378ea`
at 512, with 15 and 19 resets respectively. Changing the corpus changes the
comparison workload and can change these digests.

Compare changes on the same host, compiler and workload. Keep measurements and
validation results in the commit description; put raw logs under ignored
`reports/triage/`. Historical experiments remain in Git history.

## Memory

Measured at `24ee1e58` on 2026-09-24 (16-core Linux host, private resident bytes
from `/proc/<pid>/smaps_rollup`). Immutable game data is loaded once per process
and shared by every batch: about 300 MB (GameData arena ~103 MiB, native DAT
arena ~108 MiB, compiled sample values ~97 MiB). Processes forked after
`msl_game_data_acquire` share those pages copy-on-write and dirty only a few MB
while stepping, so a forked worker holding a batch costs about 10 MB plus its
environments.

Each environment's Match arena is zeroed at construction and does not grow with
play. Singles matches on Final Destination:

| Matchup | Arena per environment |
|---|---:|
| Fox, Falco, Falcon, Jigglypuff, Yoshi, Luigi pairs | 946–954 KiB |
| Marth vs Sheik | 1,079 KiB |
| Ice Climbers vs Ice Climbers | 1,106 KiB |
| Peach vs Peach | 1,715 KiB |
| Peach vs Ice Climbers | 1,760 KiB |

Item reserves (`msl_item_reserve_runtime_pools`: the ItCo.dat article bound,
ItemLink and DynamicBoneTable pools) are 47–59% of that arena. A host running
`E` environments over `P` forked workers needs roughly
`300 MB + 10 MB × P + 1.0–1.7 MB × E` for the simulator.

## Stadium transformations: disabled-path comparison

Measured on the same host on 2026-09-27 against `fcc46464`, using its unchanged
508-replay workload, strict native-release builds, CPU 0, 262,144 timed
match-frames, eight warmup ticks and 128 history frames. Five alternating pairs
per batch size; transformations disabled in both builds.

| Resident matches | Base FPS | With Stadium support | Difference |
|---|---:|---:|---:|
| 256 | 123,370 | 123,840 | +0.38% |
| 512 | 120,306 | 120,759 | +0.38% |

No measurable slowdown; the small increase is within run variation. All paired
digests agree: `f99a65631fc39659` (256), `f43713e454594fe1` (512).

The match remains 63,848 bytes, the public config 52 bytes, and observation plus
terminal storage 1,224 bytes per history frame. Frozen arena and pool bounds are
unchanged across the runtime census. Enabled Fox dittos use 2,112 additional
arena bytes (965,060 versus 962,948). The four immutable transformation archives
add 1,238,048 bytes to shared GameData/native-DAT arenas per process.

The frozen controller is selected during construction; no transformation flag
check is added to its frame callback. The slope-IK pose-cache correction also
applies to frozen matches, so this is a measured cost claim, not a claim of
identical instruction counts.

Native and release validation pass 518 replays / 4,771,887 frames, including
50,187 frames covering all four transformations and recorded stage events.
The Stadium lifecycle smoke covers 80,000 frames, all phases, copy/save/restore,
mode-changing resets and sealed allocation pools. Raw measurements are under
`reports/triage/stadium_transformations_20260927/` (`perf.json`, `perf-*.log`,
`census-*.log`, `final-gate.log`, `release-gate.log`, `lifecycle-final.log`).
