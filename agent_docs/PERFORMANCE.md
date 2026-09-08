# Performance checkpoint

The current production comparison contract and exact raw evidence are in
[`performance/BASELINE.md`](performance/BASELINE.md). Retained implementation boundaries are in
[`performance/RETAINED.md`](performance/RETAINED.md), and rejected or superseded experiments are
searchable from [`performance/README.md`](performance/README.md).

The September 8 classified-replay closure reaches 493 exact / 36 classified
replays on both native debug and release (529 total; zero failures). Against frozen
merge `24643394`, eight-pair median throughput changes +0.353% at resident 256 and
-0.004% at 512 after exact camera compiler recovery; noisy samples remain in the
retained evidence. Source/native smoke, Wasm parity,
large-batch lifecycle, and focused validation tests pass. See the
[retained comparison](performance/HISTORY.md#classified-replay-closure--2026-09-08)
and [closure report](CLASSIFIED_REPLAY_CLOSURE_2026-09-08.md).

The earlier throughput checkpoint was based on `02cfe013`; its release benchmark SHA-256 is
`45b02500e300c850250385bf9c0dd6e34edd0bcb808f6019b47eebc81ede3318`. Three alternating
262,144-frame comparisons against that frozen parent improve median paired throughput by 9.80% at
resident 256 and 8.23% at resident 512 with unchanged digests. Observed candidate medians are
112,253 and 106,985 FPS, so this is a qualifying +5% checkpoint but not the 150k campaign target.

The complete 3,501,461-frame validation gate reports 310 pass, 56 expected classifications, and
zero failures. Source synchronization, native API/copy/save-restore and sealed-allocation smoke,
PPC smoke, Wasm parity, and viewer/browser smoke are green. No compiler-setting experiment or
correctness compromise is part of the checkpoint.

The Slippi-AI integration additionally retains a strict PIC `python-release`
target, source-sized Peach item/JObj reserves, team-based stockout termination,
and source-owned destructor/color-overlay relocation fixes. Native smoke,
source checks, ten Python API tests and the guarded RL/evaluation checks pass.
The separate periodic replay curriculum was retired after its matched experiment
showed no measurable strength gain and 20% more time per update. Evidence and
retained boundaries are recorded in [the integration history](performance/HISTORY.md).

Mixed RL now admits normal three-player team matches, explicit starting percents,
and masked stepping for recorded-history warmup. The paired 1,280-env recording
check measured +0.20% mean update time with identical trajectories, but its wide
timing interval does not establish a strict sub-1% bound. The random-percent
follow-up passed native/API/reset checks and another mixed GPU run. See
[mixed-training evidence](performance/HISTORY.md#mixed-training-groups-and-natural-2v1-starts--2026-09-07)
for workload details, warmup costs and the configuration ABI change.
