# Active melee-core work

Queue items 4–5 are cleanly rejected before production cutover. Their proposed final boundary,
production census, Callgrind ceiling, and disposition are archived in
`history/PACKET_04_05_STAGE_COLLISION.md` and summarized in `PERFORMANCE.md`. No collision runtime,
diagnostic counter, duplicate state, or compatibility path remains in the worktree.

Queue item 6 (final compact pose/geometry consumer cutover) is next. Before runtime edits, inspect
the committed pose/geometry owner and the archived failed experiments, then replace this file with
a new packet map naming its final owner, canonical shared/dynamic state, every gameplay consumer,
displaced tree/matrix/hurt-capsule state, complete deletion boundary, confidence label, and an early
proof that exercises the final consumer shape rather than another partial cache.
