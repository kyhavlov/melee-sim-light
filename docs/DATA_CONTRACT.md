# Data Contract (living)

This doc defines what **ISO-derived game data** we extract and how it is laid out on disk so the C core can load it.

Policy:
- All gameplay-critical numbers come from **game files** (`SSBM.iso` / `_iso/*.dat`) and/or decomp.
- Extracted outputs live under `data/` and are **generated artifacts** (gitignored).
- The C core should load these at initialization (no parsing during `step`).

## Canonical paths (current target domain)

Stage (Final Destination):
- `_iso/GrNLa.dat` (source)
- `data/stages/final_destination.json` (extracted collision segments; decomp-first)
  - Note: the C core currently parses this JSON **once at init** (temporary). We will move to a compact
    binary artifact for stage collision later to avoid JSON parsing overhead/complexity.
  - Safety guard: the loader caps `line_count` at 4096 to prevent unbounded allocations on malformed files.
  - Coordinate convention:
    - `segments[].x0/y0/x1/y1` are **unscaled** `coll_data->verts` coordinates as stored in the stage DAT.
    - `unit_scale` is `grGroundParam.x0` (aka `Ground_801C0498()`), which `mpLibLoad()` uses to scale
      collision vertices at runtime (`groundCollVtx[i].pos = unit_scale * coll_data->verts[i]`).
      Decomp refs: `refs/melee/src/melee/gr/ground.c:270` and `refs/melee/src/melee/mp/mplib.c:174,252-263`.
    - The C core should apply `unit_scale` at init-time when loading the extracted geometry so simulator world
      coordinates match Slippi post-frame positions.

Common constants:
- `_iso/PlCo.dat` (source)
- `data/common/ft_common_data.json` (ftCommonData constants; decomp-first)

Characters (Fox/Falco):
- `_iso/PlFx.dat`, `_iso/PlFc.dat` (source)
- `data/characters/fox.json`, `data/characters/falco.json` (movement/ecb/laser/reflector attrs; decomp-first)
  - `ecb_joints`: 6 `s16` indices into `fp->parts[]` (as stored in `ftData_x44_t`).
    Decomp: `ft_80081B38` calls `mpColl_SetECBSource_JObj(..., bones[temp_r29->unk*].joint, ...)`.
- `data/shields/fox.bin`, `data/shields/falco.bin` (guard-tilt shield bubble centers; decomp-first, compact binary)
- `data/hurtcaps/fox.bin`, `data/hurtcaps/falco.bin` (hurt capsule init tables; decomp-first, compact binary)
- `data/hurtcaps/fox.json`, `data/hurtcaps/falco.json` (hurt capsule init tables; debug-friendly mirror; C loads `.bin` only)
- `data/hitboxes/fox.bin`, `data/hitboxes/falco.bin` (hitbox event tables; decomp-first, compact binary)
- `data/moves/fox.json`, `data/moves/falco.json` (subaction timelines for key motions + specials; decomp-first)
- `data/anims/fox.bin`, `data/anims/falco.bin` (per-msid bone matrices + TransN; decomp-first)
- `data/anims/fox.blend.bin`, `data/anims/falco.blend.bin` (blend/dynamics bytes; decomp-first)
- `data/items/lasers.bin` (Fox/Falco blaster laser params; decomp-first, compact binary)
- `data/ecb/fox_bottom.bin`, `data/ecb/falco_bottom.bin` (per-msid per-frame ECB bottom Y; decomp-shaped)
  - Source: `data/anims/<char>.bin` (SSANIM01 v3 matrices) + `data/characters/<char>.json` `ecb_joints`.
  - Per msid + integer frame `f`, we compute:
    - `min_joint_y[f] = min( joint_y(part) for part in ecb_joints )` using the translation `ty` from
      the fighter-local world matrices.
    - `ecb_bottom_rel_y[f] = min_joint_y[f]`.
  - Coordinate convention:
    - `ecb_bottom_rel_y` is in the same **fighter-local** coordinate system as `data/anims/<char>.bin`
      matrices (TransN translation removed).
    - To get world-space ECB bottom Y, add it to the fighter's world `pos_y` (Slippi post-frame position).
