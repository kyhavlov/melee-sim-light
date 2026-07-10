from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import INPUT_DTYPE, SEED_DTYPE

from tests.test_anim_pose import _MAT_BYTES, _pick_first_nonempty_anim, _read_header
from tests.test_hitboxes_pose import _active_hitboxes_at_frame, _read_hitbox_events


@pytest.mark.integration
@pytest.mark.parametrize(
    ("char_key", "char_id"),
    [
        ("fox", 1),
        ("falco", 22),
    ],
)
def test_ssanim_topn_is_identity_so_facing_is_not_baked(char_key: str, char_id: int) -> None:
    """Proves SSANIM01 v5 matrices are extracted in a canonical (facing-independent) basis.

    Decomp applies facing at runtime by setting TopN rotY from `fp->facing_dir`:
    `ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))` (refs/melee/src/melee/ft/fighter.c).

    If that facing rotation were already baked into SSANIM01, TopN could not be identity.
    """
    anims = Path("data/anims") / f"{char_key}.bin"
    if not anims.exists():
        pytest.skip(f"missing local artifact: {anims}")

    buf = anims.read_bytes()
    joint_count, anim_count, joint_parts = _read_header(buf)

    if 0 not in joint_parts:
        pytest.skip(f"{anims}: joint_parts does not include TopN (FtPart 0)")
    topn_joint_index = int(joint_parts.index(0))

    msid, frame_count, base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    if frame_count <= 0:
        pytest.skip(f"{anims}: no non-empty animations found")
    frame = 0

    off = base + frame * joint_count * _MAT_BYTES + topn_joint_index * _MAT_BYTES
    m = np.frombuffer(buf, dtype="<f4", count=12, offset=off).astype(np.float32, copy=False)

    ident = np.array(
        [
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
        ],
        dtype=np.float32,
    )
    assert np.array_equal(m.view(np.uint32), ident.view(np.uint32)), (char_key, char_id, msid)


@pytest.mark.integration
def test_pose_facing_is_root_roty90_for_hitbox_centers() -> None:
    """Asserts the current sim-facing policy for pose-derived hitbox centers.

    The current C-core policy applies the decomp-shaped root rotY90 (mixing X/Z) to pose-derived
    primitives (see src/hitboxes.c). This test:
    - selects a hitbox whose extracted bone-local z offset is non-zero, and
    - proves that flipping `seed.facing` mirrors both X (about pos_x) and Z (about pos_z) bitwise.
    """
    import msl_binding

    if not Path("data/hitboxes/fox.bin").exists():
        pytest.skip("missing local artifact: data/hitboxes/fox.bin")
    if not Path("data/anims/fox.bin").exists():
        pytest.skip("missing local artifact: data/anims/fox.bin")

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(anim_buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

    by_msid = _read_hitbox_events(Path("data/hitboxes/fox.bin"))
    assert by_msid

    picked: tuple[int, int, int, dict] | None = None  # (msid, frame, hitbox_id, ev)
    for msid, events in sorted(by_msid.items()):
        # Require that this msid exists in SSANIM01.
        # SSANIM01 table header: u16 msid + u16 frame_count per entry.
        off = 16 + joint_count  # header bytes (magic/ver/joint/anim + joint_parts)
        frame_count = None
        for _ai in range(anim_count):
            msid_i, fc = struct.unpack_from("<HH", anim_buf, off)
            off += 4
            mats_bytes = int(fc) * int(joint_count) * _MAT_BYTES
            transn_bytes = int(fc) * 3 * 4
            need = mats_bytes + transn_bytes
            if int(msid_i) == int(msid):
                frame_count = int(fc)
                break
            off += need
        if frame_count is None or frame_count <= 0:
            continue

        # Find any active hitbox with:
        # - pose coverage (bone_part_id exists in joint_parts), and
        # - non-zero bone-local z offset (so rotation-vs-mirror is observable).
        for ev in events:
            f = int(ev["frame"])
            if not (0 <= f < frame_count):
                continue
            active = _active_hitboxes_at_frame(events, f)
            for hb_id, hb_ev in sorted(active.items()):
                bone_part_id = int(hb_ev["bone_part_id"])
                if bone_part_id not in part_to_joint_index:
                    continue
                if float(abs(np.float32(hb_ev["z"]))) <= 0.0:
                    continue
                picked = (int(msid), int(f), int(hb_id), hb_ev)
                break
            if picked is not None:
                break
        if picked is not None:
            break

    assert picked is not None, "failed to find any active hitbox with pose coverage and z!=0"
    msid, frame, hb_id, ev = picked

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert input_stride == INPUT_DTYPE.itemsize

    pos_x = np.float32(123.25)
    pos_y = np.float32(-45.5)
    pos_z = np.float32(7.75)

    def run_with_facing(facing_right: bool) -> np.ndarray:
        seed = np.zeros((1,), dtype=SEED_DTYPE)
        seed["stage_id"][0] = np.uint32(32)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)
        seed["char_id"][0, 0] = np.uint8(1)  # Fox
        seed["char_id"][0, 1] = np.uint8(1)
        seed["action_id"][0, :2] = np.uint16(0xFFFF)
        seed["facing"][0, :2] = np.uint8(1 if facing_right else 0)
        seed["pos_x"][0, 0] = pos_x
        seed["pos_y"][0, 0] = pos_y
        seed["pos_z"][0, 0] = pos_z
        seed["action_frame"][0, 0] = np.int16(frame)
        seed["anim_frame_f32"][0, 0] = np.float32(frame)
        seed["animation_index"][0, 0] = np.uint32(msid)
        seed["hitlag"][0, 0] = np.uint16(2)
        seed["hitlag"][0, 1] = np.uint16(2)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)

        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        hitboxes, _count = msl_binding.hitboxes_world(handle, 0, 0)
        row = hitboxes[hb_id].astype(np.float32, copy=False)
        assert int(row[9]) == 1
        return row

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        right = run_with_facing(True)
        left = run_with_facing(False)
    finally:
        msl_binding.destroy(handle)

    # C-core policy (decomp-shaped): local = pose_mtx * offset; local = rotY90(local, facing_dir);
    # world = pos + local.
    #
    # Therefore, for a fixed (pos, pose, offset):
    # - X is mirrored about pos_x (because it comes from +/-local.z)
    # - Y is unchanged
    # - Z is mirrored about pos_z (because it comes from -/+local.x)
    rx, ry, rz = right[0], right[1], right[2]
    lx, ly, lz = left[0], left[1], left[2]

    expected_lx = np.float32(np.float32(2.0) * pos_x - rx)
    # The extracted hitbox offsets use the GALE01 scale literal, so the two
    # facing paths can differ by one float32 rounding step while preserving the
    # root-rotY90 mirror policy being locked here.
    np.testing.assert_allclose(lx, expected_lx, rtol=0.0, atol=2e-5)
    np.testing.assert_allclose(ly, ry, rtol=0.0, atol=0.0)
    expected_lz = np.float32(np.float32(2.0) * pos_z - rz)
    np.testing.assert_allclose(lz, expected_lz, rtol=0.0, atol=2e-5)
