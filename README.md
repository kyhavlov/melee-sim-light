# melee-sim-light

`melee-sim-light` is a deterministic, batched SSBM-like simulator for
high-throughput RL training and replay-driven validation.

The simulator core is C. The public Python package is `melee_sim`, with a thin
NumPy API over native batch execution.

## Quick Start

From a source checkout, install dependencies and build the native extension:

Prerequisites:

- `uv`
- Python 3.10 or newer
- `make`
- a C compiler available as `cc`

```bash
uv sync --dev
make build
```

From another project, install the local checkout with `uv`:

```bash
uv add "melee-sim-light @ file:///path/to/melee-sim-light"
```

The simulator needs extracted game data before it can create an `EnvBatch`.
Extract it from a valid SSBM ISO:

```bash
uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso
```

The extraction command writes `.msl/` in the directory where the command is run
and can be run again in place when data needs to be refreshed. `EnvBatch()`
loads `.msl/` by default. To use another data root, set `MSL_DATA_DIR` or
pass `data_dir`:

```bash
MSL_DATA_DIR=/path/to/data uv run python train.py
```

```python
env = msl.EnvBatch(batch_size=1024, data_dir="/path/to/data")
```

Source checkouts also fall back to `data/` when `.msl/` is absent. The resolved
data directory is process-global native runtime state, so choose it before
creating simulator batches.

## Minimal Step Loop

### Python

The public API is the `melee_sim` Python package. Most callers use `EnvBatch`
and a reusable `Buffers` object:

```python
import melee_sim as msl

with msl.EnvBatch(batch_size=2, length=128, num_players=2) as env:
    buffers = env.buffers()

    env.configure_match(
        buffers,
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
    msl.write_controller(buffers.controller_action_view, fox, player=0)

    neutral = msl.neutral_controller((env.length, env.batch_size))
    msl.write_controller(buffers.controller_action_view, neutral, player=1)

    env.bind(buffers)
    env.reset_all()
    env.step()

    # gamestate has one extra frame: row 0 is the reset state, row 1 is after
    # the first step.
    frame_1 = buffers.gamestate_view[1, 0]
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
    ucf_enabled: bool = True,
    ucf_cardinals_1_0_enabled: bool = False,
) -> None

env.buffers(*, observation: str = "native", action_format: str = "controller", obs_dim: int = 0) -> Buffers
env.configure_match(buffers: Buffers, config: MatchConfig | None = None, **overrides) -> None
env.bind(buffers: Buffers) -> None
env.reset_all() -> None
env.step(*, write_outputs: bool = True, write_compare: bool = False, max_frame_id: int = -1) -> None
env.reset_cursor() -> None

PlayerConfig(character: int | Character, team_id: int | None = None, facing: int | None = None)
MatchConfig(stage: int | Stage = Stage.FINAL_DESTINATION, players: tuple[PlayerConfig, ...] | None = None, ...)
```

### C

The C API in `src/api.h` exposes the same lower-level batch contract. Native integrations
can drive the core directly. Direct C callers should set `MSL_DATA_DIR` before creating a
batch when they want to load extracted data from `.msl` or another non-default directory:

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/api.h"

int main(void) {
  enum { BATCH = 2, PLAYERS = 2 };

  MslBatch* batch = msl_batch_create(BATCH, PLAYERS);
  if (batch == NULL) {
    return 1;
  }

  MslMatchConfig configs[BATCH];
  memset(configs, 0, sizeof(configs));
  for (int i = 0; i < BATCH; i++) {
    configs[i].stage_id = MSL_STAGE_ID_FINAL_DESTINATION;
    configs[i].frame_id = -123;
    configs[i].players[0].char_id = MSL_CHAR_ID_FOX;
    configs[i].players[1].char_id = MSL_CHAR_ID_FALCO;
  }

  if (msl_batch_init_match(batch, (const uint8_t*)configs, sizeof(configs[0])) != 0) {
    msl_batch_destroy(batch);
    return 1;
  }

  MslInput prev[BATCH];
  MslInput input[BATCH];
  memset(prev, 0, sizeof(prev));
  memset(input, 0, sizeof(input));

  if (msl_batch_step_input(batch, (const uint8_t*)prev, sizeof(prev[0]),
                           (const uint8_t*)input, sizeof(input[0])) != 0) {
    msl_batch_destroy(batch);
    return 1;
  }

  uint8_t viewpoint[BATCH] = {0};
  MeleeGamestate out[BATCH];
  if (melee_batch_write_gamestate(batch, viewpoint, sizeof(viewpoint[0]), (uint8_t*)out,
                                  sizeof(out[0])) != 0) {
    msl_batch_destroy(batch);
    return 1;
  }

  printf("frame=%d p0_action=%u p0_x=%f\n", out[0].frame_id,
         (unsigned)out[0].slots[0].action_id, out[0].slots[0].pos_x);

  msl_batch_destroy(batch);
  return 0;
}
```

Useful C signatures:

```c
MslBatch* msl_batch_create(int batch_size, int num_players);
void msl_batch_destroy(MslBatch* batch);

