# AGENTS.md — melee-sim-light

## Project Goal

Implement a **high-performance, batched, deterministic** SSBM-like simulator for RL.

Current target domain:
- Included fighters are **Fox, Falco, Marth, Sheik, Zelda, and Captain Falcon**. Fox/Falco remains
  the mature control surface; UCF is enabled by default.
- RL 1.0 correctness covers the supported legal-stage aggregate: Final Destination, Battlefield,
  Fountain of Dreams, frozen Pokemon Stadium, Yoshi's Story, and Dream Land N64.
- Singles (2 players) remains the primary/control workflow, but the suite includes doubles
  coverage and the runtime is expected to keep 4-player/2v2 paths viable rather than treating them
  as a future rewrite.

The primary correctness strategy is **gameplay-relevant source completion**: port the relevant
decomp call graphs, persistent state, tables, callback ownership, and scheduler order for the
supported domain closely and directly. Organize work by source owner, not validation mismatch.
Validation is evidence that challenges or supports a source-completion claim; replay names and rows
must not become the implementation queue. Prefer ambitious close rewrites and deletion of the
piecemeal implementation when the source boundary crosses several local files or systems.

Use completion terms precisely:

- **Architectural cutover complete** means one source-shaped runtime owns the boundary and the old
  implementation is deleted; connected semantic work may remain.
- **Source owner complete** means every gameplay-relevant path in the declared supported boundary
  is represented directly, with explicit source-backed exclusions.
- Do not preserve old machinery, add a runtime bridge, or narrow the source boundary merely to keep
  intermediate validation green.

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
- When touching an area that still uses local semantic lists, callback ids, action-family switches,
  or duplicated decomp-derived predicates, check whether the distinction can be promoted to an
  extracted/generated data substrate. If source/decomp data can express it cleanly, prefer the
  table-backed refactor as part of the change rather than preserving another local runtime list.
- When adding a new runtime-required extracted JSON/bin field, update the full extraction/export
  contract in the same change: source extractor, stable JSON key ordering / binary layout, runtime
  required-key loader, packaged-data fallback if any, and a fresh-extract smoke path. Do not rely on
  old checked-in/generated local data retaining the key.

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
- Runtime/rollout bridges are not acceptable closure. If gameplay needs hidden state, live pose, or
  callback phase, model that owner explicitly instead of adding a compensating runtime path. A
  teacher-forced seed reconstruction is acceptable only when it initializes real hidden source state
  for one-step reseed and does not change free-running gameplay semantics.
- RNG-owned behavior still requires deterministic pre-gate proof, but replay RAW-CLEAN work may
  model bounded source-owned RNG sites and stream phase directly. Prefer decomp-backed site
  ledgers, explicit seed/consume state, and reusable owner helpers for sites such as
  DamageFlyRoll and Wait animation variants. Avoid broad global RNG reconstruction until the
  target task requires it, and do not replace requested RNG modeling with new exceptions.

### C Core + Thin Python
- All gameplay / physics / combat logic lives in **C** under `src/`.
- Public Python under `melee_sim/` is a thin wrapper and tooling layer only.
- CPython extension and native preprocessing helpers live under `bindings/`.
- Preprocessing/eval derivation is also a hot path. Do not add per-frame/per-item Python loops,
  candidate searches, state maps, or repeated JSON/data reads to seed generation. New seed-lane
  derivation must use native C (`bindings/msl_validation_*.c`, `bindings/msl_preprocess_*.c`,
  `bindings/msl_binding_*.c`, or `bindings/msl_binding.c`) or include timing
  proof that the Python path is negligible.
- Normal validation is expected to be fast enough for frequent local use. Treat major validation
  wall-time or single-core throughput regressions as performance bugs.
- Normal validation must run from replay-derived `ValidationReplayBuffers` and native validation /
  preprocessing helpers. Python may schedule suites and format reports, but it must not reintroduce
  wide row materialization, persistent validation caches, or per-frame/per-row derivation loops in
  the normal validate-all / heldout path.

