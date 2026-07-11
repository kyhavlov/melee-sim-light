# Map-Collision Callback Ledger

This ledger is the stable reference for the five Phase 1 packets. It records ownership boundaries;
the generated `MSLMSO01` artifact remains the executable action-to-callback source of truth.

## Packet 1 — common grounded wrappers

Status: complete, awaiting review.

The v24 artifact maps 19 extracted Coll callback identities to seven stable runtime handler kinds.
Those identities cover the same 21 common rows for Fox, Falco, Marth, Sheik, Zelda, and Captain
Falcon: 126 action rows in total.

| Handler kind | Common action rows |
|---|---|
| `GROUND_B108_FALL` | Turn, KneeBend, Squat, SquatWait, SquatRv |
| `GROUND_B2DC_FALL` | TurnRun |
| `GROUND_B4B0_TEETER` | Wait, WalkSlow, WalkMiddle, WalkFast, RunBrake |
| `GROUND_RUN` | Dash, Run, RunDirect |
| `GROUND_GUARD` | GuardOn, Guard, GuardOff, GuardReflect |
| `GROUND_GUARD_SETOFF` | GuardSetOff |
| `GROUND_OTTOTTO` | Ottotto, OttottoWait |

The mapping is extracted from exact Coll symbols. The runtime does not infer these families from
action IDs. Legacy floor/wall/ceiling and locomotion collision passes explicitly skip a live
migrated handler, so the two implementations cannot run for the same callback.

## Packet 2 — common air and landing

Status: next.

Migrate common airborne wrappers, AttackAir, EscapeAir, Landing/LandingAir, and their complete
floor-loss/ground-to-air handoff. This packet should make the persistent CollData ECB and surface
state authoritative across both sides of the ground/air boundary and delete the corresponding
generic floor-probe/reject families.

## Packet 3 — damage, knockdown, and passive

Status: pending.

Migrate Damage, DamageFly, DamageFall, Down/Passive/tech collision identities and their wall,
ceiling, floor, and landing transitions. Bounded RNG sites remain separately owned; this packet is
about deterministic collision and transition order.

## Packet 4 — cliff, special, and capture

Status: pending.

Migrate CliffCatch admission and simultaneous ledge ownership, then supported character-special
and capture collision identities that use the same mpColl substrate. Preserve stable player order
and explicit live callback overrides.

## Packet 5 — remaining identities and legacy deletion

Status: pending.

Classify every remaining non-null supported-domain Coll identity as migrated or out of RL scope.
Delete the legacy map-collision dispatch, dead semantic class bits, repair packets, state lanes,
and compatibility APIs once the ledger is empty.

## Sequencing rule

Packets 2-5 can be implemented in a continuous work turn, but they should still be locked in this
order. Each packet gets its own focused tests, validation diff, and performance check before the
next one begins. Combining review only makes sense after those independent boundaries remain
visible in the diff and documentation.