int msl_batch_init_match(MslBatch* batch, const uint8_t* config_bytes,
                         size_t config_stride_bytes);
int msl_batch_init_match_masked(MslBatch* batch, const uint8_t* config_bytes,
                                size_t config_stride_bytes, const uint8_t* mask_bytes,
                                size_t mask_stride_bytes);

int msl_batch_step_input(MslBatch* batch, const uint8_t* prev_input_bytes,
                         size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                         size_t input_stride_bytes);
int melee_batch_write_gamestate(const MslBatch* batch, const uint8_t* viewpoint_player_bytes,
                                size_t viewpoint_player_stride_bytes, uint8_t* out_bytes,
                                size_t out_stride_bytes);
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
- extracted simulator data; defaults to `.msl/` when present, or set
  `MSL_DATA_DIR`
- network access on the first build to download character display assets

```bash
npm --prefix tools/viewer/slippi-viewer install
make viewer-build
make viewer
```

Open `http://127.0.0.1:8001/tools/viewer/`.

Modelplay and the live viewer both write `*.msltrace.json`. The trace format is
documented in `tools/viewer/TRACE_FORMAT.md`; it is compact JSON with
sparse-delta frame, input, and item streams. `make viewer-build` downloads the
display assets into `build/cache/viewer-zips/` on first use and
verifies them by checksum on later builds.

## Public API Contract

The public runtime surface is the `melee_sim` Python package. The native C core
and binding are implementation details; callers should treat `EnvBatch`,
`Buffers`, config dataclasses, controller helpers, and dtype helpers as the
stable API.

`EnvBatch` owns native simulator state:

- `batch_size`: number of independent match lanes advanced together
- `length`: number of step frames in one reusable buffer chunk
- `num_players`: usually `2`; `4` is supported for doubles coverage
- `data_dir`: optional extracted data root, equivalent to setting
  `MSL_DATA_DIR` before construction

The resolved data root is process-global native state. Choose it before
creating simulator batches, and do not expect two `EnvBatch` instances in the
same process to use different data roots safely.

`Buffers` owns the preallocated NumPy arrays passed to native code. Runtime
stepping does not allocate replacement buffers, so callers write into these
arrays and reuse them across chunks:

- `match_config`: initial match state for each batch lane
- `action`: controller or raw input for each simulated frame
- `gamestate`: structured native game state for frames `0..length`
- `terminal`: terminal flags for each simulated frame
- `reward`: caller-owned reward buffer
- `done`: simulator-owned done flags
- `reset_mask`: caller-owned reset commands
- `obs`: caller-owned flat policy observation buffer

`action_format="controller"` is the default and accepts normalized controller
values. `action_format="raw"` exposes packed native inputs for replay tooling
and benchmarks. Both formats share the same stepping API.

`gamestate` is the main simulator output. It has shape
`(length + 1, batch_size)`: row `0` is the reset/initial state, and row `t + 1`
is the result of stepping action row `t`. Player slots are viewpoint-relative:
self first, then allies by source player index, then opponents by source player
index.

The normal call order is:

1. Create `env = msl.EnvBatch(...)`.
2. Create `buffers = env.buffers(...)`.
3. Write match config with `env.configure_match(...)` or
   `env.configure_matches(...)`.
4. Write controller actions into `buffers.controller_action_view`.
5. Call `env.bind(buffers)`.
6. Call `env.reset_all()` to write `gamestate[0]`.
7. Call `env.step()` repeatedly.

`env.step()` consumes `action[env.t]`, advances one frame, writes
`gamestate[env.t + 1]`, writes `terminal[env.t]` and `done[env.t]`, then
increments `env.t`.

`env.reset_all()` resets every lane and writes `gamestate[0]`. `reset_mask` is
the per-frame caller input for masked resets; `done` is simulator output and
does not reset lanes by itself.

