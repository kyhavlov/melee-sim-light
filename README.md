# melee-sim-light

`melee-sim-light` is a deterministic, batched SSBM-like simulator for
high-throughput RL training and replay-driven validation.

The simulator core is C. The public Python package is `melee_sim`, with a thin
NumPy API over native batch execution.

## Install And Data

Install the package with `uv` from the repository root:

```bash
uv add "melee-sim-light @ file:///path/to/melee-sim-light"
```

Extract simulator data from a valid SSBM ISO:

```bash
uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso
```

The extraction command writes `.msl/` in the current project and skips
already-extracted source DATs on later runs. `.msl/` is disposable generated
data plus a DAT cache; delete it to regenerate from the ISO. `EnvBatch()` loads
`.msl/` by default. To use another location:

```bash
MELEE_SIM_DATA=/path/to/data uv run python train.py
```

or:

```python
env = msl.EnvBatch(batch_size=1024, data_dir="/path/to/data")
```

## Python API

The main Python API is `EnvBatch` plus `Buffers`. `length` is the number of
simulated frames in the reusable buffer chunk; `gamestate` stores one extra
frame so initial state and every post-step state are both available.

```python
import melee_sim as msl

# Two envs, 2 players, and a reusable 128-step buffer chunk per env.
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

    neutral = msl.neutral_controller((env.length, env.batch_size))
    msl.write_controller(buffers.controller_action_view, neutral, player=0)
    msl.write_controller(buffers.controller_action_view, neutral, player=1)

    env.bind(buffers)
    env.reset_all()
    env.step()

    next_frame = buffers.gamestate_view[1]
    print(next_frame["frame_id"])

    # After consuming buffers[:128], reuse the arrays for the next chunk.
    env.reset_cursor()
```

By default, `EnvBatch` loads extracted game data from `.msl/` in the current
working directory. `EnvBatch(..., data_dir=...)` overrides that path, and
`MELEE_SIM_DATA` sets the process default. Source checkouts also fall back to
`data/` when `.msl/` is absent. The resolved data directory is treated as
process-global native runtime state.

`Buffers` is the canonical batched simulation layout:

- `match_config`: initial match state for each batch lane
- `action`: controller or raw input for each simulated frame
- `gamestate`: structured native game state for frames `0..length`
- `terminal`: terminal flags for each simulated frame
- `reward`: caller-owned reward buffer
- `done`: simulator-owned done flags
- `reset_mask`: caller-owned reset commands
- `obs`: caller-owned flat policy observation buffer

`env.reset_all()` writes `gamestate[0]` and sets `env.t = 0`. `env.step()`
consumes `action[env.t]`, advances one frame, writes `gamestate[env.t + 1]`,
writes `terminal[env.t]` and `done[env.t]`, then increments `env.t`.

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
