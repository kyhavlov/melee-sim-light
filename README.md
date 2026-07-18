# melee-sim-light

`melee-sim-light` is a deterministic, batched SSBM-like simulator for
high-throughput RL training and replay-driven validation.

The simulator core is C. The public Python package is `melee_sim`, with a thin
NumPy API over native batch execution.

## Quick Start

Prerequisites:
- `uv`
- Git LFS
- Python 3.11 or newer
- a C compiler available as `cc`

Clone the repo:
```bash
git clone https://github.com/kyhavlov/melee-sim-light
cd melee-sim-light
git lfs install
git lfs pull
```

From another project, install the local checkout with `uv`:

```bash
uv add "melee-sim-light @ file:///path/to/melee-sim-light"
```

The simulator needs extracted game data before it can create an `EnvBatch`.
Extract it from a valid SSBM ISO:

```bash
uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso [--out-dir /path/to/my-msl-data]
```

By default, the extraction command creates a local `data/` root. The minimal retail archive profile
lives under `data/raw/` and the source-shaped core translates it directly during initialization.
The ISO and extracted archives are ignored and are never repository assets. A deterministic
manifest binds the extraction to the source ISO.

`EnvBatch()` and the source-shaped core both load source-checkout `data/` by default. To use or
share another data root, set `MSL_DATA_DIR` or pass `data_dir` to the current Python API:

```bash
MSL_DATA_DIR=/path/to/data uv run python train.py
```

```python
env = msl.EnvBatch(batch_size=1024, data_dir="/path/to/data")
```

Each `EnvBatch` owns immutable game data loaded from its data root. Matches in that batch share the
game data, and different batches may use different compatible roots.

## Examples

### Python

The Python API is built around two objects:

- `EnvBatch` owns the native simulator instance: batch size, player count, loaded data, and the current simulator state.
- `Buffers` owns the reusable NumPy arrays for match config, controller input, observations, and outputs. Reusing one buffer object avoids per-step allocation.

A typical RL loop creates one `EnvBatch`, writes controller inputs into its bound buffers, and calls `env.step()`:

```python
import melee_sim as msl

with msl.EnvBatch(batch_size=2, length=128, num_players=2) as env:
    env.configure_match(
        stage=msl.Stage.FINAL_DESTINATION,
        players=[
            msl.PlayerConfig(character=msl.Character.FOX),
            msl.PlayerConfig(character=msl.Character.FALCO),
        ],
    )

    # Write player inputs for the whole reusable buffer chunk.
    fox = msl.neutral_controller((env.length, env.batch_size))
    fox.buttons.B[0, 0] = True
    fox.main_stick.x[0, 0] = 1.0
    msl.write_controller(env.controller_action_view, fox, player=0)

    neutral = msl.neutral_controller((env.length, env.batch_size))
    msl.write_controller(env.controller_action_view, neutral, player=1)

    env.reset_all()
    env.step()

    # gamestate has one extra frame: row 0 is the reset state, row 1 is after
    # the first step.
    frame_1 = env.gamestate_view[1, 0]
    fox_slot = frame_1["slots"][0]
    print(frame_1["frame_id"], fox_slot["action_id"], fox_slot["pos_x"])

    # Reuse the same arrays for the next chunk after consuming frames 0..127.
    env.reset_cursor()
```

Useful Python signatures:

```python
EnvBatch(
    batch_size: int,
    length: int = 256,
    num_players: int = 2,
    *,
    data_dir: str | os.PathLike[str] | None = None,
    observation: str = "native",
    action_format: str = "controller",
    obs_dim: int = 0,
    ucf_enabled: bool = True,
    ucf_cardinals_1_0_enabled: bool = False,
) -> None

env.buffers -> Buffers
env.allocate_buffers(*, observation: str = "native", action_format: str = "controller", obs_dim: int = 0) -> Buffers
env.configure_match(buffers: Buffers | None = None, config: MatchConfig | None = None, **overrides) -> None
env.configure_matches(configs: Sequence[MatchConfig], *, env_ids: Sequence[int] | np.ndarray | None = None) -> None
env.bind(buffers: Buffers) -> None
env.reset_all() -> None
env.reset_masked(*, write_initial_observation: bool = True) -> None
env.step(*, write_outputs: bool = True, write_compare: bool = False, max_frame_id: int = -1) -> None
env.write_compare() -> None
env.copy_matches_from(source: EnvBatch, destination_indices, source_indices) -> None
env.save(match_index: int) -> bytes
env.restore(match_index: int, state: bytes | bytearray | memoryview) -> None
env.reset_cursor() -> None

PlayerConfig(character: int | Character, team_id: int | None = None, facing: int | None = None,
             controller_port: int | None = None, costume: int = 0, handicap: int = 9)
MatchConfig(stage: int | Stage = Stage.FINAL_DESTINATION, players: tuple[PlayerConfig, ...] | None = None, ...)
```

