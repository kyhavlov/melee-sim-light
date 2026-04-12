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

## Modelplay Viewer Traces

For visual bug triage, there is a throwaway model-vs-sim playback path under `tools/modelplay/`:
- `tools/modelplay/run_model_match.py`: runs one or two real `slippi-ai` checkpoints against this sim.
- `tools/modelplay/sim_env.py`: thin Python session wrapper over `msl_binding`.
- `tools/modelplay/state_adapter.py` / `tools/modelplay/viewer_trace.py`: convert sim state into a browser-viewable trace.
- `tools/modelplay/viewer/`: patched `slippi-viewer` fork that consumes trace JSON directly (not `.slp`).

Generated traces live under gitignored `reports/modelplay/`.

Important facts about these traces:
- They are **not** Slippi replay files. They are JSON objects in the viewer's internal `ReplayData`-like shape.
- They are generated from the sim's per-frame output, so they reflect sim bugs directly.
- For current Fox/Falco FD runs, the default seed is `start_record=0` of `datasets/fox_falco_fd_ucf084_recent/.../AttachedGoodNaturedGuanaco.msl`, which is the real 4-stock opening `Entry` state, not a midgame bootstrap row.
- `frameNumber` in the viewer trace is a sequential viewer frame index (`0..N`), not the sim/dataset `frame_id`.

How to regenerate a trace:

```bash
uv run python -m tools.modelplay.run_model_match \
  --slippi-ai-root /media/kyle/Windows/Users/kyleh/git/slippi-ai \
  --p1-model /path/to/model.pkl \
  --p2-model /path/to/model.pkl \
  --out reports/modelplay/<run_name>
```

How to inspect a trace in the browser:

```bash
cd tools/modelplay/viewer
npm install
npm run build
python -m http.server 8000
```

Then open `http://127.0.0.1:8000/examples/sim/` and load `reports/modelplay/<run_name>/trace.json`.

How to inspect a trace without the browser:
- The trace is plain JSON, so use `uv run python`, `jq`, or a small script to inspect exact frame windows around a reported bug.
- Useful per-frame fields are under `frames[i].players[p].state`, especially:
  - `actionStateId`
  - `actionStateFrameCounter`
  - `xPosition` / `yPosition`
  - `percent`
  - `stocksRemaining`
  - `isGrounded`
  - `hitlagRemaining`
  - `hitstunRemaining`
- Processed controller inputs are under `frames[i].players[p].inputs.processed`.

When debugging from viewer-reported bugs:
- Treat the viewer as a symptom-finding surface, not a correctness oracle.
- Use the viewer frame index first, then inspect the corresponding trace JSON window and relevant sim code paths.
- If the trace itself goes static for many frames, that usually means the sim stopped progressing in some action/state machine; inspect the action id and surrounding input window first.

## Controlled Vanilla Playback Probes

When Slippi post-frame data is not enough to decide whether a viewer symptom is real vanilla behavior, prefer a short replay-input patch plus playback engine dump:

```bash
uv run python -m tools.dolphin.patch_slp_preframe_window \
  --slp replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.slp \
  --patch-spec reports/triage/<probe>/patch_spec.json \
  --out reports/triage/<probe>/probe.slp

uv run python -m tools.dolphin.dolphin_engine_dump \
  --replay reports/triage/<probe>/probe.slp \
  --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui \
  --iso SSBM.iso \
  --start-frame <f0> --end-frame <f1> \
  --out-bin reports/triage/<probe>/dump.bin

uv run python -m tools.dolphin.extract_engine_dump_rows \
  --dump reports/triage/<probe>/dump.bin \
  --start-frame <f0> --end-frame <f1> \
  --out-dir reports/triage/<probe>/rows
```

Use this workflow instead of live memory writes or Gecko-code probes unless there is no playback-based path. It edits only copied `.slp` pre-frame payloads, then vanilla Dolphin consumes the replay normally. `patch_slp_preframe_window` refuses `--out == --slp` unless `--in-place` is passed intentionally.

Frame-numbering rules:
- `patch_slp_preframe_window` patch specs use raw Slippi frame numbers.
- Slippi parsers/viewers commonly display `raw_frame + 123`.
- Modelplay trace `frameNumber` is sequential trace/viewer indexing, not a native `.slp` frame id. If constructing a `.slp` whose parsed frame should display as a modelplay/viewer index, write raw pre-frame `frame = viewer_index - 123`.

