# Active work

## Completed performance recovery

Cleanup is committed at `83356f34`. The subsequent code, tests and performance
evidence are included in **Optimize exact pose and animation hot paths**.
The 120,000 FPS goal is met at both resident sizes: eight-sample medians are
124,141.5 FPS at 256 and 120,606.5 at
512. All 16 adjacent comparisons improve, with median paired gains of 8.07%
and 8.62%; all 32 runs are retained. Build, benchmark and host settings are unchanged.

Retained owners are canonical pose dirty/root traversal, direct field access,
source-exact scalar/vector quaternion math, packed SRT publication and masked
affine translation. The existing sample table has canonical JObj field ordering,
zero valid frames for empty nodes, and a 64-byte initialized tail for contiguous
wide reads. No new Match state, snapshot format or gameplay allocation remains.
All rejected experiments, including vertex/scheduler layouts, are removed.

Source/native/PPC/API/copy/save/sealed-allocation checks, 127 Python tests,
both full 512-recording gates (4,721,700 transitions each, zero diagnostics or
failures), and Wasm/viewer/browser smoke pass. The gated text matches the frozen
final benchmark; every original tape and the committed control hash match.

The [current baseline](performance/BASELINE.md),
[retained ledger](performance/RETAINED.md), and
[complete evidence](performance/HISTORY.md#final-code-recovery--2026-09-21)
record the reviewable result. Raw evidence and the archived completed worklog
are under `reports/triage/perf_120k_20260921/final-load/`.

## Protected workspace work

The unrelated `experiment/million-fps-proof` changes remain in stash
`pre-mewtwo-pr-23-review-2026-09-17` (`10f5e030`). Do not apply or discard them.
