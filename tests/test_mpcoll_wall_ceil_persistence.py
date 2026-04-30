from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.stage_metadata_helpers import fd_stage_segments

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_PASSIVE_WALL_JUMP = 0x00CB
SM_FALL = 20
CHAR_FALCO = 22
MSL_COLLIDE_RIGHT_WALL_PUSH = 0x00000040
MSL_COLLIDE_RIGHT_WALL_HUG = 0x00000800


def _ecb_side_y_offset_for_char_id(char_id: int) -> float:
    # ISO-extracted: data/characters/<char>.json `ecb_side_y_offset` (ftData_x44_t.unkC).
    if int(char_id) == 1:
        key = "fox"
    elif int(char_id) == 22:
        key = "falco"
    else:
        return 0.0
    d = json.loads(Path(f"data/characters/{key}.json").read_text())
    return float(d.get("ecb_side_y_offset", 0.0))


def _fd_pick_right_wall_segment() -> tuple[int, float, float, float, float]:
    # Prefer a deep (under-stage) right wall segment to avoid interacting with floor.
    walls = [
        seg
        for seg in fd_stage_segments()
        if seg.get("kind") == "right_wall" and not bool(seg.get("platform"))
    ]
    if not walls:
        raise AssertionError("no right_wall segments found in FD stage data")

    # Pick the segment with the smallest max(y0,y1) (most negative / deepest).
    seg = min(walls, key=lambda s: max(float(s["y0"]), float(s["y1"])))
    return (
        int(seg["i"]),
        float(seg["x0"]),
        float(seg["y0"]),
        float(seg["x1"]),
        float(seg["y1"]),
    )


def _fd_right_wall_segment_ids() -> set[int]:
    return {
        int(seg["i"])
        for seg in fd_stage_segments()
        if seg.get("kind") == "right_wall" and not bool(seg.get("platform"))
    }


def _fd_pick_horizontal_ceiling_segment() -> tuple[int, float, float, float, float]:
    ceils = [
        seg
        for seg in fd_stage_segments()
        if seg.get("kind") == "ceiling" and not bool(seg.get("platform"))
    ]
    if not ceils:
        raise AssertionError("no ceiling segments found in FD stage data")

    # Prefer a truly horizontal ceiling segment (|y0-y1| ~ 0).
    seg = min(ceils, key=lambda s: abs(float(s["y0"]) - float(s["y1"])))
    return (
        int(seg["i"]),
        float(seg["x0"]),
        float(seg["y0"]),
        float(seg["x1"]),
        float(seg["y1"]),
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
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
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
    # Decomp: ECB side-point Y is midpoint(bottom, top) plus ftData_x44_t.unkC.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
    # refs/melee/src/melee/ft/types.h::ftData_x44_t
    side_y = _ecb_side_y_offset_for_char_id(char_id) + 0.5 * (top_y + bottom_y)

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
    seed["facing"][0, 0] = np.uint8(1)  # right (matches ECB extents usage below)
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
        # Decomp: mpColl_80044E10_RightWall may enter through one swept candidate and
        # mpColl_800454A4_RightWall may report an adjacent right-wall envelope segment.
        assert int(c0["wall_id"][0]) in _fd_right_wall_segment_ids()
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
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
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


def test_wall_contact_triggers_on_ecb_side_crossing_not_root_on_fd() -> None:
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
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )
    assert int(CONTACTS_DTYPE.itemsize) == contacts_stride

    char_id = 1
    msid_wait = 2
    af = 0
    min_x, _max_x, _min_y, max_y = msl_binding.ecb_extents_rel(char_id, msid_wait, af)
    bottom_y = float(msl_binding.ecb_bottom_rel_y(char_id, msid_wait, af))
    top_y = float(max_y)
    left_x = float(min_x)
    side_y = _ecb_side_y_offset_for_char_id(char_id) + 0.5 * (top_y + bottom_y)

    wall_y_mid = 0.5 * (float(wy0) + float(wy1))
    wall_x_mid = 0.5 * (float(wx0) + float(wx1))
    eps = 0.05

    # Start with the ECB left side slightly to the right of the wall, then move left just enough to
    # cross with the ECB side point while keeping the fighter root on the right side.
    pos_x0 = wall_x_mid + eps - left_x
    pos_y0 = wall_y_mid - side_y

    # Choose a delta that crosses the wall at the ECB point (delta > eps) but cannot move the root
    # past the wall in a single frame (delta < |left_x| + eps).
    delta = min(2.0, max(2.0 * eps, 0.5 * abs(left_x)))
    if not (delta > eps):
        pytest.skip("ECB left extent too small to construct root-vs-ECB wall crossing")
    if not (delta < abs(left_x) + eps):
        pytest.skip("cannot guarantee root does not cross the wall for this ECB extent")

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(char_id)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(msid_wait)
    seed["action_frame"][0, 0] = np.int16(af)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["facing"][0, 0] = np.uint8(1)  # right (matches ECB extents usage below)
    seed["pos_x"][0, 0] = np.float32(pos_x0)
    seed["pos_y"][0, 0] = np.float32(pos_y0)
    seed["speed_air_x_self"][0, 0] = np.float32(-delta)
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

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0

        assert int(c0["wall_kind"][0]) != 0
        # Decomp: mpColl_80044E10_RightWall may enter through one swept candidate and
        # mpColl_800454A4_RightWall may report an adjacent right-wall envelope segment.
        assert int(c0["wall_id"][0]) in _fd_right_wall_segment_ids()
    finally:
        msl_binding.destroy(handle)


