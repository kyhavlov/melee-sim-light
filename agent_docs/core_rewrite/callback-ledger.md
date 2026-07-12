# Map-Collision Callback Ledger

This ledger is the stable reference for the five Phase 1 packets. It records ownership boundaries;
the generated `MSLMSO01` artifact remains the executable action-to-callback source of truth.

## Packet 1 — common grounded wrappers

Status: complete; committed as `452a143f`.

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

Status: exact-owner cutover complete; direct geometry cutover open.

Stable live handlers, low-level phase selection, landing transitions, and redundant semantic-class
deletion are complete. A standalone air kernel and direct Landing reuse were rejected by aggregate
validation. The persistent CollData geometry must be cut over in place inside the proven
coordinator. See [packets/02-common-air-landing.md](packets/02-common-air-landing.md).

## Packet 3 — damage, knockdown, and passive

Status: exact-owner and transition consolidation complete; direct geometry cutover open.

Damage, DamageFly, DamageFall, DownDamage, DownReflect, Down/Passive/tech callback identities and
their deterministic transition ladders now consume exact handlers. Their retained shared geometry
still prevents marking the callbacks fully migrated. See
[packets/03-damage-down-passive.md](packets/03-damage-down-passive.md).

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
