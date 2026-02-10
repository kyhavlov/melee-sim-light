# AGENTS.md — melee-sim-light

## Project Goal

Implement a **high-performance, batched, deterministic** SSBM-like simulator for RL.

Initial target domain:
- **Singles (2 players)**, Fox vs Falco, Final Destination, UCF enabled by default.
- The implementation must be structured so enabling **4 players (2v2)** later is trivial (same code paths; only config changes).

## Hard Requirements

### Performance
- **No allocations after initialization** on the hot path (including hidden allocations from logging/formatting, container growth, etc.).
- Built as a **vectorized env**:
  - SoA/AoSoA state storage for fields touched every frame.
  - Fixed-capacity pools for projectiles/transient contacts/events.
  - Deterministic iteration order and tie-breaking.

### Data-driven from game files
- Stage collision/models/coords are extracted from game files (FD first).
- Character animation/move/hitbox/hurtbox data are extracted from game files (Fox/Falco first).
- Manual overrides are allowed only as explicit, small overlays (audited and tracked separately).

### No “magic numbers” in gameplay logic
- Do not introduce unexplained constants in C gameplay code (collision extents, gravity, thresholds, timings, etc.).
- Constants must be sourced from **ISO-extracted `data/` artifacts** and/or **decomp**, even if the full system isn’t implemented yet.
  - Acceptable: “temporary hardcode” *only* if accompanied by an inline source pointer, e.g. `data/stages/final_destination.json` segment indices, or `data/characters/fox.json` keys.
  - Not acceptable: choosing constants from replay distributions/heuristics as a convenience.
- Replay-derived heuristics are an absolute last resort; if used, label them explicitly as such and justify why game data/decomp couldn’t be used.

### Decomp-backed gameplay only
- **All gameplay logic changes must be decomp-backed or sourced from game code** (melee decomp C, GALE01 ASM, or Slippi ASM where appropriate).
- Every gameplay logic change in `src/` must include a nearby inline source pointer near the branch/constant being changed:
  - `refs/melee/src/...` and/or `refs/melee/build/GALE01/asm/...` and/or `refs/slippi-ssbm-asm/...`
  - Or an extracted data key in `data/...` (cite both if applicable).
- If something is not currently decomp-explainable, do **not** “fit to the suite” in C. Prefer:
  - adding triage tooling/tests,
  - promoting a minimal seedable internal with prefix-invariance, or
  - documenting it as a blocker.

### C core + dumb Python wrapper
- All gameplay/physics/combat logic lives in **C** under `src/`.
- Python under `python/` is a **thin wrapper only**:
  - passes inputs to C,
  - receives vectorized outputs from C,
  - no gameplay logic, no “fixups,” no derived-state recomputation in Python.

## Primary Validation Loop (v1)

Primary validation is **teacher-forced, reseeded one-step** over a replay suite:

For each replay frame `t`:
1) Construct a **seed state** from replay reference data at frame `t`.
2) Apply the recorded controller inputs for frame `t`.
3) Run exactly one `step()` to predict frame `t+1`.
4) Compare predicted outputs at `t+1` to replay reference at `t+1`.
5) Aggregate metrics across all frames of the suite.

Why: full rollouts are expected to diverge in a “light” sim; the one-step test gives a stable coverage signal early.

### Seed state philosophy (“minimal/data-driven”)
Keep internal state minimal so reseeding is feasible from Slippi data:
- Prefer deriving behavior from `(action_id, action_frame, extracted per-action tables)` rather than hidden flags.
- If a mismatch clearly depends on an unrepresented timer/flag, promote it into the explicit seed schema.
- If Slippi post-frame data is insufficient for a required internal, use Dolphin engine-dumps/probes as a targeted debugging aid (not the default correctness target).

### What gets compared
Do **not** make `slippi-ai` model embeddings the primary correctness target initially.
Instead compare simulator state fields directly (and later optionally compare embedded observations as an additional lens).

## Repo Layout (intent)

