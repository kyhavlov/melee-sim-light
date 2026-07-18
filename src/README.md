# Simulator architecture

## Ownership

- `src/api.{c,h}` is the only public API.
- `src/runtime/` owns shared game data, per-match state, scheduling, projections, and savestates.
- `src/platform/` owns native/PPC/Wasm hosting and raw DAT translation.
- `src/melee/` and `src/sysdolphin/` retain source-shaped gameplay ownership.
- `src/stubs/ledger.tsv` records explicit headless exclusions.
- `tools/data/` extracts the minimal supported retail profile consumed directly by the runtime,
  without depending on a second simulator or generated gameplay metadata.
- `tools/validation/` loads replays once through Peppi and performs per-frame work in C.
- `tools/viewer/` builds the public API to Wasm for live play.

Immutable `MslCoreGameData` is shared by every match in a batch. Mutable state is match-owned and
can be copied or serialized from one batch index and restored into any compatible index. Normal
runtime paths allocate nothing after initialization.

## Correctness

The canonical gate is the 153-replay `melee_core_aggregate` suite. Exact rows must remain exact;
the existing classified rows are locked by complete mismatch fingerprints and may not be widened.
Validation uses replay RNG/stage-event authority where Slippi playback does, while free-running
runtime semantics remain source-owned.

```bash
make validation-suite
make validation-supported-domain
```

The first is the normal filtered-capable runner. The second fixes the complete supported manifest
as a material checkpoint. PPC remains a build/smoke oracle; native is the production validation
backend and owns the complete output locks.

## Source synchronization

`tools/build/source_sync.sh` compares canonical imported source against the commit pinned in
`src/upstream.lock` while accounting for the flattened canonical layout.

```bash
make source-check
tools/build/source_sync.sh diff > /tmp/melee-source.patch
```

Local gameplay deltas are classified in `src/upstream_delta_ledger.tsv`.

## Performance

The production benchmark uses packed replay input tapes, resident matches, ordinary free-running
gameplay, and the 128-frame observation/terminal ring expected by RL consumers. Python and replay
comparison are outside the timed loop. See the
[performance record](../agent_docs/melee_core/PERFORMANCE.md) for the retained baseline and
measured subsystem breakdown.