- `data/ecb/fox_extents.bin`, `data/ecb/falco_extents.bin` (per-msid per-frame ECB extrema; decomp-shaped)
  - Source: `data/anims/<char>.bin` (SSANIM01 v3 matrices) + `data/characters/<char>.json` `ecb_joints`.
  - Per msid + integer frame `f`, we compute fighter-local joint extrema over the 6 ECB source joints:
    - `min_x[f] = min( joint_x(part) for part in ecb_joints )`
    - `max_x[f] = max( joint_x(part) for part in ecb_joints )`
    - `min_y[f] = min( joint_y(part) for part in ecb_joints )`
    - `max_y[f] = max( joint_y(part) for part in ecb_joints )`
  - These correspond to the `left_x/right_x/bottom_y/top_y` extrema computed in the joint loop of
    `mpColl_LoadECB_JObj` before runtime expansion/clamping.
    Source pointer: `refs/melee/src/melee/mp/mpcoll.c:328` (ECB source joint loop).
  - Coordinate convention matches `data/anims/<char>.bin` fighter-local matrices (TransN translation removed).

Notes:
- `animation_index` in Slippi post-frames includes `0xFFFFFFFF` as a sentinel; treat that as “no animation”.
- Item reference ordering in `.msl` datasets is **sorted by item `instance_id`**, then `id`, then `type`. The sim should follow the same stable ordering for its fixed 15 slots.

## `data/items/lasers.bin` (MSLLASR1 v2)

Purpose: compact, init-time-loadable Fox/Falco blaster laser tables (spawn + projectile + hitbox).

Decomp semantics (source pointers):
- SpecialN (fighter) spawns lasers by setting `cmd_vars[2]` from movescript and calling
  `ftFx_SpecialN_CreateBlasterShot` when `cmd_vars[2] != 0` (then clearing it).
  - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
  - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
- SpecialN submotion ids (Start/Loop/End; ground + air) are game-code enums:
  - refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion
- Laser item lifetime comes from `FoxLaserAttr.lifetime` (`Article.x4_specialAttributes`):
  - refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504 (it_80275158(item, attr->lifetime))
  - refs/melee/src/melee/it/itCharItems.h::FoxLaserAttr
- Laser hitbox shape/params come from the laser article state script (Pl*.dat item article):
  - extracted by `tools/extraction/extract_character_attrs.py::_extract_fox_falco_laser` via
    `tools/extraction/extract_fighter_moves.py::_parse_subaction_events`
- Spawn bone is `FtPart_RThumbNb` and spawn local offset is the constant vector passed to `lb_8000B1CC`:
  - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  - refs/melee/src/melee/ft/forward.h::Fighter_Part (FtPart_RThumbNb id domain)
  - Constant block (decomp; not a DAT attr):
    - `sp14.x = 0`
    - `sp14.y = 1.2325000762939453f`
    - `sp14.z = 4.263599872589111f`

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLLASR1"`
  - `version: u32 = 2`
  - `record_count: u16` (currently 2: Fox + Falco)
  - `reserved: u16 = 0`
