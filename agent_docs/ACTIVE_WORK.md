# Active performance packet — direct fighter animation node binding

## Objective

Bind each Figa attachment directly to its immutable dense pose-node descriptor. Delete the linear
per-attachment scan over a program's nodes while preserving exact transition semantics and the one
shared dense sample representation.

## Final boundary

- **Final owner:** `MslFighterPosePrograms` owns one immutable track-start-to-node map alongside its
  program/node/sample tables.
- **Canonical state:** the map contains relative descriptor indices only. JObj SRT, compact joint
  animation state, and the source decoder remain the complete mutable/save-state-visible state.
- **Consumers:** `msl_fighter_pose_attach_figa` resolves the Figa program, indexes the map by the
  source track offset, verifies the track count, and binds the resulting global node index.
- **Displaced work:** every attachment no longer linearly compares every node's track start/count.
- **Deletion boundary:** the scan loop is removed. There is no last-program cache, hidden cursor,
  mutable source-tree field, per-Match lookup state, fallback scan, or character/action list.

## Evidence and acceptance

- The committed `90078dfb` profile assigns 14.53% to pose animation and 9.89% to action-animation
  callbacks after the dense publication cut.
- A gprof diagnostic recorded 2,417,704 attachment calls over 131,072 match-frames. Its sampling
  overhead is not throughput evidence, but the call count proves the repeated lookup is material.
- Retain only a repeatable whole-frame resident-512/256 gain, unchanged digests/classifications,
  unchanged per-Match memory, and the complete replay/API/save-restore/allocation/PPC/Wasm/viewer
  gate.

## Log

- 2026-07-19 — `open`
  Scope: immutable Figa program metadata and the animation attachment binding site.
  Hypothesis: a compact per-track relative-node map deletes the dominant repeated scan while keeping
  all source transition and decoder work unchanged.
  Evidence: 2.42 million calls currently perform a binary program lookup plus linear node scan in a
  131,072-frame diagnostic; the containing action/pose owners total over 24% of the cycle profile.
  Disposition: add the sidecar map during GameData initialization and replace the scan with one
  indexed load and exact assertions.
  Next: implement the shared map, verify both production digests, and run adjacent 512 measurements.

- 2026-07-19 — `retained`
  Scope: per-program track-start map plus direct Figa-tree hash lookup.
  Hypothesis: deleting both the linear node scan and binary program search should turn each
  attachment into bounded direct shared-data lookup.
  Evidence: resident-512 controls are 47,184.4/47,185.9/47,623.1 cycles/frame and candidates are
  46,168.3/46,458.8/45,916.6, reducing the median 2.16%. Resident-256 controls are
  44,781.4/45,816.6/44,986.5 and candidates are 43,561.5/43,367.1/43,492.5, reducing the median
  3.32%. Digests remain `6f91f23e3553a090` / `8ef126a41244d514`.
  Disposition: retain the final direct binding owner; it adds only immutable process-global lookup
  data and no per-Match state.
  Next: complete the full material gate and shared-memory census, then commit atomically if green.

- 2026-07-19 — `retained`
  Scope: complete material gate and shared-data census.
  Hypothesis: direct lookup must preserve every transition, build, and state contract and keep its
  memory cost process-global.
  Evidence: debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED across
  1,415,476 frames. API/copy/save-restore, 633,432-byte sealed arena, PPC, Wasm parity, viewer/
  browser, 38 Python tests, source sync, and formatting are green. The 261,195-entry track map,
  4,096-slot program hash, and larger descriptors add 544,046 shared bytes; per-Match state is
  unchanged.
  Disposition: packet complete and ready for atomic implementation/evidence commit.
  Next: commit, notify the retained win, refresh profiling only when needed, and select the next
  bounded high-impact owner.