## Required Last-Mile Behavior

- Treat a mismatch as a **triage entry point**, not the patch boundary.
- For each selected mismatch, first identify the shared data-backed owner family when possible:
  MotionState callbacks, script events, item/article kind, stage segment, part/anchor, or explicit
  seed/provenance lane. Prefer closing that owner family over fitting the motivating row.
- If decomp/data shows a feasible general source-owner fix that would cover the observed bug and
  other plausible rollout/replay variants, implement that general owner. Do not retain a narrower
  downstream action/row/stage exception just because it fixes the current replay suite. Lack of a
  current replay exercising every variant is not a reason to avoid the general fix.
- Source-completion work is encouraged: if a decomp/data-backed owner family is identified and
  bounded, it is valid to implement the full owner even when some covered variants do not currently
  have replay mismatches. Treat this as distinct from replay-fitting. Cover observed rows plus
  practical synthetic positive/negative variants, regenerate validation, and investigate results
  that disprove the claimed source boundary. Call out in review whether a packet is a
  behavior-equivalent refactor or source-completion extension; zero replay-level reds is not a
  completion requirement.
- Do not stop at the first motivating row or one small owner slice.
- Do not return with diagnosis-only prose if implementation, extraction, probes, locks, or validation remain credible local next steps.
- Do not package bare behavior-neutral substrate work as a checkpoint by itself.
- Do not back out a correct source-clear mechanic solely because current validation metrics are unchanged.
  If a decomp/data-backed behavior is bounded, allocation-safe, on the RL 1.0/system path, and
  covered by focused positive/negative tests, retain it as source-completion work.

## Working Conventions

- Do not commit unless the prompt explicitly says to.
- Treat `agent_docs/SPEC.md` as legacy old-simulator documentation; do not add source-shaped
  Melee-core plans, mechanics, or investigation results there. Keep new-core documentation under
  `agent_docs/melee_core/`, using its README for the current phase/worklog and adding a focused
  document there only when a distinct durable subject outgrows that README.
- Never hand-edit generator-owned validation reports.
- Triage/debug outputs default under gitignored `reports/triage/`, not `/tmp`.
- Never key gameplay behavior on dataset name or record id.
- The committed benchmark report is optional for ordinary changes. Before committing a change
  likely to noticeably affect simulator performance, refresh it with `make benchmark-report`.

### Native Extension Build

When running MSL from a virtualenv, build with that same Python:

```bash
make build PY=/path/to/venv/bin/python
```

If something seems stale, print `melee_sim._native.__file__` before debugging further.

## Validation Requirements

### Tests
- Unit / fast tests: `make test`
- Formatting check: `make fmt-check`
- Core sim logic changes: `make validate-all`
- Do not add one-off tests for validation replay rows already covered by `make validate-all` /
  heldout reports.
- Prefer source-owner unit tests with synthetic seeds/inputs for mechanics not isolated by
  validation reports.
- Add tests for native binding shape, data/extraction contracts, no-allocation/runtime contracts,
  validation runner/reporting behavior, and modelplay/Dolphin/out-of-band bugs.
- A replay-row test is allowed only if it is out-of-band from validation coverage or documents why
  validation reports cannot protect the invariant.
- Tests that only load a validation replay row to assert normal one-step/rollout parity are
  presumed redundant; do not add or restore them without documented out-of-band coverage.
- Do not add one file per mismatch row; fix the owner and cover the owner.

### Validation Reports
- Test-only changes should not refresh committed validation reports.
- If core sim logic changes, refresh these reports:
- `reports/validation/one_step_suite_eval.txt`
- `reports/validation/rollout_suite_eval.txt`
- `reports/validation/aggregate_recent_one_step_suite_eval.txt`
- `reports/validation/aggregate_recent_rollout_suite_eval.txt`
