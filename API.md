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

`stored[k]` is what the player in `slots[k]` keeps between moves, in the game's
own units. Slippi does not record it. A character with nothing to keep, an
unused slot and an absent player read zero, with `copied_char` at
`MSL_COPIED_NONE` and both `judge` numbers at `MSL_JUDGE_NONE`.

`stored[k].charge` is a stored move's count:

| character | move | counts | range |
| --- | --- | --- | --- |
| Donkey Kong | Giant Punch | arm swings | `0..10`, `10` is full |
| Samus | Charge Shot | charge steps | `0..7`, `7` is full |
| Mewtwo | Shadow Ball | charge cycles | `0..7`, `7` is full |
| Sheik | Needle Storm | needles in hand | `0..6`, `1` as the move starts |
| Mr. Game & Watch | Oil Panic | shots caught | `0..3`, `3` is full |
| Kirby | the copied Giant Punch, Charge Shot, Shadow Ball or Needle Storm | as the copied move | as the copied move |

The count rises while the move charges, stays through other actions and
through hits taken outside the move, and returns to `0` when the move uses it
(Sheik's needles leave one at a time). A hit during the charge clears it,
except a full Shadow Ball. A KO clears it by the respawn, except Oil Panic's
count, which the game keeps. Kirby's goes with the hat.

The values are the game's own variables, so they show the game's quirks as it
has them (NTSC 1.02; PAL's changes are not modeled). A hit also clears the
count when it lands during another move that installs the same damage
callback: Donkey Kong's and Samus's up specials, Sheik's chain while it is
out, and Mewtwo's Disable once its spark is out (a full Shadow Ball is kept).

`stored[k].copied_char` is Kirby's copied ability: the `MSL_CHARACTER_*` whose
neutral special he has, in the id space of `char_id`, so it compares directly
with another slot's `char_id`. It is `MSL_COPIED_NONE` (`255`) for Kirby
without one and for every other character. It says whose move Kirby's `charge`
and `gauge` belong to: Donkey Kong's, Samus's, Mewtwo's or Sheik's count,
Bowser's gauges, and nothing stored for the rest. The game sets it on the
frame the swallow gives him the hat (swallowing another Kirby gives that
Kirby's ability; swallowing Nana gives the Ice Climbers'), and clears it when a
taunt throws the hat away, when a hit knocks it off (the game rolls for that on
hits) and on the frame of a KO.

`stored[k].gauge` holds stored amounts that are not a count:

| character | `gauge[0]` | `gauge[1]` |
| --- | --- | --- |
| Mr. Game & Watch | the damage the caught shots would have dealt, summed: a whole number, `0` with an empty bucket, with no upper limit in the game (three of Falco's lasers are `9`, three of Mario's fireballs `18`) | `0` |
| Bowser | Fire Breath's fuel, `40..360`, `360` at rest | Fire Breath's flame size, `60..380`, `380` at rest |
| Kirby wearing Bowser's hat | his copy's fuel, `40..360` | his copy's flame size, `60..380` |
| Peach | her float's frames left, `150` on the frame a float starts, `0` while she is not floating | `0` |

Oil Panic's spill deals `floor(gauge[0] * 1.5) + 5` before staling. The spill
clears the damage with the count. A KO clears the damage at the respawn and
keeps the count, so a full bucket after a KO spills for `5`.

Each frame of Fire Breath takes `1` from the fuel and from the flame size, down
to their floors, and each frame outside the move gives `0.7` back to both, up
to the full values: 320 frames of breath empty them and 458 frames refill them.
A flame is spawned with speed `fuel / 360` and scale `size / 380`, so the fuel
is how far the breath reaches and the size how big its flames are. A respawn
makes both full. Kirby's pair goes with the hat. Unlike a count, these are not
`0` at rest.

Peach's float lasts 150 frames. The count goes down by `1` on every frame of
the float and of an air attack done out of it, and the float ends when it
reaches `0`. The game reads its variable only in those states and leaves the
rest in it when a float is let go early, so the observation publishes `0`
whenever no float is going on.

`stored[k].spent` is a set of bits. Each is something the fighter has used and
does not have again until the game gives it back:

| bit | value | set while |
| --- | --- | --- |
| `MSL_SPENT_NEUTRAL_LIFT` | `1` | the one lift of the neutral special is used |
| `MSL_SPENT_SIDE_LIFT` | `2` | the one lift of the side special is used |
| `MSL_SPENT_DOWN_LIFT` | `4` | the one lift of the down special is used |
| `MSL_SPENT_FLOAT` | `8` | Peach has floated |
| `MSL_SPENT_TETHER` | `16` | the aerial grapple is used |
| `MSL_SPENT_FOLLOWER_NEUTRAL_LIFT` | `32` | Nana's own neutral special lift is used |

Which character has which:

| character | bit | move | with the bit set |
| --- | --- | --- | --- |
| Mario, Dr. Mario | `MSL_SPENT_SIDE_LIFT` | Cape, Super Sheet | an aerial use does not rise |
| Mario, Dr. Mario | `MSL_SPENT_DOWN_LIFT` | Mario Tornado, Dr. Tornado | tapping B during an aerial use does not raise him |
| Luigi | `MSL_SPENT_DOWN_LIFT` | Luigi Cyclone | tapping B during an aerial use does not raise him |
| Marth, Roy | `MSL_SPENT_SIDE_LIFT` | Dancing Blade, Double-Edge Dance | an aerial use does not rise |
| Mewtwo | `MSL_SPENT_SIDE_LIFT` | Confusion | an aerial use does not rise |
| Peach | `MSL_SPENT_NEUTRAL_LIFT` | Toad | an aerial use does not rise |
| Peach | `MSL_SPENT_FLOAT` | the float | she cannot float |
| Ice Climbers | `MSL_SPENT_NEUTRAL_LIFT` for the leader, `MSL_SPENT_FOLLOWER_NEUTRAL_LIFT` for Nana | Ice Shot | an aerial use does not rise |
| Kirby | `MSL_SPENT_SIDE_LIFT` | Hammer | an aerial use does not rise |
| Kirby wearing Peach's or the Ice Climbers' hat | `MSL_SPENT_NEUTRAL_LIFT` | the copied Toad or Ice Shot | an aerial use does not rise |
| Link, Young Link, Samus | `MSL_SPENT_TETHER` | the aerial grapple | the same input is an air attack |

The game sets a lift bit during the first aerial use of the move (as it
starts, or part-way through for the Cape, Toad and the tornadoes), the float
bit on the frame a float starts, and the tether bit on the frame of the
grapple. It gives them back in different places, and the bits show exactly
that:

- A lift comes back on the first frame of a plain landing or of the landing
  after special fall, when the move itself touches the ground, and at the
  respawn after a KO. Kirby's copied one also goes with the hat.
- A lift does not come back on a landing in the landing lag of an air attack:
  the fighter then stands on the ground with the bit still set, and the next
  aerial use, after a jump, does not rise. A hit does not give it back either.
- Luigi's is not given back by any landing, nor by a KO. Only a cyclone that
  touches the ground does it, which one started on the ground does on its
  first frame.
- The float and the tether come back on any change of action on the ground,
  a landing in an air attack's landing lag included. A KO gives the float back
  at the respawn. It does not give the tether back: the new stock has none
  until it stands on the ground.

`stored[k].wall_jumps` counts the wall jumps since the fighter last stood on
the ground. The game uses it to weaken each wall jump after the first, and
clears it on the first frame on the ground.

`stored[k].judge` is Mr. Game & Watch's last two Judge numbers, which the game
leaves out of the next roll: `judge[0]` is the most recent, `judge[1]` the one
before. Each is the hammer's number minus one (`0..8`), as the game stores it.
A match and every new stock start with `1` and `0`, so the first hammer is
never a 1 or a 2. Everyone else reads `MSL_JUDGE_NONE` (`255`) in both.

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
| `stored` | `MslObservationStored` | what each slot's player keeps between moves | `[4]` |

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

`MslObservationStored`:

| field | type | values / range |
| --- | --- | --- |
| `charge` | `uint8_t` | a stored move's count, per character above |
| `copied_char` | `uint8_t` | Kirby's copied ability as `MSL_CHARACTER_*`, or `MSL_COPIED_NONE` (`255`) |
| `spent` | `uint8_t` | `MSL_SPENT_*` bits, per character above |
| `wall_jumps` | `uint8_t` | wall jumps since the fighter last stood on the ground |
| `judge` | `uint8_t[2]` | Mr. Game & Watch's last two Judge numbers, `0..8`, or `MSL_JUDGE_NONE` (`255`) |
| `_pad0` | `uint8_t[2]` | spare, `0` |
| `gauge` | `float[2]` | stored amounts that are not a count, per character above |

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
