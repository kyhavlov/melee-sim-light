# Map-Collision Callback Ledger

This ledger is the stable reference for the five Phase 1 packets. It records ownership boundaries;
the generated `MSLMSO01` artifact remains the executable action-to-callback source of truth.

## Packet 1 — common grounded wrappers

Status: complete; committed as `452a143f`.

The current ISO-derived artifact maps 19 extracted Coll callback identities to seven stable runtime
handler kinds.
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

The mapping is extracted from exact retail Coll callback pointer identity. The runtime does not
infer these families from action IDs. Legacy floor/wall/ceiling and locomotion collision passes
explicitly skip a live migrated handler, so the two implementations cannot run for the same
callback.

## Packet 2 — common air and landing

Status: complete.

Stable live handlers, low-level phase selection, persistent air CollData, landing transitions, and
redundant semantic-class deletion are complete. Distinct Pass, MissFoot, and CliffJump2 pointers
carry their shared source effects without being collapsed into one fake handler. See
[packets/02-common-air-landing.md](packets/02-common-air-landing.md).

## Packet 3 — damage, knockdown, and passive

Status: complete.

Damage, DamageFly, DamageFall, DownDamage, DownReflect, Down/Passive/tech callback identities and
their deterministic transition ladders consume exact handlers on the authoritative shared
CollData geometry engine. See
[packets/03-damage-down-passive.md](packets/03-damage-down-passive.md).

## Packet 4 — cliff, special, and capture

Status: complete.

CliffCatch admission, simultaneous ledge ownership, supported character-special callbacks,
capture constraints, and throw-release collision use the same mpColl substrate with stable player
order and explicit live callback overrides.

## Packet 5 — remaining identities and legacy deletion

Status: complete.

Every gameplay-relevant identity in the supported map-collision boundary is classified. MSLMSO01
v28 stores a pointer-derived wrapper-selector family separately from its compact post-collision
policy. A zero selector means the callback owns no supported map geometry (empty, attachment/item,
or explicitly out-of-domain); it never invokes a legacy fallback. The seven old geometry
coordinator files are deleted and absent from the extension build.

## Sequencing rule

Packets 2-5 were completed as one continuous Phase 1 cutover on the same source-shaped
stage/mpLib/CollData/mpColl engine. The review boundary is the complete runtime plus the explicit
residual validation ledger; replay-level follow-ups do not reintroduce a second collision path.
