# Active packet — canonical simulator cutover

The approved scope and deletion contract are in
[`CANONICAL_CUTOVER.md`](CANONICAL_CUTOVER.md). This file is only the chronological execution log.

## Log

- 2026-07-18: Preserved the entire compact-pose packet, including untracked files, as stash
  `a5e6f8f20e94b5c3bd5f49ecfb9533168c9f4daa` with subject
  `wip: compact-pose-before-canonical-cutover-20260718`. Clean cutover baseline is HEAD
  `8fd36c6e252e7ddfbdface686509bac316b05ebf`.
- 2026-07-18: Started the canonical cutover. Final owner is the new decomp-backed runtime directly
  under `src/`; canonical mutable state is its batch/match state; canonical shared state is its
  immutable game data. Consumers are the C API, replay validation/benchmarking, and the Wasm live
  viewer. The displaced boundary is the entire old simulator, Python extension, preprocessing/eval
  stack, legacy extraction graph, old tests, and old documentation. No compatibility bridge is
  permitted.
- 2026-07-18: Promoted the decomp-backed API/runtime/platform/source graph directly under `src/`,
  moved its Makefile to the repository root, and cut validation/build/viewer paths over without a
  compatibility alias. Deleted the displaced C/Python simulator, extension, tools, tests, reports,
  and historical documentation.
- 2026-07-18: Replaced legacy extraction with `tools/data`: a stdlib ISO/FST extractor emits 81
  hashed raw files plus eight deterministic MSLPART1 pose closures from an audited source-owner
  manifest. Empty-root extraction completed in 0.67 seconds; fresh data passed native and PPC
  smokes and produced the same runtime pose masks.
- 2026-07-18: Completion evidence is green: source sync (132 classified deltas), 35 Python tests,
  native/PPC smokes, Wasm API parity, live and production browser viewer smokes, and the native
  153-replay gate (63 exact, 90 unchanged classified, 0 failure/error, 1,415,476 frames). Same-host
  alternating benchmarks are performance-neutral at -0.48% (256) and -0.53% (512), with unchanged
  digests. Final step is the repository/worktree residue audit; no commit is authorized.
- 2026-07-18: Residue audit found no production reference to the displaced simulator or its paths.
  Removed its ignored compiled extension/caches and the three packet-only worktrees. The two dirty
  diagnostic worktrees were preserved first as stashes `e8ca20e952019ee4755c8aa662570092ea0d7c70`
  and `266e29405e766011376f3a1049d1b06723c7af63`; the main packet remains preserved as
  `a5e6f8f20e94b5c3bd5f49ecfb9533168c9f4daa`. The cutover is ready for review and remains
  uncommitted.
- 2026-07-18: Review correction opened. `MSLPART1` and its two committed pose-pruning packets are
  explicitly frozen for a separate post-cutover audit. This correction preserves the user-authored
  root README as its editing base, moves the active simulator guide to `src/`, removes dead
  old-simulator Make targets, and audits the public Python/C API rather than documenting a deleted
  interface as if it still existed. No commit is authorized.
- 2026-07-18: Correction implementation complete pending gates. The root README retains its original
  structure, introduction, Python example, C example, viewer section, and API reference, with only
  obsolete API names/contracts updated. The simulator guide is now `src/README.md`; the root
  Makefile exposes only canonical extraction/build/smoke/validation/benchmark targets. The public
  Python package now calls the sole `src/api.h` runtime through a hidden-visibility shared library;
  the old CPython extension and gameplay implementation remain deleted. Next: run focused API,
  build, replay, and viewer gates without touching the deferred MSLPART1 boundary.
- 2026-07-18: Correction gates retained. Source sync remains 132 classified deltas; 39 Python/tool
  tests, native and PPC smokes, and the public Python step/arbitrary-index restore checks pass. The
  complete replay gate remains 63 exact, 90 unchanged classified, zero XPASS/fail/error, and
  1,415,476 compared frames. Wasm parity and live/production browser viewer smokes pass. Final
  residue found no production use of the displaced API/build/tool paths; MSLPART1 was not modified
  or audited. The coherent cutover is ready to stage and remains uncommitted.
