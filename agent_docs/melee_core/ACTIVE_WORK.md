# Active packet — semantic public batch API

Replace the validation-shaped native surface with one compact public API shared conceptually by
the Python `EnvBatch` wrapper and direct C/PufferLib callers. Internal replay, viewer, scheduler,
and exact-wire contracts remain private and keep their existing representation.

## Ownership and deletion boundary

- Final owner: `src/api.{h,c}` owns the semantic public C contract; `melee_sim/` is its Python
  wrapper. Runtime batch scheduling remains under `src/runtime/` and exact validation/viewer wire
  types remain internal.
- Canonical public state: opaque `MslBatch`, semantic reset/input/observation/terminal structs,
  contiguous batch arrays, byte reset masks, and arbitrary-index copy/save/restore.
- Consumers: Python/Slippi-AI, direct C/PufferLib, and future native integrations.
- Private consumers: validation, replay benchmarks, and the Wasm viewer may use an explicitly
  internal runtime header when they require exact wire, striding, stage events, compare state, or
  viewer projections.
- Displaced interface: public inclusion of `runtime/wire.h`; public `MslCore*` names; public
  stride/mask-stride arguments; packed `facing_and_port`; replay-only input/config fields; public
  compare/viewer projection calls; and duplicated, ceremonial README examples/reference text.
- Deletion boundary: no public declaration or Python binding may depend on `MslCoreCompare`,
  `MslCoreViewerState`, replay stream fields, packed controller-normalization fields, or the old
  `msl_core_*` batch surface. There is no compatibility alias for this unreleased API.

## Final behavior

- `msl_batch_create(data_root, count, out)` owns immutable game data and mutable match state behind
  one handle, matching Python `EnvBatch` ownership.
- `msl_batch_reset()` resets all or selected environments from contiguous configs and writes the
  selected initial observations.
- `msl_batch_step()` advances every environment from contiguous controller rows and writes current
  observations plus terminal state in the same call.
- Public config defaults encode the supported RL ruleset: FD, two players, four stocks, normal
  damage, UCF, and deterministic seeds. Public fields are semantic rather than replay-capability
  plumbing.
- Copy and savestate operations retain arbitrary destination indices and complete match ownership.
- Normal gameplay calls allocate nothing after batch creation.

## Completion

- [x] Self-contained compact public header and thin implementation boundary.
- [x] Python wrapper and public C smoke use only the new semantic API.
- [x] Exact validation, benchmark, and viewer paths use private runtime interfaces only.
- [x] README Python/C examples are short and accurately describe the public surface.
- [x] Native/Python/save-restore, full 153-replay, Wasm parity, viewer, and source-sync gates pass
  without a correctness or benchmark-contract change.

## Log

- 2026-07-18: Packet opened at committed `78b63f0a`. The current runtime ownership is sound, but
  `src/api.h` exposes a 402-line packed internal wire header and validation/viewer concepts, while
  ordinary reset/step calls require seven stride/mask arguments. The final semantic boundary and
  complete removal list above are fixed before implementation.
- 2026-07-18: Retained the direct public/private cut. `src/api.h` is self-contained; one public
  `MslBatch` owns data and runtime state; configs are semantic; contiguous reset/step calls write
  observations directly; and public save artifacts carry viewpoint/cutoff state across arbitrary
  indices. Exact replay, compare, strided scheduling, and viewer projections moved behind
  `runtime/batch.h`. The new C smoke and Python API/restore tests pass. Python controller buffers
  now begin neutral instead of encoding full down-left input when left untouched. Next: prove the
  unchanged validation/Wasm consumers and complete replay output contract.
- 2026-07-18: Packet complete. The public shared object exports the 12 semantic `msl_*` operations
  plus the Python-only controller converter; no private `msl_core_*` symbol is exported. Python now
  projects restored state explicitly with `observe()`, and public ABI sizes are asserted in C and
  Python. Evidence: 38 repository tests; public/native/PPC smoke; 63 exact plus 90 unchanged
  classified replay results over 1,415,476 frames; Wasm parity/viewer smoke; source sync and format
  gates; and the unchanged 512-environment replay digest `3fb5823d90657775` at 82,849 FPS. The
  standard exact replay benchmark remains private by design; a public-call-path benchmark is a
  separate performance packet, not an API compatibility bridge.
