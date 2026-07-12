# Packet 3: Damage, Down, and Passive

Status: exact-owner and transition consolidation complete; direct geometry cutover remains open.

Baseline: `452a143f` (`Rewrite common grounded map collision`).

## Delivered

Stable live handlers now cover Damage, DamageFly/FlyReflect, DamageFall, DownDamage, DownReflect,
DownBound, DownWait/DownStand/DownSpot, Down/DownAttack, Passive, PassiveStand, PassiveWall, and
PassiveCeil. `DownDamage` and `DownReflect` have distinct handlers because their source callbacks
have different transition ladders; treating DownDamage as ordinary Damage was an invalid
intermediate grouping caught during the rewrite.

The retained coordinator derives `ft_80081DD4`, `B108`, and `B2DC` ownership from these exact
handlers. Damage preprocessing, hitlag ECB lifetime, shield/contact ordering, floor-loss nudge,
wall/ceiling consumers, and post-collision transitions no longer depend on the deleted damage
collision class words.

The duplicated grounded/aerial common-Damage floor ladder in `knockdown.c` is now one source-shaped
implementation of `ftCo_Damage_Coll`: DownBound threshold, Landing threshold, then the low-KB
same-motion transfer. DamageFly/DamageFall retain their separate tech/passive/DownBound ladder.
DownDamage keeps its own ground transfer, and DownReflect has the source `ftCo_80097D88` DownBound
destination. The DownDamage wall branch now shares the source wall-tech/fly-reflect helpers without
admitting ceiling tech, matching its decomp callback order.

## Result

The aggregate, primary, Falcon, and Sheik reports are identical to `452a143f`. The doubles suite
improves by six one-step discrete/strict mismatches and one total/seeded rollout break, all in
`Game_20260704T165119.slpz`; the full report diff has no reds.

Same-machine alternating performance locks are parity:

| Gate | `452a143f` | Packets 2/3 |
|---|---:|---:|
| mixed random-input, 5,000 frames × 256 | 425,020 FPS | 425,060 FPS |
| fixed five-replay sample, 15,000 records × 10 | 301,441 FPS | 301,710 FPS |

The random benchmark checksum is identical. Current p99/average ratios are 1.21x for the random
benchmark and 3.54x for the replay-derived sample, with no new pathological tail.

The combined tree deletes substantially more generated ownership/test surface than it adds,
although runtime `src/` is still near parity because the retained collision coordinator cannot yet
be deleted.

The remaining work is not another damage action list. It is the in-place replacement of the shared
airborne/grounded geometry and publication sections so these installed handlers can become fully
migrated callbacks and skip the legacy coordinator.
