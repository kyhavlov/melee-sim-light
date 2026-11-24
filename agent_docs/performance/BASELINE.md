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

## PR review recheck (2026-10-12)

Alternating strict-release executables built in the same checkout with GCC 13.3,
CPU 0, performance governor, 256 resident matches, 524,288 timed match-frames,
eight warmup ticks and the same 510-recording tape manifest. Four runs per version:

| Version | Mean match-frames/second | Range |
| --- | ---: | ---: |
| Pre-review main `45bae153` | 121,400 | 119,304–122,305 |
| Main after #77/#68, `d395cc72` | 121,469 | 120,675–122,204 |
| Initial narrowed #67 candidate | 118,529 | 117,938–118,936 |

The initial narrowed #67 candidate was 2.42% slower than current main on this workload;
merged main shows no cumulative slowdown against the pre-review baseline.
Main and pre-review output digests match (`c6e5d56628c52a37`); #67's intended
RNG changes produce `38a4d0d243799a3c`. This measures end-to-end throughput,
not isolated instruction overhead: RNG-driven gameplay can change the workload.

Two main/candidate runs each in main/candidate/candidate/main order on subsets:
Dream Land (83 tapes) averages 150,394 → 136,812 FPS (−9.03%); the other stages
(427 tapes) average 123,546 → 121,572 FPS (−1.60%). These results supersede the
prior two-run conclusion that #67 had no noticeable cost. The repair below
addresses the regression. Raw results and preserved candidate diff are under
`reports/triage/pr67_perf_recheck/`.

### Particle animation repair

Disabling the new RNG callback left Dream Land's slowdown intact while restoring
main's output digest. Profiling found 317,611 object-1 animation selections in
524,288 frames: loading visual tracks, then discarding them, dominated the added
cost. The replacement loads only source particle tracks and their AObj clocks;
it keeps source selection, event order and interpolation without visual animation
construction. No replay toggle or persistent match state was added.

Final main/original/optimized/optimized/original/main comparison on CPU 4 (same
V-cache CCD), using the same mixed-stage workload and two runs per version:

| Version | Mean FPS | Runs |
| --- | ---: | --- |
| Main `d395cc72` | 121,664 | 121,098 / 122,230 |
| Previous narrowed #67 | 119,361 | 119,906 / 118,815 |
| Particle-only animation loader | 121,624 | 121,182 / 122,065 |

The final difference from main is −0.03%; the earlier candidate was −1.89% in
this comparison. CPU 0's three-run recheck was noisier: main averaged 121,754,
optimized 120,849 (−0.74%), with medians 121,986 and 122,930 respectively. The
optimized candidate's low run coincided with 5.31% sibling-thread activity.
These bounded checks no longer show a repeatable regression; they do not prove
zero instruction overhead. Original/optimized free-running digests match on both
the mixed workload (`38a4d0d243799a3c`) and Dream Land (`ae7d8192c3e420e4`).

Strict-release validation passes all 517 recordings / 4,768,869 frames and their
output locks. Source/native smoke and native/Wasm parity pass. Raw diagnostic,
profiling and benchmark results are under `reports/triage/pr67_cost/`.

## Static-pose blend review (#58)

Against main `24784d88`, CPU 4, batch 256, 524,288 timed match-frames, two
strict-release runs per version in main/candidate/candidate/main order:

| Workload | Main FPS | Corrected blend FPS | Change |
| --- | ---: | ---: | ---: |
| Same 510 human tapes | 121,881 | 123,545 | +1.37% |
| Tilted-shield probe | 162,200 | 157,491 | −2.90% |

The mixed result is evidence of no observed slowdown, not a claimed speedup.
The shield probe uses eight FD dittos (Fox, Falco, Marth, Sheik, Dr. Mario,
Falcon, Link, Samus), 20 tilted-shield frames followed by 60 neutral frames;
a debugger confirmed it reaches `ftAnim_80070108` with partial blend weights.
Its higher shielding frequency exposes a repeatable cost hidden by the mixed
workload. Both workloads retain identical main/candidate output digests.
The fix preserves the existing batched arithmetic and adds no allocations or
state; it blends eligible unanimated joints that were incorrectly skipped.

Validation: 518/518 recordings, 4,782,367 frames; all 517 prior output locks
unchanged, one new lock. Source/native smoke, Wasm parity and 40 affected tooling
tests pass. Raw results and probe generation are in `reports/triage/pr58_review/`.

