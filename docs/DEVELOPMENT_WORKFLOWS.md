# Development Workflows

This doc is the compact operator reference for common repo workflows. Keep
`AGENTS.md` focused on policy; put command/reference detail here.

## Python / Build Setup

Use `uv`, not bare `pip`.

```bash
uv sync
uv pip install -e python
```

If the extension disappears after syncing, rerun:

```bash
uv pip install -e python
```

## Core Validation

Fast guardrails:

```bash
make test
make fmt-check
make check
```

Full committed validation refresh:

```bash
make validate-all
```

Guardrail convenience targets:

```bash
make guardrail-preflight
make guardrail-preflight-full
```

One-step and rollout direct commands:

```bash
uv run python -m tools.eval.run_one_step_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --datasets-dir datasets

uv run python -m tools.eval.run_rollout_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --datasets-dir datasets \
  --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags \
  --out reports/validation/rollout_suite_eval.txt
```

## Seed / Schema Changes

If you touch seed/state/schema surfaces such as:
- `src/api.h`, `src/api.c`
- `src/state.h`, `src/state.c`
- `tools/eval/dataset.py`
- `tools/slippi/seed_history.py`
- `tools/slippi/make_dataset_from_slp.py`

then rebuild cached datasets before validation:

```bash
uv run python -m tools.slippi.preprocess_suite \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --datasets-dir datasets \
  --force
```

## Rollout Triage

Capture and compare rollout metrics:

```bash
make rollout-capture ROLLOUT_JSON=reports/triage/current_rollout_streaks.json
make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json
make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv
make rollout-locate-summary ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv
```

Diff against a saved baseline:

```bash
make rollout-diff \
  ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json \
  ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json

make rollout-locate-diff \
  ROLLOUT_LOCATE_BEFORE=reports/triage/baseline_rollout_desyncs.tsv \
  ROLLOUT_LOCATE_AFTER=reports/triage/current_rollout_desyncs.tsv
```

Useful triage CLIs:

```bash
uv run python -m tools.eval.top_triples --help
uv run python -m tools.eval.diff_locate --help
uv run python -m tools.eval.locate_discrete_mismatches --help
uv run python -m tools.eval.locate_rollout_desyncs --help
```

Rank fixed-horizon rollout-disruptive desyncs:

```bash
make rollout-disruptive \
  SUITE=replays/suites/fox_falco_fd_ucf084_recent.json \
  DISRUPTIVE_OUT_DIR=reports/triage/disruptive_rollout_desyncs_primary

make rollout-disruptive \
  SUITE=replays/suites/aggregate_recent.json \
  DISRUPTIVE_OUT_DIR=reports/triage/disruptive_rollout_desyncs_aggregate
```

`make rollout-disruptive` runs the exact scan in worker chunks by default. Tune
CPU pressure with `DISRUPTIVE_WORKERS=1` for serial reproduction, or a higher
value such as `16`/`24` for a faster local triage pass on a many-core machine.

This reseeds at replay record `t`, advances with replay inputs without reseeding,
scores the horizon compare row at horizons `10,20,60`, records the first
mismatching frame/field in the window, and writes `rows.tsv`, `clusters.tsv`, and
`summary.json` under the selected `reports/triage/` directory. The score is a
weighted sum over action/grounding/combat discrete fields, position/velocity/
percent/shield float deltas, and item identity/position differences; the exact
weights are recorded in `summary.json`.

## Modelplay Viewer Traces

Primary doc:
- `tools/modelplay/README.md`

Entrypoint:

```bash
uv run python -m tools.modelplay.run_model_match \
  --slippi-ai-root /path/to/slippi-ai \
  --p1-model /path/to/model.pkl \
  --p2-model /path/to/model.pkl
```

Generated traces are written under `reports/triage/` or `reports/modelplay/`
depending on the tool.

## Dolphin / Vanilla Forensics

Primary doc:
- `tools/dolphin/README.md`

Known-row forensic dump:

```bash
uv run python -m tools.dolphin.forensic_row_dump \
  --row datasets/.../Example.msl:123:0 \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso
```

Controlled replay-input probe:

```bash
uv run python -m tools.dolphin.patch_slp_preframe_window \
  --slp replays/debug/.../Example.slp \
  --patch-spec reports/triage/<probe>/patch_spec.json \
  --out reports/triage/<probe>/probe.slp
```

Then run the same dump/extract flow from `tools/dolphin/README.md`.

## Data Extraction

Rebuild ISO-derived data artifacts:

```bash
uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco
```

Data contract:
- `docs/DATA_CONTRACT.md`

## Notes

- Generator-owned validation reports under `reports/validation/` should never be hand-edited.
- Triage outputs belong under gitignored `reports/triage/`, not `/tmp`.
- `--chunk` in evaluator CLIs controls in-memory batch size only; it does not limit how much of the suite gets evaluated.
- If you touch collision / ledge ownership, run `tests/test_ledge_grab_treasuredbackkangaroo_regression.py`.
- `make guardrail-baseline` is for intentional baseline refreshes only, not routine iteration.