- `src/`: C simulator core.
- `python/`: Python wrapper (FFI + vectorized I/O).
- `tools/extraction/`: ISO/game-file → extracted data pipeline.
- `tools/dolphin/`: playback/probe/engine-dump scripts (debugging support).
- `tools/slippi/`: replay parsing + suite dataset generation.
- `replays/suites/`: suite definitions (committed).
- `refs/`, `SSBM.iso`, `_iso/`, Dolphin binaries/user dirs: local-only (gitignored).
- `docs/legacy_melee_sim/`: reference docs copied from the old project (do not treat as the lite sim’s contract).
- `docs/DATA_CONTRACT.md`: what we extract from ISO and where it lives.

## Python Dependencies (use `uv`, not `pip`)

Use `uv` for Python dependencies and editable installs:
- Install deps: `uv sync`
- Install the C extension (editable): `uv pip install -e python`
- Run tools: `uv run python -m tools.eval.run_one_step_eval --help`
- Build a dataset from a replay: `uv run python -m tools.slippi.make_dataset_from_slp --slp <path.slp> --out <out.msl> --ports 1,2`
- Preprocess a suite (cached, gitignored): `uv run python -m tools.slippi.preprocess_suite --suite replays/suites/<suite>.json --datasets-dir datasets`
- Validate a preprocessed suite: `uv run python -m tools.eval.run_one_step_suite_eval --suite replays/suites/<suite>.json --datasets-dir datasets`
- Validate rollout streaks (generator-owned text report): `uv run python -m tools.eval.run_rollout_suite_eval --suite replays/suites/<suite>.json --datasets-dir datasets --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags --out reports/validation/rollout_suite_eval.txt`
- Rollout streak capture (JSON): `uv run python -m tools.eval.run_longest_rollout_streaks --suite replays/suites/<suite>.json --datasets-dir datasets --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags --out reports/triage/current_rollout_streaks.json`
- Rollout summary (headline metrics): `uv run python -m tools.eval.summarize_rollout_streaks --in reports/triage/current_rollout_streaks.json`
- Rollout diff (before vs after): `uv run python -m tools.eval.diff_rollout_streaks --before reports/triage/baseline_rollout_streaks.json --after reports/triage/current_rollout_streaks.json`
- Build ISO-derived data artifacts (gitignored): `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`

Note: `uv sync` only manages declared dependencies; re-run `uv pip install -e python` after syncing if the extension is missing.
Note: pass `--force` to `preprocess_suite` after any dataset schema changes (the cache is just for convenience).
Note: `--chunk` in evaluators is the in-memory batch size for processing; it does **not** limit how many frames/records get evaluated.
Note: fast locate-triage CLIs live under `tools/eval/`:
- `uv run python -m tools.eval.top_triples ...` (top `(seed,ref,out)` clusters, per-dataset + suite aggregate)
- `uv run python -m tools.eval.diff_locate --before <before.tsv> --after <after.tsv> ...` (row-level new/gone/delta diff for locate TSVs)
- `uv run python -m tools.eval.locate_discrete_mismatches ... --format tsv` (TSV source; columns are `seed,out,ref`)

Avoid invoking `pip` directly unless it is being run through `uv` (e.g. `uv pip ...`).

## Tests (fast guardrails)

Default tests are intended to be **very fast** and should not rebuild ISO data or reprocess replays.

- Run unit tests: `uv run pytest`
- Run integration checks (validate existing local `data/` artifacts): `uv run pytest -m integration`
- Convenience: `make test`
- Final validation before committing: `make check` (runs `fmt-check` + `test`)
Note: `make validate` only reads existing `.msl` datasets; it does **not** rebuild `.slp → .msl`. Rebuild explicitly via `preprocess_suite` (use `--force` after schema/seed derivation changes).

## Formatting (C)

- Apply formatting: `make fmt`
- Verify formatting (CI-friendly): `make fmt-check`

Current guardrails:
- `.msl` dataset format roundtrip / corruption detection
- rollback dedupe policy (“keep last snapshot per frame id”)
- C-core “no allocations after init” enforced across `reseed_seed` / `step_input` / `write_compare`
- data contract consistency (moves reference msids that exist in extracted anim tracks)

## Make targets (optional convenience)

