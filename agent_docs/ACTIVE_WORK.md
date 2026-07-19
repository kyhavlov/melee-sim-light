# Active performance packet — fused ordinary JObj world matrix

## Objective

Delete the intermediate local-matrix publication and separate concat boundary for the dominant
ordinary Euler JObj path. Prove a singular exact world-matrix evaluator early; reject the packet
without leaf tuning if it cannot produce a material resident-512 gain.

## Final boundary

- **Final owner:** the hosted Dolphin matrix platform owns an exact Euler-SRT-to-parent-world
  evaluator; `HSD_JObjMakeMatrix` selects it for non-root, non-quaternion JObjs.
- **Canonical state:** `jobj->mtx` remains the only matrix state. The evaluator consumes the existing
  JObj scale/rotation/translation, optional parent scale compensation, and parent world matrix, then
  writes the final world matrix directly.
- **Consumers:** ordinary non-quaternion JObjs with a parent. Root JObjs retain source
  `HSD_MtxSRT`; quaternion JObjs retain `HSD_MtxSRTQuat`; subsequent animation-parent translation,
  RObj/IK, capture, dynamics, hit/hurt, and collision consumers continue to read `jobj->mtx`.
- **Displaced work:** constructing and storing a complete local 3x4 matrix, reloading it through the
  general alias-safe `PSMTXConcat` interface, the concat temporary/copy, and the two separate call
  boundaries.
- **Deletion boundary:** the admitted path calls only the fused evaluator. It has no synchronized
  local/world matrices, compatibility flag, alternate pose state, approximate math, legacy
  extractor, or fallback dispatch. Explicitly excluded source cases remain source-owned rather than
  being emulated by the new owner.

## Evidence and acceptance

- Retained commit `9d54eedd` measures 74,340 FPS at 512 and 78,599 FPS at 256 with digests
  `6f91f23e3553a090` and `8ef126a41244d514`.
- A clean Callgrind drill-down records 95,656 `HSD_MtxSRT` calls and 95,004 parent concats in the
  sampled gameplay subset. `HSD_MtxSRT` is about 10.3% inclusive and its two JObj matrix callers plus
  concat dominate the sampled instruction tree. Isolated exact concat compilation was only +1.27%,
  and O1/O2/O3 admission for the source matrix owner was about one point, so this packet must delete
  the boundary rather than tune either leaf.
- Early proof requires unchanged 512 digest and three adjacent samples. Target at least 5%; a
  three-to-five-point result is retainable only when the approved final boundary is complete,
  displaced machinery is deleted, and further work would expand into unrelated JObj consumers.
- Retention requires both benchmark digests and the complete 153-replay classification, native
  source/API/copy/save-restore/allocation, PPC, Wasm/viewer, pytest, source-sync, and formatting gates.
  Commit implementation and evidence atomically only after those gates.

## Log

- 2026-07-19 — `open`
  Scope: ordinary Euler JObj local SRT construction followed immediately by parent concat.
  Hypothesis: combining the exact operations inside one optimized owner removes the intermediate
  matrix traffic, general alias handling, temporary copy, and call boundaries that neither prior
  leaf experiment could eliminate.
  Evidence: 95,004 of 95,656 sampled SRT constructions are followed by parent concat; the retained
  profile attributes 14.32% to pose animation, while the sampled matrix tree is substantially
  larger because it also serves ECB and dynamics consumers.
  Disposition: implement only the singular non-root, non-quaternion world evaluator and run the
  digest plus adjacent resident-512 proof before any full gate.
  Next: preserve the exact SRT and paired-single concat operation boundaries in one hosted owner.
- 2026-07-19 — `open`
  Scope: first exact scalar fused evaluator, followed by exact unit-parent-scale admission.
  Hypothesis: boundary deletion alone establishes viability; the common exact-unit scale case also
  removes three divides and six multiplies without changing any source result.
  Evidence: the first candidate preserved the 512 digest at roughly +2.9% by paired median. Exact
  unit-scale admission raised representative throughput from about 77.0k to 77.4k FPS.
  Disposition: the boundary is viable but its scalar general-concat arithmetic remains below the
  final form; retain unit-scale admission and replace that arithmetic, not the representation.
  Next: evaluate the three affine output rows across SIMD columns with unchanged FMA order.
- 2026-07-19 — `open`
  Scope: exact SIMD-across-columns native concat inside the singular fused evaluator.
  Hypothesis: three four-lane affine rows remove scalar publication/packing work while retaining the
  independent per-column PPC multiply/FMA sequence.
  Evidence: the 512 digest remains exact and adjacent candidate throughput reaches a 77,319 FPS
  median, about four whole-frame points over clean HEAD.
  Disposition: advance the final evaluator to the complete correctness and production gate.
  Next: run the 153-replay, native state, PPC, Wasm/viewer, and refreshed-profile gates.
- 2026-07-19 — `retained`
  Scope: exact fused Euler SRT and parent-world publication for every hosted non-root,
  non-quaternion JObj, including a SIMD-across-columns native evaluator and a source-equivalent
  portable evaluator.
  Hypothesis: the final world-matrix owner can consume exact local components directly, deleting
  general alias handling, a complete temporary matrix round trip, and two source call boundaries.
  Evidence: three adjacent 512 controls are 74,332/74,623/73,679 FPS and candidates are
  77,319/77,732/76,889 FPS. Raw medians improve 74,332 to 77,319 FPS (+4.02%); median paired change
  is +4.17%. Final 256 samples are 81,309/81,753/81,824 FPS, an 81,753 median (+4.01% over the
  retained 78,599 baseline). Digests remain `6f91f23e3553a090` and `8ef126a41244d514`.
  The complete replay gate remains 63 PASS / 90 unchanged CLASSIFIED / zero failures across
  1,415,476 frames. Source, native allocation/API/copy/save-restore, PPC, Wasm/viewer, pytest, and
  formatting gates pass; persistent storage remains 633,432 arena bytes and 695,048 savestate
  bytes with 825 initialization allocations.
  Disposition: retain the singular fused owner. The measured four-point whole-frame cut is durable
  boundary deletion comparable to earlier retained packets despite missing the aspirational 5%
  target; no local/world dual representation or general concat fallback exists on the admitted path.
  Next: commit implementation and evidence atomically, refresh the profile-backed queue, and begin
  the next bounded final-form packet.