### Presentation-only joint exclusion

Profiling put 4.81% of the shield probe's instrumented time in
`ftAnim_80070108`, including 3.74% in its blend kernel. Removing the animation
shortcut also admitted the shared `JOBJ_MSL_GAMEPLAY_COLD` placeholder that
`ftparts.c` uses for presentation-only bones: its part flags are clear, so it
passes `msl_fighter_part_can_animate`. The retained fix checks that existing
marker, while still blending every eligible unanimated gameplay joint.

Final paired main/candidate/candidate/main runs with the same settings above:

| Workload | Main FPS (runs) | Final FPS (runs) | Change |
| --- | ---: | ---: | ---: |
| Tilted shield | 165,807 (165,769 / 165,844) | 164,672 (164,758 / 164,585) | −0.68% |
| Same 510 human tapes | 124,608 (123,704 / 125,511) | 123,693 (123,754 / 123,631) | −0.73% |

This removes most of the original 2.9% shield-heavy penalty, not literally all
measured cost. Both workload digests are unchanged. All 518 replay locks and
4,782,367 compared frames pass; source/native smoke and Wasm parity pass.
There is no new state, allocation, cache, or arithmetic change.

Rejected alternatives: compact quaternion worklists were 1.43% slower than the
unoptimized PR; shared vector math and independent axis comparisons recovered
less than 1% together. Both were reverted in favor of the narrower owner fix.
Raw profiles, experiments and final measurements: `reports/triage/pr58_perf/`.

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

## Frozen FD replay capability

PR #43 against main `8e4fa0a7`, Linux amd64 GCC 13.3, CPU 4 on the V-cache
CCD, performance governor. Two strict-release runs per cell; 256 resident
matches, 524,288 timed match-frames, eight warmup ticks, history 128.
The fixed 510-tape corpus is the same as the preceding PR checks. Candidate
copies insert only the new zero capability byte and update the tape header;
all input bytes are identical. The FD subset contains 139 tapes.

| Workload | Main FPS | Candidate FPS | Change |
| --- | ---: | ---: | ---: |
| Mixed, patch off | 122,594 | 122,023 | −0.47% |
| FD only, patch off | 140,776.5 | 140,211.5 | −0.40% |
| FD only, patch on | 140,776.5 | 141,161.5 | +0.27% |

These short samples show no material slowdown; their spread exceeds the
mean differences. The enabled case changes the simulated rules and is not
an equivalent-computation speedup. Default digests match main (`38a4d0d243799a3c`
mixed, `04e4f4f92b207d08` FD). No additional allocations or tables; the FD
callback adds one capability check. Public API constructors keep the bit clear.

The supplied FD Marth/Icies replay improves from 12,798/12,846 to exact.
All 519 replays (4,795,213 frames) pass, preserving all 518 previous locks.
Native checks, Wasm parity and viewer schema pass. The existing Python selection
passes all 151 tests, with its usual timing-test exclusion; no new synthetic
gameplay tests.
Raw logs: `reports/triage/pr43_review/`.

## Stadium display schedule review

PR #60 against main `b8b87dba`, Linux amd64 GCC 13.3, CPU 4 on the V-cache
CCD, performance governor. Same 515 human input tapes on both versions;
256 resident matches, 524,288 timed match-frames, eight warmup ticks,
history 128. Stadium subset: 56 tapes; non-Stadium: 459.

Reuse the existing grStadium display functions with image/text work excluded,
removing the copied scheduler. Countdown events use their existing source
functions; positive frames perform one countdown check. No new per-env storage
or allocations. The optional legacy camera projection was not exercised by
these tapes and is not covered by these performance conclusions.

| Check | Main FPS | Candidate FPS | Change |
| --- | ---: | ---: | ---: |
| Final mixed, two runs each | 121,561 | 123,258.5 | +1.40% |
| Stadium, two runs each before countdown-only guard | 165,037.5 | 164,322 | −0.43% |
| Final non-Stadium, two runs each | 120,854 | 119,742 | −0.92% |

Results were variable. An initial candidate mixed run reached only 106,467;
main also fell to 116,959. A subsequent three-pair mixed check was −1.66%
before the countdown guard, whereas the final pair above changed direction.
These short checks do not establish either a speedup or zero overhead; there
is no consistent multi-percent slowdown. Non-Stadium outputs agree exactly
(digest `5216865834eaa01a`). Mixed/Stadium digests change with the corrected RNG
schedule and match the author's version after consolidation.

