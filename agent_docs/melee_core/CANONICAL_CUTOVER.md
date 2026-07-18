# Canonical simulator cutover

## Objective

Make the validated decomp-backed simulator the repository's only simulator. Promote it directly
under `src/`, make its batch API canonical, cut validation and the live Wasm viewer over to that
API, replace inherited old-simulator extraction dependencies, and delete the displaced simulator
and its tools, tests, and documentation. The cutover remains uncommitted for review.

The dirty compact-pose experiment is preserved as stash
`a5e6f8f20e94b5c3bd5f49ecfb9533168c9f4daa` and is not an input to this cutover.

## Final ownership and layout

- `src/api.{c,h}`: canonical public batch API, copy, and arbitrary-index save/restore.
- `src/runtime/`: mutable match state, immutable shared game data ownership, scheduler, projections,
  savestates, and runtime support.
- `src/platform/`: native/PPC/Wasm platform boundary and raw DAT loading.
- `src/stubs/`: explicit headless source exclusions only.
- `src/melee/`, `src/sysdolphin/`, and required decomp support roots: source-shaped gameplay.
- Root `Makefile`: extraction, native/PPC/Wasm builds, smokes, validation, and benchmarks.
- `melee_sim/`: thin public Python/NumPy adapter over the canonical C shared library; it owns no
  gameplay implementation.
- `tools/data/`: independently owned minimal ISO extraction and deterministic new-core artifacts.
- `tools/validation/`: replay storage, suite scheduling, native replay projection, and benchmark prep.
- `tools/viewer/`: live Wasm viewer and the renderer/assets it actually consumes.

There is no second runtime, custom CPython gameplay extension, or production compatibility bridge
after this cutover. C, Python, validation, and Wasm all use `src/api.h` and the same runtime.

## Preserve and promote

- Promote `src/melee_core/api.{c,h}`, `runtime/`, `platform/`, `stubs/`, and
  `gameplay/src/*` into the canonical paths above.
- Preserve `refs/melee`, `refs/slippi-ssbm-asm`, validation replays, suite manifests,
  classifications, and output locks.
- Preserve the batch API, save/restore, native runtime, PPC reference build, and Wasm runtime.
- Preserve the user-facing `melee_sim` API, replacing its old extension with a thin adapter over
  the canonical shared library.
- Preserve new-core C/API/Wasm/viewer/validation smokes and the full replay suite.
- Preserve source sync, native DAT layout generation, toolchain setup, replay storage, suite I/O,
  replay validation, and replay benchmark preparation under their final owners.
- Move `bindings/msl_replay_validate.c` into the validation owner before deleting `bindings/`.
- Preserve the live viewer and required Slippi viewer renderer/assets.
- Preserve concise retained performance evidence and the upstream-delta ledger.
- Preserve repository-local Melee skills and update their canonical source paths.

## Rewrite instead of carrying legacy dependencies

- Implement a minimal new-core-owned extractor which reads the ISO/FST, extracts only required
  retail files into `MSL_DATA_DIR`, deterministically produces required runtime artifacts such as
  `MSLPART1`, and imports nothing from `melee_sim` or the existing `tools/extraction`.
- Replace validation's `melee_sim.raw_data` import with the new data-root module.
- Update viewer build scripts to use the canonical build and extractor.
- Replace root build, package/tool metadata, AGENTS instructions, and formatting paths. Update the
  user-authored root README in place only where the canonical API and commands changed.
- Rewrite retained tests which currently import the old character registry, raw-data module,
  extraction package, or Dolphin replay patcher.
- Python owns no gameplay implementation: ISO extraction/code generation, one-time Peppi replay
  loading, suite scheduling, report formatting, and a thin public wrapper over C batch calls.

## Delete

- Every old-simulator root `src/` file not replaced by promoted canonical source.
- `msl_binding.py`, the old extension/package build, old `melee_sim` implementation modules, and all
  old bindings after the new-core replay validator is relocated.
- Old eval, modelplay, benchmark, extraction, validation-buffer/history, and package-smoke tools.
- Old simulator unit/regression/binding/preprocessing/modelplay/seed-history/API tests and fixtures.
- `agent_docs/SPEC.md`, old rewrite/system/workflow plans, historical packet accumulation, and
  tracked obsolete reports.
- Viewer/modelplay adapters and examples whose only consumer is the displaced simulator.
- Stale generated build/cache/egg artifacts after a fresh canonical extraction/build succeeds.
- Only the pose worktrees created for the preserved packet; unrelated worktrees are out of scope.

`SSBM.iso`, refs, replay inputs, and the currently working extracted data are not removed until an
empty-directory extraction with the replacement tool has succeeded.

## Completion gates

1. No production reference remains to the old `msl_binding`, `src/melee_core`,
   `tools/extraction`, `tools/eval`, or `tools/modelplay` runtime paths.
2. Fresh extraction from the ISO into an empty `MSL_DATA_DIR` succeeds.
3. Native release, PPC reference smoke, and Wasm builds succeed from canonical paths.
4. Batch API, copy, arbitrary-index save/restore, viewer, replay-validation, and replay-benchmark
   smokes pass.
5. The native supported 153-replay suite has the same or better exact/classified results and no
   new classification.
6. The retained 256/512 benchmark does not regress beyond ordinary measurement noise.
7. The live viewer builds solely through the canonical API.
8. Repository source, build logic, tests, and docs describe only one simulator.

## Execution order

1. Preserve the dirty packet and establish this contract.
2. Promote source and root build/API ownership, then cut validation and viewer paths over.
3. Establish the independent extraction/data-root path and prove a fresh empty-directory setup.
4. Delete the displaced runtime, bindings, tools, tests, docs, and reports in one cutover tree.
5. Repair only canonical-path fallout; do not add compatibility modules or aliases.
6. Run the completion gates, remove stale local artifacts, and leave the complete tree uncommitted.