After consuming a full chunk, `env.reset_cursor()` sets `env.t = 0` so the same
arrays can be reused with new actions.

## Actions

The default action format is `controller`, matching the primitive controller
shape used by slippi-ai:

- `buttons.A/B/X/Y/Z/L/R/D_UP`: `uint8` booleans
- `main_stick_x/main_stick_y`: `float32` in `[0, 1]`
- `c_stick_x/c_stick_y`: `float32` in `[0, 1]`
- `shoulder`: `float32` in `[0, 1]`

`env.step()` converts this controller view to native input inside the binding
before stepping. `Buffers.empty(..., action_format="raw")` keeps
raw packed input storage for replay tooling and benchmark parity.

## Gamestate

`buffers.gamestate_view` is a structured NumPy view over the native gamestate
buffer. It contains:

- match fields: `frame_id`, `frame_pre_random_seed`, `stage_id`, `num_players`,
  `viewpoint_player`, `is_teams`
- `slots[4]`: viewpoint-relative player slots with position, speeds, percent,
  shield, action, hitlag, hitstun, character, stocks, facing, ground state,
  jumps, `hurtbox_state`, and `invulnerable`
- `items[15]`: fixed item slots with existence, type/state, owner, instance,
  attack identity, position, velocity, damage, timer, spawn id, and misc bytes
- `stage.randall`: `exists`, `x`, and `y`

Player slots are ordered from the selected viewpoint: self first, then allies by
source player index, then opponents by source player index. Unused slots have
`present == 0`.

## Resets

`done[env.t]` is simulator output. `reset_mask[env.t]` is caller input.

```python
buffers.reset_mask[env.t, env_ids] = 1
env.reset_masked()
```

Resetting clears the previous-controller-input state for those lanes. Benchmark
and replay-style runs that need exact parity with a preexisting input stream can
explicitly seed previous input from an action frame:

```python
env.set_previous_input(0)
```

To change match setup for specific lanes, write new configs before a masked
reset:

```python
env.configure_matches(
    buffers,
    [
        msl.MatchConfig(players=(
            msl.PlayerConfig(character=msl.Character.FALCO),
            msl.PlayerConfig(character=msl.Character.FOX),
        )),
    ],
    env_ids=[3],
)
buffers.reset_mask[env.t, 3] = 1
env.reset_masked()
```

## Replay Validation

For quick triage of a single Slippi replay, `tools.eval.validate_replay`
builds a temporary `.msl` dataset and prints one-step or rollout results to
stdout without updating the committed validation reports:

```bash
uv run python -m tools.eval.validate_replay \
  --replay /path/to/Game.slp \
  --mode rollout
```

Use `--mode one-step` for direct seeded one-step validation, or `--mode both`
to run both views. By default the tool selects the human player ports from the
replay; pass `--ports 1,2` to choose ports explicitly.

## Project Notes

Long-lived implementation notes live in `agent_docs/`. Start with
`agent_docs/README.md` for the active docs index and archived cleanup candidates.

## Observation Schema

`buffers.gamestate_view` is a structured NumPy view with shape
`(length + 1, batch_size)` and dtype `msl.gamestate_dtype()`.

```python
row = buffers.gamestate_view[env.t, 0]
self_slot = row["slots"][0]
first_item = row["items"][0]
randall = row["stage"]["randall"]

print(row["frame_id"])
print(self_slot["pos_x"], self_slot["pos_y"])
print(first_item["exists"], first_item["type"])
print(randall["exists"], randall["x"], randall["y"])
```

Top-level gamestate fields:

| field | dtype | shape |
| --- | --- | --- |
| `frame_id` | `int32` | scalar |
| `frame_pre_random_seed` | `uint32` | scalar |
| `stage_id` | `uint32` | scalar |
| `num_players` | `uint8` | scalar |
| `viewpoint_player` | `uint8` | scalar |
| `is_teams` | `uint8` | scalar |
| `stage` | `gamestate_stage_dtype()` | scalar |
| `slots` | `gamestate_player_dtype()` | `(4,)` |
| `items` | `item_dtype()` | `(15,)` |

`slots[4]` is viewpoint-relative: self first, then allies by source player
index, then opponents by source player index. Unused slots have `present == 0`.