The supplied replay improves from 4,761/4,802 to exact. Final GNU release gate:
520/520 replays, 4,800,015 frames, all 519 previous output locks unchanged.
Native checks, Wasm parity and schema pass; 70 existing tooling tests pass.
Raw measurements and experiment log: `reports/triage/pr60_review/`.

## Replay-only build-option cost screening (2026-10-19)

Current main `b2eb33d4`, compared separately with PR #60 `600543b2` and
PR #45 `e4dbf0d0`. Applied each PR's runtime changes unchanged to main in the
same checkout; excluded suite/docs/ledger changes. This measures the submitted
#60 implementation, not the earlier local scheduler consolidation above.
Strict release, GCC 13.3, CPU 4 on the V-cache CCD, performance governor,
256 resident matches, 524,288 timed match-frames, eight warmup ticks and
128-frame observation history. Fixed 515-tape corpus; Stadium subset 56 tapes.
Initialization and staggered preroll are outside timing. Two runs per version
in main/candidate/candidate/main order for each comparison.

| Workload | Main FPS (runs) | PR FPS (runs) | Mean FPS change |
| --- | ---: | ---: | ---: |
| #60 mixed | 123,279 / 125,013 | 124,120 / 124,611 | +0.18% |
| #60 Stadium | 165,950 / 164,212 | 163,880 / 166,905 | +0.19% |
| #45 mixed, compatibility off, repeat | 123,526 / 124,381 | 123,916 / 125,071 | +0.44% |

No meaningful slowdown is observed. These small positive differences are not
claimed speedups; they are below the approximately 1% run variation. This does
not establish zero overhead or bound other workloads. On this evidence neither
PR warrants a replay-only build option solely to remove a measured regression.

The first #45 pair was main 124,373 / 115,319 against PR 124,441 / 123,725.
The unusually slow trailing control prompted the complete bounded repeat above;
all original samples are retained. The default PR and every main run have the
same output digest `ddc712b63b811c17`. Its v5 tapes insert only a zero arithmetic
capability byte and adjust the header; all original config/input bytes were
verified unchanged. Assembly confirms TLS loads and conditional branches remain
in collision math, with some checks shared across multiple FMA operations.
The enabled non-FMA compatibility mode was not timed.

#60 changes random-stream consumption and therefore gameplay workload: mixed
digests are main `ddc712b63b811c17`, PR `087098f69bccc6f3`; Stadium digests are
main `1fff7fb6167d5fee`, PR `33492a7db4652c7f`. These are end-to-end measurements,
not an isolated measurement of the scheduler's instruction cost. This audit
runs no full replay correctness gate and does not constitute merge approval.
Hardware performance counters were unavailable under the host's existing
permissions; host settings were not changed.

Raw logs, exact patches and generated measurements:
`reports/triage/pr60_pr45_cost_20261019/`. Original branch and uncommitted changes
were restored exactly before recording these results. No extra worktrees.

## Stadium scheduler integration (2026-10-19)

PR #60 `600543b2` rebased onto `b2eb33d4`; initial cleanup removes 196 net
implementation lines by reusing the decomp display functions and existing
Ground.u.display state. Rendering/image/text work remains excluded. The author
removed the unsupported legacy-camera path; this supersedes the earlier review
blocker. VS countdown calls preserve source order, and normal frames skip the
countdown cases.

Same host/settings and 515/56-tape mixed/Stadium workloads as the screening
above. Two paired runs each, submitted implementation versus cleanup:

| Workload | Submitted FPS (runs) | Cleaned FPS (runs) | Mean FPS change |
| --- | ---: | ---: | ---: |
| Mixed | 125,369 / 124,898 | 124,490 / 123,884 | -0.76% |
| Stadium | 166,763 / 165,174 | 166,027 / 165,767 | -0.04% |

Output digests agree on both workloads (`087098f69bccc6f3` mixed,
`33492a7db4652c7f` Stadium). The mixed slowdown is a merge blocker: proceeding
on the first two-run comparison was not justified. A balanced four-run repeat
measured submitted 125,442 / 125,470 / 124,931 / 123,597 against cleanup
123,998 / 122,858 / 124,366 / 123,070 (-1.03%). CPU frequencies were similar;
sibling contention varied. No remote update or merge occurred.

