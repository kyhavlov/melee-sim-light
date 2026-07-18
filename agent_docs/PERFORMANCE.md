# Retained performance evidence

Only production-contract, correctness-green results belong here. Historical experiments remain in
Git history and ignored triage artifacts, not in this active evidence file.

## Benchmark contract

- Host: AMD Ryzen 9 9950X3D; CPU 0 is the 96 MiB V-cache domain.
- Workload: all 153 supported validation replays packed once into native input tapes.
- Timed work: free-running gameplay across a true resident batch plus a caller-owned 128-frame
  ring of 980-byte observations and 16-byte terminal rows.
- Checkpoint: 512 environments on one CPU core; 256 is the cache-pressure secondary result.
- Acceptance requires unchanged workload digests and the complete replay/API/save-restore/Wasm
  gates with no new or widened classification.

```bash
make benchmark-prepare
make benchmark-9950x3d-vcache-256
make benchmark-9950x3d-vcache-512
```

## Retained pre-cutover baseline

The latest committed runtime before the canonical repository cutover retained:

| Environments | Complete FPS | Digest |
|---:|---:|---:|
| 256 | 63,315 | `bdc54107c51fa3d7` |
| 512 | 85,156 | `3fb5823d90657775` |

The full correctness gate was 63 exact passes, 90 unchanged exact classifications, zero
XPASS/fail/error, and 1,415,476 compared frames. The canonical-cutover result is recorded below.

## Canonical-cutover A/B

An alternating three-sample same-host comparison used exact committed HEAD as the control and the
uncommitted canonical layout as the candidate. Both ran the same extracted data, packed 153-replay
workload, release flags, CPU 0, and resident observation history.

| Environments | HEAD median FPS | Cutover median FPS | Delta | Digest |
|---:|---:|---:|---:|---:|
| 256 | 62,151 | 61,850 | -0.48% | `bdc54107c51fa3d7` |
| 512 | 83,445 | 83,004 | -0.53% | `3fb5823d90657775` |

The candidate is performance-neutral within ordinary run variance. The native correctness gate
remains 63 exact, 90 unchanged classified, zero XPASS/fail/error, and 1,415,476 compared frames.

Current maximum supported construction uses roughly 1.27 MiB of per-match arena at the reached
four-player high-water. Ordinary singles use about 0.92 MiB. Shared game-data archive/native-DAT
storage is paid once per process. The public observation history costs 127,488 bytes per environment
at 128 frames.

## Gameplay-pose admission ownership A/B

An interleaved three-sample same-host comparison used exact `4a3340b0` plus its extracted pose
artifact as the control and the immutable `ftparts.c` admission table as the candidate. Both used
the same raw archives, packed 153-replay workload, release flags, CPU 0, and resident observation
history.

| Environments | HEAD median FPS | Immutable-table median FPS | Delta | Digest |
|---:|---:|---:|---:|---:|
| 256 | 60,030 | 60,911 | +1.47% | `bdc54107c51fa3d7` |
| 512 | 80,045 | 80,780 | +0.92% | `3fb5823d90657775` |

This is retained as performance-neutral: the packet removes initialization/data ownership rather
than frame work, and the small positive delta is within ordinary layout/run variance. Shared
`MslCoreGameData` shrank from 380,216 to 371,728 bytes (-8,488 bytes); the admitted table is one
read-only executable constant rather than mutable data copied into every GameData owner.

## Measured frame-time owners

| Exclusive owner | Canonical share | Midgame share |
|---|---:|---:|
| Fighter map/stage collision callback | 18.15% | 18.10% |
| Live pose animation evaluation | 13.34% | 16.21% |
| Hit/damage resolution | 13.20% | 10.80% |
| Stage object animation/callbacks | 9.34% | 10.03% |
| Action-state animation callback | 7.56% | 8.76% |
| Controller/input/IASA | 7.46% | 6.42% |
| Fighter dynamics | 6.68% | 5.07% |
| Camera callbacks | 4.52% | 4.50% |
| Contact/hurt/hit/shield publication | 2.82% | 2.32% |
| Scheduler traversal/dispatch | 2.93% | 2.93% |
| Finish/publication | 2.31% | 2.31% |
| Observation | 1.95% | 1.95% |
| Combat collision detection | 1.61% | 1.27% |
| Ground IK | 1.19% | 1.20% |
| Items/articles | 0.83% | 1.34% |

## Retained architectural results

- True resident 256/512 batches and arbitrary-index save/restore.
- Compact construction-time allocation/relocation metadata and reached source-class storage.
- Direct-bound Match/GameData owners rather than repeated compatibility accessor calls.
- Audited strict-O3 allowlist for portable whole owners.
- Supported-stage invariant/presentation callback deletion: +5.5% at 256, +6.8% at 512.
- Gameplay-observed headless camera cut: +0.3% at 256, +1.5% at 512.

The 500k path remains complete compact pose/geometry ownership, aligned AoSoA hot state, fixed
homogeneous phase loops, and AVX2/AVX-512 kernels. Scalar compatibility bridges, partial caches,
and isolated leaf tuning are not acceptable substitutes for those deletion boundaries.
