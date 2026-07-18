# Active performance packets — compiler/layout, then match pools

Work sequentially. A candidate is retained only when it performs the same production contract with
the established digests and no widened replay classification. Record rejected compiler and pool
experiments in `PERFORMANCE.md`; append only surviving changes to `RETAINED_PERFORMANCE.md`.

## Packet 1 — exact-safe compiler and layout closure

- Final owner: the root `Makefile` native-release profile.
- Canonical state: one source-shaped runtime and one optimized native executable; profile data is
  build-only input and never runtime state.
- Consumers: native release benchmark and validation executables. PPC and Wasm retain their own
  compiler contracts.
- Displaced code/state: none. This packet changes optimization/code layout only; it may not add a
  gameplay fallback, float workaround, alternate runtime, or generated artifact under source
  control.
- Deletion boundary: rejected profiles and their build artifacts are removed. Only flags/profile
  workflow with repeatable production throughput and exact output may remain.

Sequence: recheck global `-O1`/`-O2` on the current tree, test useful whole-owner levels if global
optimization fails, then train and evaluate replay-guided PGO/code layout on the strongest exact
base. Use adjacent CPU-0 controls at 512 and the 256 cache-pressure checkpoint. Run the complete
153-replay gate for every candidate that is both exact-digest and materially faster.

## Packet 2 — arena and source-pool compaction

- Final owner: `runtime/scalar.c` match construction and the source `HSD_ObjAlloc` pools it seals.
- Canonical state: one Match arena containing the source intrusive pools, sized by supported-domain
  construction and explicit runtime headroom. No second allocator or pool representation.
- Consumers: source object allocation, gameplay, relocation, copy, and arbitrary-index savestate.
- Displaced state: free objects reserved by the uniform all-pool floor or owner counts that exceed
  a supported runtime need; empty/unreached pools receive no speculative reserve.
- Deletion boundary: replace broad reservation with explicit per-owner capacity. Do not retain a
  compatibility reserve, dynamic growth after sealing, or replay-keyed capacity. Construction-live
  objects and every runtime-reachable supported owner remain source-shaped.

First measure construction and full-replay high water by source pool. Capacity decisions must also
respect public player/item limits and source mechanics rather than treating the replay maximum as a
complete proof. Report per-environment arena/savestate reduction, four-player maximum, 256/512
throughput, and allocation-lock status.

Temporary measurement may touch `runtime/main.c`, the native validation runner, and an opt-in Make
define solely to emit per-job pool high water. That instrumentation is outside the final owner and
must be deleted before retention.

## Completion gates

- Same 256/512 benchmark workload and digests as `CURRENT_BASELINE.md`.
- `source-check`, `native-smoke`, complete supported-domain validation, and `wasm-smoke` green.
- No heap allocation after initialization; copy and arbitrary-index save/restore remain green.
- `CURRENT_BASELINE.md` and `RETAINED_PERFORMANCE.md` describe the final retained result.

## Log

- 2026-07-18: Opened at `6399f75b`. Current production baseline is 61,830 FPS at 256 and
  83,013 FPS at 512 with digests `bdc54107c51fa3d7` and `3fb5823d90657775`. Began by recovering
  retained performance changes from Git history before compiler experiments.
- 2026-07-18: Global `-Og` and `-O1` improved the 512 diagnostic to roughly 102k/119k FPS but
  changed both production digests; the `-O1` full-suite worker also crashed. Global `-O2` exposed
  an unresolved unsupported-Ness reference at link. Rejected all three blanket profiles without a
  workaround. Replay-trained PGO over the existing exact allowlist preserved both digests and
  reached 86.1k at 512 but was neutral at 256; repeat A/B remains open. Next: test `-O1` only on
  measured complete O0 owners, then decide whether PGO plus any new exact owners is material.
- 2026-07-18: Closed the compiler sweep with one retained replacement. `lb_00B0.c -O2` is exact
  and faster, while the existing `mpcoll.c -O3` owned a stale release-only
  `ExpertWorthlessFinch` output-lock failure. Restoring `mpcoll.c` to the source profile and
  admitting `lb_00B0.c` at O2 makes the release gate 63 PASS / 90 CLASSIFIED / zero failures and
  improves adjacent throughput about 2.6% at 256 and 3.0% at 512. Whole-runtime Og/O1/O2, other
  measured O1/O2 owners, and replay-trained PGO were rejected.
- 2026-07-18: Full-replay temporary pool instrumentation measured construction and runtime high
  water for all 153 cases, then was deleted. Replaced the uniform all-pool reserve with explicit
  runtime owners. Ordinary singles arena/savestate payload fell 921,656 to 685,888 bytes (-25.6%);
  maximum supported construction fell 1,266,476 to 997,972 (-21.2%); maximum source-pool storage
  fell 757,540 to 542,072 bytes (-28.4%). The pool cut is throughput-neutral in alternating 512
  runs and retains conservative AObj/FObj, matrix, process, item, and 151-link ceilings. Native
  smoke, allocation lock, 512 lifecycle, ordinary validation, and release validation are green.
  Next: final Wasm/Python/source gates and documentation audit.
- 2026-07-18: Closed both packets. `make test` passes all 38 tests plus native/API/save-restore
  smokes; source sync is clean; Wasm state/viewer parity passes with a 558,424-byte snapshot;
  ordinary and release 153-replay gates are green; format checks pass. The refreshed profiler
  preserves the 512 digest and records the expected map-collision share increase after restoring
  exact `mpcoll.c`. No experiment process or temporary profiler remains.