| `slots` field | dtype |
| --- | --- |
| `present` | `uint8` |
| `source_player` | `uint8` |
| `team_relation` | `uint8` |
| `team_id` | `uint8` |
| `pos_x`, `pos_y` | `float32` |
| `speed_air_x_self`, `speed_ground_x_self`, `speed_y_self` | `float32` |
| `speed_x_attack`, `speed_y_attack` | `float32` |
| `percent`, `shield_hp` | `float32` |
| `action_id` | `uint16` |
| `action_frame` | `int16` |
| `hitlag`, `hitstun` | `uint16` |
| `char_id`, `stocks`, `facing`, `on_ground` | `uint8` |
| `jumps_left`, `hurtbox_state`, `invulnerable` | `uint8` |

`items[15]` is a fixed item slot array. Inactive item slots have `exists == 0`.

| `items` field | dtype |
| --- | --- |
| `exists`, `state` | `uint8` |
| `type` | `uint16` |
| `owner` | `int8` |
| `instance_id`, `attack_id`, `attack_instance` | `uint16` |
| `direction` | `float32` |
| `vel_x`, `vel_y` | `float32` |
| `pos_x`, `pos_y` | `float32` |
| `damage` | `uint16` |
| `timer` | `float32` |
| `spawn_id` | `uint32` |
| `misc0`, `misc1`, `misc2`, `misc3` | `uint8` |

`stage.randall` is populated on Yoshi's Story:

| `stage.randall` field | dtype |
| --- | --- |
| `exists` | `uint8` |
| `x`, `y` | `float32` |

## Controller Action Schema

The default action format is `controller`. `buffers.controller_action_view` has
shape `(length, batch_size)` and dtype `msl.controller_input_dtype()`.

```python
import numpy as np
import melee_sim as msl

controller = msl.neutral_controller((env.length, env.batch_size))

# Hold right on the main stick for every frame and env.
controller = controller._replace(
    main_stick=msl.Stick(
        x=np.full((env.length, env.batch_size), 0.8, dtype=np.float32),
        y=np.full((env.length, env.batch_size), 0.5, dtype=np.float32),
    ),
)

# Press A on frames 10..14.
controller.buttons.A[10:15, :] = True

msl.write_controller(buffers.controller_action_view, controller, player=0)
```

Per-player controller fields:

| field | dtype | accepted range |
| --- | --- | --- |
| `buttons.A` | `uint8` bool | `0` or `1` |
| `buttons.B` | `uint8` bool | `0` or `1` |
| `buttons.X` | `uint8` bool | `0` or `1` |
| `buttons.Y` | `uint8` bool | `0` or `1` |
| `buttons.Z` | `uint8` bool | `0` or `1` |
| `buttons.L` | `uint8` bool | `0` or `1` |
| `buttons.R` | `uint8` bool | `0` or `1` |
| `buttons.D_UP` | `uint8` bool | `0` or `1` |
| `main_stick_x`, `main_stick_y` | `float32` | `[0, 1]` |
| `c_stick_x`, `c_stick_y` | `float32` | `[0, 1]` |
| `shoulder` | `float32` | `[0, 1]` |

Stick values are normalized controller coordinates. `0.5` is neutral, `0.0`
is minimum, and `1.0` is maximum. `write_controller()` broadcasts any NumPy
shape compatible with the target action view.

## Raw Action Schema

Replay tooling and benchmarks can request packed native inputs instead:

```python
buffers = msl.Buffers.empty(length=128, batch_size=32, action_format="raw")
raw = buffers.raw_action_view

raw["p"][:, :, 0]["buttons"] = msl.BUTTON_A
raw["p"][:, :, 0]["main_x"] = 80
raw["p"][:, :, 0]["main_y"] = 0
```

Per-player raw input fields:

| field | dtype | native meaning |
| --- | --- | --- |
| `buttons` | `uint16` | packed button mask |
| `main_x`, `main_y` | `int8` | raw main-stick axes, usually `[-80, 80]` |
| `c_x`, `c_y` | `int8` | raw C-stick axes, usually `[-80, 80]` |
| `l`, `r` | `uint8` | raw trigger values |

Button masks exported by `melee_sim`:

| constant | value |
| --- | --- |
| `BUTTON_A` | `0x0100` |
| `BUTTON_B` | `0x0200` |
| `BUTTON_X` | `0x0400` |
| `BUTTON_Y` | `0x0800` |
| `BUTTON_Z` | `0x0010` |
| `BUTTON_L` | `0x0040` |
| `BUTTON_R` | `0x0020` |
| `BUTTON_D_UP` | `0x0008` |
