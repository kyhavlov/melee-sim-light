from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tests.test_anim_pose import _MAT_BYTES, _pick_first_nonempty_anim, _read_header


def test_ecb_extents_rel_matches_ssanim01_tz_ty_joint_extrema() -> None:
    """Locks down the ECB extents axis convention used across extraction + C-core consumers.

    Contract:
    - extents X is extracted from SSANIM01 v3 matrix tz (float index 11)
    - extents Y is extracted from SSANIM01 v3 matrix ty (float index 7)
    over the 6 ECB joints listed in data/characters/<char>.json::ecb_joints.
    - returned extents are scaled by ftData/co_attrs model_scaling (data/characters/<char>.json::model_scaling)
    """
    import msl_binding

    anims = Path("data/anims/fox.bin")
    attrs = Path("data/characters/fox.json")
    assert anims.exists()
    assert attrs.exists()

    buf = anims.read_bytes()
    joint_count, anim_count, joint_parts = _read_header(buf)

    msid, frame_count, base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    assert frame_count > 0
    frame = 0

    d = json.loads(attrs.read_text())
    ecb_joints = d.get("ecb_joints")
    assert isinstance(ecb_joints, list) and len(ecb_joints) == 6

    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}
    ecb_joint_indices: list[int] = []
    for p in ecb_joints:
        assert isinstance(p, int)
        if p < 0:
            continue
        ji = part_to_joint_index.get(int(p))
        assert ji is not None, f"missing ecb_joint part_id={p} in SSANIM01 joint_parts"
        ecb_joint_indices.append(int(ji))
    assert ecb_joint_indices

    tz = np.empty((len(ecb_joint_indices),), dtype=np.float32)
    ty = np.empty((len(ecb_joint_indices),), dtype=np.float32)
    for i, ji in enumerate(ecb_joint_indices):
        off = base + frame * joint_count * _MAT_BYTES + ji * _MAT_BYTES
        m = np.frombuffer(buf, dtype="<f4", count=12, offset=off).astype(np.float32, copy=False)
        ty[i] = m[7]
        tz[i] = m[11]

    want_min_x = tz.min()
    want_max_x = tz.max()
    want_min_y = ty.min()
    want_max_y = ty.max()

    model_scaling = float(d.get("model_scaling", 1.0))
    want_min_x *= model_scaling
    want_max_x *= model_scaling
    want_min_y *= model_scaling
    want_max_y *= model_scaling

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride > 0

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        got_min_x, got_max_x, got_min_y, got_max_y = msl_binding.ecb_extents_rel(1, int(msid), int(frame))
    finally:
        msl_binding.destroy(handle)

    got = np.array([got_min_x, got_max_x, got_min_y, got_max_y], dtype=np.float32)
    want = np.array([want_min_x, want_max_x, want_min_y, want_max_y], dtype=np.float32)
    assert np.array_equal(got.view(np.uint32), want.view(np.uint32)), (int(msid), int(frame))
