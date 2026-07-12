# Packet 2: Common Air and Landing

Status: exact-owner cutover complete; direct geometry cutover remains open.

Baseline: `452a143f` (`Rewrite common grounded map collision`).

## Delivered

MSLMSO01 v25 installs stable live handler kinds for common air, FallSpecial, AttackAir, EscapeAir,
Landing, and LandingAir. The two landing callback symbols remain distinct handlers even though
`ftCo_LandingAir_Coll` delegates to `ftCo_Landing_Coll`; this preserves the exact extracted owner
while the retained coordinator still has different historical behavior for the two rows.

The collision coordinator now selects the callback's low-level phase directly from that handler.
Locomotion landing, transformed-platform floor-skip lifetime, walljump eligibility, EscapeAir
ownership, and Landing edge ownership consume the same handler instead of overlapping class words
and local action-family predicates.

The following generated compatibility categories and their parity tests were deleted for migrated
callbacks:

- common-air Coll and walljump classes;
- Landing/LandingAir Coll classes;
- common grounded `B108`/`B2DC`/`B4B0` class2 words left over from Packet 1;
- AttackAir/EscapeAir membership in the broad `ft_80081D0C` and platform-pass classes.

`CliffJump2`, `MissFoot`, and `Pass` retain a narrow compatibility class until Packet 4 because
they do not yet have stable handler kinds.

## Rejected direct cutovers

Two source-shaped prototypes were tested and removed:

1. A compact standalone airborne mpColl kernel passed 410 focused tests but raised aggregate
   one-step mismatches from 9,918 to 29,797. It omitted important line-remap, persistent ECB, and
   transformed-platform behavior already present in the coordinator.
2. Routing Landing through Packet 1's `B4B0` kernel improved aggregate one-step by 26 rows and
   seeded rollout breaks by two, but introduced discrete regressions in three replays and two new
   `falcon_demo` rollout breaks. The totals were misleading; the cutover was removed.

The conclusion is structural: Packet 2 geometry must be rewritten vertically inside the proven
coordinator. Replacing it with a second compact sweep or reusing the grounded kernel wholesale is
not validation-safe.

## Result

All aggregate and per-replay validation metrics are identical to `452a143f`; the report diff has no
hard, distribution-only, or unclassified reds. The packet therefore removes duplicated ownership
without claiming a correctness gain from geometry that has not yet been safely replaced.

