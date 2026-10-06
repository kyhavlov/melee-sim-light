# Runtime API Reference

The public native surface is declared in [`src/api.h`](src/api.h). One `MslBatch` owns its mutable environments
and initialization-only scratch; the immutable game data is loaded once per process (one data root
per process) and shared by every batch. All runtime arrays are contiguous and have
`msl_batch_size(batch)` rows.

| call | purpose |
| --- | --- |
| `msl_game_data_acquire(data_root)` / `msl_game_data_release()` | load the process's game data ahead of any batch (e.g. before forking) and drop that hold |
| `msl_batch_create(data_root, count, out)` | load data (if not already loaded) and create independent environments |
| `msl_batch_reset(batch, configs, mask, observations)` | reset all or selected environments and write initial observations |
| `msl_batch_step(batch, inputs, observations, terminals)` | advance every environment one frame and write outputs |
| `msl_batch_observe(batch, observations, terminals)` | project current state without advancing |
| `msl_batch_copy(...)` | copy arbitrary environments between compatible batches |
| `msl_batch_save(...)` / `msl_batch_restore(...)` | serialize or restore one complete environment |

`data_root` may be the extraction root or its `raw/` directory. A `NULL` reset mask selects every
environment; otherwise each nonzero byte selects its corresponding row. Save artifacts include the
complete gameplay state plus public episode/viewpoint metadata and can restore into any index.

## Python API

The main Python operations are:

```python
env.configure_match(...)
env.configure_matches(configs, env_ids=...)
env.reset_all()
env.reset_matches(env_ids)
env.step()
env.observe()
state = env.save(env_index)
env.restore(env_index, state)
env.copy_matches_from(source, destination_indices, source_indices)
```

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
player index, then opponents by source player index. Eliminated leaders have
`present == 0` and zero dynamic fields; `source_player`, `team_relation`, `team_id`,
`char_id`, and `stocks` remain available. Unused leader slots have
`source_player == 255` and all other fields zero.

`followers[k]` is the Ice Climbers follower (Nana) of the player in `slots[k]`,
in the same `MslObservationPlayer` layout. Her `char_id` is her own fighter kind,
`11`, which is not an `MSL_CHARACTER_*` value; `source_player`, `team_relation`,
`team_id` and `stocks` repeat her leader's.
It has `present == 1` exactly while Slippi records follower rows: while her
fighter is awake, including her death animation, but not while she is eliminated
until the leader's Rebirth. Otherwise, and for every other character, it is
zeroed.

`recorded[k]` holds four values of the player in `slots[k]` that a Slippi
recording carries in its post-frame row and `MslObservationPlayer` leaves out,
in the recording's own encoding. `follower_recorded[k]` is the same for
`followers[k]`. A record is zeroed while its row is absent.

`state_flags` is Slippi's State Bit Flags 1 to 5: the fighter's flag bytes at
`0x2218`, `0x221A`, `0x221B`, `0x221C` and `0x221F`. Bits that Slippi's
specification or the decompilation names:

| byte | bit | constant | meaning |
| --- | --- | --- | --- |
| `state_flags[0]` | `0x80` | `MSL_STATE0_ALLOW_INTERRUPT` | the move has reached the frame from which it can be interrupted (IASA); `allow_interrupt` in the decompilation, unnamed by Slippi. A move clears it as it starts and its script raises it; it keeps its last value outside such moves |
| | `0x10` | `MSL_STATE0_REFLECT` | a reflector is active |
| | `0x08` | `MSL_STATE0_REFLECT_KEEPS_OWNER` | the reflector does not take ownership of the projectile (Mewtwo's Confusion) |
| | `0x02` | `MSL_STATE0_ABSORB` | an absorber is active (Oil Panic) |
| `state_flags[1]` | `0x20` | `MSL_STATE1_HITLAG` | in hitlag |
| | `0x10` | `MSL_STATE1_DEFENDER_HITLAG` | in hitlag as the one hit (not shield hitlag) |
| | `0x08` | `MSL_STATE1_FAST_FALL` | fast-falling |
| | `0x04` | `MSL_STATE1_SUBACTION_INVULNERABLE` | intangible or invincible from the move's own script |
| `state_flags[2]` | `0x80` | `MSL_STATE2_SHIELD` | the shield is up |
| | `0x04` | `MSL_STATE2_HOLDING` | holding another fighter (a grab or a command grab) |
| `state_flags[3]` | `0x20` | `MSL_STATE3_POWERSHIELD` | powershield active |
| | `0x04` | `MSL_STATE3_DETECTION_ON_SHIELD` | this fighter's detection hitbox touches a shield |
| | `0x02` | `MSL_STATE3_HITSTUN` | in hitstun |
| `state_flags[4]` | `0x80` | `MSL_STATE4_OFFSCREEN` | off screen. The game's render pass sets it, so against a recording it can differ for a frame; replay validation does not hold the sim to this bit |
| | `0x40` | `MSL_STATE4_DEAD` | dead: through the death animation, cleared by the respawn |
| | `0x10` | `MSL_STATE4_INACTIVE` | asleep; never set here, because a sleeping fighter's row is absent |
| | `0x08` | `MSL_STATE4_FOLLOWER` | a follower (Nana) |
| | `0x02` | `MSL_STATE4_CLOAK` | Cloaking Device |

The other bits are passed through as recorded and have no name in either
source.

`l_cancel` is nonzero only on the frame an aerial attack lands in its landing
lag: `MSL_L_CANCEL_HIT` (`1`) when the shield button came inside the window and
the lag is halved, `MSL_L_CANCEL_MISSED` (`2`) when it did not. It is
`MSL_L_CANCEL_NONE` (`0`) on every other frame, and for landings that have no
landing-lag action.

`ground_id` is the index of the stage collision line the fighter stands on, or
last stood on while in the air: it changes on the landing frame. A fighter
that has stood on nothing since it respawned reads `65535`. The indices are
the stage file's own, so they mean something per stage only; each platform is
its own line or run of lines (on Battlefield the main stage under the left
platform is `1`, the left platform `2` and the right platform `4`).

`last_attack_landed` is the Slippi attack id of the last attack of this
fighter that hit another fighter (`2` for the first jab, `10` for a forward
smash; the ids are Slippi's attack table, the same ids `MslItem.attack_id`
and the stale-move queue use). The value stays after the move ends. It is `0`
before the first hit and from the respawn after a KO.

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
| `recorded` | `MslObservationRecorded` | recorded values of each slot's player | `[4]` |
| `follower_recorded` | `MslObservationRecorded` | recorded values of each slot's follower | `[4]` |

`MslObservationPlayer`:

| field | type | values / range |
| --- | --- | --- |
| `present` | `uint8_t` | `0` or `1` |
| `source_player` | `uint8_t` | roster index `0..num_players-1`, or `255` for unused slots |
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

`MslObservationRecorded`:

| field | type | values / range |
| --- | --- | --- |
| `state_flags` | `uint8_t[5]` | Slippi's State Bit Flags 1 to 5, `MSL_STATE*` bits above |
| `l_cancel` | `uint8_t` | `MSL_L_CANCEL_*`: `0` none, `1` hit, `2` missed |
| `ground_id` | `uint16_t` | stage collision line index |
| `last_attack_landed` | `uint8_t` | Slippi attack id, `0` for none |

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

`MslObservationStage` exposes moving platforms and Whispy's wind:

| field | type | values / range |
| --- | --- | --- |
| `randall.exists` | `uint8_t` | `0` or `1` |
| `randall.x`, `randall.y` | `float` | midpoint of the live collision floor in world coordinates |
| `fod_platforms.left`, `fod_platforms.right` | `float` | Fountain of Dreams platform heights |
| `whispy` | `uint8_t` | `MSL_WHISPY_NONE` (0), `MSL_WHISPY_LEFT` (1), `MSL_WHISPY_RIGHT` (2) |

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

## Sharing game data across worker processes

An `EnvBatch` needs the process's immutable game data, about 300 MB, which is loaded once per
process. A trainer that shards its environments over one `EnvBatch` per CPU core would pay that per
worker if each worker loaded it independently. Instead, load it once in a template process and
fork the workers from that: the game data is never written after initialization (workers stepping
thousands of frames dirty only a few MB), so the forked copies share its pages copy-on-write.

The training process itself is usually not a safe template (a JAX or CUDA context, thread pools).
Use `multiprocessing`'s `forkserver`, which is a fresh interpreter, and give it a preload module
that loads the data before any worker is forked:

```python
# myproject/sim_preload.py -- imported by the forkserver before it forks workers.
import os
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")  # workers don't need BLAS threads
import gc
import melee_sim

melee_sim.preload_game_data()  # MSL_DATA_DIR, or pass the data dir explicitly
gc.collect()
gc.freeze()  # keep the workers' first GC pass from copying the inherited heap
```

```python
import multiprocessing as mp

ctx = mp.get_context("forkserver")
ctx.set_forkserver_preload(["myproject.sim_preload"])
workers = [ctx.Process(target=step_shard, args=(i,)) for i in range(16)]
```

Each worker then builds its own `EnvBatch` as usual; `msl_batch_create` finds the data already
loaded. In C the equivalent is `msl_game_data_acquire(root)` in the template before forking, and
`msl_game_data_release()` when the template no longer needs it.

Constraints: one data root per process (a second root is `MSL_INVALID_STATE`), and the usual
one: a batch is driven from one thread at a time. Which thread does not matter; every reset and
step rebinds the runtime's thread-local context from the batch. Without a preload nothing changes:
each process loads the data on its first batch.