def test_bottom_wall_push_does_not_promote_common_air_walljump_on_fd() -> None:
    # Decomp: mpColl marks Collide_RightWallHug only when the ECB side point hits the wall; bottom
    # and top fallback hits mark Collide_RightWallPush. ftWallJump_8008169C checks the Hug bit, so
    # a bottom-only contact must not start the walljump input phase even with stick-away input.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044E10_RightWall
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    try:
        wall_i, wx0, wy0, wx1, wy1 = _fd_pick_right_wall_segment()
    except AssertionError as e:
        pytest.skip(str(e))

    contacts_dtype = np.dtype(
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
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )
    assert int(contacts_dtype.itemsize) == contacts_stride

    af = 0
    min_x, _max_x, _min_y, max_y = msl_binding.ecb_extents_rel(CHAR_FALCO, SM_FALL, af)
    bottom_y = float(msl_binding.ecb_bottom_rel_y(CHAR_FALCO, SM_FALL, af))
    side_y = _ecb_side_y_offset_for_char_id(CHAR_FALCO) + 0.5 * (float(max_y) + bottom_y)
    wall_x_mid = 0.5 * (float(wx0) + float(wx1))
    wall_y_mid = 0.5 * (float(wy0) + float(wy1))
    if abs(float(min_x)) < 0.5:
        pytest.skip("Falco Fall ECB left extent too small for bottom-only wall contact")
    if min(float(wy0), float(wy1)) <= wall_y_mid + (side_y - bottom_y) <= max(float(wy0), float(wy1)):
        pytest.skip("Falco Fall side point still overlaps selected wall segment")

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["ground_friction_mul"][0, :2] = np.float32(1.0)
    seed["char_id"][0, 0] = np.uint8(CHAR_FALCO)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["action_frame"][0, 0] = np.int16(af)
    seed["anim_frame_f32"][0, 0] = np.float32(float(af))
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(wall_x_mid + 0.05)
    seed["pos_y"][0, 0] = np.float32(wall_y_mid - bottom_y)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["jumps_left"][0, 0] = np.uint8(1)
    seed["tilt_timer_x"][0, 0] = np.uint8(0)
    seed["walljump_input_timer"][0, 0] = np.uint8(254)
    seed["walljump_wall_side_i8"][0, 0] = np.int8(0)

    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    input_t["p"]["main_x"][0, 0] = np.int8(80)
    prev_input = input_t.copy()
    input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
    prev_input_bytes = prev_input.view(np.uint8).reshape((1, input_stride))

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)
    finally:
        msl_binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    contacts = out_contacts.view(contacts_dtype).reshape((1,))[0]
    flags = int(contacts["coll_env_flags"][0])
    # Decomp: mpColl_800454A4_RightWall resolves the airborne ECB envelope after the bottom
    # candidate is found, so the reported segment can be an adjacent right-wall line.
    assert int(contacts["wall_id"][0]) in _fd_right_wall_segment_ids()
    assert flags & MSL_COLLIDE_RIGHT_WALL_PUSH
    assert (flags & MSL_COLLIDE_RIGHT_WALL_HUG) == 0
    assert int(out["action_id"][0]) != ACT_PASSIVE_WALL_JUMP


def test_ceiling_contact_triggers_on_ecb_top_crossing_not_root_on_fd() -> None:
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
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )
    assert int(CONTACTS_DTYPE.itemsize) == contacts_stride

    char_id = 1
    msid_wait = 2
    af = 0
    _min_x, _max_x, _min_y, max_y = msl_binding.ecb_extents_rel(char_id, msid_wait, af)
    top_y = float(max_y)

    ceil_y = float(cy0)
    ceil_x_mid = 0.5 * (float(cx0) + float(cx1))
    eps = 0.05

    # Start with ECB top slightly below the ceiling, then move up just enough to cross at the top
    # point while keeping the root strictly below the ceiling line.
    pos_x0 = ceil_x_mid
    pos_y0 = (ceil_y - top_y) - eps
    delta = min(3.0, max(2.0 * eps, 0.5 * top_y))
    if not (delta > eps):
        pytest.skip("ECB top extent too small to construct root-vs-ECB ceiling crossing")
    if not (delta < top_y + eps):
        pytest.skip("cannot guarantee root does not cross the ceiling for this ECB extent")

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
    seed["speed_y_self"][0, 0] = np.float32(delta)

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

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0

        assert int(c0["ceiling_id"][0]) == ceil_i
        assert float(c0["ceiling_normal_y"][0]) < 0.0
    finally:
        msl_binding.destroy(handle)
