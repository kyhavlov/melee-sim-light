# Engine Dump Schema (v1 draft)

This document defines the binary format for **engine‑dump replays** used as the primary
validation target (live engine internal state captured per frame).

## Boundary (Single Canonical Snapshot)

Snapshot boundary is the **post‑frame** engine state captured at the libmelee step / input‑block
boundary, i.e. immediately after `Console.step()` returns and before the next input flush.

**Rule:** all fields are captured at the *same* boundary. No per‑field offsets are permitted.
If a field appears offset relative to replay data, fix the sim or capture boundary; do not add
field‑specific offsets in validation.

## Encoding Rules

- **Endianness**: little‑endian for all integer fields.
- **Float values** are stored as **raw IEEE‑754 f32 bit patterns** (`u32`) to enable bit‑exact
  comparisons across languages.
- All counts/offsets are **bytes** unless otherwise specified.

## File Layout (v1)

```
[Header]
[FrameRecord * frame_count]
[InputRecord * frame_count * port_count]
[FighterRecord * frame_count * port_count]
[ItemRecord * total_items]
[HitboxRecord * frame_count * port_count * 4]
[HurtboxRecord * frame_count * port_count * 15]
```

### Header

| Field | Type | Notes |
| --- | --- | --- |
| `magic` | `[u8; 8]` | `MSIMDMP\0` |
| `version` | `u32` | schema version (v1 = 1) |
| `endian_tag` | `u32` | `0x01020304` for sanity |
| `frame_count` | `u32` | number of frames dumped |
| `port_count` | `u8` | number of active ports (usually 2 or 4) |
| `stage_id` | `u16` | stage enum id |
| `is_teams` | `u8` | match setting flag |
| `costume_id` | `u8` | costume/color id (`fp+0x619`) |
| `replay_uuid` | `[u8; 16]` | optional metadata (zeroed if unknown) |
| `frames_offset` | `u32` | byte offset to FrameRecord array |
| `inputs_offset` | `u32` | byte offset to InputRecord array |
| `fighters_offset` | `u32` | byte offset to FighterRecord array |
| `items_offset` | `u32` | byte offset to ItemRecord array |
| `hitboxes_offset` | `u32` | byte offset to HitboxRecord array |
| `hurtboxes_offset` | `u32` | byte offset to HurtboxRecord array |
| `total_items` | `u32` | total ItemRecord count |
| `reserved1` | `[u8; 11]` | padding/alignment |

### FrameRecord (per frame)

| Field | Type | Notes |
| --- | --- | --- |
| `frame_index` | `i32` | Melee frame index (signed) |
| `rng_state` | `u32` | RNG state at dump boundary (global seed) |
| `rng_seed` | `u32` | Frame-start RNG seed from replay playback (when present) |
| `rng_steps` | `u32` | Steps from **previous frame’s** `rng_seed` to this frame’s `rng_state` (bounded search; `0xFFFFFFFF` if unknown) |
| `item_count` | `u16` | number of items in this frame |
| `item_offset` | `u32` | index into ItemRecord array |
| `flags` | `u16` | bit 0 = `rng_seed_exists`, remaining bits reserved |
| `game_timer` | `u32` | match timer (seconds or raw counter per global address). **Not validated** (derived). |
| `randall_exists` | `u8` | 1 if Randall exists on this frame |
| `randall_x_bits` | `u32` | Randall center X (f32 bits) |
| `randall_y_bits` | `u32` | Randall Y (f32 bits) |
| `fountain0_exists` | `u8` | Fountain left platform exists |
| `fountain0_y_bits` | `u32` | Fountain left platform Y (f32 bits) |
| `fountain1_exists` | `u8` | Fountain right platform exists |
| `fountain1_y_bits` | `u32` | Fountain right platform Y (f32 bits) |
| `costume_id` | `u8` | costume/color id (`fp+0x619`) |

### InputRecord (per frame, per port)

Controller state used to drive the match (self‑contained dump). Inputs are stored in the
same **normalized float** representation used by the simulator and libmelee input API.

**Alignment:** `InputRecord[i]` contains the inputs sampled on frame `i` that are applied
to advance the simulation from frame `i` to frame `i+1`. Validation compares the *post‑frame*
state after applying inputs `i` against `FrameRecord[i+1]`.

