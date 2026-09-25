# Runtime API Reference

The public native surface is declared in [`src/api.h`](src/api.h). One `MslBatch` owns its immutable game data,
mutable environments, and initialization-only scratch. All runtime arrays are contiguous and have
`msl_batch_size(batch)` rows.

| call | purpose |
| --- | --- |
| `msl_batch_create(data_root, count, out)` | load data and create independent environments |
| `msl_batch_reset(batch, configs, mask, observations)` | reset all or selected environments and write initial observations |
| `msl_batch_step(batch, inputs, observations, terminals)` | advance every environment one frame and write outputs |
| `msl_batch_observe(batch, observations, terminals)` | project current state without advancing |
| `msl_batch_copy(...)` | copy arbitrary environments between compatible batches |
| `msl_batch_save(...)` / `msl_batch_restore(...)` | serialize or restore one complete environment |

`data_root` may be the extraction root or its `raw/` directory. A `NULL` reset mask selects every
environment; otherwise each nonzero byte selects its corresponding row. Save artifacts include the
complete gameplay state plus public episode/viewpoint metadata and can restore into any index.

## Native Input And Match Config

`msl_match_config_default()` returns a ready-to-run Fox/Falco match on Final Destination with four
stocks and UCF enabled. Callers only override fields they need:

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `stage` | `uint32_t` | `MSL_STAGE_*` | stage id |
| `random_seed` | `uint32_t` | any `uint32_t` | deterministic reset seed |
| `max_frame` | `int32_t` | `-1` or nonnegative | optional episode cutoff |
| `damage_ratio` | `float` | positive | global damage ratio |
| `num_players` | `uint8_t` | `2`, `3`, or `4` | active source players |
| `is_teams` | `uint8_t` | `0` or `1` | nonzero for teams |
| `friendly_fire` | `uint8_t` | `0` or `1` | enable team damage |
| `stocks` | `uint8_t` | `1..255` | starting stocks |
| `viewpoint_player` | `uint8_t` | active player index | player placed in observation slot zero |
| `ucf_cardinals` | `uint8_t` | `0` or `1` | enable the UCF 1.0 cardinal patch |
| `players[4]` | `MslPlayerConfig[4]` | active entries `< num_players` | per-player configuration |

`MslPlayerConfig` uses explicit, unpacked fields. Team and controller port accept their `*_AUTO`
constant; facing accepts `MSL_FACING_LEFT`, `MSL_FACING_RIGHT`, or `MSL_FACING_AUTO`.

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `character` | `uint8_t` | `MSL_CHARACTER_*` | supported character |
| `team` | `int8_t` | auto or `0..2` | team assignment |
| `facing` | `int8_t` | left, auto, or right | starting direction |
| `controller_port` | `int8_t` | auto or `0..3` | physical controller port |
| `costume` | `uint8_t` | character costume id | starting costume |
| `handicap` | `uint8_t` | `1..9` | starting handicap; normally `9` |

`MslInput` contains one raw controller row per source player:

| field | type | values / range | native meaning |
| --- | --- | --- | --- |
| `players[4].buttons` | `uint16_t` | OR of `MSL_BUTTON_*` | packed digital buttons |
| `players[4].main_x`, `main_y` | `int8_t` | `-80..80` | raw main stick |
| `players[4].c_x`, `c_y` | `int8_t` | `-80..80` | raw C-stick |
| `players[4].l`, `r` | `uint8_t` | `0..255` | raw analog triggers |

## Native Gamestate Output

`MslObservation` is the policy-facing state written by `msl_batch_reset()`, `msl_batch_step()`,
and `msl_batch_observe()`.

Player slots are viewpoint-relative: `slots[0]` is self, then allies by source
player index, then opponents by source player index. Unused slots have
`present == 0`.

`followers[k]` is the Ice Climbers follower (Nana) of the player in `slots[k]`,
in the same `MslObservationPlayer` layout. Her `char_id` is her own fighter kind,
`11`, which is not an `MSL_CHARACTER_*` value; `source_player`, `team_relation`,
`team_id` and `stocks` repeat her leader's.
It has `present == 1` exactly while Slippi records follower rows: while her
fighter is awake, including her death animation, but not while she sleeps
until the leader's Rebirth. Otherwise, and for every other character, it is
zeroed.

