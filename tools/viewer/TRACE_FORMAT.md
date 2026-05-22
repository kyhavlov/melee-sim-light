# MSL Trace Format

`MSLTRACE1` is the stable trace format for melee-sim-light viewer tooling. It is
not model-specific, live-viewer-specific, or tied to the current slippi-viewer frame
object shape. Model runners, live viewer export, and downstream users should be able to
write this format and load it in the viewer.

The v1 encoding is compact JSON. Generated traces should not be pretty-printed.
State streams use sparse changed-field rows with periodic keyframes so files stay
small without making random access depend on replaying an entire match from the
beginning.

## Contract

Top-level payload:

```json
{"format":"MSLTRACE1","schemaVersion":1,"producer":{"name":"melee-sim-light","version":null},"createdAt":"2026-05-21T00:00:00Z","match":{},"inputs":{},"frames":{},"stage":{},"items":{},"metadata":{}}
```

Required top-level fields:

- `format`: exactly `"MSLTRACE1"`.
- `schemaVersion`: integer schema version. This document defines version `1`.
- `match`: match setup needed to interpret frame rows.
- `frames`: per-frame fighter state needed for viewing and debugging.

Optional top-level fields:

- `producer`: source that wrote the trace.
- `createdAt`: ISO-8601 timestamp.
- `inputs`: per-player controller input streams.
- `stage`: sparse stage-object state used when the sim has a canonical runtime value.
- `items`: sparse item state stream.
- `metadata`: producer-owned extra data. The viewer must ignore unknown metadata.

Unknown fields may appear at any object level. Readers must ignore fields they do
not understand.

## Match

```json
{"stageId":32,"numPlayers":2,"isTeams":false,"players":[{"port":1,"charId":1,"teamId":0},{"port":2,"charId":22,"teamId":1}],"startFrame":0}
```

- `stageId`: public MSL stage id. Use the constants in `src/ids.h`.
- `numPlayers`: number of active players in the trace.
- `isTeams`: whether team display/ownership is active.
- `players`: active players in row order. Frame player arrays use this order.
- `players[].port`: one-based controller port.
- `players[].charId`: public MSL character id. Use the constants in `src/ids.h`.
- `players[].teamId`: integer team id when teams are active. Use `0` for red,
  `1` for blue, and `2` for green when a team value is needed.
- `startFrame`: first trace frame number. For sim-init traces this is normally
  `0`.

## Sparse Rows

Changed fields are encoded as `[fieldIndex, value]`, where `fieldIndex` indexes
the stream's field list. A keyframe stores a full ordered row. A delta stores
only changed fields relative to the previous decoded row in the same stream.

Frame and input streams use these operation ids:

- `0`: keyframe.
- `1`: delta.

Writers should emit a keyframe at frame `startFrame` and then at a bounded
interval. `keyframeInterval` records that intended interval. Readers must not
assume every interval is exact; early keyframes are allowed.

## Inputs

Inputs are optional. They are useful for reproduction and debugging, but a trace
with only frame state must still be viewable.

```json
{"encoding":"sparse-delta-v1","keyframeInterval":60,"fields":["buttons","mainX","mainY","cX","cY","l","r"],"players":[[[0,0,[0,0,0,0,0,0,0]],[1,8,[[0,256],[1,0.7]]]],[[0,0,[0,0,0,0,0,0,0]]]]}
```

- `encoding`: exactly `"sparse-delta-v1"`.
- `keyframeInterval`: intended maximum frame distance between input keyframes.
- `fields`: names for each scalar in an input row.
- `players`: one input stream per `match.players` entry.
- Keyframe row shape: `[0, frame, fullInputRow]`.
- Delta row shape: `[1, frame, changes]`.
- Stick and trigger analog values are normalized floats in `[-1, 1]` for sticks
  and `[0, 1]` for triggers.
- `buttons` uses the public MSL button bitmask from `src/buttons.h`.

The v1 input field order is:

```text
buttons, mainX, mainY, cX, cY, l, r
```

Frames without an input row carry the previous decoded input for that player.

## Frames

Frame rows are the primary trace data. Player rows are ordered to match
`match.players`.

```json
{"encoding":"sparse-delta-v1","keyframeInterval":60,"fields":["frame","randomSeed","players"],"playerFields":["charId","actionId","actionFrame","x","y","facing","grounded","percent","shield","stocks","jumps","hitlag","hitstun","hurtbox","reflect","fastfall","shielding","inHitstun","powershield","dead"],"rows":[[0,0,12345,[[1,14,0,-30,0,1,1,0,60,4,2,0,0,0,0,0,0,0,0,0],[22,14,0,30,0,-1,1,0,60,4,2,0,0,0,0,0,0,0,0,0]]],[1,1,null,[[[2,1]],[[2,1]]]]]}
```

