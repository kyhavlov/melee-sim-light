# Viewer track

Visual-only state a replay viewer draws and a recording does not carry: what
the game's effects, colors, stage parts and camera were doing. Gameplay never
reads it. A viewer outside this repository wraps the core, turns the track on,
and draws from it; the core only reports.

## The switches

Two, as the replay arithmetic modes have (`MSL_REPLAY_ARITHMETIC`):

- the build: `make ... MSL_VISUAL_TRACK_BUILD=1` compiles the track. The
  default, 0, compiles none of it: no hook in game code, no thread-local, no
  field in the match, no viewer API; such a core is upstream main's and
  refuses a config that asks for a track. Viewers, validation (`make test`,
  `validate_replay`'s build) and CI build with 1.
- the match: `MSL_VISUAL_TRACK`, bit 7 of `MslCoreMatchConfig.camera_mode`
  (runtime/wire.h; the byte carries presentation requests and gameplay never
  reads it), asks a visual build for this match's track. The config stays 64
  bytes. Every public path (the batch API, validation, benchmarks) leaves the
  bit clear.

In a visual build with the bit clear nothing is allocated, loaded or written;
each hook is one untaken branch on a thread-local pointer bound with the
match, beside the others there.

With the bit set, `msl_core_match_init` gives the match a track (heap memory
outside the match arena, freed with the match) and every step ends by
appending the frame's lines to it. `msl_core_match_viewer_track(match, &size)`
returns the lines written since the last call. The track reads simulation
state; it does not change gameplay (the locks below check the strict gameplay
pass and the same output fingerprint with the track off).

Limits: one track per match. A savestate restore or match copy keeps the
destination's own track (or none); a track is not part of a savestate. The
batch API has no way to turn it on. The validation runner writes it only in
its one-shot stream mode.

## The viewer API

A standalone viewer wraps the shared library through `api.h`'s
`msl_viewer_*` functions, which replay one recording the way the validation
runner does (`runtime/main.c --stream`) and keep the track on:

- `msl_viewer_create(data_root, config, config_size, previous_input,
  input_size, &viewer)` takes the runner's header rows (the
  `MslCoreMatchConfig` wire row and the `MslCoreInput` before the first
  frame), sets `MSL_VISUAL_TRACK`, loads the viewer's own game data and
  initializes the match;
- `msl_viewer_step(viewer, frame, frame_size)` steps one `MslCoreStreamFrame`
  wire row (the frame's seed, inputs and stage events);
- `msl_viewer_track(viewer, &size)` returns the lines written since the last
  step (after create: the match's setup);
- `msl_viewer_output(viewer, &size)` returns the step's `MslCoreCompare`, the
  row the runner writes;
- `msl_viewer_destroy(viewer)` frees the match and its game data.

## Lines

One JSON array per line: `[frame, kind, values...]`. `frame` is the game's
frame number (Slippi's: -123 is the first). A line written during a step
carries the frame that step produces; one written while the match is built
carries the first frame.

Two sorts of kind:

- **States** are written on the first frame they have a value and whenever it
  changes. The identity fields after the kind (a port, a slot) and the kind
  make the key; a viewer keeps the last value per key.
- **Events** are written when they happen, in the order the game did them.

Within a frame the step's events come first, in the game's order, then what
is read at the frame's end: each fighter's states, then the stage's, the
items', and the last rows of the table. Whole numbers (ids, counts, colors,
seeds) are JSON numbers. Every float is its IEEE single's bits as a JSON
string, `"0x"` and eight hex digits (`"0x3f800000"` is 1.0): no decimal
rounding, so two hosts or C libraries write the same line, and a viewer gets
the game's value back exactly (`Float32Array` over the word). `port` is the
fighter's port (0-3) and `sub` 0 for the leader, 1 for Nana.

| Kind | Sort | Values | Source |
|---|---|---|---|
| `tint` | state | port, sub, slot (0-2), index, rgba, flags, light rgba, light x, light yz; or index 0 when the overlay stops | `Fighter.x408/x488/x508` color overlays |
| `charge` | state | port, sub, field, value | stored charges: `dk`, `ss`, `mt`, `sk`, `gw`, Kirby's `kb.kind`, `kb.dk`, `kb.ss` |
| `part` | state | port, sub, model part, variant | `Fighter.x5F4_arr[i].idx` |
| `anim` | state | port, sub, animation index, source kind, frame, rate, blend frames, blend frame | the animation the game plays, and whose table it is |
| `pos` | state | port, sub, x, y, z, rot x, y, z, scale | the top joint, while in DeadUp states (4-10) or off the plane |
| `shake` | state | port, sub, x, y, z (0 when it stops) | the draw offset `ftCommon_8008021C` sums: hit shake, the held-mash wobble, the smash-charge shake |
| `hide` | state | port, sub, bits (1 invisible, 2 `x221E_b5`, 4 `x2226_b5`) | written once something is hidden |
| `tex` | event | port, sub, slot, index (slot -1: every slot back) | switchable textures, eyes and mouth (`ftAnim_800704F0`, `ftAnim_80070654`) |

## Validation

`replays/suites/visual_state.json` lists recordings whose viewer track is
known to be the game's: their effects matched the game's own particle code
frame for frame and their poses were checked against the game. For each,
`replays/suites/visual_state_locks.json` stores digests of its track
(blake2b-64): all lines, each kind's lines, and each minute's lines.
`tests/test_visual_state_locks.py` replays each with the track on through the
validation runner (`validate_replay(..., visual_track=True)`, a visual build),
requires the strict gameplay pass and the same output fingerprint as with the
track off, and compares the digests. `python -m tools.validation.visual_state`
checks them; `--write` records them.

The locks are recorded on the Linux reference build, like the gameplay
output locks: `python -m tools.validation.visual_state --write`.

### Unlocked kinds

A kind is locked only once a recording proves it: its lines matched the
game's frame for frame. A kind the track writes that no recording of the suite
proves yet is unlocked: `visual_state.UNLOCKED_KINDS` leaves its lines out of
every digest (the whole track's, each kind's, each minute's), so a change to
it fails nothing.
