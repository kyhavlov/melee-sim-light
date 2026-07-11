# SSANIM Axis/Basis End-to-End (Fox/Falco)

This note answers the concrete question that caused the earlier regression:

> Are our extracted SSANIM01 matrices and pose-derived offset tables already in “fighter-facing applied” space,
> so that applying an additional facing Y-rotation would double-transform X/Z?

**Conclusion:** No. `data/anims/<char>.bin` matrices are in a canonical, facing-independent “fighter-local” basis (TopN identity backs this).
For the pose-derived primitive offset tables (hurtcaps/hitboxes/shields), the available evidence is consistent with them being bone-local and
facing-independent as well; applying an additional facing transform on top of already-faced data is not supported by what we see today. A true
facing transform must be applied exactly once (runtime or bake), but enabling that safely requires additional ground-truth validation (see checklist).

---

## Coordinate conventions (current sim assumptions)

- **World axes:** `X` = stage left/right, `Y` = up, `Z` = depth.
  - Stage collision is currently modeled in `X/Y` only (2.5D); we still carry `pos_z` and pose-derived `z` for combat geometry.
- **Matrix layout:** SSANIM01 v5 joint matrices are **row-major 3x4** and applied as `out = M * [x y z 1]^T`.
  - See `src/mtx34.h` (`msl_mtx34_mul_point`) and `tools/extraction/extract_ecb_extents.py` (layout comment).
- **TransN handling:** `data/anims/<char>.bin` stores per-joint matrices with **TransN translation removed** (written separately as the v4 tail).
  - Extractor: `tools/extraction/extract_fighter_anims.py` and native bake path in `bindings/msl_binding_debug.c` (stores TransN, then zeroes its `cur_pos`).

---

## What decomp does (ground truth intent)

### Facing is a root Y-rotation (not an X-mirror)

When a motion state is set, the game applies facing by setting the **root part’s Y rotation** from `fp->facing_dir`:

- `refs/melee/src/melee/ft/fighter.c` calls:
  - `ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir));`
- `refs/melee/src/melee/ft/ftparts.c` shows `ftPartSetRotY` is a direct `HSD_JObjSetRotationY` setter (not additive).

This is a **true Y-axis rotation**, so it necessarily **mixes X and Z** for attached points.

### Local→world attachment uses the runtime joint matrix

The general “attach local offset to a joint” helper is:

- `refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC`
  - Calls `HSD_JObjSetupMatrix(arg0)` then `MTXMultVec(arg0->mtx, pos0, pos1)` when an offset is present.

So any pose-derived primitive (hurtcaps/hitboxes/shields) that uses `lb_8000B1CC` in engine is computed from a **runtime** joint matrix that
already includes:
- facing root rotation, and
- model scale (applied at the model/JObj level).

---

## What we extract today (SSANIM01 v5 matrices)

`data/anims/<char>.bin` is produced by `tools/extraction/extract_fighter_anims.py` (or its native equivalent in `bindings/msl_binding_debug.c`), which:

- Evaluates per-part local SRT from the rest pose + FObj tracks (`HSD_MtxSRT` semantics).
- Concatenates to per-part **world matrices** via `PSMTXConcat` semantics.
- Removes TransN translation per frame and writes it as the v4 tail.
- **Does not apply `fp->facing_dir` anywhere** (there is no “facing” input in the bake; the matrices are baked in one canonical orientation).

### Empirical proof: TopN is identity in the extracted files

For Fox/Falco, TopN (FtPart id `0`) is included and its matrix is the **identity** in a non-empty animation:

- `tests/test_ssanim_axis_basis.py::test_ssanim_topn_is_identity_so_facing_is_not_baked`

If facing were already baked into SSANIM01, TopN could not be identity because decomp sets its Y rotation to `±π/2` from `fp->facing_dir`.

**Therefore:** SSANIM01 v5 matrices in `data/anims/<char>.bin` are **facing-independent**.

---

## Offset tables basis (hurtcaps / hitboxes / shields)

These tables are consumed as **bone-local offsets** which become fighter-local/world-space only after multiplying by a pose matrix:

- **Hurtcaps**: `data/hurtcaps/<char>.bin` offsets are read directly from `ftHurtboxInit` layout in the fighter DAT and written out verbatim.
  - Extractor: `tools/extraction/extract_fighter_hurtcapsules.py` (`a_offset`/`b_offset`/`scale`).
- **Hitboxes**: `data/hitboxes/<char>.bin` stores movescript-derived hitbox center offsets `(x,y,z)` keyed by msid + frame.
  - Format is documented in `agent_docs/DATA_CONTRACT.md` (`MSLHITB1 v2`).
- **Shields**: `data/shields/<char>.bin` stores guard-tilt bubble center offsets `(x,y,z)` sampled from msid 38 via the same SSANIM SRT evaluator.
  - Extractor: `tools/extraction/extract_shield_tilt_table.py`.

None of these formats have a “facing” dimension, and the extraction code for hurtcaps/shields does not apply any mirroring/rotation.

**Evidence suggests:** these offsets are intended as **facing-independent** bone-local data, to be turned into world-space points by multiplying
against the runtime joint matrix (which includes facing). A direct, non-handwavy proof would be an engine/dolphin dump of `lb_8000B1CC` results
for the same (joint, local offset) under both facings and a demonstration that the only difference comes from the root Y rotation, not from any
implicit mirroring baked into the offset tables themselves.

---

## Current sim behavior (why we regressed when adding true Y-rotation)

Today, the sim applies:

- fighter scale uniformly in pose space (`scale_y`), and
- “facing” as **mirror X only** (no X/Z mixing),

for pose-derived primitives:
- `src/hurtboxes.c` (hurtcaps endpoints)
- `src/hitboxes.c` (hitbox centers)
- `src/shields.c` (shield tilt X sign)

### Empirical proof: flipping facing negates only X (Z unchanged)

- `tests/test_ssanim_axis_basis.py::test_pose_facing_is_mirror_x_only_for_hitbox_centers`

This test picks a hitbox event whose bone-local `z` offset is non-zero, runs one step with `facing=right` and `facing=left`, and proves:
- `x` is mirrored about `pos_x`, and
- `z` is bitwise-identical between facings.

So “mirror X only” is real and observable today.

### Why a decomp-shaped Y-rotation can regress suite metrics right now

Even though decomp-facing is a Y-rotation, switching from “mirror X only” to “true Y-rotation” changes 3D placement by mixing X/Z.
With the current 2.5D stage model and incomplete combat/ECB fidelity, those placement changes can move hitbox–hurtcap overlaps in ways that
worsen one-step suite metrics (notably `mismatch.hitlag`) until the rest of the pipeline is made consistent.

Importantly: this regression does **not** imply SSANIM01 or offsets already had facing baked in (the TopN identity test contradicts that).

---

## Checklist: what’s required to safely enable true facing Y-rotation

1. **Choose the exact decomp-facing transform and where to apply it**
   - Decomp sets TopN rotation: `rotY = M_PI_2 * facing_dir`.
   - For our pre-concatenated joint world matrices, a consistent way is: `M_faced = R_y(rotY) * M_canonical` for every joint.
2. **Prove axis mapping against an engine dump**
   - Use Dolphin to dump `HSD_JObj->mtx` (or `lb_8000B1CC` outputs) for a known joint + known local offset, for both facings.
   - Verify that `R_y(rotY) * M * offset` matches the dump (within float32 tolerance) before changing gameplay.
3. **Apply it everywhere pose-derived geometry is used**
   - hurtcaps, hitboxes, shields, ECB sources/extents (if/when ECB uses pose directly), and any other future pose-derived primitives.
4. **Add a decomp-backed regression fixture**
   - Commit a tiny engine-dump-based test vector (one joint matrix + one local point + expected world result for both facings).
5. **Only then change runtime behavior + suite reports (separate chunk)**
   - Re-run `make validate` and commit the updated `reports/validation/one_step_suite_eval.txt` in that follow-up change.

---

## Remaining uncertainty / what would resolve it

- A small set of Dolphin “ground truth” samples for `lb_8000B1CC` (joint mtx + input offset + output point) for Fox/Falco in a few actions
  (idle, dash, aerial) would make the axis/basis mapping unambiguous and would let us safely re-enable decomp-facing rotation.