Flattening the countdown checks did not demonstrate recovery and was rejected.
A non-Stadium control was variable too; this does not clear the regression.
Strict-O2 and strict-O3 compilation experiments were not retained. The original
cleanup was not performance-approved; the refinement below supersedes it.
GNU release gate: 523/523 recordings, 4,830,487 frames, all 522 old locks
unchanged and the new 4,802-frame replay locked. Source/native checks,
Wasm parity/schema and 151 tooling/API tests pass (one existing timing test
excluded). Raw results: `reports/triage/pr60_merge_20261019/`.

### Stadium scheduler refinement (2026-10-20)

Retain the normal compiler profile. In hosted builds, combine display states
whose remaining work is the same timer transition, and inline
`grStadium_801D2344` and `grStadium_801D32D0`. This removes the extra helper calls
without duplicating the state machine or adding state, allocation, or options.
The final cleanup removes 197 net implementation/ledger lines versus the
rebased submitted version. Temporary compiler-profile and diagnostic changes
are absent from the tree.

Current root `make native-release-benchmark` reproduces the validated candidate
binary SHA256 `b8f7c4fc240b6d7fe6c953fe3a1eb7814e92017bb82c069917699ed93366142f`.
The exact candidate passed 523/523 replays and 4,830,487 frames with all old output
locks unchanged, native/Wasm checks and 151 tests (one existing timing exclusion).

Measurements use V-cache CPU7 on the same 9950X3D, 256 resident matches and
128-frame observation history. No other process affinities or host settings
were changed. Main is `b2eb33d4`; submitted is rebased `b166d2ac`. All samples
are retained, including the noisy initial short comparison (-0.64% versus main,
-0.08% versus submitted; one candidate run had 6.3% sibling activity).

The decisive original-workload repeat uses eight adjacent, balanced pairs,
524,288 timed frames per run and the same 515-tape corpus:

| Version | FPS, all eight runs in order | Mean FPS |
| --- | --- | ---: |
| Main | 122754 / 123294 / 121742 / 123579 / 122068 / 123792 / 123763 / 123215 | 123025.875 |
| Refinement | 123251 / 123252 / 122980 / 123337 / 122895 / 123929 / 123134 / 124557 | 123416.875 |

Mean change is +0.318%. The paired log-ratio Student-t 95% interval is
[-0.163%, +0.802%]; the independent-sample Welch interval is
[-0.274%, +0.915%]. Sibling activity is 0–0.96%. Both estimates exclude the
previous 0.76–1.03% loss in this workload. They do not prove zero overhead or a
speedup. Original digests remain main `ddc712b63b811c17` and refinement
`087098f69bccc6f3`, matching the submitted implementation.

Longer windows are supporting checks, not a substitute for that original case:

| Comparison | Timed frames/run | Runs/version | Control FPS | Refinement FPS | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mixed versus main | 2097152 | 8 | 150098.875 | 150976.875 | +0.585% |
| Mixed versus submitted | 2097152 | 4 | 150617.5 | 152175.5 | +1.034% |
| Stadium versus submitted | 2097152 | 2 | 191472.5 | 192356 | +0.461% |

The extended mixed paired interval is [-0.164%, +1.339%]; its unpaired interval
is wider, [-0.795%, +1.983%], because both versions slowed together during the
confirmation block. Extended gameplay has a different workload mix and higher
absolute FPS. Submitted/refinement digests agree: mixed `cff2bc01d3ae1a3e`,
Stadium `2e85084d4bd21676`. Main's mixed digest is `a1839119d6efabf9`; the PR
intentionally changes RNG consumption, so comparisons with main are end-to-end
workload measurements rather than isolated instruction-cost measurements.

Decision: retain the refinement. No measurable regression in the final original
workload or the supporting comparisons; the earlier cleanup's regression is no
longer supported by these measurements. This clears the measured performance
blocker, with the uncertainty bounds above. No remote push/merge was performed
as part of this retest. Raw samples, contention records and reproducible summary:
`reports/triage/pr60_merge_20261019/resume-*.{log,json}` and
`summarize_resume.py` in that directory.

## Split double-FMA replay compatibility (2026-10-20)

