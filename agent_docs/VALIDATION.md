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
