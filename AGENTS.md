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
- When extracted/decomp table data can express an owner or action-family distinction, use that
  table-backed predicate instead of adding a new local action-id list. If a manual semantic
  predicate is still needed, document why the table data is insufficient.
- Before adding row-local gameplay logic, check whether the owner is already expressible through
  the generated data substrates:
  - `MSLMSO01` MotionState callback/owner classes
  - `MSLSTG01` stage collision/line metadata
  - `MSLPART1` fighter part/anchor metadata
  - `MSLITAR1` item/article constants
  - `MSLFTSC1` decoded script timeline events
- If the needed distinction is present in those tables, use or extend the table-backed helper.
  If the needed table field is missing but source data is available, prefer extraction/table
  promotion over another hardcoded branch.
- Core/shared systems must not branch on character id as a proxy for missing collision, timer, or callback state.
  Character-specific branches are allowed only for actual character-specific mechanics or data-table lookups, with nearby
  decomp or extracted-data backing.

### C Core + Thin Python
- All gameplay / physics / combat logic lives in **C** under `src/`.
- Python under `python/` is a thin wrapper and tooling layer only.

## Validation Model

Primary validation is **teacher-forced, reseeded one-step** over replay suites:
1. Build a seed state from replay row `t`
2. Apply replay inputs for `t`
3. Run exactly one step
4. Compare against replay reference at `t+1`

Use full rollout as a stability / regression lens.

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

Do not attempt to do less work just to have a clean win. Some cases will call for fixing multiple issues simultaneously to cleanly close out a system. THIS IS WHAT WE WANT.

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
- For disruptive rollout targets, run `tools.eval.next_desync_investigation` before patching and
  include the packet path in the worklog/handoff. Use the packet as evidence, not as an
  authoritative diagnosis.
- For each selected mismatch, first identify the shared data-backed owner family when possible:
  MotionState callbacks, script events, item/article kind, stage segment, part/anchor, or explicit
  seed/provenance lane. Prefer closing that owner family over fitting the motivating row.
- For assigned multi-item work, the handoff bar applies to the whole list, not the first completed item.
- Do not stop at the first motivating row or one small owner slice during checklist burn-down.
- Investigation, tooling, extraction, replay probes, seed-surface work, and final gameplay code are all part of the same task.
- Keep working until ALL assigned work is ready for review.
- Do not return with diagnosis-only prose if implementation, extraction, probes, locks, or validation remain credible local next steps.

When broadening is justified:
- grounded and airborne variants
- entry / steady / exit ownership
- callback ordering
- item / shield / hitlag / persistence interactions

## Working Conventions

- Do not commit unless the prompt explicitly says to.
- Prefer adding newly learned mechanics to `SPEC.md`.
- Never hand-edit generator-owned validation reports.
- Triage/debug outputs default under gitignored `reports/triage/`, not `/tmp`.
- Never key gameplay behavior on dataset name or record id.
- Keep review state inspectable and include `git status --porcelain` in handoffs; do not revert useful work merely to return a clean tree.

## Validation Requirements

### Tests
- Unit / fast tests: `make test`
- Formatting check: `make fmt-check`
- Core sim logic changes: `make validate-all`

### Seed / Schema Changes
Suite validation builds seed rows directly from `.slp` files by default, so
`make validate-all` does not require a prior `preprocess_suite` run. If you
touch persistent dataset cache/schema surfaces, or you need to verify `.msl`
cache regeneration, also run forced cache refreshes:

- `tools/eval/dataset.py`
- `tools/slippi/preprocess_suite.py`
- cache writer/reader paths in `tools/slippi/make_dataset_from_slp.py`

```bash
uv run python -m tools.slippi.preprocess_suite \
  --suite replays/suites/fox_falco_fd_ucf084_recent.json \
  --datasets-dir datasets \
  --force
uv run python -m tools.slippi.preprocess_suite \
  --suite replays/suites/aggregate_recent.json \
  --datasets-dir datasets \
  --force
```

For gameplay seed/runtime changes such as:
- `src/api.h`, `src/api.c`
- `src/state.h`, `src/state.c`
- `tools/slippi/seed_history.py`
- `tools/slippi/make_dataset_from_slp.py`

run `make validate-all` after focused tests. Use forced cache refresh as an
additional cache-contract check when those changes affect persistent `.msl`
encoding or metadata.

If you touch extraction code or generated data contracts, also run `make build_data` and ensure
stale-version/data-contract tests cover the artifact. Behavior-equivalent extraction/table changes
should not refresh validation reports unless generated report content actually changes.

### Validation Reports
- Test-only changes should not refresh committed validation reports.
- If core sim logic changes, refresh these reports:
- `reports/validation/one_step_suite_eval.txt`
- `reports/validation/rollout_suite_eval.txt`
- `reports/validation/aggregate_recent_one_step_suite_eval.txt`
- `reports/validation/aggregate_recent_rollout_suite_eval.txt`

### Modelplay Regression Fixtures

- Tests must not depend on full `reports/modelplay/**/trace.json` artifacts at runtime.
- For modelplay-visible regressions, extract the minimal relevant replay window into
  `tests/fixtures/modelplay/` and load that compact fixture in the test.
- Keep the original trace path as fixture metadata/source context only; the test should pass when
  the large local trace artifact is absent.

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

## Handoff Standard

For active checklist burn-down work, hand off only when the checklist item is closed. A final handoff should say:
- what owner family or named residual bucket was targeted
- whether the checklist item is now closed
- which bridges were deleted
- which residuals remain
- why each remaining residual is decomp-justified or belongs to another named owner
- what validation was run
- `git status --porcelain`
