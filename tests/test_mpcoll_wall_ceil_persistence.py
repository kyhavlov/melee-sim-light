from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E


def _fd_pick_right_wall_segment() -> tuple[int, float, float, float, float]:
    fd = json.loads(Path("data/stages/final_destination.json").read_text())
    unit_scale = float(fd.get("unit_scale", 1.0))
    # Prefer a deep (under-stage) right wall segment to avoid interacting with floor.
    walls = [
        seg
        for seg in fd["segments"]
        if seg.get("kind") == "right_wall" and not bool(seg.get("platform"))
    ]
    if not walls:
        raise AssertionError("no right_wall segments found in FD stage data")

    # Pick the segment with the smallest max(y0,y1) (most negative / deepest).
    seg = min(walls, key=lambda s: max(float(s["y0"]), float(s["y1"])))
    return (
        int(seg["i"]),
        unit_scale * float(seg["x0"]),
        unit_scale * float(seg["y0"]),
        unit_scale * float(seg["x1"]),
        unit_scale * float(seg["y1"]),
    )


def _fd_pick_horizontal_ceiling_segment() -> tuple[int, float, float, float, float]:
    fd = json.loads(Path("data/stages/final_destination.json").read_text())
    unit_scale = float(fd.get("unit_scale", 1.0))
    ceils = [
        seg
        for seg in fd["segments"]
        if seg.get("kind") == "ceiling" and not bool(seg.get("platform"))
    ]
    if not ceils:
        raise AssertionError("no ceiling segments found in FD stage data")

    # Prefer a truly horizontal ceiling segment (|y0-y1| ~ 0).
    seg = min(ceils, key=lambda s: abs(float(s["y0"]) - float(s["y1"])))
    return (
        int(seg["i"]),
        unit_scale * float(seg["x0"]),
        unit_scale * float(seg["y0"]),
        unit_scale * float(seg["x1"]),
        unit_scale * float(seg["y1"]),
    )


def test_wall_contact_persists_across_frames_on_fd() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    contacts_stride = int(sizes["collision_contacts"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    try:
        wall_i, wx0, wy0, wx1, wy1 = _fd_pick_right_wall_segment()
    except AssertionError as e:
        pytest.skip(str(e))

    # Packed view of MslDebugCollisionContacts (struct is packed; align=False).
    CONTACTS_DTYPE = np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
        ],
        align=False,
    )
    assert int(CONTACTS_DTYPE.itemsize) == contacts_stride

    # Choose Fox Wait so ECB tables are available and deterministic.
    char_id = 1
    msid_wait = 2
    af = 0
    min_x, max_x, _min_y, max_y = msl_binding.ecb_extents_rel(char_id, msid_wait, af)
    bottom_y = float(msl_binding.ecb_bottom_rel_y(char_id, msid_wait, af))
    top_y = float(max_y)
    # Facing right: left=min_x, right=max_x.
    left_x = float(min_x)
    # Side point uses midpoint_y between top and bottom (mpColl_80042384).
    side_y = 0.5 * (top_y + bottom_y)

    # Place the fighter so their left ECB side crosses a right-wall segment while moving left.
    # Use the segment midpoint in Y to stay inside bounds with small gravity drift.
    wall_y_mid = 0.5 * (float(wy0) + float(wy1))
    # For right wall collision, we hit using the fighter's left side: cur_lx = pos_x + left_x.
    wall_x_mid = 0.5 * (float(wx0) + float(wx1))
    eps = 0.05
    pos_x0 = wall_x_mid + eps - left_x
    pos_y0 = wall_y_mid - side_y

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(char_id)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(msid_wait)
    seed["action_frame"][0, 0] = np.int16(af)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(pos_x0)
    seed["pos_y"][0, 0] = np.float32(pos_y0)
    # Move left into the right wall for multiple frames.
    seed["speed_air_x_self"][0, 0] = np.float32(-2.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        msl_binding.alloc_reset()
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)
        c0 = out_contacts.view(CONTACTS_DTYPE).reshape((1,))[0]

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)
        c1 = out_contacts.view(CONTACTS_DTYPE).reshape((1,))[0]

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0

        assert int(c0["wall_kind"][0]) != 0
        assert int(c0["wall_id"][0]) == wall_i
        assert int(c1["wall_kind"][0]) != 0
        assert int(c1["wall_id"][0]) == wall_i
    finally:
        msl_binding.destroy(handle)


def test_ceiling_contact_persists_across_frames_on_fd() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    contacts_stride = int(sizes["collision_contacts"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    try:
        ceil_i, cx0, cy0, cx1, cy1 = _fd_pick_horizontal_ceiling_segment()
    except AssertionError as e:
        pytest.skip(str(e))

    if abs(float(cy0) - float(cy1)) > 1e-3:
        pytest.skip("FD ceiling has no near-horizontal segment to test")

    CONTACTS_DTYPE = np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
        ],
        align=False,
    )
    assert int(CONTACTS_DTYPE.itemsize) == contacts_stride

    char_id = 1
    msid_wait = 2
    af = 0
    _min_x, _max_x, _min_y, max_y = msl_binding.ecb_extents_rel(char_id, msid_wait, af)
    top_y = float(max_y)

    # Place fighter under the horizontal ceiling and move upward into it.
    # Use x midpoint of the segment and start slightly below so we intersect on the first frame.
    ceil_y = float(cy0)
    ceil_x_mid = 0.5 * (float(cx0) + float(cx1))
    eps = 0.05
    pos_x0 = ceil_x_mid
    pos_y0 = (ceil_y - top_y) - eps

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(char_id)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(msid_wait)
    seed["action_frame"][0, 0] = np.int16(af)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(pos_x0)
    seed["pos_y"][0, 0] = np.float32(pos_y0)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(3.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        msl_binding.alloc_reset()
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)
        c0 = out_contacts.view(CONTACTS_DTYPE).reshape((1,))[0]

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)
        c1 = out_contacts.view(CONTACTS_DTYPE).reshape((1,))[0]

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0

        assert int(c0["ceiling_id"][0]) == ceil_i
        assert int(c1["ceiling_id"][0]) == ceil_i
        # Ceiling normal should point down (ny < 0) for the horizontal underside.
        assert float(c0["ceiling_normal_y"][0]) < 0.0
        assert float(c1["ceiling_normal_y"][0]) < 0.0
    finally:
        msl_binding.destroy(handle)

