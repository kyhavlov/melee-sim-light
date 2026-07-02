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

`make build` is incremental by default. Use `make build BUILD_FORCE=1` only
when you need to force a full extension rebuild.

## Parallel Worktrees

For parallel bug-fix work, create a sibling worktree on its own branch:

```bash
git worktree add -b bugfix/my-topic ../melee-sim-light-my-topic HEAD
cd ../melee-sim-light-my-topic
uv sync --dev
make build
```

Large local assets are intentionally ignored. To avoid duplicating them, use
hardlinks for files that tooling treats as repo-local inputs and symlinks for
read-only source/reference trees:

```bash
cp -al ../melee-sim-light/datasets datasets
cp -al ../melee-sim-light/replays/debug replays/debug
rsync -a ../melee-sim-light/data/ data/
ln -s ../melee-sim-light/_iso _iso
find ../melee-sim-light/refs -mindepth 1 -maxdepth 1 -printf '%f\n' |
  while read -r name; do
    [ -e "refs/$name" ] || ln -s "../../melee-sim-light/refs/$name" "refs/$name"
  done
```

Keep local ISO links out of `git status` with worktree-local excludes:

```bash
printf '/_iso\n' >> "$(git rev-parse --git-path info/exclude)"
```

Validation no longer uses a persistent row-cache directory.

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

`make validate-all` runs the standard one-step and rollout report generators
after the incremental build check. Suite eval builds datasets directly from
`.slp` files by default, so a separate preprocess step is not required for
normal validation. It uses worker subprocesses by default; set
`VALIDATE_WORKERS=1` for serial output/debugging.
The standard aggregate suite is `replays/suites/aggregate_recent.json`; it
includes the current FD validation set plus selected two-player Battlefield,
Fountain of Dreams, frozen Pokemon Stadium, Yoshi's Story, and Dream Land N64
coverage. Focused stage suites exist under `replays/suites/*_recent.json` for
Battlefield, Fountain of Dreams, Pokemon Stadium, Yoshi's Story, and Dream Land
N64.

Held-out/generalization scorecard:

```bash
make validate-heldout
```

`make validate-heldout` runs every suite listed in
`replays/suites/heldout.json`, writes reports under
`reports/validation/heldout/`, and emits a normalized cross-suite summary.
Held-out replay files are local-only ignored assets expected at the suite paths;
do not commit them or LFS-track them. Held-out replays are separate from
aggregate comparability: do not add them to `aggregate_recent.json`, and do not
use held-out rows as direct lock targets or implementation selectors. Adding or
replacing held-out replay selections is a data curation packet and must stay
disjoint from normal validation suites.

Diff validation reports against a baseline:

```bash
uv run python -m tools.eval.validation_report_diff \
  --before HEAD \
  --after reports/validation
```

Use `--fail-on-regression` in long-work checkpoints when a nonzero exit should
flag any suite or replay-level validation regression for investigation.

Replay-buffer validation benchmark:

```bash
uv run python -m tools.eval.benchmark_validation_replay \
  --suite replays/suites/aggregate_recent.json \
  --out-dir reports/triage/validation_perf/current
```

For platform-stage burn-down, compare owner families by replay-normalized and
stage-normalized deltas against the FD/cardinal controls. Raw aggregate totals
are useful, but they can hide whether an owner is truly platform-specific or a
generic FD-visible system.

One-step and rollout direct commands:

```bash
uv run python -m tools.eval.run_one_step_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json

uv run python -m tools.eval.run_rollout_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags \
  --out reports/validation/rollout_suite_eval.txt
```

Normal validation reads `.slp/.slpz` replay files directly through
`ValidationReplayBuffers`. Persistent row-cache validation commands are not
supported.

## Seed / Schema Changes

If you touch seed/state/schema surfaces such as:
- `src/api.h`, `src/api.c`
- `src/state.h`, `src/state.c`
- `tools/slippi/seed_history.py`
- `tools/slippi/validation_buffer_builder.py`

then run validation normally; it rebuilds the seed rows in memory from the
suite `.slp/.slpz` files. Do not rebuild or compare against persistent row-cache
files; that validation surface has been removed.

Native preprocessing derivations use process-wide generated table roots. For non-default generated
data, set `MSL_DATA_DIR` before importing/initializing the native binding; compatibility
`data_root`/`data_dir` wrapper parameters intentionally reject non-default paths so the old Python
fallbacks cannot run accidentally.

For profiling the default no-cache path explicitly:

```bash
uv run python -m tools.eval.run_one_step_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json

uv run python -m tools.eval.run_rollout_suite_eval \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json
```

## Rollout Triage

Capture and compare rollout metrics from current reports/triage payloads:

```bash
make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json
```

Diff against a saved baseline:

```bash
make rollout-diff \
  ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json \
  ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json
```

Useful triage CLIs:

```bash
uv run python -m tools.eval.stage_rollout_summary --help
uv run python -m tools.eval.benchmark_validation_replay --help
```

## Dolphin Probes

Use the active playback-only tooling under `tools/dolphin/`; do not use
`tools/dolphin/legacy/` for new investigations. Current validation no longer
uses legacy row-cache labels, so Dolphin probes should be launched from explicit
replay paths plus record/player coordinates.

For controlled vanilla experiments from authored Slippi pre-frame fields, use:

```bash
uv run python -m tools.dolphin.slp_scenario_probe \
  --scenario reports/triage/<probe>/scenario.json \
  --dolphin refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --baseline
```

This authors a copied replay's `0x37` pre-frame input/state fields, then runs
the same fast playback dump path. It is useful for webplay/modelplay branch
experiments, but it is not full hidden-state memory injection.

Interpreter-only probes are opt-in with flags such as `--collision-probe`,
`--damagefall-probe`, `--throw-laser-event-probe`, and
`--laser-shield-reflect-event-probe`. These flags install the matching
`MSL_*_PROBE_PATH` environment variables and frame gates. Normal engine dumps
stay on the JIT/null-backend path.

Interpreter mode is extremely slow. Keep the interpreter window to the exact
target frame or two needed for the hidden event; do not use broad context
windows in interpreter mode. Use wider JIT dump windows for surrounding replay
context and tiny interpreter probe windows for the event.

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
uv run python -m tools.extraction.build_data --iso-dir _iso --stages grnla,grnba,griz,grps,grst,grop
```

Data contract:
- `agent_docs/DATA_CONTRACT.md`

## Notes

- Generator-owned validation reports under `reports/validation/` should never be hand-edited.
- Triage outputs belong under gitignored `reports/triage/`, not `/tmp`.
- `--chunk` in evaluator CLIs controls in-memory batch size only; it does not limit how much of the suite gets evaluated.
- If you touch collision / ledge ownership, run `tests/test_ledge_grab_treasuredbackkangaroo_regression.py`.
- Use `tools.eval.validation_report_diff` before accepting regenerated validation reports; routine iteration should not hand-edit reports.