- `encoding`: exactly `"sparse-delta-v1"`.
- `keyframeInterval`: intended maximum frame distance between frame keyframes.
- `fields`: frame row fields. V1 row shapes are fixed below.
- `playerFields`: names for each scalar in a player row.
- Keyframe row shape: `[0, frame, randomSeed, players]`.
- Delta row shape: `[1, frame, randomSeed, playerChanges]`.
- `randomSeed`: sim RNG seed at the start of the frame, or `null` when omitted
  or unchanged.
- `players` in a keyframe: full player rows in `match.players` order.
- `playerChanges` in a delta: one changed-field list per player in
  `match.players` order.

Required player fields:

- `charId`: public MSL character id.
- `actionId`: internal action state id.
- `actionFrame`: action frame counter.
- `x`, `y`: world position.
- `facing`: `-1` for left, `1` for right.
- `grounded`: `1` when grounded, `0` otherwise.
- `percent`: current damage percent.
- `stocks`: stocks remaining.

Optional standard player fields:

- `shield`: shield size.
- `jumps`: jumps remaining.
- `hitlag`: hitlag frames remaining.
- `hitstun`: hitstun frames remaining.
- `hurtbox`: `0` vulnerable, `1` invulnerable, `2` intangible.
- `reflect`: `1` while reflect is active.
- `fastfall`: `1` while fastfalling.
- `shielding`: `1` while shield is active.
- `inHitstun`: `1` while in hitstun.
- `powershield`: `1` while powershield is active.
- `dead`: `1` when dead.

Additional player columns may be appended by adding names to `playerFields`.
Readers must use the field list instead of hard-coded positions outside the v1
required fields.

## Stage

Stage rows carry dynamic stage objects that should render from simulator-owned
state rather than viewer-side approximations. Missing `stage` data is allowed.

```json
{"encoding":"sparse-delta-v1","keyframeInterval":60,"fields":["randallExists","randallX","randallY"],"rows":[[0,0,[0,0,0]],[1,477,[[0,1],[1,-95.9],[2,-33.2489]]]]}
```

- `randallExists`: `1` when Yoshi's Story Randall has a simulator-owned current
  position for this frame.
- `randallX`, `randallY`: Randall center from the same generated `MSLSTG01`
  platform transform consumed by collision.

## Items

Items are optional. Omit `items` when a trace has no item data.

```json
{"encoding":"sparse-delta-v1","keyframeInterval":60,"fields":["alive","typeId","state","owner","x","y","vx","vy","facing","damage","timer","spawnId","misc0","misc1","misc2"],"rows":[]}
```

- `encoding`: exactly `"sparse-delta-v1"`.
- `keyframeInterval`: intended maximum frame distance between live item
  keyframes.
- `fields`: names for each scalar in an item state row.
- Keyframe row shape: `[0, frame, slot, fullItemRow]`.
- Delta row shape: `[1, frame, slot, changes]`.
- `slot`: live item slot or stable trace-local slot.
- `changes`: changed fields for this item slot at this frame.
- `alive`: `1` when an item slot is active, `0` when the item despawns.
- `typeId`: public or extracted MSL item/article id when available.
- `state`: item state id.
- `owner`: owning player index, owning port, or `-1` for none. Producers must
  document which owner encoding they use until item ids are promoted to a
  public contract.
- `x`, `y`: world position.
- `vx`, `vy`: velocity.
- `facing`: item facing direction.
- `damage`: item damage taken or carried damage.
- `timer`: item expiration/lifetime timer.
- `spawnId`: stable spawn id when available.
- `misc0`, `misc1`, `misc2`: item-type-specific small values. Prefer promoting
  these to named fields when they become stable viewer/debug concepts.

An item reader reconstructs each slot by carrying the previous decoded item row
forward and applying changed fields. A delta row with `alive = 0` closes that
slot.

## Reader Profile

A conforming viewer reader must:

- validate `format` and `schemaVersion`;
- validate each stream `encoding`;
- use field lists to decode changed-field rows;
- reconstruct full frame state from keyframes and deltas;
- render traces with no `inputs` or no `items`;
- ignore unknown fields and metadata;
- reject rows missing required v1 fields with a clear error.

The viewer may convert `MSLTRACE1` into its internal render model. That render
model is not part of this trace contract.

## Writer Profile

A conforming writer should:

- write compact JSON by default, without indentation or extra whitespace;
- write public ids from `src/ids.h` and button masks from `src/buttons.h`;
- emit an initial keyframe for every state stream it writes;
- emit bounded periodic keyframes for seekability;
- omit unchanged fields from delta rows;
- omit optional streams and optional columns that are not needed;
- keep frame and input arrays zero-based from `match.startFrame` unless the trace
  is explicitly preserving another source frame space;
- round floats only for file-size or readability reasons, not as part of the
  semantic contract;
- put model-specific data under `metadata.model` rather than in required frame
  columns;
- put reproduction/debug provenance under `metadata.provenance`.

The C core should not need to own JSON serialization. A future C trace API should
write frame records into caller-owned memory; Python, JS, or downstream tooling
can serialize those records into `MSLTRACE1`.