### C Native API

The native API is built around immutable game data, one simulator batch, and caller-owned arrays:

- `MslCoreGameData` owns the extracted game data shared by every match in a batch.
- `MslCoreBatch` owns independent mutable match states.
- `MslCoreMatchConfig` rows describe resets, while `MslCoreInput` rows provide controller input.
- Output arrays receive observations, terminal flags, or complete validation state.
- Batch functions accept byte strides and optional masks so integrations can reuse their own fixed layouts.

A typical native loop creates the shared game data and batch, resets its matches, steps one input
row, and writes policy-facing observations:

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/api.h"

int main(void) {
  enum { BATCH = 2 };

  MslCoreGameData* game_data = NULL;
  MslCoreBatch* batch = NULL;
  if (msl_core_game_data_create("data/raw", &game_data) != MSL_CORE_OK ||
      msl_core_batch_create(game_data, BATCH, &batch) != MSL_CORE_OK) {
    msl_core_game_data_destroy(game_data);
    return 1;
  }

  MslCoreMatchConfig configs[BATCH];
  memset(configs, 0, sizeof(configs));
  for (int i = 0; i < BATCH; i++) {
    configs[i].stage_id = MSL_CORE_STAGE_FINAL_DESTINATION;
    configs[i].frame_id = -123;
    configs[i].frame_pre_random_seed = 1;
    configs[i].initial_random_seed = 1;
    configs[i].match_damage_ratio = 1.0f;
    configs[i].num_players = 2;
    configs[i].stock_count = 4;
    configs[i].ucf_shield_sdi_enabled = 1;
    configs[i].ucf_sdi_enabled = 1;
    configs[i].players[0].char_id = MSL_CORE_CHARACTER_FOX;
    configs[i].players[0].handicap = 9;
    configs[i].players[1].char_id = MSL_CORE_CHARACTER_FALCO;
    configs[i].players[1].handicap = 9;
  }

  if (msl_core_batch_reset_matches(batch, configs, sizeof(configs[0]), NULL, 0) !=
      MSL_CORE_OK) {
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    return 1;
  }

  MslCoreInput input[BATCH];
  memset(input, 0, sizeof(input));
  input[0].p[0].main_x = 80;

  if (msl_core_batch_step_matches(batch, input, sizeof(input[0]), NULL, 0) !=
      MSL_CORE_OK) {
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    return 1;
  }

  uint8_t viewpoint[BATCH] = {0};
  MslCoreObservation out[BATCH];
  if (msl_core_batch_write_observation(batch, viewpoint, sizeof(viewpoint[0]), out,
                                       sizeof(out[0]), NULL, 0) != MSL_CORE_OK) {
    msl_core_batch_destroy(batch);
    msl_core_game_data_destroy(game_data);
    return 1;
  }

  printf("frame=%d p0_action=%u p0_x=%f\n", out[0].frame_id,
         (unsigned)out[0].slots[0].action_id, out[0].slots[0].pos_x);

  msl_core_batch_destroy(batch);
  msl_core_game_data_destroy(game_data);
  return 0;
}
```

Useful C signatures:

```c
MslCoreResult msl_core_game_data_create(const char* data_root, MslCoreGameData** out);
void msl_core_game_data_destroy(MslCoreGameData* game_data);
MslCoreResult msl_core_batch_create(const MslCoreGameData* game_data, uint32_t match_count,
                                    MslCoreBatch** out);
void msl_core_batch_destroy(MslCoreBatch* batch);

MslCoreResult msl_core_batch_reset_matches(MslCoreBatch* batch, const MslCoreMatchConfig* configs,
                                           size_t config_stride, const uint8_t* mask,
                                           size_t mask_stride);
MslCoreResult msl_core_batch_step_matches(MslCoreBatch* batch, const MslCoreInput* inputs,
                                          size_t input_stride, const uint8_t* mask,
                                          size_t mask_stride);