| Field | Type | Notes |
| --- | --- | --- |
| `buttons` | `u32` | digital button bitmask (GameCube). Includes `HSD_PAD_LR` when L/R analog > 0 |
| `stick_x_bits` | `u32` | f32 bits (range approx ‑1..1) |
| `stick_y_bits` | `u32` | f32 bits |
| `cstick_x_bits` | `u32` | f32 bits |
| `cstick_y_bits` | `u32` | f32 bits |
| `l_shoulder_bits` | `u32` | f32 bits (range 0..1) |
| `r_shoulder_bits` | `u32` | f32 bits |

### FighterRecord (per frame, per port)

| Field | Type | Notes |
| --- | --- | --- |
| `flags` | `u32` | reserved for per‑fighter flags |
| `pos_x_bits` | `u32` | f32 bits |
| `pos_y_bits` | `u32` | f32 bits |
| `pos_z_bits` | `u32` | f32 bits |
| `self_vel_x_bits` | `u32` | f32 bits |
| `self_vel_y_bits` | `u32` | f32 bits |
| `gr_vel_bits` | `u32` | f32 bits |
| `action_state` | `u16` | fighter motion state id |
| `anim_id` | `u16` | current anim index |
| `anim_frame_bits` | `u32` | f32 bits |
| `action_frame_bits` | `u32` | f32 bits |
| `state_flags_2218` | `u8` | raw state flag byte |
| `state_flags_221a` | `u8` | raw state flag byte |
| `state_flags_221b` | `u8` | raw state flag byte |
| `state_flags_221c` | `u8` | raw state flag byte |
| `state_flags_221f` | `u8` | raw state flag byte |
| `invulnerable` | `u8` | hurtbox collision state (0=vulnerable, 1=invulnerable, 2=intangible) |
| `ground_or_air` | `u8` | 0=ground, 1=air |
| `stocks` | `u8` | remaining stocks |
| `team` | `u8` | team id |
| `costume_id` | `u8` | costume/color id (`fp+0x619`) |
| `facing_bits` | `u32` | f32 bits |
| `percent_bits` | `u32` | f32 bits |
| `hitlag_left_bits` | `u32` | f32 bits |
| `misc_as_bits` | `u32` | f32 bits (raw misc AS var at `fp+0x2340`) |
| `shield_health_bits` | `u32` | f32 bits |
| `ecb_top_x_bits` | `u32` | f32 bits |
| `ecb_top_y_bits` | `u32` | f32 bits |
| `ecb_bottom_x_bits` | `u32` | f32 bits |
| `ecb_bottom_y_bits` | `u32` | f32 bits |
| `ecb_left_x_bits` | `u32` | f32 bits |
| `ecb_left_y_bits` | `u32` | f32 bits |
| `ecb_right_x_bits` | `u32` | f32 bits |
| `ecb_right_y_bits` | `u32` | f32 bits |
| `reserved1` | `u16` | padding |

Notes:
- Percent is stored as the **full engine float** at the post‑frame boundary (not truncated).
- Slippi replay percent fields are *diagnostic only* and may be rounded/truncated; do not use
  them for engine‑dump validation.

## Replay Parity Notes (Dump vs Slippi)

These differences are *expected* when comparing dumps directly against Slippi replay data:

- **Frame alignment**: dumps are captured at the post‑frame boundary. When comparing to Slippi,
  use `canonicalize_slippi_sample_last` to collapse rollbacks, then align **dump frame `f`**
  to **replay frame `f-1`**.
- **Percent**: Slippi percent is truncated to an integer; use `floor(dump_percent)` to match.
- **Team**: Slippi uses `-1` for FFA. Engine dump stores the raw team id (usually `0`).
  Normalize `-1 → 0` to compare.
- **Misc AS / hitstun**: dump stores raw `misc_as_bits` from `fp+0x2340` (Slippi `p{port}_misc_as`).
  Slippi’s `p{port}_hitstun_left` is derived from `misc_as` + the hitstun flag, so it will not
  match the raw dump value.
- **Facing**: dump stores raw `facing_dir` float; Slippi stores a boolean. Compare by sign
  (`>= 0 → 1`, `< 0 → 0`).
- **Invulnerable**: dump stores the raw hurtbox collision state (`fp+0x1988/0x198C`, 0/1/2),
  matching Slippi’s source; replays typically show only 0/1 unless intangible is present.