Concrete rerun7 shield-poke probe result:
- Source symptom: `reports/modelplay/20260409_rl_doubles_v27_7000_rerun7/trace.json` around viewer frame 2124, Falco shielding vs Fox getup attack.
- Sim trace showed Falco still holding shield (`Guard` action 179, shield active, L held), then entering damage on the next frame. This was not shield release.
- Controlled vanilla probe from `GracefulAttachedTurtle.slp` around raw frames 4644..4685 showed neutral/toward shield entering `GuardSetOff` action 181 with shield HP loss at raw frame 4665.
- The same vanilla setup with Falco facing away and holding a down-tilted shield entered damage action 87 at raw frame 4665 with shield HP unchanged, confirming a legitimate shield-poke shape for that setup.

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
- Rollout locate rows (TSV first-break catalogue): `uv run python -m tools.eval.locate_rollout_desyncs --suite replays/suites/<suite>.json --datasets-dir datasets --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags --out reports/triage/current_rollout_desyncs.tsv`
- Rollout locate summary/ranking: `uv run python -m tools.eval.summarize_rollout_locate --in reports/triage/current_rollout_desyncs.tsv --top 20`
- Rollout locate diff (before vs after): `uv run python -m tools.eval.diff_rollout_locate --before reports/triage/baseline_rollout_desyncs.tsv --after reports/triage/current_rollout_desyncs.tsv --top 20`
- Build ISO-derived data artifacts (gitignored): `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`
- Patch a short `.slp` pre-frame window for controlled vanilla playback: `uv run python -m tools.dolphin.patch_slp_preframe_window --slp <src.slp> --patch-spec reports/triage/<run>/patch_spec.json --out reports/triage/<run>/probe.slp`
- Capture playback engine dump (CLI-only, no libmelee): `uv run python -m tools.dolphin.dolphin_engine_dump --replay <path.slp> --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui --iso SSBM.iso --start-frame <f0> --end-frame <f1> --out-bin reports/triage/<run>/dump.bin`
- Extract frame-window rows from dump: `uv run python -m tools.dolphin.extract_engine_dump_rows --dump reports/triage/<run>/dump.bin --start-frame <f0> --end-frame <f1>`
- One-row forensic dump from dataset row: `uv run python -m tools.dolphin.forensic_row_dump --row <dataset.msl:record:p> --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui --iso SSBM.iso`

Note: `uv sync` only manages declared dependencies; re-run `uv pip install -e python` after syncing if the extension is missing.
Note: pass `--force` to `preprocess_suite` after any dataset schema changes (the cache is just for convenience).
Note: `--chunk` in evaluators is the in-memory batch size for processing; it does **not** limit how many frames/records get evaluated.
Note: fast locate-triage CLIs live under `tools/eval/`:
- `uv run python -m tools.eval.top_triples ...` (top `(seed,ref,out)` clusters, per-dataset + suite aggregate)
- `uv run python -m tools.eval.diff_locate --before <before.tsv> --after <after.tsv> ...` (row-level new/gone/delta diff for locate TSVs)
- `uv run python -m tools.eval.locate_discrete_mismatches ... --format tsv` (TSV source; columns are `seed,out,ref`)
- `uv run python -m tools.eval.locate_rollout_desyncs ... --out reports/triage/current_rollout_desyncs.tsv` (rollout first-break TSV; columns are `seed,out,ref` plus streak context and stable cluster key)
- `uv run python -m tools.eval.summarize_rollout_locate --in <rollout.tsv>` (top rollout desync clusters by frequency, streak-loss proxy, and seeded-break count)
- `uv run python -m tools.eval.diff_rollout_locate --before <before.tsv> --after <after.tsv>` (cluster-level new/gone/impact delta diff for patch review)

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
- `make preprocess-aggregate`: build/update cached `datasets/` for the broader mixed-FD aggregate suite
- `make validate`: run one-step suite eval (assumes datasets exist)
- `make validate OUT=reports/validation/one_step_suite_eval.txt`: write the report to a file (commit this)
- `make validate-aggregate`: write the aggregate one-step report to `reports/validation/aggregate_recent_one_step_suite_eval.txt`
- `make validate-rollout OUT=reports/validation/rollout_suite_eval.txt`: write rollout suite report to a file (commit this)
- `make validate-rollout-aggregate`: write the aggregate rollout report to `reports/validation/aggregate_recent_rollout_suite_eval.txt`
- `make rollout-capture ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`: capture rollout JSON snapshot (gitignored)
- `make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`: print suite + per-dataset rollout headline metrics
- `make rollout-diff ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json`: print rollout metric deltas
- `make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv`: write row-level rollout first-break TSV (gitignored)
- `make rollout-locate-summary ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv`: rank rollout desync clusters and per-dataset distribution
- `make rollout-locate-diff ROLLOUT_LOCATE_BEFORE=reports/triage/baseline_rollout_desyncs.tsv ROLLOUT_LOCATE_AFTER=reports/triage/current_rollout_desyncs.tsv`: diff rollout locate TSV clusters
- `make build_data`: extract ISO-derived `data/` artifacts
- `make guardrail-preflight`: fast gate (build + hard-row lock pack + seed==ref diff + float top-key diff)
- `make guardrail-preflight-full`: full suite tests + guardrail diff checks
- `make guardrail-baseline`: regenerate committed guardrail baseline fixtures under `tests/fixtures/guardrails/current_main/`
- `make forensic-rows ARGS='--row <dataset>:<record>:<p>'`: deterministic per-row forensic report under `reports/triage/`
- `make dolphin-engine-dump ARGS='--replay <path.slp> --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui --iso SSBM.iso --start-frame <f0> --end-frame <f1> --out-bin reports/triage/<run>/dump.bin'`
- `make dolphin-extract ARGS='--dump reports/triage/<run>/dump.bin --start-frame <f0> --end-frame <f1>'`
- `make dolphin-forensic-row ARGS='--row <dataset.msl:record:p> --dolphin refs/Ishiiruka/build/Binaries/dolphin-emu-nogui --iso SSBM.iso'`

