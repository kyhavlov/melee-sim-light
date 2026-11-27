# Replay regeneration

From a source checkout with the validation dependencies and extracted game data,
run `python -m tools.regenerate_replays archive.zip`. The input ZIP can contain
`.slp`, `.slp.gz`, `.slpz`, or `.slpz.gz` members. The output defaults to
`archive.regenerated.zip` and contains verified `.slp` files and a
`regeneration.jsonl` manifest. The manifest records an outcome for each
selected replay, including parse failures, unsupported captures, and the first
validation mismatch in rejected replays.

Use `--start` to skip replay members and `--limit` to select a batch. Use
`--output` to name each batch's ZIP. `--workers` sets the number of persistent
simulator workers. On Linux, `--cores 0,2,4,6` pins one worker per listed CPU.

Regenerated replays preserve the original recording version, metadata, and
finalized gameplay events. The Post-Frame payload size identifies the added
stored fighter state, so the original Pre-Frame layout still decodes correctly.
The converter requires a complete replay by default and preserves the recorded
Game End event, including placements and quit information.
`--include-finalized-prefixes` also accepts a truncated file that ends on a
finalized frame bookend after all available transitions pass validation.
These members have `complete: false` in the manifest and no Game End event.

## Exactness

By default, the converter requires an exact match on the
[gameplay projection](../../agent_docs/VALIDATION.md). It verifies stored fighter
state on replays that already contain it. For older Ice Climbers recordings,
the converter also tries split multiply-add arithmetic if the default run differs.
It uses that run only if every compared field matches exactly, and records
`jit_arithmetic: split-fma` in the manifest.

`--disable-strict-exactness` allows
the differences below when filling in missing stored state. The original
recorded values are preserved and the manifest counts accepted differences.

| Field | Maximum difference | Conditions |
| --- | --- | --- |
| Fighter X or Y position | 0.0002 game units | Includes Nana. |
| Item X or Y position | 0.002 game units | |
| Item X or Y velocity | 0.00005 game units/frame | |
| Nana's self-induced ground X speed | 0.00005 game units/frame | |
| Grabber's self-induced ground X speed | 0.00001 game units/frame | Both grounded in the same `CatchPull`, `CatchDashPull`, `CatchWait`, or `CatchAttack` state. One speed must be zero. |
| Falco's self-induced vertical speed | 1e-20 game units/frame | Both airborne on animation frame 1 of aerial Phantasm. One speed must be zero. |
| Self-induced vertical speed during a star KO | 0.000001 game units/frame | Both in the same `DeadUpStar` or `DeadUpStarIce` state. |
| Samus's self-induced air X or vertical speed | 0.00005 game units/frame | Both airborne in `AirCatchHit`. |
| Popo's self-induced air X or vertical speed | 0.000001 game units/frame | Both in aerial Belay's throw state with Nana present. |
| Fox/Falco laser or lowest-charge Shadow Ball angle | 0.000005 radians | The sampled angle byte must agree with a value within two ULPs of the direction of travel on each side. |
| Fox/Falco laser angle after a shield bounce | 0.00001 radians | The same reconstruction check applies at the verified bounce and remains valid while both sampled angle bytes stay unchanged. |
| Sheik needle trail anchor Y | 0.00001 game units | The sampled bytes must agree with both anchors reconstructed from projectile movement and frame history. |
| Mario/Dr. Mario cape item state | States 0 and 1 compare equal | These states use the same item script and callbacks. |
| Zelda/Sheik L-cancel status | Invalid values above 2 | On the first frame after transforming, the old recorder could read the dormant fighter's byte before it was initialized. |

The numeric limits are grouped in [the native validator](native.c).
