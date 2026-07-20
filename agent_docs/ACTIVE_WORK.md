# Active performance packet — fused hosted dynamics transforms

## Objective

Replace the general temporary-matrix pipeline inside the exact fighter dynamics solver with one
hosted transform evaluator that publishes only the two demanded world bases and next-link basis.
Preserve the complete source constraint/collision/quaternion solver and canonical DynamicsData/JObj
state while deleting matrices and concatenations whose intermediate values have no consumer.

## Final boundary

- **Final owner:** `lb_8001044C` owns a hosted exact transform kernel adjacent to the source solver;
  PPC retains the upstream matrix sequence.
- **Canonical state:** `DynamicsDesc`/`DynamicsData`, their JObj rotations/SRT, angular velocity,
  world positions, and the source linked order remain the complete mutable/save-state-visible state.
- **Consumers:** stiffness, gravity, force, collider/floor constraints, quaternion publication,
  dynamic hurt capsules, attachments, and subsequent frames consume the same canonical fields.
- **Displaced work:** per-link translation/scale matrices, general 3x4 concatenations for sparse
  transforms, and the tail's unobserved rotation/scale result.
- **Deletion boundary:** hosted execution has one fused exact transform path, with no shadow
  dynamics state, cached alternate pose, character/action dispatch, approximate math, or fallback.

## Source and profile evidence

- The corrected 512 profile assigns 8.78% of the production contract to
  `Fighter_8006D9AC`; the earlier dynamics census attributes about 67% of solver cycles to demanded
  orientation/constraint math and another 23% to setup/floor work.
- `refs/melee/src/melee/lb/lbspdisplay.c::lb_8001044C` constructs translation, Euler, and scale
  matrices through repeated `PSMTXConcat`, but the loop consumes only two transformed child vectors,
  the current origin, the inverse parent axis, and the next-link parent basis.
- `src/platform/dolphin_mtx.c` documents the exact hosted paired-single contraction order required
  by those operations; the fused evaluator must preserve those float/FMA boundaries.

## Acceptance

- Prove the fused transform output exact before widening the cut. Both production digests and the
  complete replay/API/copy/save-restore/allocation/PPC/Wasm/viewer gates must remain unchanged.
- Add no gameplay allocation, Match/shared state, character list, or runtime branch.
- Retain only a repeatable whole-frame gain at resident 512 and 256; otherwise remove the kernel and
  record the rejected result.

## Log

- 2026-07-19 — `open`
  Scope: common per-link world-basis construction and tail world-position publication in
  `lb_8001044C`.
  Hypothesis: computing only the demanded columns/positions with the same source contraction order
  removes several complete 3x4 temporary matrices and general concatenations from the 8.78% owner.
  Evidence: every retained supported live chain uses the same transform prefix regardless of its
  three/four-node shape, collider count, or floor behavior; no source branch specialization is
  required.
  Disposition: implement one hosted fused helper and cut all supported hosted calls to it.
  Next: verify the 512 digest immediately, inspect any residual at the first mismatching state, and
  benchmark before touching the constraint solver.

- 2026-07-19 — `open`
  Scope: first fused transform implementation and production/replay proof.
  Hypothesis: eliminating general transform temporaries should materially reduce the dynamics owner.
  Evidence: resident-512 throughput rises to 94,798 FPS, but digest changes to
  `0147f963403d7d6a`; release validation confirms widespread downstream divergence rather than an
  accepted residual. The performance viability is real, not correctness evidence.
  Disposition: retain the final fused structure as the open candidate and recover exact intermediate
  rounding before any wider optimization.
  Next: add a compile-time diagnostic that executes source and fused transform publication side by
  side at the helper boundary, report the first matrix/position bit difference, then remove it.

- 2026-07-19 — `open`
  Scope: alias-safe exact fused bases and tail position after diagnostic removal.
  Hypothesis: one preserved parent snapshot for the in-place next-link update recovers the source
  matrices without restoring general concatenations.
  Evidence: the diagnostic proved every fused matrix and origin bit-identical after fixing the
  in-place parent alias. The production digest is restored; corrected-profile dynamics cycles fall
  316.8M to 291.6M (-8.0%), or about 0.7 whole-frame points. Candidate 512 samples are stable around
  44,930 cycles per frame, but adjacent controls are noisy and the current cut is borderline.
  Disposition: keep the exact final structure open and complete the same deletion boundary over its
  three remaining general vector/transpose consumers before deciding retention.
  Next: fuse the two basis-vector publications and inverse-parent axis transform with identical FMA
  ordering, then remeasure the complete transform packet.

- 2026-07-19 — `retained`
  Scope: complete fused basis/origin/direction/inverse-axis dynamics transform boundary.
  Hypothesis: sharing the source origin and directly serving all transform consumers should make the
  final exact packet repeatably material.
  Evidence: resident-512 control/candidate medians are 45,022.1/44,491.0 cycles per frame (-1.18%);
  resident-256 medians are 42,537.4/42,115.5 (-0.99%). Digests remain
  `6f91f23e3553a090` / `8ef126a41244d514`. Corrected-profile dynamics cycles fall from 316.8M at
  the parent to 277.6M (-12.4%).
  Disposition: retain the final hosted transform evaluator; it deletes demanded matrix work without
  adding state, fallback, or domain specialization.
  Next: record the material gate and commit atomically if green.

- 2026-07-19 — `retained`
  Scope: complete material correctness/build/state gate.
  Hypothesis: exact helper-boundary proofs and production digests must extend across every supported
  source path and platform.
  Evidence: debug and optimized-release validation remain 63 PASS / 90 unchanged CLASSIFIED across
  1,415,476 frames. Native API/copy/save-restore/allocation, PPC, Wasm parity, viewer/browser, 38
  Python tests, source sync, and formatting are green; memory is unchanged.
  Disposition: packet complete and ready for its atomic implementation/evidence commit.
  Next: commit, notify the retained win, then profile/select the next bounded owner.