Notes:
- Prefer `make build/test/validate` (they run `python/setup.py build_ext --inplace --force` via `uv run`); avoid invoking `python/setup.py` directly.
- If you touch collision/ledge code, run `tests/test_ledge_grab_treasuredbackkangaroo_regression.py`.
- `make guardrail-baseline` is for intentional baseline updates only (after a reviewed mainline behavior change). Do not refresh it during normal iteration.
- Active Dolphin workflow is playback CLI dump + extraction (`tools/dolphin/README.md`). Legacy live probes were moved under `tools/dolphin/legacy/`.

Validation output snapshots:
- Commit the latest suite reports under `reports/validation/` whenever you change core sim logic:
  - `reports/validation/one_step_suite_eval.txt`
  - `reports/validation/rollout_suite_eval.txt`
  - `reports/validation/aggregate_recent_one_step_suite_eval.txt`
  - `reports/validation/aggregate_recent_rollout_suite_eval.txt`

Variables:
- `SUITE=replays/suites/fox_falco_fd_ucf084_recent.json`
- `AGG_SUITE=replays/suites/aggregate_recent.json`
- `DATASETS_DIR=datasets`
- `CHUNK=4096`

## Working Conventions

- Prefer adding new “mechanics we learned about” into `SPEC.md` (Mechanics Inventory) immediately, even if not implemented yet.
- Avoid symlinks for tooling/binaries; prefer explicit paths in config.
- Never hand-edit `reports/validation/one_step_suite_eval.txt` or `reports/validation/rollout_suite_eval.txt` (generator-owned); put extra notes in commits/PR text or separate docs.
- Triage/debug scripts must default outputs under gitignored `reports/triage/` (or print to stdout). Never default to `/tmp`. Never stage files under `reports/triage/`.
- Rollout-first workflow (recommended when treating rollout as primary):
  1) Capture baseline once: `make rollout-capture ROLLOUT_JSON=reports/triage/baseline_rollout_streaks.json`
  2) Capture row-level baseline once: `make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/baseline_rollout_desyncs.tsv`
  3) After changes, capture current: `make rollout-capture ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`
  4) After changes, capture current row-level locate: `make rollout-locate ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv`
  5) Inspect headline metrics: `make rollout-summary ROLLOUT_JSON=reports/triage/current_rollout_streaks.json`
  6) Inspect row-level priorities: `make rollout-locate-summary ROLLOUT_LOCATE_TSV=reports/triage/current_rollout_desyncs.tsv`
  7) Compare before/after headline metrics: `make rollout-diff ROLLOUT_BEFORE=reports/triage/baseline_rollout_streaks.json ROLLOUT_AFTER=reports/triage/current_rollout_streaks.json`
  8) Compare before/after row-level clusters: `make rollout-locate-diff ROLLOUT_LOCATE_BEFORE=reports/triage/baseline_rollout_desyncs.tsv ROLLOUT_LOCATE_AFTER=reports/triage/current_rollout_desyncs.tsv`

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
