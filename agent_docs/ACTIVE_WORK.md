# Active performance packet — fighter map collision

## Outcome

Reduce the common fighter map-collision owner by at least 5% of total 512-environment frame time
without changing ECB construction, surface admission, candidate order, floor publication, ledge
logic, transformed-platform behavior, or any replay classification.

## Packet boundary

- Final owner: common fighter collision orchestration in `melee/mp/mpcoll.c` and stage-geometry
  candidate enumeration in `melee/mp/mplib.c`. MotionState callbacks remain source owners of which
  common collision mode is requested.
- Canonical state: existing `CollData`, `CollJoint`, `CollLine`, `CollVtx`, joint bounds, line flags,
  and transformed vertex positions. Extracted geometry remains authoritative; no stage-id or
  character-id proxy is permitted.
- Consumers: floor, ceiling, left/right wall, ledge, squeeze, and moving-platform queries reached by
  supported singles and doubles callbacks.
- Displaced work: whichever repeated common candidate-enumeration or orchestration work attribution
  proves dominant. The intended broad-phase cut may replace linked/full-range traversal with a
  compact data-driven candidate path, but exact source narrow-phase tests and source iteration/
  tie-breaking order remain the final authority.
- Deletion boundary: the retained native path has one candidate representation and no generic
  fallback, dual candidate list, replay exception, stage-specific shortcut, or compatibility
  dispatch. Non-hosted source behavior is unchanged. Temporary attribution is removed.

## Execution

1. Reconfirm committed 256/512 digests and attribute `Fighter_procMap` through MotionState callback,
   mpColl mode, mplib query kind, joint visitation, and line/intersection work.
2. Name the dominant final cut before production edits. Reject the packet if the measurable
   removable share cannot plausibly yield 5% overall.
3. Implement the complete data-driven cut, preserving query order and using existing dynamic
   geometry/flags rather than stage identity.
4. Require exact benchmark digests, 63 PASS / 90 unchanged CLASSIFIED / zero failures, native API/
   copy/save-restore, sealed allocation, PPC, Wasm/viewer, source, and format gates.
5. Use adjacent CPU-0 control/candidate pairs at 512 and 256, remove all instrumentation, update
   retained evidence, and hand off uncommitted.

## Baseline

- Runtime: `be1d9d12`
- 256 digest: `8ef126a41244d514`
- 512 digest: `6f91f23e3553a090`
- Last retained medians: 44,414 FPS at 256 and 42,276 FPS at 512
- Correctness: 63 PASS / 90 CLASSIFIED / zero XPASS/fail/error

## Log

- 2026-07-18: Opened from clean `be1d9d12`. Source inspection confirms retail already computes a
  swept ECB/ledge bounding box and marks out-of-range collision joints before floor/wall/ceiling
  queries. Therefore a second generic AABB layer is not assumed useful. First experiment is
  attribution-only: separate common orchestration, joint bounding, candidate lines, exact
  intersections, and publication work before selecting a production representation.
- 2026-07-18: Callback-owner attribution completed with exact benchmark digest. Fighter map
  collision owns 21.77% of the production contract, but no leaf dominates: the largest callback is
  `ftCo_AttackAir_Coll` at 2.18%, followed by `ftCo_DamageFly_Coll` at 1.68%, `ftCo_Dash_Coll` at
  1.40%, and `ftCo_Wait_Coll` at 1.38%. Disposition: `retained` diagnostic evidence; leaf
  specialization is rejected. Next experiment times shared `mpColl` orchestration, bounding,
  line-endpoint expansion, and exact `mpLib` queries before naming the production cut.
- 2026-07-18: Shared-owner attribution found 3,655,507 wall queries and 23,233,780 exact line
  intersection calls in 65,536 benchmark match-frames. Air collision orchestration alone entered
  95,309 times. Bounding is only 0.57% of the instrumented contract, while repeated wall queries
  account for 19.14%; floor and ceiling queries are much smaller. Disposition: `retained`
  diagnostic evidence. The named final cut is one conservative wall-line broad phase per
  high-level left/right wall pass, before its repeated source narrow-phase segment/quad queries.
  Static lines use their current data-driven endpoint bounds; transformed/remapped joints always
  fall through to source narrow phase. The cut displaces only provably empty wall passes and does
  not cache or replace any collision result.
- 2026-07-18: Implemented the named native broad phase at all six common air, grounded, and
  ceiling-owned left/right wall passes. Exact benchmark digest remains `6f91f23e3553a090`.
  Attribution after the cut falls from 3,655,507 to 196,752 wall queries (-94.6%) and from
  23,233,780 to 3,061,555 exact intersections (-86.8%). Three adjacent uninstrumented 512 pairs
  measured 42,408/45,638 (+7.62%), 41,592/44,998 (+8.19%), and 42,463/45,976 (+8.27%); median
  paired gain is +8.19% and raw-median gain is +7.62%. At 256, three pairs improve by +12.24%,
  +8.38%, and +9.45% (raw-median +9.71%), retaining digest `8ef126a41244d514`. Disposition:
  `retained`; the complete 63 PASS / 90 unchanged CLASSIFIED replay gate, native source/API/copy/
  save-restore, sealed allocation, PPC, Wasm/viewer, and formatting gates are green. All temporary
  attribution and the experimental control switch have been removed.