- `make build`: build the C extension (`python/setup.py build_ext --inplace`)
- `make test`: build extension then run `pytest`
- `make preprocess`: build/update cached `datasets/` for a suite
- `make validate`: run one-step suite eval (assumes datasets exist)
- `make validate OUT=reports/validation/one_step_suite_eval.txt`: write the report to a file (commit this)
- `make validate-rollout OUT=reports/validation/rollout_suite_eval.txt`: write rollout suite report to a file (commit this)
- `make rollout-capture ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`: capture rollout JSON snapshot (gitignored)
- `make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`: print suite + per-dataset rollout headline metrics
- `make rollout-diff ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json`: print rollout metric deltas
- `make build_data`: extract ISO-derived `data/` artifacts
- `make guardrail-preflight`: fast gate (build + hard-row lock pack + seed==ref diff + float top-key diff)
- `make guardrail-preflight-full`: full suite tests + guardrail diff checks
- `make guardrail-baseline`: regenerate committed guardrail baseline fixtures under `tests/fixtures/guardrails/current_main/`
- `make forensic-rows ARGS='--row <dataset>:<record>:<p>'`: deterministic per-row forensic report under `reports/triage/`

Notes:
- Prefer `make build/test/validate` (they run `python/setup.py build_ext --inplace --force` via `uv run`); avoid invoking `python/setup.py` directly.
- If you touch collision/ledge code, run `tests/test_ledge_grab_treasuredbackkangaroo_regression.py`.
- `make guardrail-baseline` is for intentional baseline updates only (after a reviewed mainline behavior change). Do not refresh it during normal iteration.

Validation output snapshots:
- Commit the latest suite reports under `reports/validation/` whenever you change core sim logic:
  - `reports/validation/one_step_suite_eval.txt`
  - `reports/validation/rollout_suite_eval.txt`

Variables:
- `SUITE=replays/suites/fox_falco_fd_ucf084_recent.json`
- `DATASETS_DIR=datasets`
- `CHUNK=4096`

## Working Conventions

- Prefer adding new “mechanics we learned about” into `SPEC.md` (Mechanics Inventory) immediately, even if not implemented yet.
- Avoid symlinks for tooling/binaries; prefer explicit paths in config.
- Never hand-edit `reports/validation/one_step_suite_eval.txt` or `reports/validation/rollout_suite_eval.txt` (generator-owned); put extra notes in commits/PR text or separate docs.
- Triage/debug scripts must default outputs under gitignored `reports/triage/` (or print to stdout). Never default to `/tmp`. Never stage files under `reports/triage/`.
- Rollout-first workflow (recommended when treating rollout as primary):
  1) Capture baseline once: `make rollout-capture ROLLOUT_JSON=reports/triage/baseline_rollout_streaks.json`
  2) After changes, capture current: `make rollout-capture ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`
  3) Inspect headline metrics: `make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`
  4) Compare before/after: `make rollout-diff ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json`

## Agent Checklist (do this every work chunk)

- Do not commit unless the prompt explicitly says to.
- Any C gameplay change without a nearby decomp/asm/data citation is not reviewable; add the citation or don’t land it.
- Test-only change: do **not** regenerate `reports/validation/one_step_suite_eval.txt` or `reports/validation/rollout_suite_eval.txt`.
- Sim-logic change: run `make test`, `make validate OUT=reports/validation/one_step_suite_eval.txt`, and `make validate-rollout OUT=reports/validation/rollout_suite_eval.txt`.
- Seed/schema change trigger: if you touch any of:
  - `src/api.h` seed structs, `src/api.c` reseed/write paths
  - `src/state.h`/`src/state.c` (new SoA fields)
  - `tools/eval/dataset.py`
  - `tools/slippi/seed_history.py` or `tools/slippi/make_dataset_from_slp.py`
  then you must run:
  - `uv run python -m tools.slippi.preprocess_suite --suite $SUITE --datasets-dir $DATASETS_DIR --force`
  and ensure the validate header stamp updates.
- Integration regressions: never mutate `seed_t` / `ref_t1` buffers; assert replay-real preconditions instead. Always:
  - `pytest.importorskip("msl_binding")`
  - dataset + artifact skip policy
  - `binding.destroy(handle)` in `finally`
- Never key C gameplay behavior on dataset name / record id (record ids belong only in tests/triage).
- Keep git state clean for review: avoid partial staging (`AM` / `MM`) and include `git status --porcelain` in handoffs.