- Records (`record_count` entries), fixed-size:
  - `char_id: u8` (Slippi/GALE01 character id: Fox=1, Falco=22)
  - `pad0: u8 = 0`
  - `shot_itkind: u16` (`ftFox_DatAttrs.x1C_FOX_BLASTER_SHOT_ITKIND`)
  - `gun_itkind: u16` (`ftFox_DatAttrs.x20_FOX_BLASTER_GUN_ITKIND`)
  - `spawn_bone_part_id: u16` (`Fighter_Part` / `FtPart_*` id; pass as `part_id` to `anim_pose_get_matrix`)
  - `ground_start_msid: u16` (ground SpecialN start submotion id; decomp enum constant)
  - `ground_loop_msid: u16` (ground SpecialN loop submotion id; decomp enum constant)
  - `ground_end_msid: u16` (ground SpecialN end submotion id; decomp enum constant)
  - `air_start_msid: u16` (air SpecialN start submotion id; decomp enum constant)
  - `air_loop_msid: u16` (air SpecialN loop submotion id; decomp enum constant)
  - `air_end_msid: u16` (air SpecialN end submotion id; decomp enum constant)
  - `blaster_angle: f32` (`ftFox_DatAttrs.x10_FOX_BLASTER_ANGLE`)
  - `blaster_speed: f32` (`ftFox_DatAttrs.x14_FOX_BLASTER_VEL`)
  - `spawn_off_xyz: 3 * f32` (bone-local offset used by `lb_8000B1CC` in `ftFx_SpecialN_FtGetHoldJoint`;
    decomp constant, not a DAT attr)
  - `lifetime_frames: u16` (rounded from `FoxLaserAttr.lifetime`)
  - `shoot_frame_count_ground: u8` (<= 8)
  - `shoot_frame_count_air: u8` (<= 8)
  - `reserved1: u16 = 0`
  - `shoot_frames_ground[8]: 8 * u16` frames where movescript sets `cmd_vars[2] != 0` (ground loop)
  - `shoot_frames_air[8]: 8 * u16` frames where movescript sets `cmd_vars[2] != 0` (air loop)
  - `laser_damage: f32` (from laser article hitbox script)
  - `laser_size: f32` (hitbox size/radius from laser article hitbox script)
  - `laser_angle: u16` (degrees; from laser article hitbox script)
  - `laser_kbg: u16` (knockback growth; from laser article hitbox script)
  - `laser_wsk: u16` (weight set knockback; from laser article hitbox script)
  - `laser_bkb: u16` (base knockback; from laser article hitbox script)
  - `laser_shield_damage: i8` (signed; from laser article hitbox script)
  - `pad2: u8[3] = 0`
  - `hitbox_offsets_x_count: u8` (<= 16)
  - `pad3: u8[3] = 0`
  - `hitbox_offsets_x[16]: 16 * f32` X offsets of consecutive hitboxes along the beam (from the article state script)

Runtime semantics (current C-core policy for v2 lasers):
- The simulator uses `spawn_bone_part_id` + `spawn_off_xyz` with `anim_pose_get_matrix(...)` to compute world spawn points.
- New laser items are allocated in a fixed 15-slot pool with deterministic (stable) ordering matching dataset sorting:
  `(instance_id, spawn_id, type)`.

## `data/shields/<char>.bin` (MSLSHLD1 v1)

Purpose: compact, init-time-loadable table for guard-tilt shield bubble center offsets.

Decomp semantics:
- Shield collision uses a dedicated `HitResult` (`fp->shield_hit`) bound to a "shield" joint:
  - Binding: `ftColl_8007B1B8` assigns `fp->shield_hit.bone` + `size` + `offset`.
    Decomp: `refs/melee/src/melee/ft/ftcoll.c:1371-1383`.
  - Checks: hitbox-vs-shield uses `lbColl_80007BCC(..., &this_fp->shield_hit, ...)`.
    Decomp: `refs/melee/src/melee/ft/ftcoll.c:1098` and `refs/melee/src/melee/lb/lbcollision.c:1510-1569`.
- While guarding, Melee applies a guard-tilt animation timeline (submotion id `ftCo_SM_Guard = 38`) to
  the fighter model (including the shield joint). The guard tilt "frame" is tracked in
  `fp->mv.co.guard.x8`:
  - Initialization: `ftCo_800921DC` sets `mv.co.guard.x8 = 10`.
    Decomp: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:246-265`.
  - Update from stick direction: `ftCo_80091BC4` computes a polar angle from `(lstick.y, lstick.x * facing_dir)`
    and updates `mv.co.guard.x8` (with smoothing/inertia via `mv.co.guard.x4`).
    Decomp: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:131-171`.
  - Application to pose: `ftCo_80091E78` drives `FtPart_TransN` animation and calls `ftAnim_80070710(jobj, fp->mv.co.guard.x8)`
    to set the guard tilt frame.
    Decomp: `refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:211-239`.

Simulator usage:
- `shields_refresh()` uses this table to compute a **world-space** shield bubble center `(x,y,z)` for debug geometry
  and shield-vs-body precedence in debug combat contact classification.
