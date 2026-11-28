# Replay regeneration

From a source checkout with the validation dependencies and extracted game data,
run `python -m tools.regenerate_replays archive.zip`. The input ZIP can contain
`.slp`, `.slp.gz`, `.slpz`, or `.slpz.gz` members. The output defaults to
`archive.regenerated.zip` beside the input ZIP and contains verified `.slp` files and a
`regeneration.jsonl` manifest. The manifest records an outcome for each
selected replay, including parse failures, unsupported captures, and the first
validation mismatch in rejected replays.

The default processes the full ZIP with up to 16 workers and Deflate level 5.
The CLI uses the native release build. Workers validate and compress replays
in parallel, then stream the compressed entries into one output ZIP.
Use `--start` and `--limit` for a range, or `--output` to choose the output path.
`--workers` overrides the worker count. On Linux, `--cores 0,2,4,6` pins one
worker per listed CPU. `--compression-level` overrides the Deflate level.

Regenerated replays preserve the original recording version, metadata, and
finalized gameplay events. The Post-Frame payload size identifies the added
stored fighter state, so the original Pre-Frame layout still decodes correctly.
The converter preserves the recorded Game End event. If the event is missing
and a finalized singles match ends with exactly one player out of stocks, the
manifest records `game_end: "missing"` and the winning port.
If rollback repeats an identical Game End, the converter keeps the final copy.
The converter also regenerates incomplete recordings through their last
confirmed frame. These members have `complete: false` in the manifest.
Use `--complete-only` to skip them. When trimming a provisional tail, the converter records the
discarded frame count and updates the final bookend and any `lastFrame`
metadata to match the output. When Game End follows a finalized bookend,
the converter keeps Game End and drops any later speculative frames.
If recording stopped before writing the raw stream length, the converter
can recover the finalized prefix when it validates exactly. If an active
player's rows stop early in an unclosed recording, the prefix ends before
the first missing row. The converter can also
repair a length that extends past a finalized Game End into metadata.
The manifest marks repaired envelopes with `repaired_envelope: true`.

Older netplay captures with the recorded UCF 0.84 patch can omit raw C-stick
axes. The converter tries an input consistent with the recorded stick position
and ASDI displacement, then validates the full replay. The manifest records
the number of reconstructed snaps. The original input events are preserved.

## Exactness

The converter compares the [gameplay projection](../../agent_docs/VALIDATION.md)
and verifies stored fighter state on replays that already contain it. For older Ice Climbers recordings,
the converter also tries split multiply-add arithmetic if the default run differs.
It uses that run only if every compared field matches exactly, and records
`jit_arithmetic: split-fma` in the manifest.
Regeneration also requires an exact match when a known recording platform
lacks the arithmetic provenance required for the locked replay suite.
For netplay captures with an attack-velocity mismatch, it also tries retail
`fnmsubs` arithmetic and records `fnmsubs_profile: retail` when validation passes.
If the first mismatch is only the sign of zero, that retry can use the same
movement tolerances as the original run.
An Ice Climbers capture can require both settings.
For a DamageFlyRoll mismatch, netplay captures can also retry using only the
recorded frame-start RNG seeds. An exact match records `rng_profile: frame-start`.
On Final Destination, older netplay captures can record the background freeze
code while the background animates. An exact retry with the animated background
records `fd_background_profile: animated`.
For Ice Climbers in teams, an exact retry that carries the previous frame's
RNG into stock sharing records `rng_profile: stock-share`.
A final exact retry can drive Nana with her recorded Pre-Frame inputs for
human-controlled Ice Climbers. It records `recorded_follower_inputs: true`.

When filling in missing stored state, the converter allows the differences
below and counts them in the manifest. The original recorded values are
preserved. Use `--strict-exactness` to require exact matches.

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
