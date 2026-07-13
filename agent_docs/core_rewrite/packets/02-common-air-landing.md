# Packet 2: Common Air and Landing

Status: complete.

Baseline: `452a143f` (`Rewrite common grounded map collision`).

## Delivered

- Exact `MSLMSO01` callback recipes install common-air, FallSpecial, AttackAir, EscapeAir,
  Landing, LandingAir, Pass, MissFoot, and CliffJump2 owners without rebuilding callback families
  from action lists.
- `mp_lib.c` owns directional floor/wall/ceiling sweeps, projection, graph links, endpoint
  extension, remap, moving/transformed surfaces, and surface velocity from `MSLSTG01`.
- `mp_coll.c` owns the source order for repeated wall passes, ceiling, floor admission,
  stay-airborne projection, platform pass, squeeze/restore, and environment publication.
- `mpcoll_source_air.c` owns persistent CollData current/previous/desired ECB, root endpoints,
  six-unit subdivision, floor skip, and immediate landing/floor-loss transitions.
- Final Destination, Battlefield, Fountain, frozen Stadium, Yoshi's, and Dream Land share the same
  kernel; stage behavior is selected by extracted line metadata and live stage state.

The earlier compact prototype was discarded because it omitted persistent ECB/remap and moving
surface ownership. The completed implementation is the full source-shaped replacement, not that
prototype and not a wrapper around the old coordinator.

## Result

The common-air identities no longer enter `mpcoll_floor.c`, `mpcoll_ground.c`, or
`mpcoll_wall_ceil.c`; those files are deleted in Packet 5. Source-owner locks cover hard/soft
landing, platform pass, floor skip, ledges, connected surfaces, squeeze, and wall/ceiling
persistence. Final suite-level results are recorded in Packet 5 and the Phase-1 residual ledger.
