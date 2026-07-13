# Decision 0002: Source Completion Is the Program, Validation Is Evidence

Status: accepted.

Date: 2026-07-12.

## Decision

The primary goal of `core-rewrite` is to port all gameplay-relevant Melee code and data for the
supported characters, stages, and singles/doubles runtime closely enough that the simulator's
owners, state, callback order, and logic match the decomp directly.

Work is organized by source ownership boundaries, not validation mismatches. Replays locate missing
or incorrect ownership and measure progress; they do not define patch scope or the implementation
queue. A mismatch that belongs to an owner not yet ported is expected to close naturally when that
owner is rewritten. A mismatch inside an owner claimed source-complete is evidence that the claimed
boundary or port needs another source audit.

## Completion language

Use these terms precisely:

- **Architectural cutover complete:** one source-shaped runtime owns the boundary and the displaced
  implementation is deleted. Residual semantic differences may remain because connected owners are
  not yet ported or the new owner still needs source-completion work.
- **Source owner complete:** every gameplay-relevant source path in the declared supported boundary
  is represented, its persistent state and callback order are direct, and any exclusions are
  explicit source-backed non-goals.
- **Program complete:** gameplay-relevant owners for the supported domain are source-complete and
  old semantic approximations, bridges, and duplicated paths are gone.

Phase 1 packets 1–5 are architecturally complete. They establish one map-collision runtime and
delete the old coordinator. They are not a claim that every upstream MotionState, downstream
contact, or remaining map-collision semantic is already source-complete.

## Working rules

1. Start from decomp call graphs, tables, persistent structs, and scheduler order. Use replay rows
   only after the source boundary is understood.
2. Prefer close, direct rewrites and generated data over preserving locally evolved abstractions.
   Large deletion and scope expansion are desirable when they remove a false ownership boundary.
3. Expand a packet across connected files when required to complete one source invariant. Do not
   replace that expansion with a compatibility bridge merely to keep an intermediate report green.
4. Do not retain old machinery as fallback for migrated owners. Temporary comparison code must be
   debug-only and deleted at cutover.
5. During construction, focused source-owner tests and small probes are the primary locks. Broad
   validation is diagnostic until a coherent cutover exists.
6. At cutover, regenerate validation and benchmark reports. Investigate broad regressions or rows
   that disprove the claimed source owner; record other residuals as evidence for later owners.
7. Follow-up queues are source inventories and ownership gaps, never lists of replay names to make
   green.

## Consequences

- A source-clear rewrite may be retained through temporary or isolated validation regression.
- Aggregate improvement is useful evidence, but it is not a substitute for decomp coverage.
- An unchanged metric does not make a source-completion packet valueless.
- A locally green bridge is a regression in architecture if it delays modeling the real owner.
- Reviews should compare the new owner with its decomp surface and audit missing paths/state before
  spending time on individual replay outcomes.
