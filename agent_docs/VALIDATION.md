# Replay validation

The aggregate inventory is `replays/suites/melee_core_aggregate.json`.
`melee_core_classifications.json` owns residual explanations and source evidence;
generated expectations and output locks remain under `replays/suites/`.
Only certified Linux/amd64 GNU builds may record these identities; see
[ENVIRONMENT.md](ENVIRONMENT.md).

```sh
make source-check native-smoke
make validation-suite
make validation-suite VALIDATION_SUITE=replays/suites/gamewatch.json VALIDATION_BACKEND=ppc
make viewer-smoke
```

Exact means the strict gameplay projection in `tools/validation/native.c`,
including source-backed item masks in `src/runtime/item_projection.h`.
Validation supplies recorded RNG observations and available stage events.
It does not establish an unseeded rollout or reproduce omitted render timing.
Classifications retain exact mismatch snapshots; they are not tolerances.
Never infer missing controller inputs or execution settings from expected output.

Remaining capture boundaries include omitted magnifier VI/render phase, historic
floating-point execution, missing raw C-stick before UCF, and source-unwritten
bytes. Investigate the owning writer before assigning a capture limitation.
Instruction-level adaptations are recorded in `src/upstream_delta_ledger.tsv`.

## Recording provenance

- [Yoshi/Bowser](validation/yoshi_bowser_provenance.json): original downloads,
  capture bounds and lossless compression hashes.
- [Mewtwo](validation/mewtwo_provenance.json): controller-driven reflection and
  absorption captures, controls, Dolphin build/settings and hashes.
- [Game & Watch](validation/gamewatch_provenance.json): corpus and scripted
  Judge/Chef/bucket captures, Dolphin build/settings and hashes.
- [CPU inputs](validation/cpu_provenance.json): synced/separated Ice Climbers
  captures and the diagnostic Peach recording.
- [Arithmetic profiles](validation/arithmetic_provenance.json): July Mewtwo and
  Game & Watch captures, first-writer operands, profile comparisons and limits.
- [Roy](validation/roy_provenance.json): 28 corpus games (2026 Mainline
  Dolphin including a level-3 CPU Fox game, Wii console captures from D20
  Melee and Yeti Weekly, one console game mirrored over the network) with
  capture metadata and hashes.
- [Pichu](validation/pichu_provenance.json): 14 corpus games (Dolphin and Wii
  console captures from Yeti Weekly and D20 Melee, two of them classified
  missing-raw-cstick-asdi) with capture metadata and hashes.

The interaction coverage tests inspect what a recording actually contains.
Callback smokes exercise source paths with extracted data; they are not
independent Dolphin recordings or exhaustive branch coverage.

The scripts in `tools/validation/record_*_interactions.exs` run from an ExPhil
checkout with its compiled bridge and libmelee_ex dependencies. For example:

```sh
devenv shell -- elixir -pa '_build/dev/lib/*/ebin' \
  /path/to/melee-sim-light/tools/validation/record_mewtwo_interactions.exs \
  missile /absolute/fresh/output /path/to/dolphin-emu-headless /path/to/melee.iso
```

Keep each recording's executable hash, arithmetic settings, controller schedule
and original/compressed hashes. Use ordinary controller inputs and finalized
games; do not inject positions, actions, damage or savestates into capture fixtures.
Store new validation replays as `.slpz`; direct tooling also accepts `.slp`.
Forensic outputs and incomplete recordings belong under ignored `reports/triage/`.

## Recording arithmetic

Suite entries accept `fnmsubs_profile: "retail" | "dolphin-legacy"`; the CLI
override is `--fnmsubs-profile`. Both select the existing match capability before
initialization. Omission retains the metadata/scene default. Those defaults are
heuristics, not proof of the historical executable's arithmetic. Explicit
profiles are reported in validation results and preserved in benchmark tapes
and cache identities; they do not change comparison rules or public RL defaults.

Retail `fnmsubs` negates after fused subtraction, `-(a*b-c)`. Legacy Dolphin
JIT arithmetic can fuse `c-a*b` instead, changing the sign of an exact zero;
a later `atan2f` can turn that sign into a different angle. The provenance record
links both JIT implementations and the captured first-writer operands. Its
four recordings infer arithmetic behavior, not the identity of an unavailable
historical build. Do not select profiles by automatic retry or treat every float
residual as a profile mismatch. Trace the owning writer and preserve the evidence.

`make fnmsubs-smoke` checks cancellation, signed zeros and rounded underflow.
`replays/suites/legacy_arithmetic.json` is in the aggregate; overriding it with
`--fnmsubs-profile retail` reproduces the recorded negative controls. The tests
alternate profiles on one persistent runner to check isolation between jobs.

## Recorded CPU inputs

CPU replay ports require a level in 1..9. Source player slots and AI callbacks
remain active. Exact processed sticks, trigger and buttons enter at Slippi's
`Playback/Core/RestoreGameFrame.asm` boundary (GALE01 `0x8006B0DC`), after AI
sampling and before input edges/timers. Physical controller bytes remain separate.
Validation also supplies `Recording/SendGamePreFrame.asm`'s RNG observation;
Slippi playback itself restores RNG there only when resync is enabled.
This lane never restores position, action, damage or velocity and does not expose
autonomous CPU control through the public RL API.

Nana keeps source follower AI inputs for human and CPU Popo alike; the recorded
input hook skips followers. The Ice Climbers suite covers synced and separated
followers, but neither recording contains a solo Nana death/respawn wait.
The Peach fixture tests a 5,000-transition prefix only: its full recording has
18 strict knockback-Y zero-sign differences at frames 5109–5126 and is excluded
from the aggregate. Comparison rules are unchanged.

Controller-only benchmark tapes cannot represent CPU processed inputs. Direct
export rejects them; `make benchmark-prepare` reports CPU exclusions and retains
all selected human recordings. A CPU-only suite is an error. Tape format v3
includes the private CPU-level config byte; rebuild native/Wasm stream consumers.
