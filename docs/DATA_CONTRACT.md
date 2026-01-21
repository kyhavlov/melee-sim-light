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
- `data/hurtcaps/fox.bin`, `data/hurtcaps/falco.bin` (hurt capsule init tables; decomp-first, compact binary)
- `data/hurtcaps/fox.json`, `data/hurtcaps/falco.json` (hurt capsule init tables; debug-friendly mirror; C loads `.bin` only)
- `data/moves/fox.json`, `data/moves/falco.json` (subaction timelines for key motions + specials; decomp-first)
- `data/anims/fox.bin`, `data/anims/falco.bin` (per-msid bone matrices + TransN; decomp-first)
- `data/anims/fox.blend.bin`, `data/anims/falco.blend.bin` (blend/dynamics bytes; decomp-first)
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
  The sim currently does **not** model those fighter-scale fields yet, so world radius is `scale` only.

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