Top-level fields:

| field | type | values / range | shape |
| --- | --- | --- | --- |
| `frame_id` | `int32_t` | match frame id (starts at -123 in pre-match countdown) | scalar |
| `frame_pre_random_seed` | `uint32_t` | any `uint32_t` | scalar |
| `stage_id` | `uint32_t` | `MSL_STAGE_*` | scalar |
| `num_players` | `uint8_t` | `2`, `3`, or `4` | scalar |
| `viewpoint_player` | `uint8_t` | `0..num_players-1` | scalar |
| `is_teams` | `uint8_t` | `0` or `1` | scalar |
| `stage` | `MslObservationStage` | stage-owned state | scalar |
| `slots` | `MslObservationPlayer` | viewpoint-relative players | `[4]` |
| `items` | `MslItem` | active/inactive item slots | `[15]` |
| `followers` | `MslObservationPlayer` | follower of each slot's player | `[4]` |

`MslObservationPlayer`:

| field | type | values / range |
| --- | --- | --- |
| `present` | `uint8_t` | `0` or `1` |
| `source_player` | `uint8_t` | `0..num_players-1` when present |
| `team_relation` | `uint8_t` | `0` self, `1` ally, `2` opponent |
| `team_id` | `uint8_t` | team assignment |
| `pos_x`, `pos_y` | `float` | world coordinates |
| `speed_air_x_self`, `speed_ground_x_self`, `speed_y_self` | `float` | self velocity components |
| `speed_x_attack`, `speed_y_attack` | `float` | attack/knockback velocity components |
| `percent` | `float` | damage percent, `0..999.9` |
| `shield_hp` | `float` | shield health, `0..60` |
| `action_id` | `uint16_t` | GALE01 action id |
| `action_frame` | `int16_t` | current action frame |
| `hitlag`, `hitstun` | `uint16_t` | remaining frames |
| `char_id` | `uint8_t` | `MSL_CHARACTER_*` |
| `stocks` | `uint8_t` | remaining stocks |
| `facing` | `uint8_t` | `0` left, `1` right |
| `on_ground` | `uint8_t` | `0` or `1` |
| `jumps_left` | `uint8_t` | remaining air jumps |
| `hurtbox_state` | `uint8_t` | GALE01 hurtbox state id |
| `invulnerable` | `uint8_t` | `0` or `1` |

`MslItem` slots are fixed-capacity. Inactive slots have `exists == 0`.

| field | type | values / range |
| --- | --- | --- |
| `exists` | `uint8_t` | `0` or `1` |
| `state` | `uint8_t` | item state id |
| `type` | `uint16_t` | item kind/type id |
| `owner` | `int8_t` | source player id, or `-1` if none/unknown |
| `instance_id`, `attack_id`, `attack_instance` | `uint16_t` | attack/staling identity |
| `direction` | `float` | facing/direction scalar |
| `vel_x`, `vel_y` | `float` | world velocity |
| `pos_x`, `pos_y` | `float` | world coordinates |
| `damage` | `uint16_t` | item damage value |
| `timer` | `float` | item timer/frame counter |
| `spawn_id` | `uint32_t` | deterministic spawn identity |
| `misc0`, `misc1`, `misc2`, `misc3` | `uint8_t` | item-specific bytes |

`MslObservationStage` exposes the policy-relevant moving platform state:

| field | type | values / range |
| --- | --- | --- |
| `randall.exists` | `uint8_t` | `0` or `1` |
| `randall.x`, `randall.y` | `float` | world coordinates |
| `fod_platforms.left`, `fod_platforms.right` | `float` | Fountain of Dreams platform heights |

## Terminal Output

`MslTerminal` is written by `msl_batch_step()` and `msl_batch_observe()`:

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `frame_id` | `int32_t` | current frame id | current frame |
| `stage_id` | `uint32_t` | `MSL_STAGE_*` | current stage |
| `done` | `uint8_t` | `0` or `1` | any terminal condition |
| `match_ended` | `uint8_t` | `0` or `1` | in-game match end |
| `stockout` | `uint8_t` | `0` or `1` | player/team out of stocks |
| `max_frame_reached` | `uint8_t` | `0` or `1` | caller-specified frame cutoff |
