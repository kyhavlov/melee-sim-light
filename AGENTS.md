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
- Build ISO-derived data artifacts (gitignored): `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnba --chars fox,falco`

Note: `uv sync` only manages declared dependencies; re-run `uv pip install -e python` after syncing if the extension is missing.
Note: pass `--force` to `preprocess_suite` after any dataset schema changes (the cache is just for convenience).
Note: `--chunk` in evaluators is the in-memory batch size for processing; it does **not** limit how many frames/records get evaluated.

Avoid invoking `pip` directly unless it is being run through `uv` (e.g. `uv pip ...`).

## Working Conventions

- Prefer adding new “mechanics we learned about” into `SPEC.md` (Mechanics Inventory) immediately, even if not implemented yet.
- Avoid symlinks for tooling/binaries; prefer explicit paths in config.