- This is **suite-neutral**: it does not mutate fighter state, and it does not affect validated compare outputs.

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLSHLD1"`
  - `version: u32 = 1`
  - `frame_count: u16` number of timeline frames stored (typically `round(fig.frames)+1` for msid 38; Fox/Falco is 371)
  - `neutral_frame: u16` guard tilt neutral frame (decomp: `mv.co.guard.x8` initial value; typically 10)
- Payload:
  - `xyz: frame_count * 3 * f32` (packed as `[x0,y0,z0, x1,y1,z1, ...]`)

Coordinate semantics:
- Each `(x,y,z)` is the translation component (`tx,ty,tz`) of the shield joint's **fighter-local** transform under
  the `ftCo_SM_Guard` (msid 38) tilt timeline, evaluated at integer frame `f`.
- These coordinates share the same fighter-local convention as `data/anims/<char>.bin` matrices (TransN/root translation removed).
- `shields_refresh()` consumes them as follows:
  - Compute a target tilt frame from stick direction:
    - `rad = atan2(stick_y, stick_x * facing_dir)` in `[0, 2π)`, then `deg = clamp(rad * 180/π, 0..359)`.
    - `tilt_frame = clamp(neutral_frame + deg, 0..frame_count-1)`.
  - Blend between neutral and angled offsets by stick magnitude `mag = clamp(sqrt(x^2+y^2), 0..1)`:
    - `d = neutral_xyz + mag * (tilt_xyz - neutral_xyz)`.
  - Apply per-fighter runtime scale: `d *= fighter_scale_y` (uniform scalar).
  - Convert to world space: `world = (pos_x, pos_y, 0) + (d.x * facing_dir, d.y, d.z)`.

## `data/hurtcaps/<char>.bin` (MSLHURT1 v1)

Purpose: compact, init-time-loadable tables for fighter hurt capsule init records (`ftHurtboxInit`).

Decomp semantics:
- Init struct: `struct ftHurtboxInit` in `refs/melee/src/melee/ft/chara/ftCommon/types.h`.
- Application: `ftColl_HurtboxInit` in `refs/melee/src/melee/ft/ftcoll.c` copies the record into a
  `FighterHurtCapsule` / `HurtCapsule` and binds the capsule to a bone via:
  `hurt->capsule.bone = fp->parts[hurt->capsule.bone_idx].joint`.
- Endpoint positions are later computed from the bone transform + offsets (see `lb_8000B1CC` usage in
  `refs/melee/src/melee/lb/lbcollision.c` around `checkPos` / `lbColl_80008248`).

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLHURT1"`
  - `version: u32 = 1`
  - `capsule_count: u16`
  - `reserved: u16 = 0`
- Records (`capsule_count` entries), each:
  - `bone_part_id: u16`
  - `height: u8`
  - `is_grabbable: u8`
  - `pad: u16 = 0`
  - `a_offset: 3 * f32`
  - `b_offset: 3 * f32`
  - `scale: f32`

Field semantics:
- `bone_part_id` is `ftHurtboxInit.bone_idx` (decomp type `Fighter_Part`; `refs/melee/src/melee/ft/forward.h`).
  This is the same id domain as SSANIM `joint_parts` entries and must be passed as `part_id` to
  `anim_pose_get_matrix(...)` in the sim.
- `a_offset` / `b_offset` are the local-space offsets copied into `HurtCapsule.a_offset` / `b_offset`.
- `scale` is copied into `HurtCapsule.scale` (treated as capsule radius in collision code).
  Note: vanilla sometimes applies additional per-fighter scale factors (e.g. `fp->x34_scale.y`)
  when deriving bounds from hurt capsules (`refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c::ftCo_800A0DA4`).
  The sim models this as `MslSeed.fighter_scale_y` / `MslStateSoA.fighter_scale_y` and applies it
  to pose-derived world endpoints and radius.

## `data/hitboxes/<char>.bin` (MSLHITB1 v1)

Purpose: compact, init-time-loadable hitbox event tables (movescript-derived) keyed by submotion id.

Current simulator usage:
- Loaded at init only (no parsing/allocations on the per-frame hot path).
- Used to compute pose-driven **world-space hitbox centers** for debug readback.
- **No combat resolution yet**: these hitboxes must not affect state transitions/physics/collision/timers.

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLHITB1"`
  - `version: u32 = 1`
  - `entry_count: u32` number of index entries
