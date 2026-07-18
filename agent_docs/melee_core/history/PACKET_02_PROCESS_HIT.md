# Packet 2 — ProcessHit idle/event boundary

- **Queue item / confidence:** item 2; potential until the inner owner profile identifies proven
  event-free work, then definite for that work.
- **Final owner:** `Fighter_ProcessHit_8006D1EC` remains the sole ordered resolution owner for each
  fighter. The existing contact producers remain authoritative for its pending damage, shield,
  reflect, absorb, grab, hitlag, and attacker-knockback state.
- **Canonical state:** the source `Fighter` damage/contact fields, shield health and shield-active
  bit, hitlag state, hurt capsules, and source callback/action state. No parallel pending flag or
  queue is introduced merely to accelerate this packet.
- **Consumers:** damage and knockback transitions; shield damage/break; hitlag and SDI entry;
  reflect/absorb/hurt callbacks; source/attacker writeback; afterimage state; and the following
  fighter/item contact pass that consumes published hurt capsules.
- **Displaced work/code:** only branches, resets, callbacks, or projections proven unnecessary on
  the common no-pending-event path. The exact event path retains source order and semantics.
- **Deletion boundary:** the candidate selects a minimal event-free path directly from canonical
  source state. It does not synchronize a second representation, call a fallback, or retain a
  compatibility dispatch.
- **Explicit exclusions:** collision/contact generation, pose publication, camera, item resolution,
  and any hurt-capsule update consumed by the next contact pass.

## Chronological log

- **2026-07-17 23:07 PDT — packet opened.** Scope: queue item 2 only. Hypothesis: most fighter
  callbacks have no pending combat event, but the current source function still executes a long
  resolution/reset tail; a canonical-state guard can skip a measurable portion without changing
  event ordering. Evidence: the complete owner is 10.8–13.2% of frame time, while source inspection
  shows unconditional shield recovery, `ftCo_800C8D00`, effect cleanup, hit-event field resets,
  afterimage maintenance, and hurt-capsule publication. Disposition: **open**; capsule publication
  is gameplay-consumed. Next: measure inner branch frequency and exclusive cost.
- **2026-07-17 23:14 PDT — canonical branch census.** Scope: temporary macro-gated counters in the
  unchanged production callback over the canonical 512-environment workload. Result: 436,989 calls
  included 93,628 hidden-fighter no-ops; of 343,361 visible calls, 329,584 (96.0%) satisfied the
  conservative idle predicate. Only 1,979 carried a primary event, 904 knockback, 9,006 active
  shield drain, 16 delayed-damage timers, 165 pending effect cleanups, and 2,737 active afterimages.
  Digest stayed `3fb5823d90657775`. Disposition: **retained evidence**; profiling code removed.
- **2026-07-17 23:16 PDT — final ablation.** Scope: one direct idle guard over every source field
  consumed or cleared by the callback, after shield/timer advancement and before the event tail.
  Required hurt-capsule publication remained. Result: the complete suite was green (63 exact, 90
  unchanged classified, zero XPASS/fail/error, 1,415,476 output locks), but adjacent CPU-0 A/B was
  83,094 to 83,696 FPS at 512 (+0.72%) and 62,992 to 62,761 at 256 (-0.37%). Disposition:
  **rejected**. The owner cost is the capsule projection, so a scalar event guard is noise-level
  machinery that the final pose/geometry boundary would delete. Production code restored exactly.

Detailed ignored evidence: `reports/triage/perf_candidates/packet2_process_hit.md`.
