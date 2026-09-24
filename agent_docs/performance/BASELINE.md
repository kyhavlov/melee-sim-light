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
