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

## Structured Dtypes

`melee_sim` exposes NumPy dtype helpers for the packed native buffers:

- `input_dtype()`
- `controller_input_dtype()`
- `item_dtype()`
- `match_config_dtype()`
- `compare_dtype()`
- `seed_dtype()`
- `gamestate_dtype()`
- `gamestate_player_dtype()`
- `gamestate_randall_dtype()`
- `gamestate_stage_dtype()`
- `terminal_dtype()`

Raw allocation helper:

- `raw_buffer(batch_size, kind)`

## Performance Contract

The hot path is a two-phase binding model:

1. Allocate NumPy buffers.
2. Bind them to `EnvBatch`.
3. Step with `env.step()`.

The native binding validates dtype, shape, and stride at bind time. Per-frame
stepping performs pointer offsets into already-bound arrays and does not
allocate Python or C gameplay memory.

The simulator does not own multiprocessing, worker pools, learner queues, reward
logic, or trajectory postprocessing. Downstream training code owns those layers
around `EnvBatch` and `Buffers`.

## Package Shape

```text
melee_sim/
  __init__.py
  env_batch.py
  buffers.py
  controller.py
  dtypes.py
  _native.so
```

`melee_sim._native` is private. Public code imports `melee_sim`.