### ItemRecord (per item)

| Field | Type | Notes |
| --- | --- | --- |
| `item_id` | `u32` | unique id (or 0 if unknown) |
| `kind` | `u16` | item kind/type id |
| `state` | `u16` | item state id |
| `owner_port` | `i8` | -1 if none |
| `flags` | `u8` | reserved |
| `pos_x_bits` | `u32` | f32 bits |
| `pos_y_bits` | `u32` | f32 bits |
| `pos_z_bits` | `u32` | f32 bits |
| `vel_x_bits` | `u32` | f32 bits (0 if unknown) |
| `vel_y_bits` | `u32` | f32 bits (0 if unknown) |
| `vel_z_bits` | `u32` | f32 bits (0 if unknown) |
| `facing_bits` | `u32` | f32 bits |
| `anim_id` | `u16` | item anim id |
| `reserved0` | `u16` | padding |
| `anim_frame_bits` | `u32` | f32 bits |
| `lifetime_bits` | `u32` | f32 bits (life timer) |
| `damage` | `u32` | raw damage (item hitbox) |

## Expansion Plan

All future fields must preserve raw‑bits encoding for floats to allow exact equality checks.

### Comprehensive Field List (v3)

**Legend**:
- **Dump**: `v0` (already captured), `todo` (not yet captured)
- **Sim**: `ok` (sim has the state), `todo` (sim missing/doesn't export)
- **Compare**: `ok` (validator compares), `todo` (not yet compared)

**Note:** v3 dumps and validates all fields listed below. The **Sim** column indicates
whether the simulator currently implements/updates the field (mismatches are expected).

#### Global / Match

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `stage_id` | `u16` | GameStart / stage enum (Slippi post‑frame global). | v0 | ok | todo |
| `is_teams` | `u8` | GameStart / match settings. | todo | ok | todo |
| `rng_state` | `u32` | Engine RNG (global seed) at dump boundary. | v0 | todo | ok |
| `rng_seed` | `u32` | Replay frame-start RNG seed (playback restore). | v2 | todo | ok |
| `rng_steps` | `u32` | Steps from previous `rng_seed` to `rng_state` (bounded). | v2 | todo | ok |
| `game_timer` | `u32` | Match timer (decomp: `gm` / `mg` time). | todo | todo | todo |
| `replay_uuid` | `[u8; 16]` | Metadata sanity (not engine state). | todo | n/a | todo |

#### Stage Actors / Dynamics

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `randall_exists` | `u8` | Stage runtime (Yoshi’s Story). | todo | ok | todo |
| `randall_x_bits` | `u32` | Stage runtime position. | todo | ok | todo |
| `randall_y_bits` | `u32` | Stage runtime position. | todo | ok | todo |
| `fountain_platform_{0,1}_exists` | `u8` | Stage runtime (FoD). | todo | ok | todo |
| `fountain_platform_{0,1}_y_bits` | `u32` | Stage runtime (FoD). | todo | ok | todo |

#### Fighter Core (per frame, per port)

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `pos_{x,y,z}_bits` | `u32` | Fighter struct `pos` (`refs/melee/src/melee/ft/types.h`). | v0 | ok | ok |
| `self_vel_{x,y}_bits` | `u32` | Fighter struct `x80` vel (`ft/types.h`). | v0 | ok | ok |
| `gr_vel_bits` | `u32` | Fighter struct `gr_vel` (`ft/types.h`). | v0 | ok | ok |
| `facing_bits` | `u32` | Fighter struct `facing_dir` (`ft/types.h`). | v0 | ok | ok |
| `percent_bits` | `u32` | Fighter struct `percent` (`ft/types.h`). | v0 | ok | ok |
| `ground_or_air` | `u8` | Fighter struct ground/air flag (`ft/types.h`). | v0 | ok | ok |
| `action_state` | `u16` | Fighter motion state (Slippi `p{port}_action`). | todo | todo | todo |
| `action_frame_bits` | `u32` | Action frame counter (Slippi `p{port}_action_frame`). | todo | todo | todo |
| `anim_id` | `u16` | Fighter anim id (`fp->anim_id`, `ft/types.h`). | v0 | ok | ok |
| `anim_frame_bits` | `u32` | Fighter anim frame (`fp->anim_frame`, `ft/types.h`). | v0 | ok | ok |

#### Fighter Flags / Status (per port)

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `state_flags_2218` | `u32` | Fighter flags (`fp->x2218`, `ft/types.h`). | todo | todo | todo |
| `state_flags_221a` | `u32` | Fighter flags (`fp->x221A`, `ft/types.h`). | todo | todo | todo |
| `state_flags_221b` | `u32` | Fighter flags (`fp->x221B`, `ft/types.h`). | todo | todo | todo |
| `state_flags_221c` | `u32` | Fighter flags (`fp->x221C`, `ft/types.h`). | todo | todo | todo |
| `state_flags_221f` | `u32` | Fighter flags (`fp->x221F`, `ft/types.h`). | todo | todo | todo |
| `invulnerable` | `u8` | Hurtbox collision state (0/1/2, Slippi source). | todo | todo | todo |
| `hitlag_left` | `u8` | Fighter hitlag (`ft` damage struct). | todo | ok | todo |
| `hitstun_left` | `u8` | Fighter hitstun (`ft` damage struct). | todo | ok | todo |
| `shield_health_bits` | `u32` | Fighter shield (`ft` guard struct). | todo | ok | todo |
| `stocks` | `u8` | Match state (`pl` / `gm` stock). | todo | ok | todo |
| `team` | `u8` | Match state team (GameStart). | todo | ok | todo |

#### Fighter Collision / ECB / Hitboxes (per port)

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `ecb_top_bits` | `u32` | ECB (collision capsule). | todo | todo | todo |
| `ecb_bottom_bits` | `u32` | ECB (collision capsule). | todo | todo | todo |
| `ecb_left_bits` | `u32` | ECB (collision capsule). | todo | todo | todo |
| `ecb_right_bits` | `u32` | ECB (collision capsule). | todo | todo | todo |
| `hurt_capsules` | `struct` | Hurtbox capsules (model/hurtcaps). | todo | ok | todo |
| `hitboxes` | `struct` | Active hitboxes (movescript). | todo | ok | todo |

#### Items / Articles (per item)

| Field | Type | Source | Dump | Sim | Compare |
| --- | --- | --- | --- | --- | --- |
| `item_id` | `u32` | Item struct pointer (unique id). | v0 | ok | todo |
| `kind` | `u16` | ItemKind (`refs/melee/src/melee/it/types.h`). | v0 | ok | todo |
| `state` | `u16` | Item state / msid (`it/types.h`). | v0 | ok | todo |
| `owner_port` | `i8` | Item owner (`it/types.h`). | v0 | ok | todo |
| `pos_{x,y,z}_bits` | `u32` | Item `pos` (`it/types.h`). | v0 | ok | todo |
| `vel_{x,y,z}_bits` | `u32` | Item `x40_vel` (`it/types.h`). | v0 | ok | todo |
| `dir_bits` | `u32` | Item facing dir (`it/types.h` `facing_dir`). | todo | ok | todo |
| `anim_id` | `u16` | Item anim (`it/types.h`). | todo | todo | todo |
| `anim_frame_bits` | `u32` | Item anim frame (`it/types.h`). | todo | todo | todo |
| `lifetime` | `u16` | Projectile life (item‑specific). | todo | ok | todo |
| `damage` | `u16` | Item hitbox damage (item‑specific). | todo | ok | todo |

Notes:
- Item fields derive from the decomp Item struct (`refs/melee/src/melee/it/types.h`).
- Slippi item channels cover only existence/kind/state/pos; the dump should capture the
  full item struct fields needed for exact sim parity.

### HitboxRecord (per frame, per port, per hitbox)

Captured from `HitCapsule` (size 0x138). Offsets follow `refs/datasheet/hitbox_offsets.txt`.

| Field | Type | Notes |
| --- | --- | --- |
| `state` | `u32` | hitbox status |
| `group` | `u32` | hitbox group |
| `damage` | `u32` | raw damage int |
| `damage_stale_bits` | `u32` | f32 bits (staled damage) |
| `offset_{x,y,z}_bits` | `u32` | f32 bits (bone offsets) |
| `size_bits` | `u32` | f32 bits |
| `angle` | `u32` | knockback angle |
| `kbg` | `u32` | knockback growth |
| `wsk` | `u32` | weight-dependent knockback |
| `bkb` | `u32` | base knockback |
| `element` | `u32` | hit element |
| `shield_damage` | `u32` | shield damage |
| `sfx` | `u32` | hit SFX |
| `sfx_kind` | `u32` | SFX kind |
| `flags0..7` | `u8` | raw flag bytes (0x40..0x47) |
| `bone_ptr` | `u32` | attachment bone pointer |
| `pos_{x,y,z}_bits` | `u32` | f32 bits (world position) |

### HurtboxRecord (per frame, per port, per hurtbox)

Captured from `FighterHurtCapsule` (size 0x4C).

| Field | Type | Notes |
| --- | --- | --- |
| `state` | `u32` | hurt capsule state |
| `a_offset_{x,y,z}_bits` | `u32` | f32 bits |
| `b_offset_{x,y,z}_bits` | `u32` | f32 bits |
| `scale_bits` | `u32` | f32 bits |
| `a_pos_{x,y,z}_bits` | `u32` | f32 bits |
| `b_pos_{x,y,z}_bits` | `u32` | f32 bits |
| `bone_idx` | `i32` | bone index |
| `height` | `u32` | HurtHeight enum |
| `is_grabbable` | `u8` | 1 if grabbable |
| `flags` | `u8` | raw flag byte |

Notes:
- `FighterHurtCapsule::is_grabbable` is stored as a 32-bit value in the retail binary
  (see `refs/melee/build/GALE01/asm/melee/ft/ftcoll.s` `ftColl_HurtboxInit`: `stw r7, 0x48(r4)`).
  The dumper reads `u32` and stores the **low byte** as `is_grabbable` to avoid big-endian MSB=0 issues.

## Field Reference Index (Where Each Field Comes From)

This section points to **authoritative code locations** for each dump field. The best
first stop for offsets is the dumper itself:

- `refs/Ishiiruka/Source/Core/Core/Slippi/EngineDumpWriter.cpp`

### Header / Global

- `stage_id`: set from playback settings in `EngineDumpWriter::Begin` (`m_stage_id`), sourced
  from Slippi replay metadata (GameStart).
- `is_teams`: `TEAMS_FLAG_ADDR` in `EngineDumpWriter.cpp` (game global), decomp in
  `refs/melee/src/melee/gm/` (match settings).
- `frame_count`, `port_count`: derived from playback session bounds / active ports.
- `replay_uuid`: currently zeroed by the dumper (metadata only).

### FrameRecord

- `frame_index`: global frame counter; see Slippi asm `refs/slippi-ssbm-asm/Common/Common.s`
  (`loadGlobalFrame`, `CONST_FirstFrameIdx`).
- `flags`: bit 0 = `rng_seed_exists` (1 if the replay provided a frame seed).
- `rng_state`: `RNG_STATE_ADDR` in `EngineDumpWriter.cpp` (global RNG seed at
  `0x804D5F90`). RNG logic is in `refs/melee/src/sysdolphin/baselib/random.c`;
  seeded at boot in `refs/melee/src/melee/gm/gmmain.c` (`*seed_ptr = OSGetTick()`).
  Slippi playback restores it via `refs/slippi-ssbm-asm/Playback/Core/RestoreInitialRNG.s`.
- `rng_seed`: frame-start seed from the replay (`Slippi::FrameData::randomSeed`), applied
  by playback restore logic (`RestoreGameFrame`).
- `rng_steps`: bounded search from **previous frame’s** `rng_seed` to `rng_state` using
  the same LCG as `HSD_Rand`. `0xFFFFFFFF` indicates not found within the configured search bound.
- `game_timer`: `GAME_TIMER_ADDR` in `EngineDumpWriter.cpp`, decomp in `refs/melee/src/melee/gm/`.
- `randall_*`: stage runtime; decomp in `refs/melee/src/melee/gr/grYStory*.c`.
- `fountain*_y`: stage runtime; decomp in `refs/melee/src/melee/gr/grIzumi*.c`.

### InputRecord

Inputs are **not** read from memory; they come from Slippi playback’s `FrameData`:

- `Slippi::PlayerFrameData` in `refs/Ishiiruka/Externals/SlippiLib/SlippiGame.h`.
- Button mapping uses the bitmask constants in `EngineDumpWriter.cpp`.

### FighterRecord

Offsets and fields are read directly from `Fighter` (decomp: `refs/melee/src/melee/ft/types.h`).
Exact offsets are defined in `EngineDumpWriter.cpp`.

- `pos_{x,y,z}_bits`: `fp->cur_pos` (`fp+0xB0`).
- `self_vel_{x,y}_bits`: `fp->self_vel` (`fp+0x80`, `fp+0x84`).
- `gr_vel_bits`: `fp->gr_vel` (`fp+0xEC`).
- `action_state`: motion state id at `fp+0x10` (see Slippi asm `SendGamePostFrame.asm`).
- `anim_id`: `fp+0x14` (current animation id).
- `action_frame_bits`: `fp+0x894` (AS frame; Slippi post‑frame).
- `anim_frame_bits`: `fp+0x8A8` (animation frame).
- `state_flags_2218..221F`: `fp+0x2218` etc (Slippi post‑frame flags).
- `invulnerable`: hurtbox collision state `fp+0x1988/0x198C`
  (Slippi `SendGamePostFrame.asm` “HurtboxCollision_Send”).
- `ground_or_air`: `fp->ground_or_air` (`fp+0xE0`).
- `stocks`: global addresses `P1_STOCK_ADDR` / `P2_STOCK_ADDR` in `EngineDumpWriter.cpp`.
- `team`: `fp+0x61B`.
- `costume_id`: `fp+0x619` (costume/color index used by `CostumeListsForeachCharacter`).
- `facing_bits`: `fp->facing_dir` (`fp+0x2C`).
- `percent_bits`: `fp->percent` (`fp+0x1830`).
- `hitlag_left_bits`: `fp->dmg.x195c_hitlag_frames` (`fp+0x195C`).
- `misc_as_bits`: motion vars `fp+0x2340` (`p{port}_misc_as` in Slippi post‑frame).
- `shield_health_bits`: `fp->shield_health` (`fp+0x1998`).
- `ecb_*`: ECB points at `fp+0x794` (top), `0x79C` (bottom), `0x7A4` (right), `0x7AC` (left).
  ECB update logic is in `refs/melee/src/melee/ft/ftcoll.*`.

### ItemRecord

Items are read from the **Item Manager** via GObj traversal:

- Item manager pointer: `ITEM_MANAGER_PTR` (`r13 - 0x3E74`) in `EngineDumpWriter.cpp`.
- Item struct: `refs/melee/src/melee/it/types.h` (`struct Item`).
- `item_id`: raw `Item*` pointer (stored as u32).
- `kind`: `Item->kind` (`ITEM_KIND_OFF`).
- `state`: `Item->state` (`ITEM_STATE_OFF`).
- `owner_port`: `Item->owner` (`ITEM_OWNER_OFF`).
- `pos_*`: `Item->pos` (`ITEM_POS_OFF`).
- `vel_*`: `Item->vel` (`ITEM_VEL_OFF`).
- `facing_bits`: `Item->facing` (`ITEM_FACING_OFF`).
- `anim_id`: `Item->anim_id` (`ITEM_ANIM_ID_OFF`).
- `anim_frame_bits`: `Item->anim_frame` (`ITEM_ANIM_FRAME_OFF`).
- `lifetime_bits`: `Item->lifetime` (`ITEM_LIFETIME_OFF`).
- `damage`: from item hitbox 0 (`ITEM_HITBOX0_OFF`).

Slippi replay item fields are recorded in `refs/slippi-ssbm-asm/Recording/SendItemInfo.s`.

### HitboxRecord

Hitboxes are read from `HitCapsule` arrays:

- `fp+0x914` (`HitCapsule x914[4]`) in `refs/melee/src/melee/ft/types.h`.
- `HitCapsule` definitions in `refs/melee/src/melee/ft/chara/ftCommon/types.h`.
- Collision logic and helpers in `refs/melee/src/melee/ft/ftcoll.*`.

### HurtboxRecord

Hurtboxes are read from `FighterHurtCapsule` array:

- `fp+0x11A0` (size `0x4C`, count 15), see `refs/melee/src/melee/ft/types.h`.
- Hurtbox math in `refs/melee/src/melee/ft/ftcoll.*`.
