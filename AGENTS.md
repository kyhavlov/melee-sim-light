# AGENTS.md — melee-sim-light

## Project Goal

Implement a **high-performance, batched, deterministic** SSBM-like simulator for RL.

Current target domain:
- **Singles (2 players)**, Fox vs Falco, Final Destination, UCF enabled by default.
- The implementation must stay structured so enabling **4 players (2v2)** later is a config/codepath extension, not a rewrite.

## Hard Requirements

### Performance
- **No heap allocations after initialization** on any runtime gameplay path.
- This includes direct allocation calls and hidden allocations from formatting, logging, container growth, helper buffers, or convenience wrappers.
- Runtime means anything exercised in normal sim execution, especially `reseed_seed`, `step_input`, `write_compare`, and the frame-step passes they call.
- Debug-only or forensic allocations are acceptable only in clearly non-runtime tooling / debug codepaths and must never run during normal sim execution.
- Use deterministic iteration order and tie-breaking.
- Keep hot gameplay state in SoA/AoSoA-style layouts and fixed-capacity pools.

### Data-driven from game files
- Stage collision/models/coords should come from extracted game data.
- Character animation/move/hitbox/hurtbox data should come from extracted game data.
- Manual overrides are allowed only as explicit, small, audited overlays.

### Data / Decomp Discipline
- Gameplay logic in `src/` must be **decomp-backed or game-data-backed**.
- Do not add unexplained gameplay constants or replay-fit “magic numbers”.
- Temporary hardcodes are only acceptable with a nearby source pointer to decomp/asm or extracted `data/...`.
- Every gameplay logic change in `src/` needs a nearby source pointer:
  - `refs/melee/src/...`
  - `refs/melee/build/GALE01/asm/...`
  - `refs/slippi-ssbm-asm/...`
  - and/or extracted data in `data/...`
- If a behavior is not yet decomp-explainable, do **not** fit to a replay row in C. Prefer:
  - tooling / probes / locks
  - promoting a minimal explicit internal
  - documenting the blocker

### C Core + Thin Python
- All gameplay / physics / combat logic lives in **C** under `src/`.
- Python under `python/` is a thin wrapper and tooling layer only.

## Validation Model

Primary validation is **teacher-forced, reseeded one-step** over replay suites:
1. Build a seed state from replay row `t`
2. Apply replay inputs for `t`
3. Run exactly one step
4. Compare against replay reference at `t+1`

Full rollout is secondary and used as a stability / regression lens.

## Current RL 1.0 Checklist

Primary tracker:
- `docs/RL10_COMPLETION_CHECKLIST.md`

Use that file to understand:
- the comprehensive RL 1.0 mechanic-family inventory
- which owner families are effectively closed
- which deep passes are next
- what counts as a bridge vs a residual
- what is out of scope for RL 1.0

### Current Working Mode

We are in **finish-the-sim** mode.

Default workflow:
- identify a **shared owner family**
- read the decomp boundary first
- replace bridgey piecemeal logic with the shared owner
- leave only narrow, decomp-justified residuals

Do **not** default to row-fix iteration when a shared family is still open.
For major mechanic work, the default task shape is a **deep owner-family closure pass**, not a mismatch-row fix campaign.

## What “Done” Means

### Bridge
A compensating path that exists because a shared owner is still missing or mis-owned.

### Residual
A real move-specific or adjacent-family difference that remains after the shared owner is closed.

### Closed Family
A family is only “closed” when:
- the shared owner path is in place by default
- remaining behavior is narrow and decomp-justified
- the family no longer depends on bridges for its core behavior

## Required Last-Mile Behavior

- Treat a mismatch as a **triage entry point**, not the patch boundary.
- Prefer closing a full decomp-backed owner family while the context is loaded.
- Do not stop at the first motivating row if adjacent same-family behavior is still open.
- Investigation, tooling, extraction, replay probes, seed-surface work, and final gameplay code are all part of the same task.
- Do not return with diagnosis-only prose if there is still a credible local next step.

When broadening is justified:
- grounded and airborne variants
- entry / steady / exit ownership
- callback ordering
- item / shield / hitlag / persistence interactions

When a family is claimed complete:
- add focused replay-real locks
- run validation
- say explicitly what residuals remain and why they are not more bridge debt

## Working Conventions

- Do not commit unless the prompt explicitly says to.
- Prefer adding newly learned mechanics to `SPEC.md`.
- Never hand-edit generator-owned validation reports.
- Triage/debug outputs default under gitignored `reports/triage/`, not `/tmp`.
- Never key gameplay behavior on dataset name or record id.
- Keep review state clean and include `git status --porcelain` in handoffs.

## Validation Requirements

### Tests
- Unit / fast tests: `make test`
- Formatting check: `make fmt-check`
- Core sim logic changes: `make validate-all`

### Seed / Schema Changes
If you touch any of:
- `src/api.h`, `src/api.c`
- `src/state.h`, `src/state.c`
- `tools/eval/dataset.py`
- `tools/slippi/seed_history.py`
- `tools/slippi/make_dataset_from_slp.py`

then also run:

```bash
uv run python -m tools.slippi.preprocess_suite \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --datasets-dir datasets \
  --force
```

### Validation Reports
- Test-only changes should not refresh committed validation reports.
- If core sim logic changes, refresh and commit these reports:
- `reports/validation/one_step_suite_eval.txt`
- `reports/validation/rollout_suite_eval.txt`
- `reports/validation/aggregate_recent_one_step_suite_eval.txt`
- `reports/validation/aggregate_recent_rollout_suite_eval.txt`

## Operational References

Use the docs for detailed workflows instead of expanding this file:
- Docs index:
  - `AGENTS.md`: operating contract and repo-phase rules
  - `docs/RL10_COMPLETION_CHECKLIST.md`: live RL 1.0 execution tracker
  - `docs/DEVELOPMENT_WORKFLOWS.md`: common commands and operator workflows
  - `SPEC.md`: mechanics inventory and learned behavior notes
- RL 1.0 checklist: `docs/RL10_COMPLETION_CHECKLIST.md`
- Development workflows: `docs/DEVELOPMENT_WORKFLOWS.md`
- Data contract: `docs/DATA_CONTRACT.md`
- Architecture: `docs/ARCHITECTURE.md`
- Mismatch roadmap: `docs/roadmaps/mismatch_roadmap.md`
- Decomp process ordering: `docs/DECOMP_PROC_ORDER.md`
- Modelplay viewer workflow: `tools/modelplay/README.md`
- Dolphin playback / forensic workflow: `tools/dolphin/README.md`
- Legacy probe notes: `docs/legacy_melee_sim/`

Useful commands:

```bash
make test
make fmt-check
make validate-all
uv run python -m tools.eval.run_one_step_suite_eval --suite replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets
uv run python -m tools.eval.run_rollout_suite_eval --suite replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags --out reports/validation/rollout_suite_eval.txt
```

## Handoff Standard

Every substantial handoff should say:
- what owner family was targeted
- whether the family is now closed or still partial
- which bridges were deleted
- which residuals remain
- why each remaining residual is decomp-justified
- what validation was run
- `git status --porcelain`
