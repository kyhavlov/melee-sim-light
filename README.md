# melee-sim-light

[![melee-core validation](https://github.com/kyhavlov/melee-sim-light/actions/workflows/validation.yml/badge.svg?branch=main)](https://github.com/kyhavlov/melee-sim-light/actions/workflows/validation.yml)

`melee-sim-light` is a Melee gameplay simulator made for reinforcement learning.
It can run many independent matches at once, with exact equivalence to Melee's
engine. The simulator is written in C, built from the
[SSBM Decompilation](https://github.com/doldecomp/melee) project's code, and
has a Python API through `melee_sim` and NumPy.

It supports the full playable roster and all six competitive stages, in singles
and teams matches.

Gameplay is intended to match Melee with Slippi's gameplay changes. The remaining
known exception is offscreen magnifier damage: its 1% damage ticks can be off by
one frame in some situations (compared to vanilla Melee). This is because Melee
ties this damage to rendering; the simulator checks visibility once per frame.

Exact Slippi replay reproduction can also differ because of historical rounding
differences, missing raw inputs, and uninitialized recorded bytes. These are
replay limitations rather than missing gameplay mechanics.

Benchmarks ([details](agent_docs/performance/BASELINE.md)):

| CPU | Batch size | Per-core FPS |
| ---: | ---: | ---: |
| AMD Ryzen 9 9950X3D | 256 | 124,142 |
| AMD Ryzen 9 9950X3D | 512 | 120,607 |

## Quick Start

You'll need Python 3.11 or newer, GCC, GNU Make, and binutils.

Install directly from GitHub in your Python project:

```bash
uv add "melee-sim-light @ git+https://github.com/kyhavlov/melee-sim-light.git"
```

Or with pip:

```bash
pip install "git+https://github.com/kyhavlov/melee-sim-light.git"
```

Before running the simulator, extract the game data from a Melee NTSC 1.02
(GALE01 revision 2) ISO:

```bash
uv run python -m melee_sim.extract_data --iso /path/to/SSBM.iso
```

If you installed with pip, use `python` in place of `uv run python`.

This creates a `data/` directory in your current directory, which the simulator
loads by default. To store the data elsewhere, add `--out-dir /path/to/data` when
extracting, then pass `data_dir` to `EnvBatch` or set `MSL_DATA_DIR`:

```bash
MSL_DATA_DIR=/path/to/data uv run python train.py
```

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

    env.reset_all()
    # Wait through start-of-match countdown, since the game begins on frame -123
    while env.current_frame[0]["frame_id"] < 0:
        env.step()
    env.reset_cursor()

    fox = msl.neutral_controller((env.length, env.batch_size))
    fox.buttons.B[0] = True
    fox.main_stick.x[0] = 1.0
    msl.write_controller(env.controller_action_view, fox, player=0)

    for _ in range(env.length):
        env.step()

    state = env.current_frame[0]
    print(state["frame_id"], state["slots"][0]["pos_x"])
```

Controller buffers start neutral. Each `step()` writes the next observation row.
`reset_cursor()` moves the cursor back to the start of the buffer to reuse it.

See [Python API operations](API.md#python-api) for configuration, reset, and save/restore calls.

For multiple workers, see [sharing game data across processes](API.md#sharing-game-data-across-worker-processes).

### C Native API

`MslBatch` owns the mutable environments and shares immutable game data within its process.
Reset and step consume contiguous arrays whose length is the batch size; the simulator
allocates nothing on those paths.

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

See the [runtime API reference](API.md) for native calls, match configuration,
controller inputs, game state, and terminal outputs.
