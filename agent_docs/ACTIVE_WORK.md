# Active performance packet — dense ordinary pose samples

## Objective

Replace the ordinary integral-frame Figa publication loop with an immutable, node-indexed dense
sample table. Publish each supported node's exact SRT values directly to its canonical JObj without
scanning source tracks, probing a validity bit per track, or redispatching component types.

## Final boundary

- **Final owner:** shared `MslFighterPosePrograms` owns one descriptor and dense sample stream per
  Figa node; each live pose joint binds its descriptor once when the animation is attached.
- **Canonical state:** JObj SRT fields and flags remain the only mutable pose state and remain fully
  save/restore-visible. Tables are immutable shared GameData.
- **Consumers:** the existing integral, unit-rate ordinary animation path reads dense samples;
  fractional rates, paths, duplicate or unsupported channels, and other legal dynamic cases retain
  the exact mutable source decoder.
- **Displaced work:** ordinary publication no longer walks Figa tracks twice, reads source track
  types, probes sparse validity bits, calls generic type publication, or stores absent samples.
- **Deletion boundary:** the old program-wide sparse value/validity representation and runtime track
  scan are removed. There is no second mutable pose, legacy extractor, runtime mode, or fallback for
  a descriptor classified as dense.

## Evidence and acceptance

- The committed profile assigns 17.46% of frame cycles to pose animation.
- An earlier exact proof on `fe25fec5` improved resident-512 cycles/frame by 2.00% raw and 2.07% in
  adjacent pairs. It was rejected under a previous packet's 3% threshold, but is a durable owner
  representation rather than leaf tuning and is now being reconstructed against `d05e385c`.
- Retain only repeatable whole-frame gain at 512 and 256, unchanged digests and replay
  classifications, exact arbitrary-index copy/save-restore, no per-Match memory growth, and the full
  native/allocation/PPC/Wasm/viewer gate.

## Log

- 2026-07-19 — `open`
  Scope: shared Figa program layout, animation-attach binding, and the ordinary integral-frame
  publication path.
  Hypothesis: dense per-node values and direct component publication delete immutable metadata and
  dispatch from the dominant ordinary path while leaving true dynamic cases source-exact.
  Evidence: the prior implementation was exact and measured +2%; the current profile assigns 17.46%
  to the containing owner.
  Disposition: reconstruct the representation against current HEAD without disturbing the retained
  compact ECB evaluator, then benchmark and improve only within this final boundary.
  Next: port the descriptor/value layout and direct publisher, run both production digests, and
  compare resident-512 cycles/frame against an adjacent clean binary.

- 2026-07-19 — `retained`
  Scope: complete dense node descriptors, initialization-time sample construction and binding, and
  direct ordinary SRT publication.
  Hypothesis: the final dense representation should delete enough immutable metadata work to reduce
  the whole-frame cost, without changing the dynamic decoder or mutable pose state.
  Evidence: resident-512 controls are 47,623.8/47,902.1/47,868.8 cycles/frame and candidates are
  46,030.4/46,124.2/46,090.4, reducing the median 3.71%. Resident-256 controls are
  44,892.6/45,184.1/45,181.3 and candidates are 43,510.7/43,266.4/43,658.5, reducing the median
  3.70%. Digests remain `6f91f23e3553a090` / `8ef126a41244d514`.
  Disposition: retain the single representation; it exceeds the earlier proof and has no per-Match
  state or memory cost.
  Next: complete the full material gate and commit the packet atomically if green.

- 2026-07-19 — `retained`
  Scope: complete material gate and memory census.
  Hypothesis: the dense shared representation must preserve every runtime and artifact contract, not
  only the benchmark digests.
  Evidence: debug and optimized-release validation are each 63 PASS / 90 unchanged CLASSIFIED across
  1,415,476 frames. API, arbitrary-index copy/save-restore, 633,432-byte sealed arena, PPC, Wasm
  parity, viewer/browser, 38 Python tests, source sync, and formatting are green. Shared GameData
  grows by 0.71 MiB; per-Match state is unchanged.
  Disposition: packet complete and ready for atomic implementation/evidence commit.
  Next: commit, notify the retained win, refresh the committed profile, and select the next bounded
  high-impact owner.

- 2026-07-19 — `rejected`
  Scope: packing the immutable node descriptor from 16 to 12 bytes by storing its direct admission
  bit in the channel mask.
  Hypothesis: the smaller shared table would reduce cache traffic in the ordinary publisher.
  Evidence: the exact candidate cost 47,492--47,875 cycles/frame in adjacent resident-512 pairs
  versus 47,311--47,538 for the 16-byte descriptor. The non-power-of-two stride loses more than its
  0.50 MiB shared-memory saving recovers.
  Disposition: restore the naturally aligned 16-byte descriptor; no packed representation remains.
  Next: rebuild the already-gated final candidate and commit it with retained evidence.
