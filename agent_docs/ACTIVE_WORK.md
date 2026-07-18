# Active packet — production-path subsystem profiler

Add one opt-in profiler for the resident replay benchmark without changing the runtime schedule,
workload, outputs, or ordinary production objects. Record the current committed throughput and
subsystem profile in `agent_docs/CURRENT_BASELINE.md`.

## Ownership and deletion boundary

- Final owner: `src/runtime/subsystem_profile.{h,c}` owns cycle accounting and reporting.
- Canonical measured path: the existing scalar `msl_core_batch_step_matches` path, including its
  exact per-match source scheduler, observation writes, and terminal writes.
- Consumers: the private replay benchmark, one bounded root Make target, and the current-baseline
  document. Profiling is never part of the public C or Python API.
- Profile state: process-global counters exist only in the dedicated instrumented executable.
  Timers use RDTSCP and subtract measured timer overhead from every sample.
- Displaced code: the rejected `MSL_CORE_PHASE_PROFILE` two-match interleaving scheduler and its
  Makefile/test declarations.
- Deletion boundary: no profiling option may select a different scheduler, tile matches, skip
  output work, change replay seeds, or alter normal native/Python/Wasm objects.

## Completion

- [x] Dedicated profile build leaves the production execution path structurally unchanged.
- [x] Output separates whole-frame phases, important nested fighter work, and scheduled source
  callback owners with symbol names.
- [x] One bounded command reproduces the profile on the packed 153-replay workload.
- [x] `agent_docs/CURRENT_BASELINE.md` records current committed 256/512 throughput, digests,
  profiler output, exact commands, host, and commit.
- [x] Production digests and focused correctness/build gates remain unchanged.

## Log

- 2026-07-18: Opened at `bae97324`. The retained subsystem table exists, but its accurate
  unchanged-schedule instrumentation survives only in stash `0d17defb`; the in-tree
  `MSL_CORE_PHASE_PROFILE` instead selects a rejected two-match scheduler. Retain only the useful
  low-overhead timing boundaries, port them to canonical `src/`, and delete the rejected path.
- 2026-07-18: Retained the dedicated RDTSCP profiler build. It keeps the production scalar loop and
  callback order, reproduced the 512 digest `3fb5823d90657775`, and reports the public contract,
  scalar phases, fighter drill-down, and 55 symbolized callback owners. Rejected profiler-only FPS
  as a throughput metric because the nested counters intentionally add diagnostic overhead.
- 2026-07-18: Refreshed uninstrumented CPU-0 evidence: 256 = 61,826–61,835 FPS with digest
  `bdc54107c51fa3d7`; 512 median = 83,013 FPS (82,756–84,651) with digest
  `3fb5823d90657775`. Recorded provenance and profile output in `CURRENT_BASELINE.md`. Next: source,
  native, validation, and Wasm compile gates; then audit the final diff without committing.
- 2026-07-18: Closed verification. `source-check`, `native-smoke`, the complete 153-replay gate
  (`63 PASS`, `90 CLASSIFIED`, zero XPASS/fail/error), and `wasm-smoke` are green. The profile code
  is absent from normal objects, and the rejected schedule-altering phase profiler is deleted.
- 2026-07-18: Naming review removed the inherited generic `core` qualifier from every profiler
  symbol and compile guard (`msl_profile_*`, `MslProfileBucket`, `MSL_PROFILE_*`). Recorded the
  plain `msl_` namespace rule in `AGENTS.md`; the older runtime-wide naming debt is a separate
  boundary cleanup, not part of this profiler packet.
