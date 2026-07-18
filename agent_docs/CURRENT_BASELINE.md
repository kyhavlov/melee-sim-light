# Current performance baseline

This is the comparison point for retained performance work. Refresh it only from the production
replay workload below, and require the same digests and correctness status before accepting a new
baseline. The instrumented profiler identifies owners; its FPS is not a throughput result.

## Provenance

- Runtime commit: `9021688409efdcbb5091402b3747da55c554e684`
- Date: 2026-07-18
- Host: AMD Ryzen 9 9950X3D, CPU 0 (V-Cache CCD), Linux 6.17 x86-64
- Compiler: GCC 13.3.0
- Workload: all 153 packed supported-domain replays, 32 representative seeds, 32,768 match-frames,
  128-frame observation history, resident batch, eight warmup ticks
- Release flags: the root Makefile's strict `native-release` profile with `-march=native
  -mtune=native`
- Ordinary native correctness gate: 153/153 accepted (`63 PASS`, `90 CLASSIFIED`, no
  XPASS/fail/error)
- Rebuilt release-profile gate: 153/153 accepted (`63 PASS`, `90 CLASSIFIED`, no
  XPASS/fail/error)
- Native source/smoke and Wasm smoke gates: green

## Uninstrumented throughput

```sh
make benchmark-9950x3d-vcache-256
make benchmark-9950x3d-vcache-512
```

| Batch | Production FPS | Fresh-run range | Digest |
|---:|---:|---:|---|
| 256 | 66,474 | 66,004–66,943 | `bdc54107c51fa3d7` |
| 512 | 89,950 | 89,868–90,032 | `3fb5823d90657775` |

Compare changes with repeated, alternating control/candidate runs when the expected gain is close
to ordinary run variance.

## Latest retained changes

Commits `a46cc202` and `90216884` replace the stale `mpcoll.c -O3` admission with exact
`lb_00B0.c -O2` and compact sealed per-Match source pools. The release binary accepts all 153
cases and preserves both benchmark digests.

Adjacent same-host compiler A/B measured +2.6% at 256 (63,071 to 64,699) and about +3.0% at 512
(84,425 control mean to 86,938 candidate mean). The subsequent pool cut was neutral at 512: the
two reversed pairs differed by -0.27% and +0.33%.

Ordinary singles Match arena/savestate payload is 685,888 bytes, down from 921,656 (-25.6%). The
maximum supported four-player arena is 997,972 bytes, down from 1,266,476 (-21.2%). Maximum source
pool storage is 542,072 bytes, down from 757,540 (-28.4%); the 512-environment lifecycle smoke
reports 0.36 GiB initialized state plus the caller-owned 0.06 GiB observation ring.

The refreshed profiler keeps digest `3fb5823d90657775`. Its main ownership shift is
intentional: restoring exact `mpcoll.c` raises `Fighter_procMap` from 12.56% to 14.72%, while the
new O2 transform owner and lower total cycle count reduce animation and ProcessHit costs.

| Current owner | Contract share |
|---|---:|
| `step` | 97.75% |
| `observation` | 1.93% |
| `terminal` | 0.31% |
| `Fighter_8006A360` | 19.71% |
| `Fighter_ProcessHit_8006D1EC` | 14.76% |
| `Fighter_procMap` | 14.72% |
| `Fighter_8006D9AC` | 8.55% |
| `Camera_8002B3D4` | 4.13% |

## Reference subsystem profile at `6399f75b`

```sh
make subsystem-profile
```

The tables below preserve the detailed profile captured when the profiler was introduced. The
dedicated executable uses the identical resident match loop, per-match scheduler order,
inputs, output writes, and compiler optimization policy as the production workload. RDTSCP hooks
and counters exist only in that executable. Its output digest is
`3fb5823d90657775`, identical to the uninstrumented 512 run. Nested tables overlap their parent;
all shares use `step + observation + terminal` as the public-contract denominator.

RDTSCP pair overhead subtracted from every sample: 43 cycles.

### Public production contract

| Owner | Calls | Net cycles | Contract share |
|---|---:|---:|---:|
| `step` | 64 | 2,014,858,138 | 97.91% |
| `observation` | 64 | 36,882,347 | 1.79% |
| `terminal` | 64 | 6,098,045 | 0.30% |

### Step phases

| Owner | Calls | Net cycles | Contract share |
|---|---:|---:|---:|
| `prepare` | 32,768 | 16,667,445 | 0.81% |
| `scheduler` | 32,768 | 1,945,391,165 | 94.54% |
| `finish` | 32,768 | 37,046,349 | 1.80% |

### Fighter and finish drill-down

| Owner | Calls | Net cycles | Contract share |
|---|---:|---:|---:|
| `fighter_animation` | 69,420 | 201,680,621 | 9.80% |
| `action_anim_callback` | 69,420 | 157,090,739 | 7.63% |
| `input_action_callback` | 68,734 | 35,300,162 | 1.72% |
| `physics_callback` | 68,734 | 9,499,861 | 0.46% |
| `stage_collision_callback` | 68,957 | 217,390,585 | 10.56% |
| `contact_publication` | 69,647 | 75,105,520 | 3.65% |
| `finish_fighter_visibility` | 32,768 | 21,669,033 | 1.05% |
| `finish_item_matrices` | 32,768 | 2,488,797 | 0.12% |

### Animation drill-down

| Owner | Calls | Net cycles | Contract share |
|---|---:|---:|---:|
| `pose_animation` | 69,793 | 159,591,748 | 7.76% |
| `action_script` | 69,793 | 5,083,460 | 0.25% |
| `secondary_pose` | 69,793 | 2,775,091 | 0.13% |
| `capture_pose` | 69,793 | 1,648,663 | 0.08% |

### Scheduled callback owners

| Owner | Calls | Net cycles | Contract share |
|---|---:|---:|---:|
| `Fighter_8006A360` | 83,136 | 409,901,198 | 19.92% |
| `Fighter_ProcessHit_8006D1EC` | 83,136 | 330,838,603 | 16.08% |
| `Fighter_procMap` | 83,136 | 258,531,050 | 12.56% |
| `Fighter_8006D9AC` | 83,136 | 166,362,700 | 8.08% |
| `Fighter_8006C80C` | 83,136 | 90,227,846 | 4.38% |
| `Camera_8002B3D4` | 32,768 | 79,171,084 | 3.85% |
| `Fighter_Spaghetti_8006AD10` | 83,136 | 77,133,271 | 3.75% |
| `Fighter_procUpdate` | 83,136 | 41,447,786 | 2.01% |
| `grIzumi_801CC358` | 8,064 | 34,904,906 | 1.70% |
| `grStory_801E33E0` | 5,248 | 24,156,454 | 1.17% |
| `Ground_801C1CD0` | 29,632 | 23,501,564 | 1.14% |
| `Fighter_UnkCallCameraCallback_8006D9EC` | 83,136 | 22,200,814 | 1.08% |
| `grOldPupupu_80210A24` | 4,224 | 12,589,798 | 0.61% |
| `Fighter_8006A1BC` | 83,136 | 12,137,266 | 0.59% |
| `Fighter_8006CB94` | 83,136 | 10,542,654 | 0.51% |
| `Fighter_8006C5F4` | 83,136 | 10,162,663 | 0.49% |
| `msl_ground_headless_epoch_proc` | 96,448 | 7,669,007 | 0.37% |
| `Fighter_CallAcessoryCallbacks_8006C624` | 83,136 | 7,042,282 | 0.34% |
| `mpLib_800587FC` | 32,768 | 6,175,660 | 0.30% |
| `Fighter_8006DA4C` | 83,136 | 4,056,749 | 0.20% |

The raw machine-readable counters remain under ignored `build/melee_core/subsystem-profile/` for
deeper inspection; the command above resolves callback addresses against its exact executable.