MslCoreResult msl_core_batch_write_observation(const MslCoreBatch* batch,
                                               const uint8_t* viewpoints,
                                               size_t viewpoint_stride,
                                               MslCoreObservation* output,
                                               size_t output_stride,
                                               const uint8_t* mask, size_t mask_stride);

MslCoreResult msl_core_batch_match_save_size(const MslCoreBatch* batch,
                                             uint32_t match_index, size_t* required_size);
MslCoreResult msl_core_batch_save_match(const MslCoreBatch* batch, uint32_t match_index,
                                        void* buffer, size_t buffer_size, size_t* written);
MslCoreResult msl_core_batch_restore_match(MslCoreBatch* batch, uint32_t match_index,
                                           const void* buffer, size_t buffer_size);
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

The native runtime surface is declared in `src/api.h`. The Python wrapper uses
the same structs and layout, but this section describes the data model in C
terms.

Core runtime calls:

| call | purpose |
| --- | --- |
| `msl_core_game_data_create(data_root, out)` | load immutable extracted game data |
| `msl_core_batch_create(game_data, count, out)` | create independent matches sharing that data |
| `msl_core_batch_reset_matches(batch, configs, stride, mask, mask_stride)` | reset all or selected matches |
| `msl_core_batch_step_matches(batch, inputs, stride, mask, mask_stride)` | advance all or selected matches one frame |
| `msl_core_batch_write_observation(...)` | write one policy-facing observation per match |
| `msl_core_batch_write_terminal(...)` | write terminal flags |
| `msl_core_batch_copy_matches(...)` | copy arbitrary match states between compatible batches |
| `msl_core_batch_save_match(...)` / `restore_match(...)` | serialize or restore one complete match state |

Data root:

- Direct C callers pass the extracted `data/raw` directory to
  `msl_core_game_data_create()`.
- The Python wrapper accepts either the containing data root or its `raw/`
  directory through `data_dir` or `MSL_DATA_DIR`.
- Game data is explicitly owned and can be shared by compatible batches; it is
  not process-global runtime state.

Timing contract:

- `msl_core_batch_reset_matches()` writes the initial state and clears prior
  controller history.
- Each `msl_core_batch_step_matches()` consumes one current `MslCoreInput` row;
  the match owns the prior input needed by Melee input logic.
- Observation, state, terminal, and viewer projections write the current
  post-step state without advancing it.

Batch functions take a pointer plus a byte stride. This lets callers keep fixed
arrays, struct-of-arrays wrappers, or padded records without runtime allocation.

## Native Input And Match Config

`MslCoreMatchConfig` starts or resets a match:

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `stage_id` | `uint32_t` | `MSL_CORE_STAGE_*` | GALE01/Slippi stage id |
| `frame_id` | `int32_t` | normal match start `-123` | starting frame id |
| `frame_pre_random_seed` | `uint32_t` | any `uint32_t` | starting frame RNG seed |
| `initial_random_seed` | `uint32_t` | any `uint32_t` | seed used during match/stage construction |
| `match_damage_ratio` | `float` | positive | global damage ratio; normally `1.0` |
| `num_players` | `uint8_t` | `2` or `4` | active source players |
| `is_teams` | `uint8_t` | `0` or `1` | nonzero for teams |
| `friendly_fire` | `uint8_t` | `0` or `1` | enable team damage |
| `stock_count` | `uint8_t` | `1..255` | starting stocks; normally `4` |
| `camera_mode` | `uint8_t` | `0` or `1` | `0` normal gameplay camera, `1` free camera |
| capability fields | `uint8_t` | `0` or `1` | Slippi/UCF/stage-stream runtime capabilities |
| `players[4]` | `MslCoreMatchPlayerConfig[4]` | active entries `< num_players` | per-player character/team/facing |

`MslCoreMatchPlayerConfig`:

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `char_id` | `uint8_t` | `MSL_CORE_CHARACTER_*` | supported GALE01/Slippi character id |
| `team_id` | `uint8_t` | `0=red, 1=blue, 2=green` | team assignment |
| `facing_and_port` | `uint8_t` | packed facing and optional port | bit 0 facing; bits 1..3 one-based physical port |
| `costume_id` | `uint8_t` | character costume id | starting costume |
| `handicap` | `uint8_t` | `1..9` | starting handicap; normally `9` |

`MslCoreInput` contains one raw controller row per source player:

| field | type | values / range | native meaning |
| --- | --- | --- | --- |
| `p[4].buttons` | `uint16_t` | OR of `MSL_CORE_BUTTON_*` | packed digital button mask |
| `p[4].main_x`, `main_y` | `int8_t` | `-80..80` | raw main-stick axes |
| `p[4].c_x`, `c_y` | `int8_t` | `-80..80` | raw C-stick axes |
| `p[4].l`, `r` | `uint8_t` | `0..255` | raw analog trigger values |

Button masks:

| constant | value |
| --- | --- |
| `MSL_CORE_BUTTON_A` | `0x0100` |
| `MSL_CORE_BUTTON_B` | `0x0200` |
| `MSL_CORE_BUTTON_X` | `0x0400` |
| `MSL_CORE_BUTTON_Y` | `0x0800` |
| `MSL_CORE_BUTTON_Z` | `0x0010` |
| `MSL_CORE_BUTTON_L` | `0x0040` |
| `MSL_CORE_BUTTON_R` | `0x0020` |
| `MSL_CORE_BUTTON_START` | `0x1000` |
| `MSL_CORE_BUTTON_D_UP` | `0x0008` |
| `MSL_CORE_BUTTON_D_DOWN` | `0x0004` |

## Native Gamestate Output

`MslCoreObservation` is the policy-facing state written by
`msl_core_batch_write_observation()`.

Player slots are viewpoint-relative: `slots[0]` is self, then allies by source
player index, then opponents by source player index. Unused slots have
`present == 0`.

Top-level fields:

| field | type | values / range | shape |
| --- | --- | --- | --- |
| `frame_id` | `int32_t` | match frame id (starts at -123 in pre-match countdown) | scalar |
| `frame_pre_random_seed` | `uint32_t` | any `uint32_t` | scalar |
| `stage_id` | `uint32_t` | `MSL_CORE_STAGE_*` | scalar |
| `num_players` | `uint8_t` | `2` or `4` | scalar |
| `viewpoint_player` | `uint8_t` | `0..num_players-1` | scalar |
| `is_teams` | `uint8_t` | `0` or `1` | scalar |
| `stage` | `MslCoreObservationStage` | stage-owned state | scalar |
| `slots` | `MslCoreObservationPlayer` | viewpoint-relative players | `[4]` |
| `items` | `MslCoreItem` | active/inactive item slots | `[15]` |

`MslCoreObservationPlayer`:

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
| `char_id` | `uint8_t` | `MSL_CORE_CHARACTER_*` |
| `stocks` | `uint8_t` | remaining stocks |
| `facing` | `uint8_t` | `0` left, `1` right |
| `on_ground` | `uint8_t` | `0` or `1` |
| `jumps_left` | `uint8_t` | remaining air jumps |
| `hurtbox_state` | `uint8_t` | GALE01 hurtbox state id |
| `invulnerable` | `uint8_t` | `0` or `1` |

`MslCoreItem` slots are fixed-capacity. Inactive slots have `exists == 0`.

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

`MslCoreObservationStage` exposes the policy-relevant moving platform state:

| field | type | values / range |
| --- | --- | --- |
| `randall.exists` | `uint8_t` | `0` or `1` |
| `randall.x`, `randall.y` | `float` | world coordinates |
| `fod_platforms.left`, `fod_platforms.right` | `float` | Fountain of Dreams platform heights |

## Terminal Output

`MslCoreTerminal` is written by `msl_core_batch_write_terminal()`:

| field | type | values / range | meaning |
| --- | --- | --- | --- |
| `frame_id` | `int32_t` | current frame id | current frame |
| `stage_id` | `uint32_t` | `MSL_CORE_STAGE_*` | current stage |
| `done` | `uint8_t` | `0` or `1` | any terminal condition |
| `match_ended` | `uint8_t` | `0` or `1` | in-game match end |
| `stockout` | `uint8_t` | `0` or `1` | player/team out of stocks |
| `max_frame_reached` | `uint8_t` | `0` or `1` | caller-specified frame cutoff |

## Replay Validation Helper

For quick triage of a single Slippi replay, `tools.validation.validate_replay` loads `.slp` or
`.slpz` input once through Peppi and performs the per-frame rollout and comparison in C. It prints
the strict result or locked classification to stdout without changing committed reports:

```bash
uv run python -m tools.validation.validate_replay /path/to/Game.slp --backend native
```

Use `--frames N` for a prefix or `--suite replays/suites/melee_core_aggregate.json` for a suite.
The active source ownership, correctness contract, build layout, and retained performance evidence
are documented in [src/README.md](src/README.md).