- Index (`entry_count` entries), each:
  - `msid: u16` submotion id / Slippi post-frame `animation_index` (lower 16 bits)
  - `rec_count: u16` number of event records for this msid
  - `rec_bytes: u32 = rec_count * 44`
  - `payload_off: u32` absolute byte offset to this msid's first event record
- Records (`rec_count` entries), each 44 bytes:
  - `frame: u16` integer timeline key (interpreted against seeded `action_frame`)
  - `kind: u8` (`0` = set/enable, `1` = clear)
  - `hitbox_id: u8` slot id (0..3 typical). If `kind==1` and `hitbox_id==0xFF`, this is a clear-all record.
  - `bone_part_id: u32` fighter part id (same domain as SSANIM `joint_parts`; pass as `part_id` to
    `anim_pose_get_matrix(...)`).
  - `x: f32`, `y: f32`, `z: f32` bone-local hitbox center offset
  - `radius: f32`
  - `damage: f32` (stored but not yet used for resolution)
  - `u16_tail[8]: 8 * u16` additional extracted fields (decomp-shaped):
    - `u16_tail[0]`: `angle` (0..361; 361 is Sakurai angle sentinel)
    - `u16_tail[1]`: `kbg` (knockback growth)
    - `u16_tail[2]`: `wsk` (weight set knockback)
    - `u16_tail[3]`: `bkb` (base knockback)
    - `u16_tail[4]`: `element` (low 8 bits) and `shield_damage` (high 8 bits, 2's complement `s8`)
    - `u16_tail[5]`: `sfx_severity` (low 8 bits) and `sfx_kind` (high 8 bits)
    - `u16_tail[6]`: bitfield of boolean flags:
      - bit 15: `rebound`
      - bit 14: `clank`
      - bit 13: `ignore_fighter_scale`
      - bit 12: `ignore_thrown_fighters`
      - bit 11: `item_hit_interaction`
      - bit 10: `hit_aerial`
      - bit 9: `hit_grounded`
    - `u16_tail[7]`: reserved (currently 0)

Runtime semantics (current C-core policy):
- Events are applied in file order up to `frame` to derive the current active hitbox definition per `hitbox_id`.
- Pose lookup failures for a specific hitbox skip that hitbox only (do not affect anything else).
- World-space center is pose-driven and uses the same conventions as hurtcaps/shields:
  - `local = (pose_mtx * (x,y,z)) * fighter_scale_y`
  - `local.x *= facing_dir` (approximation: mirror X when facing left; we do not apply a true facing
    Y-rotation here, so Z is not rotated)
  - `world = (pos_x,pos_y,pos_z) + local`
  - `radius *= fighter_scale_y` unless `ignore_fighter_scale` is set (hitbox flags bit 13).

## `data/hurtbox_states/<char>.bin` (MSLHURM1 v1)

Purpose: compact, init-time-loadable movescript-derived hurt capsule state timelines keyed by submotion id.

Current simulator usage:
- Loaded at init only (no parsing/allocations on the per-frame hot path).
- Used by `hurtboxes_refresh()` to skip/disable capsules that are not "can be hit" (disabled/intangible).
- Used by `combat_select_body_hits_one()` to gate BODY selection via `state.hurtcap_enabled`.

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLHURM1"`
  - `version: u32 = 1`
  - `frame_count: u16` fixed length for each msid payload (default extractor uses 240)
  - `capsule_count: u16` lane count (<=32; should match `data/hurtcaps/<char>.bin` capsule_count)
  - `entry_count: u32` number of index entries
- Index (`entry_count` entries), each:
  - `msid: u16` submotion id / Slippi post-frame `animation_index` (lower 16 bits)
  - `reserved: u16 = 0`
  - `payload_bytes: u32 = frame_count * 8`
  - `payload_off: u32` absolute byte offset to this msid's payload
- Payload for each msid:
  - `states_u64[frame_count]: frame_count * u64`
    - Each u64 is a packed array of **2-bit lanes**: lane `i` is the `HurtCapsuleState` for capsule `i`.
    - Lane `i` corresponds to capsule `i` in `data/hurtcaps/<char>.bin` order.

State lane semantics:
- Decomp enum: `HurtCapsuleState` in `refs/melee/src/melee/lb/forward.h`:
  - `0 = HurtCapsule_Enabled`   (can be hit; eligible for BODY)
  - `1 = HurtCapsule_Disabled`  (not eligible)
  - `2 = Intangible`            (not eligible)

Runtime semantics (current C-core policy):
- If there is no table entry for a given msid, the simulator treats all capsules as enabled.
- For a given frame, only capsules with lane state == `HurtCapsule_Enabled` are eligible for BODY contacts.

## `data/hit_status/<char>.bin` (MSLHSTA1 v1)

Purpose: compact, init-time-loadable movescript-derived **hit status** timelines keyed by submotion id (opcode 26).

Current simulator usage:
- Loaded at init only (no parsing/allocations on the per-frame hot path).
- Used by `combat_select_body_hits_one()` to skip BODY selection when the defender is not in "normal" hit status.

Binary layout (little-endian):
- Header:
  - `magic[8] = "MSLHSTA1"`
  - `version: u32 = 1`
  - `frame_count: u16` fixed length for each msid payload (default extractor uses 240)
  - `reserved: u16 = 0`
  - `entry_count: u32` number of index entries
- Index (`entry_count` entries), each:
  - `msid: u16` submotion id / Slippi post-frame `animation_index` (lower 16 bits)
  - `reserved: u16 = 0`
  - `payload_bytes: u32 = frame_count * 1`
  - `payload_off: u32` absolute byte offset to this msid's payload
- Payload for each msid:
  - `hit_status_u8[frame_count]: frame_count * u8`

Decomp pointers:
- `ftAction_80071A14` (opcode 26 handler) → `ftColl_8007B62C(gobj, state)`.
  - `refs/melee/src/melee/ft/ftaction.c:539`
  - `refs/melee/src/melee/ft/ftcoll.c`

Runtime semantics (current C-core policy):
- If there is no table entry for a given msid, the simulator treats hit status as "normal" (eligible for BODY).

## What the C core should load (minimum)

To avoid “mystery drift”, prefer loading the following early:
- **Stage collision**: segments + ledge flags + platform flags + a stable `ground_id` mapping.
- **Fighter attributes**: gravity/terminal velocity, traction/friction, jump velocities, ledge snap params, ECB joints/offsets.
- **Move event tables**: hitbox/hurtbox state changes, IASA/interrupt windows, articles (laser), throw flags, etc.
- **Animation attachment data**:
  - Either (A) full per-bone matrices per frame for the bones we need, or
  - (B) precomputed per-frame attachment transforms for hitboxes/hurtboxes/ECB sources.
  - The extractor currently outputs (A)-style matrices in `data/anims/*.bin` for Fox/Falco.

`ground_id` semantics (current assumption):
- Our datasets store `ground_id` directly from Slippi post-frame `ground` (see `tools/slippi/make_dataset_from_slp.py`).
- The simulator treats `ground_id` as the **stage collision segment/line index** from the stage’s collision table.
  - For Final Destination this matches the ISO-extracted indices in `data/stages/final_destination.json` (`segments[].i`), e.g. the main floor uses segment indices `0`, `1`, and `2`.
  - If we discover a stage-specific remapping (decomp / Slippi schema clarification), update this note and the stage loader accordingly.

## Build the data (one-shot / cached)

Generate all required artifacts for the current target domain:

```bash
uv run python -m tools.extraction.build_data \
  --iso-dir _iso \
  --stage grnla \
  --chars fox,falco
```

If `_iso/` is missing required `.dat` files, extract them from `SSBM.iso` first:

```bash
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*GrNLa.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlCo.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlFx.dat'
uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlFc.dat'
```

## Schema notes / stability

This project avoids explicit “versioning” in naming. Instead:
- Generated files should include enough **self-checks** (magic strings, record sizes, offsets) to fail loudly when incompatible.
- Contract changes should be reflected here immediately, and extraction scripts updated accordingly.

## Fast consistency check (no rebuild)

If you already have local `data/` artifacts, you can validate basic consistency without re-extracting:

```bash
uv run pytest -m integration
```
