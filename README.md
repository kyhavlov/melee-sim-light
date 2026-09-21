# melee-sim-light

[![melee-core validation](https://github.com/kyhavlov/melee-sim-light/actions/workflows/validation.yml/badge.svg?branch=main)](https://github.com/kyhavlov/melee-sim-light/actions/workflows/validation.yml)

`melee-sim-light` is a deterministic, batched simulator for high-throughput RL training,
built on the [SSBM Decompilation](https://github.com/doldecomp/melee) project.

The simulator core is C. The public Python package is `melee_sim`, with a thin
NumPy API over native batch execution.

Currently supports all competitive stages, and all characters except for Kirby.

Gameplay should be 1:1 parity with vanilla Melee/Slippi, with the following remaining exception:

- Offscreen magnifier damage isn't frame-exact: 1% ticks can shift, affecting knockback and survival.
  Melee gates this damage through rendering, which can lag gameplay updates. The sim uses one
  deterministic visibility update per step; emulating console display/polling timing is outside its RL scope.

A few other exceptions prevent exact Slippi replay reproduction: historical rounding differences,
missing raw inputs, and uninitialized recorded bytes. These don't represent missing gameplay mechanics
for RL; reproducing them is either not useful or impossible with the information recorded.

Benchmarks:
| CPU | Batch size | Per-core FPS |
| ---: | ---: | ---: |
| AMD Ryzen 9 9950X3D | 256 | 116,598 |
| AMD Ryzen 9 9950X3D | 512 | 120,618 |

## Quick Start

Prerequisites:
- Python 3.11 or newer (if using the Python API)
- GCC (for building the core simulation)

Clone the repo:
```bash
git clone https://github.com/kyhavlov/melee-sim-light
cd melee-sim-light
```

From another project, install the local checkout with `uv`:

```bash
uv add "melee-sim-light @ file:///path/to/melee-sim-light"
```

The simulator needs extracted game data before it can run.
Extract it from a valid SSBM ISO:

```bash
uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso
```

By default, the extraction command creates a local `data/` root. The minimal retail archive profile
lives under `data/raw/` and the decomp-based core translates it directly during initialization.
The ISO and extracted archives are ignored and are never repository assets. A deterministic
manifest binds the extraction to the source ISO. This is the complete data setup; it directly
extracts every required archive and `main.dol` from a GALE01 revision 2 ISO. Pass
`--out-dir /path/to/my-msl-data` to use a different root.

`EnvBatch()` and the decomp-based core both load `data/` in the current working directory by
default. To use or share another data root, set `MSL_DATA_DIR` or pass `data_dir` to the current
Python API:

```bash
MSL_DATA_DIR=/path/to/data uv run python train.py
```

Each MSL process owns immutable game data loaded from its data root, and a configurable batch size of
game environments. Matches in that batch share the global immutable game data and each have their own
independent mutable state.

## Examples

### Python API

```python
import melee_sim as msl

with msl.EnvBatch(batch_size=512, length=128) as env:
    env.configure_match(
        stage=msl.Stage.FINAL_DESTINATION,
        players=[
            msl.PlayerConfig(msl.Character.FOX),
            msl.PlayerConfig(msl.Character.FALCO),
        ],
    )

    fox = msl.neutral_controller((env.length, env.batch_size))
    fox.buttons.B[0] = True
    fox.main_stick.x[0] = 1.0
    msl.write_controller(env.controller_action_view, fox, player=0)

    env.reset_all()
    for _ in range(env.length):
        env.step()

    state = env.current_frame[0]
    print(state["frame_id"], state["slots"][0]["percent"])
```

Controller buffers start neutral. `gamestate_view[0]` is the reset state and each call to
`step()` writes the next row. Rebind or reset the cursor to reuse a fixed-length buffer chunk.

The main Python operations are:

```python
env.configure_match(...)
env.configure_matches(configs, env_ids=...)
env.reset_all()
env.reset_masked()
env.step()
env.observe()
state = env.save(env_index)
env.restore(env_index, state)
env.copy_matches_from(source, destination_indices, source_indices)
```

### C Native API

`MslBatch` owns the shared immutable game data and every mutable environment. Reset and step consume
contiguous arrays whose length is the batch size; the simulator allocates nothing on those paths.

```c
#include <stdio.h>

#include "src/api.h"

int main(void)
{
  enum { BATCH_SIZE = 512 };
  MslBatch* sim = NULL; // Opaque simulator batch.
  MslMatchConfig matches[BATCH_SIZE]; // Reset configuration for each match.
  MslInput inputs[BATCH_SIZE] = { 0 }; // Neutral inputs for the next frame.
  MslObservation observations[BATCH_SIZE]; // Current state of each match.
  MslTerminal terminals[BATCH_SIZE]; // Episode status for each match.

  for (int i = 0; i < BATCH_SIZE; ++i) {
    matches[i] = msl_match_config_default();
    matches[i].random_seed = (uint32_t) i;
  }

  MslResult result = msl_batch_create("data", BATCH_SIZE, &sim);
  if (result == MSL_OK) {
    result = msl_batch_reset(sim, matches, NULL, observations);
  }
  if (result != MSL_OK) {
    fprintf(stderr, "%s\n", msl_result_string(result));
    msl_batch_destroy(sim);
    return 1;
  }

  inputs[0].players[0].main_x = 80;
  result = msl_batch_step(sim, inputs, observations, terminals);
  if (result != MSL_OK) {
    fprintf(stderr, "%s\n", msl_result_string(result));
    msl_batch_destroy(sim);
    return 1;
  }
  printf("frame=%d x=%f\n", observations[0].frame_id,
         observations[0].slots[0].pos_x);

  msl_batch_destroy(sim);
  return 0;
}
```

## Viewer

![Live viewer screenshot](viewer-live.png)

There's a web viewer in `tools/viewer` that supports both a live play mode
for driving an exported WASM version of the sim with keyboard/controller input, and watching
saved replay traces that can be written from sim gamestate data. It uses the
[`@gcpreston/slippi-viewer`](https://www.npmjs.com/package/@gcpreston/slippi-viewer)
project for game visualization:

Prerequisites:

- Emscripten activated so `emcc` is on `PATH`, for the WASM build
- Node.js/npm available for the renderer build
- extracted simulator data; defaults to source-checkout `data/`, or set `MSL_DATA_DIR`
- network access on the first build to download character display assets

```bash
npm --prefix tools/viewer/slippi-viewer install
make viewer-build
make viewer
```

Open `http://127.0.0.1:8001/tools/viewer/`.

The live viewer writes `*.msltrace.json`. The trace format is
documented in `tools/viewer/TRACE_FORMAT.md`; it is compact JSON with
sparse-delta frame, input, and item streams. `make viewer-build` downloads the
display assets into `build/cache/viewer-zips/` on first use and
verifies them by checksum on later builds.

## Runtime API Reference

The public native surface is declared in `src/api.h`. One `MslBatch` owns its immutable game data,
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
| `num_players` | `uint8_t` | `2` or `4` | active source players |
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

Top-level fields:

| field | type | values / range | shape |
| --- | --- | --- | --- |
| `frame_id` | `int32_t` | match frame id (starts at -123 in pre-match countdown) | scalar |
| `frame_pre_random_seed` | `uint32_t` | any `uint32_t` | scalar |
| `stage_id` | `uint32_t` | `MSL_STAGE_*` | scalar |
| `num_players` | `uint8_t` | `2` or `4` | scalar |
| `viewpoint_player` | `uint8_t` | `0..num_players-1` | scalar |
| `is_teams` | `uint8_t` | `0` or `1` | scalar |
| `stage` | `MslObservationStage` | stage-owned state | scalar |
| `slots` | `MslObservationPlayer` | viewpoint-relative players | `[4]` |
| `items` | `MslItem` | active/inactive item slots | `[15]` |

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
