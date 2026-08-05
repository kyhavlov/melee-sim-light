# Gameplay-live compiled Figa samples — rejected 2026-08-04

## Parent and workload

- Branch: `perf/decomp-throughput`, dirty aggregate based on `02cfe013`.
- Immediate control and candidate were compiled from the same source and flags. The control forced
  every direct program node live only during GameData finalization; the candidate used the complete
  construction-derived live set. Both frozen diagnostic binaries are ignored under
  `reports/triage/`.
- Workload: balanced 366-case manifest, strict native release build, CPU 0 on the V-cache CCD,
  resident 256 and 512, 65,536 measured match-frames, eight warmup ticks, ABBA ordering.

## Intended owner and deletion boundary

`MslFighterPosePrograms` would remain the one immutable exact integer-frame sample owner. During
GameData construction, every loaded motion record would map its source Figa nodes through the
source part flags and `ftPartsRemap` into every structurally possible supported target. Only nodes
mapping to `ftParts_HeadlessGameplayMask` parts would receive exact sample rows. Runtime attachment
asserted that its node had been admitted.

The packet added no gameplay state, cache, allocation, fallback, second table, approximate value,
or replay-derived admission. Non-direct decoder ownership was unchanged. PPC and Wasm were
unchanged.

## Implementation and correctness

Initialization was split into metadata collection, all-motion node admission, and final exact
sample compilation. Common-source (`FTKIND_NONE`) motions were closed across every loaded target;
fighter-owned motions were closed for their owner. The complete closure retained 14,333,871 of
19,855,156 float values, removing 5,521,285 values: 21.1 MiB or 27.8% of the 75.7 MiB stream.

The runtime admission assertion survived all 366 benchmark cases and 200,700 pre-roll match-frames.
The 256 and 512 short benchmark digests remained `e6f2a9b270b4161b` and `75046e348333b63b`.

## Controlled performance result

| Resident | All-row control arms | Compact candidate arms | Geometric-center throughput |
|---:|---:|---:|---:|
| 256 | 38,322.8 / 39,176.4 c/f | 38,648.1 / 39,548.4 c/f | -0.90% |
| 512 | 38,281.4 / 38,072.8 c/f | 39,109.4 / 37,044.5 c/f | +0.30% |

The compact form produced a large gain on CPU 8's non-V-cache CCD, confirming that the memory
reduction is real, but that is not the campaign's canonical target. On the V-cache CCD the complete
sample stream already fits the relevant cache regime closely enough that deleting cold rows does
not repay the changed frame-row layout at resident 256.

## Decision and revisit criterion

Reject and remove the production packet. No implementation was salvaged. The dictionary preflight
was also rejected: only 58.18% of values fit the hottest 65,535 global bit patterns, while
per-program 16-bit dictionaries reduce 79,420,624 bytes to just 78,298,672 before metadata and add
an indirection to every publication.

Revisit only if the canonical hardware target changes materially, or if a new canonical pose
representation deletes demanded sample publication/traversal in addition to immutable bytes. A
smaller table by itself is not sufficient evidence.