PR #45 on main `898216ef` supersedes the earlier negligible-cost screening.
The always-compiled helper regressed the default path in all four paired runs,
with minimal sibling activity. Forcing it inline was worse and was rejected.
Compiling the mode out removes its TLS state, binding updates and arithmetic
branches; packing the options into the existing arithmetic byte also retains
the original 64-byte match config and 63,848-byte MslCoreMatch.

`MSL_REPLAY_ARITHMETIC=0` is the normal native/Python/Wasm build: source calls
expand directly to `__builtin_fma`. Validation runners explicitly build with
`MSL_REPLAY_ARITHMETIC=1`; only a reviewed recording selects split operations.
The default binary rejects that request instead of silently using fused math.
Build stamps track mode changes. Default benchmark exports omit recordings
requiring the compatibility build; an explicit option includes them. The
canonical 524-recording validation suite includes the new capture.

Same Ryzen9950X3D, V-cache CPU7, 515 input tapes, 256 resident matches,
524,288 timed frames/run, history128 and warmup8. No other builds/tests ran
during timing. For the final candidate, v4-to-v5 tape conversion changes only
the version header; config and input bytes are identical. Every timing run
has digest `087098f69bccc6f3`, identical to main. No samples were discarded.

| Candidate | Main mean FPS | Candidate mean FPS | Change | Pairs |
| --- | ---: | ---: | ---: | ---: |
| Always-compiled helper, 65-byte config | 124269.25 | 123404.75 | -0.696% | 4 |
| Forced-inline helper, 65-byte config (rejected) | 123297 | 120788.5 | -2.035% | 4 |
| Compiled out, 65-byte config (not accepted) | 122791.75 | 122215.75 | -0.469% | 4 |
| Compiled out, original 64-byte config (retained) | 123807.417 | 124134.667 | +0.264% | 12 |

The retained candidate's paired log-ratio Student-t 95% interval is
[-0.323%, +0.854%]. Removing the entire pair with the highest sibling load
(2.76%) as a sensitivity check gives +0.062%, interval [-0.363%, +0.489%].
This supports parity, not a speedup or a claim of zero timing variance; the
previous 0.70% loss is no longer supported. The earlier 65-byte compiled-out
sample included a 5.3% sibling-load candidate, so it does not establish how
much of that sample's loss came from layout versus contention.
[All raw samples](split_fma.csv) retain the initial negative results too.
Production benchmark SHA256:
`59a4aaa167f40767dcdfb2dbbf515f7a07df896aad88e905de575862da5a7d6c`.

Final enabled build: 524/524 replays, 4,844,177 frames, all 523 old locks
unchanged. The new recording passes 13,690/13,690 on native and PPC; native
lock `8e4967f9b77e2104`. Default production native/Wasm/viewer checks and 152
Python/tooling/API tests pass (one existing timing exclusion). An actual
recording verifies production rejection and default/opt-in benchmark exports.
Nana's first writer at frame209 is retained with operands, PPC instructions,
fused/split results and JIT source references in
`agent_docs/validation/arithmetic_provenance.json`. The record establishes
arithmetic semantics, not the unavailable recording CPU/build identity.

## Vanilla magnifier leader publication (2026-10-20)

PR #53 against main `fd3bac02`: only leaders publish each port's vanilla
magnifier bit. Nana no longer overwrites Popo's visibility. No new state or
allocation; the default stage-bounds damage rule ignores this publication.
The follower check still executes in default games.

Four alternating pairs, strict production build (`MSL_REPLAY_ARITHMETIC=0`),
Ryzen 9950X3D V-cache CPU7, 519 input tapes, 256 resident matches, 524,288 timed
frames, history128, warmup8; no concurrent builds/tests. Control removes only
the added condition and otherwise has main's runtime source. Main mean
124,439.25 FPS, candidate 124,975.50 FPS (+0.431%); all four pairs favor the
candidate. Paired log-ratio 95% interval [+0.125%, +0.739%], sibling activity
0–1.95%. No observed slowdown; this short check is not a general speedup claim.
All eight output digests are `c4faccdbd5171957`.
[Raw samples](magnifier_leader.csv).

The Collision ICs/DK capture matches 7,570/7,570 compared frames with its recorded
UCF 0.8 profile. Removing only the leader check makes its regression test fail:
Popo misses 1% at frame2369, with 731 mismatching frames. The test runs the full
capture diagnostically; missing raw C-stick inputs and render-dependent damage
still exclude it from strict admission. The aggregate remains 524 replay locks /
4,844,177 frames, all passing.
