# Resident step/output fusion — rejected 2026-08-04

## Parent and workload

- Branch: `perf/decomp-throughput`, dirty aggregate based on `02cfe013`.
- Same-source frozen unfused control SHA-256:
  `2db1026f52cf6361e6a7e33aba4ec035c4a511b17ca88a2dd3ee63acfd197526`.
- Exact fused candidate SHA-256:
  `9de7f89d2ba9937dfc3ae117f96392a863ca0a74b88f1d7bfe0dfc8a7c455909`.
- Balanced 366-case manifest, strict native release build, CPU 0, resident 256 and 512,
  131,072 measured match-frames, eight warmup ticks.

## Intended owner and deletion boundary

The runtime batch loop would replace the production operation's three resident sweeps (`step` all
Matches, then observe all, then terminal all) with one per-Match
`step -> observation -> terminal` loop. Match state and caller-owned output rows remained the only
canonical state. Standalone APIs, masked-step behavior, scheduler ordering inside each Match,
output formats, allocation, and save/restore were unchanged.

This was not the previously rejected phase-interleaved scheduler: it kept a complete scalar Match
frame intact and moved only that Match's already-demanded output publication immediately after it.

## Correctness and controlled result

The 32,768-frame production digests stayed exact at both sizes (`4124834367a202ec` and
`588be8489df1a62d`). The longer isolated results were:

| Resident | Unfused control (c/f) | Fused candidate (c/f) | Result |
|---:|---:|---:|---:|
| 256 | 37,402.9 / 37,041.9 | 37,577.9 / 37,719.9 | 0.5% / 1.8% slower |
| 512 | 36,604.7 | 37,695.2 | 3.0% slower |

## Decision and revisit criterion

Reject and remove the packet. A complete scalar frame churns more state than remains usefully hot
for output publication, while interleaving the observation writer gives up the instruction and
sequential-output locality of the existing dedicated sweeps. No code or API was salvaged.

Revisit only if the simulator's canonical Match representation becomes materially smaller or the
outputs consume a new compact hot state directly. Reordering the current scalar operations is not
a cache-locality win.
