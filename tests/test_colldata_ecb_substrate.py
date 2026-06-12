from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_DAMAGE_FLY_N = 0x0058
ACT_ATTACK_11 = 0x002C
ACT_ESCAPE_AIR = 0x00EC
ACT_GUARD = 0x00B3
ACT_PASS = 0x00F4
ACT_ATTACK_AIR_N = 0x0041
ACT_FX_SPECIAL_HI_FALL = 0x0166

SM_WAIT1_0 = 2
SM_FALL = 20
SM_ATTACK_11 = 46
SM_ESCAPE_AIR = 44
SM_FX_SPECIAL_HI_FALL = 311

CHAR_FOX = 1
STAGE_FD = 32
STAGE_FOD = 2
STAGE_BATTLEFIELD = 31
STAGE_YOSHI = 8

BUTTON_L = 0x0040
BUTTON_A = 0x0100

FLOOR_RESULT_NONE = 0
FLOOR_RESULT_DIRECT = 1
FLOOR_RESULT_GROUNDED_4A908_RETRY = 2
FLOOR_MODE_BOTTOM_SWEEP = 1
FLOOR_MODE_ROOT_PROJECTION = 2
FLOOR_MODE_EDGE_SNAP = 3
FLOOR_MODE_STAGE_OBJECT_CARRY = 4
FLOOR_MODE_4A908_RETRY = 5
FLOOR_MODE_DIRECT_PUBLICATION = 7
MSL_COLLIDE_CEILING_PUSH = 0x00002000
MSL_COLLIDE_CEILING_HUG = 0x00004000
MSL_COLLIDE_LEFT_EDGE = 0x00100000
MSL_COLLIDE_RIGHT_EDGE = 0x00200000
MSL_COLLIDE_EDGE = 0x00800000


def _input_bytes() -> np.ndarray:
    import msl_binding

    return np.zeros((1, int(msl_binding.sizes()["input"])), dtype=np.uint8)


def _seed_base(stage_id: int, action_id: int, submotion_id: int, x: float, y: float) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(submotion_id)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["floor_skip_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["floor_skip_segment_valid_u8"][0, :] = np.uint8(0)
    seed["anim_frame_f32"][0, :2] = seed["action_frame"][0, :2].astype(np.float32)
    return seed


def _colldata_ecb_dtype() -> np.dtype:
    return np.dtype(
        [
            ("current_valid", ("u1", (4,))),
            ("prev_valid", ("u1", (4,))),
            ("desired_valid", ("u1", (4,))),
            ("desired_locked_owner", ("u1", (4,))),
            ("floor_result_valid", ("u1", (4,))),
            ("floor_result_source", ("u1", (4,))),
            ("floor_result_mode", ("u1", (4,))),
            ("damage_hitlag_floor_contact_runtime", ("u1", (4,))),
            ("escapeair_floor_producer_runtime", ("u1", (4,))),
            ("floor_probe_valid", ("u1", (4,))),
            ("floor_probe_owner", ("u1", (4,))),
            ("floor_probe_reject_reason", ("u1", (4,))),
            ("floor_probe_raw_bottom_sweep_hit", ("u1", (4,))),
            ("floor_probe_projection_hit", ("u1", (4,))),
            ("floor_probe_carried_source_owned", ("u1", (4,))),
            ("floor_probe_carried_runtime_owned", ("u1", (4,))),
            ("floor_sweep_prev_source_owned", ("u1", (4,))),
            ("floor_sweep_prev_runtime_owned", ("u1", (4,))),
            ("damage_allow_sdi", ("u1", (4,))),
            ("damage_hitlag_downward_sdi_consumed", ("u1", (4,))),
            ("tilt_timer_y_frame_start", ("u1", (4,))),
            ("tilt_timer_y", ("u1", (4,))),
            ("ledge_side", ("i1", (4,))),
            ("ledge_cooldown", ("u1", (4,))),
            ("cliff_ledge_floor_segment_seeded", ("u1", (4,))),
            ("specialhi_rotate_model_valid", ("u1", (4,))),
            ("specialhi_rotate_model_action", ("u1", (4,))),
            ("specialhi_collision_ecb_valid", ("u1", (4,))),
            ("floor_probe_reject_bits", ("<u8", (4,))),
            ("floor_probe_source_phases", ("<u4", (4,))),
            ("floor_result_segment_id", ("<u2", (4,))),
            ("cliff_ledge_floor_segment_id", ("<u2", (4,))),
            ("floor_probe_carried_segment_id", ("<u2", (4,))),
            ("floor_probe_candidate_segment_id", ("<u2", (4,))),
            ("floor_probe_projected_segment_id", ("<u2", (4,))),
            ("floor_probe_candidate_line_idx", ("<i2", (4,))),
            ("floor_probe_projected_line_idx", ("<i2", (4,))),
            ("floor_skip_segment_id", ("<u2", (4,))),
            ("floor_skip_valid", ("u1", (4,))),
            ("is_on_platform", ("u1", (4,))),
            ("floor_speed_valid", ("u1", (4,))),
            ("left_wall_speed_valid", ("u1", (4,))),
            ("right_wall_speed_valid", ("u1", (4,))),
            ("ceiling_speed_valid", ("u1", (4,))),
            ("squeeze_restore_valid", ("u1", (4,))),
            ("joint_id_skip", ("<i2", (4,))),
            ("joint_id_only", ("<i2", (4,))),
            ("current_bottom_rel_y", ("<f4", (4,))),
            ("current_top_rel_y", ("<f4", (4,))),
            ("current_left_rel_x", ("<f4", (4,))),
            ("current_right_rel_x", ("<f4", (4,))),
            ("current_side_rel_y", ("<f4", (4,))),
            ("prev_bottom_rel_y", ("<f4", (4,))),
            ("prev_top_rel_y", ("<f4", (4,))),
            ("prev_left_rel_x", ("<f4", (4,))),
            ("prev_right_rel_x", ("<f4", (4,))),
            ("prev_side_rel_y", ("<f4", (4,))),
            ("squeeze_restore_bottom_rel_y", ("<f4", (4,))),
            ("squeeze_restore_top_rel_y", ("<f4", (4,))),
            ("squeeze_restore_left_rel_x", ("<f4", (4,))),
            ("squeeze_restore_right_rel_x", ("<f4", (4,))),
            ("squeeze_restore_side_rel_y", ("<f4", (4,))),
            ("desired_bottom_rel_y", ("<f4", (4,))),
            ("desired_top_rel_y", ("<f4", (4,))),
            ("desired_left_rel_x", ("<f4", (4,))),
            ("desired_right_rel_x", ("<f4", (4,))),
            ("desired_side_rel_y", ("<f4", (4,))),
            ("floor_result_contact_x", ("<f4", (4,))),
            ("floor_result_contact_y", ("<f4", (4,))),
            ("floor_result_normal_x", ("<f4", (4,))),
            ("floor_result_normal_y", ("<f4", (4,))),
            ("floor_probe_prev_bottom_x", ("<f4", (4,))),
            ("floor_probe_prev_bottom_y", ("<f4", (4,))),
            ("floor_probe_cur_bottom_x", ("<f4", (4,))),
            ("floor_probe_cur_bottom_y", ("<f4", (4,))),
            ("substep_prev_pos_x", ("<f4", (4,))),
            ("substep_prev_pos_y", ("<f4", (4,))),
            ("substep_cur_pos_x", ("<f4", (4,))),
            ("substep_cur_pos_y", ("<f4", (4,))),
            ("floor_sweep_prev_pos_x", ("<f4", (4,))),
            ("floor_sweep_prev_pos_y", ("<f4", (4,))),
            ("last_pos_x", ("<f4", (4,))),
            ("last_pos_y", ("<f4", (4,))),
            ("specialhi_rotate_model", ("<f4", (4,))),
            ("specialhi_ecb_bottom_x", ("<f4", (4,))),
            ("specialhi_ecb_bottom_y", ("<f4", (4,))),
            ("specialhi_ecb_top_x", ("<f4", (4,))),
            ("specialhi_ecb_top_y", ("<f4", (4,))),
            ("specialhi_ecb_left_x", ("<f4", (4,))),
            ("specialhi_ecb_left_y", ("<f4", (4,))),
            ("specialhi_ecb_right_x", ("<f4", (4,))),
            ("specialhi_ecb_right_y", ("<f4", (4,))),
            ("floor_speed_x", ("<f4", (4,))),
            ("floor_speed_y", ("<f4", (4,))),
            ("left_wall_speed_x", ("<f4", (4,))),
            ("left_wall_speed_y", ("<f4", (4,))),
            ("right_wall_speed_x", ("<f4", (4,))),
            ("right_wall_speed_y", ("<f4", (4,))),
            ("ceiling_speed_x", ("<f4", (4,))),
            ("ceiling_speed_y", ("<f4", (4,))),
            ("wall_probe_corr_x", ("<f4", (4,))),
            ("wall_probe_segment_id", ("<i2", (4,))),
            ("wall_probe_valid", ("u1", (4,))),
            ("wall_probe_side", ("u1", (4,))),
            ("wall_probe_commit_kind", ("u1", (4,))),
            ("wall_probe_candidate_count", ("u1", (4,))),
        ],
        align=False,
    )


def _collision_contacts_dtype() -> np.dtype:
    return np.dtype(
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


def _ecb_rel_points(msid: int, frame: int, facing: int = 1, lock_bottom: bool = False) -> dict[str, float]:
    import msl_binding

    min_x, max_x, _min_y, max_y = msl_binding.ecb_extents_rel(CHAR_FOX, msid, frame)
    bottom = 0.0 if lock_bottom else float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, msid, frame))
    lx = (1.0 if facing else -1.0) * float(min_x)
    rx = (1.0 if facing else -1.0) * float(max_x)
    left, right = (lx, rx) if lx <= rx else (rx, lx)
    top = float(max_y)
    return {
        "bottom": bottom,
        "top": top,
        "left": left,
        "right": right,
        "side": 0.5 * (top + bottom),
    }


def _fox_ecb_bottom_rel_y(msid: int, frame: int) -> float:
    import msl_binding

    return float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, msid, frame))


def test_mp_coll_check_bounding_uses_swept_current_and_prev_ecb() -> None:
    import msl_binding

    # refs/melee/src/melee/mp/mpcoll.c::mpCollCheckBounding
    msl_binding.alloc_reset()
    aabb = msl_binding.mpcoll_check_bounding_aabb(
        0.0,
        0.0,
        20.0,
        -2.0,
        -4.0,
        4.0,
        0.0,
        8.0,
        -5.0,
        5.0,
        0.0,
        7.0,
        0,
        6.0,
        3.0,
        20.0,
    )

    assert float(aabb["left"]) == pytest.approx(-4.0)
    assert float(aabb["right"]) == pytest.approx(25.0)
    assert float(aabb["bottom"]) == pytest.approx(-2.0)
    assert float(aabb["top"]) == pytest.approx(8.0)
    stats = msl_binding.alloc_stats()
    assert int(stats["calls"]) == 0
    assert int(stats["bytes"]) == 0


def test_mp_coll_check_bounding_ledge_flag_expands_source_snap_envelope() -> None:
    import msl_binding

    # `flags & 0b100` adds ledge-snap horizontal reach and vertical snap-height bounds from both
    # current and previous CollData roots.
    # refs/melee/src/melee/mp/mpcoll.c::mpCollCheckBounding
    base = msl_binding.mpcoll_check_bounding_aabb(
        0.0,
        0.0,
        20.0,
        -2.0,
        -4.0,
        4.0,
        0.0,
        8.0,
        -5.0,
        5.0,
        0.0,
        7.0,
        0,
        6.0,
        3.0,
        20.0,
    )
    ledge = msl_binding.mpcoll_check_bounding_aabb(
        0.0,
        0.0,
        20.0,
        -2.0,
        -4.0,
        4.0,
        0.0,
        8.0,
        -5.0,
        5.0,
        0.0,
        7.0,
        0x4,
        6.0,
        3.0,
        20.0,
    )

    assert float(ledge["left"]) == pytest.approx(float(base["left"]) - 6.0)
    assert float(ledge["right"]) == pytest.approx(float(base["right"]) + 6.0)
    assert float(ledge["bottom"]) == pytest.approx(-9.0)
    assert float(ledge["top"]) == pytest.approx(13.0)


def test_mp_coll_end_static_publication_boundary_matches_source_env_bits() -> None:
    import msl_binding

    # `mpCollEnd` routes floor finalization when forced by the wrapper or edge env bits, and routes
    # ceiling finalization when ceiling hug/push bits are set. Dynamic Ground callbacks remain
    # outside this static Phase 2 helper.
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollEnd,mpCollEnd_inline,mpCollEnd_inline2}
    msl_binding.alloc_reset()
    forced = msl_binding.mpcoll_end_publication(2, 0xFFFF, 0, True, True, 12.5, 10.0)
    stats = msl_binding.alloc_stats()
    assert int(stats["calls"]) == 0
    assert int(stats["bytes"]) == 0
    assert int(forced["floor_callback"]) == 1
    assert int(forced["floor_callback_arg"]) == 1
    assert int(forced["ceiling_callback"]) == 0
    assert float(forced["dy"]) == pytest.approx(2.5)

    edge = msl_binding.mpcoll_end_publication(3, 0xFFFF, MSL_COLLIDE_LEFT_EDGE, False, False, 8.0, 9.5)
    assert int(edge["floor_callback"]) == 1
    assert int(edge["floor_callback_arg"]) == 2
    assert float(edge["dy"]) == pytest.approx(-1.5)

    ceiling = msl_binding.mpcoll_end_publication(
        0xFFFF, 7, MSL_COLLIDE_CEILING_PUSH | MSL_COLLIDE_RIGHT_EDGE, False, False, 1.0, -2.0
    )
    assert int(ceiling["floor_callback"]) == 0
    assert int(ceiling["ceiling_callback"]) == 1
    assert int(ceiling["ceiling_segment_id"]) == 7

    assert int(msl_binding.mpcoll_end_publication(2, 0xFFFF, 0, False, False, 0.0, 0.0)["floor_callback"]) == 0
    assert (
        int(
            msl_binding.mpcoll_end_publication(
                0xFFFF, 0xFFFF, MSL_COLLIDE_LEFT_EDGE, False, False, 0.0, 0.0
            )["floor_callback"]
        )
        == 0
    )
    for edge_bit in (MSL_COLLIDE_EDGE, MSL_COLLIDE_LEFT_EDGE, MSL_COLLIDE_RIGHT_EDGE):
        assert int(msl_binding.mpcoll_end_publication(2, 0xFFFF, edge_bit, False, False, 0.0, 0.0)["floor_callback"]) == 1
    for ceiling_bit in (MSL_COLLIDE_CEILING_PUSH, MSL_COLLIDE_CEILING_HUG):
        assert int(msl_binding.mpcoll_end_publication(0xFFFF, 4, ceiling_bit, False, False, 0.0, 0.0)["ceiling_callback"]) == 1
    assert (
        int(
            msl_binding.mpcoll_end_publication(
                0xFFFF, 0xFFFF, MSL_COLLIDE_CEILING_HUG, False, False, 0.0, 0.0
            )["ceiling_callback"]
        )
        == 0
    )


def _read_colldata_after_reseed(seed: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == dtype.itemsize
    out = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.debug_write_colldata_ecb(handle, out)
        return out.view(dtype).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _step_with_colldata(seed: np.ndarray) -> tuple[np.void, np.void]:
    import msl_binding

    sizes = msl_binding.sizes()
    colldata_dtype = _colldata_ecb_dtype()
    compare = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
    colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, _input_bytes(), _input_bytes())
        msl_binding.write_compare(handle, compare)
        msl_binding.debug_write_colldata_ecb(handle, colldata)
        return (
            compare.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
            colldata.view(colldata_dtype).reshape((1,))[0].copy(),
        )
    finally:
        msl_binding.destroy(handle)


def _read_colldata(handle: object) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == dtype.itemsize
    colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
    msl_binding.debug_write_colldata_ecb(handle, colldata)
    return colldata.view(dtype).reshape((1,))[0].copy()


def test_reseed_initializes_current_prev_desired_ecb_from_current_and_prev_pose() -> None:
    seed = _seed_base(STAGE_FD, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 0.0, 45.0)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(7)

    snap = _read_colldata_after_reseed(seed)
    prev_expected = _ecb_rel_points(SM_FALL, 7)
    desired_expected = _ecb_rel_points(SM_ESCAPE_AIR, 4)

    assert int(snap["current_valid"][0]) == 1
    assert int(snap["prev_valid"][0]) == 1
    assert int(snap["desired_valid"][0]) == 1
    for prefix in ("current", "prev"):
        assert float(snap[f"{prefix}_bottom_rel_y"][0]) == pytest.approx(prev_expected["bottom"])
        assert float(snap[f"{prefix}_top_rel_y"][0]) == pytest.approx(prev_expected["top"])
        assert float(snap[f"{prefix}_left_rel_x"][0]) == pytest.approx(prev_expected["left"])
        assert float(snap[f"{prefix}_right_rel_x"][0]) == pytest.approx(prev_expected["right"])
        assert float(snap[f"{prefix}_side_rel_y"][0]) == pytest.approx(prev_expected["side"])
    assert float(snap["desired_bottom_rel_y"][0]) == pytest.approx(desired_expected["bottom"])
    assert float(snap["desired_top_rel_y"][0]) == pytest.approx(desired_expected["top"])
    assert float(snap["desired_left_rel_x"][0]) == pytest.approx(desired_expected["left"])
    assert float(snap["desired_right_rel_x"][0]) == pytest.approx(desired_expected["right"])
    assert float(snap["desired_side_rel_y"][0]) == pytest.approx(desired_expected["side"])


def test_reseed_ecb_facing_mirrors_left_right_without_changing_vertical_points() -> None:
    seed_right = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, 45.0)
    seed_left = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, 45.0)
    seed_right["action_frame"][0, 0] = np.int16(6)
    seed_left["action_frame"][0, 0] = np.int16(6)
    seed_right["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed_left["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed_left["facing"][0, 0] = np.uint8(0)

    right = _read_colldata_after_reseed(seed_right)
    left = _read_colldata_after_reseed(seed_left)

    assert float(left["desired_left_rel_x"][0]) == pytest.approx(
        -float(right["desired_right_rel_x"][0])
    )
    assert float(left["desired_right_rel_x"][0]) == pytest.approx(
        -float(right["desired_left_rel_x"][0])
    )
    assert float(left["desired_bottom_rel_y"][0]) == pytest.approx(
        float(right["desired_bottom_rel_y"][0])
    )
    assert float(left["desired_top_rel_y"][0]) == pytest.approx(float(right["desired_top_rel_y"][0]))
    assert float(left["desired_side_rel_y"][0]) == pytest.approx(
        float(right["desired_side_rel_y"][0])
    )


def test_reseed_locked_bottom_applies_to_desired_not_seeded_previous_ecb() -> None:
    import msl_binding

    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, 45.0)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(5)
    seed["ecb_lock_timer"][0, 0] = np.uint8(5)

    snap = _read_colldata_after_reseed(seed)

    assert float(snap["desired_bottom_rel_y"][0]) == pytest.approx(0.0)
    assert float(snap["current_bottom_rel_y"][0]) == pytest.approx(
        float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, SM_FALL, 5))
    )


def test_reseed_locked_desired_ecb_bottom_lane_preserves_source_bottom() -> None:
    import msl_binding

    seed = _seed_base(STAGE_FD, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 0.0, 45.0)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(8)
    preserved = float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, SM_ESCAPE_AIR, 6))
    seed["ecb_lock_bottom_rel_y_f32"][0, 0] = np.float32(preserved)
    seed["ecb_lock_bottom_rel_y_valid_u8"][0, 0] = np.uint8(1)

    snap = _read_colldata_after_reseed(seed)

    assert float(snap["desired_bottom_rel_y"][0]) == pytest.approx(preserved)
    assert float(snap["desired_top_rel_y"][0]) == pytest.approx(
        _ecb_rel_points(SM_ESCAPE_AIR, 2)["top"]
    )
    assert float(snap["current_bottom_rel_y"][0]) == pytest.approx(
        float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, SM_ESCAPE_AIR, 1))
    )


def test_reseed_floor_skip_debug_exposes_only_source_platform_skip() -> None:
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FALL, SM_FALL, -40.0, 45.0)
    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(2)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)

    snap = _read_colldata_after_reseed(seed)

    assert int(snap["floor_skip_valid"][0]) == 1
    assert int(snap["floor_skip_segment_id"][0]) == 2

    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(1)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)
    snap_nonplatform = _read_colldata_after_reseed(seed)

    assert int(snap_nonplatform["floor_skip_valid"][0]) == 0
    assert int(snap_nonplatform["floor_skip_segment_id"][0]) == 0xFFFF


def test_static_mpcoll_platform_and_floor_speed_helpers_use_coll_surface() -> None:
    # Source shape:
    # - mpColl_IsOnPlatform reads LINE_FLAG_PLATFORM from CollData.floor.index.
    # - mpCollGetSpeedFloor calls mpGetSpeed on CollData.floor.index. Static legal-stage floors have
    #   no endpoint delta, so they return valid zero speed.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_IsOnPlatform,mpCollGetSpeedFloor}
    # refs/melee/src/melee/mp/mplib.c::{mpLineGetFlags,mpGetSpeed}
    import msl_binding

    platform = msl_binding.stage_floor_segment(STAGE_BATTLEFIELD, 2)
    hard_floor = msl_binding.stage_floor_segment(STAGE_FD, 0)
    assert platform is not None and int(platform["is_platform"]) == 1
    assert hard_floor is not None and int(hard_floor["is_platform"]) == 0

    seed_platform = _seed_base(STAGE_BATTLEFIELD, ACT_WAIT, SM_WAIT1_0, -40.0, 27.2)
    seed_platform["on_ground"][0, 0] = np.uint8(1)
    seed_platform["ground_id"][0, 0] = np.uint16(2)
    snap_platform = _read_colldata_after_reseed(seed_platform)

    assert int(snap_platform["is_on_platform"][0]) == 1
    assert int(snap_platform["floor_speed_valid"][0]) == 1
    assert float(snap_platform["floor_speed_x"][0]) == pytest.approx(0.0)
    assert float(snap_platform["floor_speed_y"][0]) == pytest.approx(0.0)

    seed_hard = _seed_base(STAGE_FD, ACT_WAIT, SM_WAIT1_0, 0.0, 0.0)
    seed_hard["on_ground"][0, 0] = np.uint8(1)
    seed_hard["ground_id"][0, 0] = np.uint16(0)
    snap_hard = _read_colldata_after_reseed(seed_hard)

    assert int(snap_hard["is_on_platform"][0]) == 0
    assert int(snap_hard["floor_speed_valid"][0]) == 1
    assert float(snap_hard["floor_speed_x"][0]) == pytest.approx(0.0)
    assert float(snap_hard["floor_speed_y"][0]) == pytest.approx(0.0)


def test_moving_mpcoll_floor_speed_helper_uses_shared_surface_packet() -> None:
    # Dynamic mpCollGetSpeedFloor debug state must read the same FoD/Randall surface packet as
    # runtime support carry, not the old static-only zero-speed substrate.
    height = np.float32(17.100000381469727)
    velocity = np.float32(-0.1)
    fod_seed = _seed_base(STAGE_FOD, ACT_WAIT, SM_WAIT1_0, -35.0, 1.125 + float(height) * 0.75)
    fod_seed["on_ground"][0, 0] = np.uint8(1)
    fod_seed["ground_id"][0, 0] = np.uint16(0)
    fod_seed["stage_fod_platform_height_f32"][0, 1] = height
    fod_seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    fod_seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    fod_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    fod_seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(4)
    fod_snap = _read_colldata_after_reseed(fod_seed)

    assert int(fod_snap["floor_speed_valid"][0]) == 1
    assert float(fod_snap["floor_speed_x"][0]) == pytest.approx(0.0)
    assert float(fod_snap["floor_speed_y"][0]) == pytest.approx(float(velocity) * 0.75, abs=1e-6)

    randall_seed = _seed_base(STAGE_YOSHI, ACT_WAIT, SM_WAIT1_0, 95.0, -13.64989 + 0.0001)
    randall_seed["frame_id"][0] = np.int32(477)
    randall_seed["on_ground"][0, 0] = np.uint8(1)
    randall_seed["ground_id"][0, 0] = np.uint16(1000)
    randall_snap = _read_colldata_after_reseed(randall_seed)

    assert int(randall_snap["floor_speed_valid"][0]) == 1
    assert abs(float(randall_snap["floor_speed_x"][0])) > 0.01


def test_mp_coll_interpolate_promotion_copies_current_to_prev_then_desired_to_current() -> None:
    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, 80.0)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(5)

    before = _read_colldata_after_reseed(seed)
    _compare, after = _step_with_colldata(seed)

    assert float(after["prev_bottom_rel_y"][0]) == pytest.approx(float(before["current_bottom_rel_y"][0]))
    assert float(after["prev_top_rel_y"][0]) == pytest.approx(float(before["current_top_rel_y"][0]))
    assert float(after["prev_left_rel_x"][0]) == pytest.approx(float(before["current_left_rel_x"][0]))
    assert float(after["prev_right_rel_x"][0]) == pytest.approx(float(before["current_right_rel_x"][0]))
    assert float(after["prev_side_rel_y"][0]) == pytest.approx(float(before["current_side_rel_y"][0]))
    assert float(after["current_bottom_rel_y"][0]) == pytest.approx(
        float(after["desired_bottom_rel_y"][0])
    )
    assert float(after["current_top_rel_y"][0]) == pytest.approx(float(after["desired_top_rel_y"][0]))
    assert float(after["current_left_rel_x"][0]) == pytest.approx(
        float(after["desired_left_rel_x"][0])
    )
    assert float(after["current_right_rel_x"][0]) == pytest.approx(
        float(after["desired_right_rel_x"][0])
    )
    assert float(after["current_side_rel_y"][0]) == pytest.approx(
        float(after["desired_side_rel_y"][0])
    )


def test_mp_coll_interpolate_multisubstep_prev_ecb_is_penultimate_step() -> None:
    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, 80.0)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(5)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(96.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    before = _read_colldata_after_reseed(seed)
    compare, after = _step_with_colldata(seed)

    max_delta = abs(float(compare["pos_y"][0]) - 96.0)
    for field in ("bottom_rel_y", "top_rel_y", "left_rel_x", "right_rel_x", "side_rel_y"):
        max_delta = max(
            max_delta,
            abs(float(after[f"current_{field}"][0]) - float(before[f"current_{field}"][0])),
        )
    steps = int(max_delta / 6.0) + 1 if max_delta > 6.0 else 1
    assert steps > 1
    factor = float(steps - 1) / float(steps)

    for field in ("bottom_rel_y", "top_rel_y", "left_rel_x", "right_rel_x", "side_rel_y"):
        initial = float(before[f"current_{field}"][0])
        current = float(after[f"current_{field}"][0])
        expected_prev = initial + (current - initial) * factor
        assert float(after[f"prev_{field}"][0]) == pytest.approx(expected_prev)


def test_callback_local_floor_result_is_stable_for_fd_hard_floor() -> None:
    bottom = _fox_ecb_bottom_rel_y(SM_FALL, 0)
    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, -bottom + 0.10)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-bottom + 2.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_BOTTOM_SWEEP
    assert int(snap["floor_result_segment_id"][0]) == int(compare["ground_id"][0])
    assert float(snap["floor_result_contact_y"][0]) == pytest.approx(
        float(compare["pos_y"][0]) - 0.0001,
        abs=1e-5,
    )
    assert float(snap["substep_prev_pos_y"][0]) == pytest.approx(-bottom + 2.0)


def test_callback_local_floor_result_is_stable_for_platform_floor() -> None:
    import msl_binding

    platform = msl_binding.stage_floor_segment(STAGE_BATTLEFIELD, 2)
    platform_y = float(platform["y0"])
    bottom = _fox_ecb_bottom_rel_y(SM_FALL, 0)
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FALL, SM_FALL, -40.0, platform_y - bottom + 0.10)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(platform_y - bottom + 2.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(compare["ground_id"][0]) == 2
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_BOTTOM_SWEEP
    assert int(snap["floor_result_segment_id"][0]) == 2
    assert float(snap["floor_result_contact_y"][0]) == pytest.approx(
        float(compare["pos_y"][0]) - 0.0001,
        abs=1e-5,
    )


def test_callback_local_floor_result_records_direct_publication_mode() -> None:
    seed = _seed_base(STAGE_FD, ACT_WAIT, SM_WAIT1_0, 0.0, 0.05)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_DIRECT_PUBLICATION
    assert int(snap["floor_result_segment_id"][0]) == int(compare["ground_id"][0])


def test_callback_local_floor_result_records_edge_snap_mode() -> None:
    seed = _seed_base(STAGE_FD, ACT_ATTACK_11, SM_ATTACK_11, 85.0, 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_EDGE_SNAP
    assert int(snap["floor_result_segment_id"][0]) == int(compare["ground_id"][0])
    assert float(snap["floor_result_contact_x"][0]) == pytest.approx(75.0, abs=1e-6)


def test_callback_local_floor_result_records_root_projection_mode() -> None:
    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 0.0, -0.2)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_N)
    seed["seed_prev_action_frame"][0, 0] = np.int16(3)
    seed["speed_y_self"][0, 0] = np.float32(-0.1)
    seed["speed_y_attack"][0, 0] = np.float32(-1.0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-0.15)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_ROOT_PROJECTION
    assert int(snap["floor_result_segment_id"][0]) == int(compare["ground_id"][0])


def test_callback_local_floor_result_records_stage_object_carry_mode() -> None:
    seed = _seed_base(STAGE_YOSHI, ACT_WAIT, SM_WAIT1_0, -80.0, -13.649894)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-79.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-13.649894)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(compare["ground_id"][0]) == 0
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_DIRECT
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_STAGE_OBJECT_CARRY
    assert int(snap["floor_result_segment_id"][0]) == 0


def test_callback_local_4a908_retry_result_is_retained_consumer() -> None:
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_WAIT, SM_WAIT1_0, 30.0, -6.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(30.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)

    assert int(compare["on_ground"][0]) == 1
    assert int(compare["ground_id"][0]) == 1
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_GROUNDED_4A908_RETRY
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_4A908_RETRY
    assert int(snap["floor_result_segment_id"][0]) == 1
    assert float(snap["floor_result_contact_y"][0]) == pytest.approx(float(compare["pos_y"][0]))


def test_callback_local_4a908_retry_respects_floor_skip_without_stale_floor_result() -> None:
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_WAIT, SM_WAIT1_0, -40.0, 25.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(26.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    compare, snap = _step_with_colldata(seed)
    assert int(compare["on_ground"][0]) == 1
    assert int(compare["ground_id"][0]) == 2
    assert int(snap["floor_result_valid"][0]) == 1
    assert int(snap["floor_result_source"][0]) == FLOOR_RESULT_GROUNDED_4A908_RETRY
    assert int(snap["floor_result_mode"][0]) == FLOOR_MODE_4A908_RETRY
    assert int(snap["floor_result_segment_id"][0]) == 2

    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(2)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)
    compare_skip, snap_skip = _step_with_colldata(seed)

    assert int(compare_skip["on_ground"][0]) == 0
    assert int(compare_skip["ground_id"][0]) == 3
    assert int(snap_skip["floor_skip_valid"][0]) == 0
    assert int(snap_skip["floor_skip_segment_id"][0]) == 0xFFFF
    assert int(snap_skip["floor_result_valid"][0]) == 0
    assert int(snap_skip["floor_result_source"][0]) == FLOOR_RESULT_NONE


def test_floor_skip_written_by_platform_pass_and_cleared_after_other_floor() -> None:
    import msl_binding

    seed = _seed_base(STAGE_BATTLEFIELD, ACT_GUARD, 0xFFFFFFFF, 30.0, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["shield_hp"][0, 0] = np.float32(60.0)

    sizes = msl_binding.sizes()
    neutral = _input_bytes()
    drop = _input_bytes()
    drop_v = drop.view(INPUT_DTYPE).reshape((1,))
    drop_v["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    drop_v["p"]["main_y"][0, 0] = np.int8(-55)
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, neutral, drop)
        pass_snap = _read_colldata(handle)
        assert int(pass_snap["floor_skip_valid"][0]) == 1
        assert int(pass_snap["floor_skip_segment_id"][0]) == 4

        prev = drop
        cur = drop
        landed_on_other_floor = False
        final_snap = pass_snap
        for _ in range(80):
            msl_binding.step_input(handle, prev, cur)
            msl_binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            final_snap = _read_colldata(handle)
            if int(row["on_ground"][0]) == 1 and int(row["ground_id"][0]) != 4:
                landed_on_other_floor = True
                break
            prev = cur

        assert landed_on_other_floor
        assert int(final_snap["floor_skip_valid"][0]) == 0
        assert int(final_snap["floor_skip_segment_id"][0]) == 0xFFFF
    finally:
        msl_binding.destroy(handle)


def test_floor_joint_skip_suppresses_ordered_bottom_sweep_on_static_floor() -> None:
    # Source mpColl_80044628_Floor forwards CollData.joint_id_skip to mpCheckFloor before the
    # candidate scan. Skipping the owning joint must leave the callback-local floor result empty
    # even though the ECB bottom crosses the line.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    colldata_dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == colldata_dtype.itemsize

    floor_seg = msl_binding.stage_floor_segment(3, 34)
    assert floor_seg is not None
    joint_id = int(floor_seg["joint_id"])

    seed = _seed_base(3, ACT_WAIT, SM_WAIT1_0, 0.0, 1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["speed_y_self"][0, 0] = np.float32(-5.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, joint_id, -1)

        msl_binding.alloc_reset()
        msl_binding.step_input(handle, inp, inp)
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(colldata_dtype).reshape((1,))[0]
        stats = msl_binding.alloc_stats()

        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
        assert int(snap["floor_result_valid"][0]) == 0
        assert int(snap["floor_result_segment_id"][0]) == 0xFFFF
    finally:
        msl_binding.destroy(handle)


def test_mp_copy_colldata_copies_modeled_state_without_action_routing() -> None:
    # Source `mpCopyCollData` copies CollData root/ECB/env/surface/skip/filter state, not the
    # Fighter motion-state owner. This debug-only helper exercises the modeled MSL CollData lanes
    # without routing any action family through a wrapper.
    # refs/melee/src/melee/mp/mpcoll.c::mpCopyCollData
    import msl_binding

    sizes = msl_binding.sizes()
    colldata_dtype = _colldata_ecb_dtype()
    contacts_dtype = _collision_contacts_dtype()
    assert int(sizes["colldata_ecb"]) == colldata_dtype.itemsize
    assert int(sizes["collision_contacts"]) == contacts_dtype.itemsize

    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FALL, SM_FALL, -40.0, 45.0)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(2)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["on_ground"][0, 0] = np.uint8(1)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, 5, 6)
        msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 1, 7, 8)
        msl_binding.debug_set_coll_env_flags(handle, 0, 0, 0x40)

        before = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
        msl_binding.debug_write_colldata_ecb(handle, before)
        before_snap = before.view(colldata_dtype).reshape((1,))[0]
        assert int(before_snap["floor_skip_valid"][0]) == 1
        assert int(before_snap["floor_skip_valid"][1]) == 0
        assert float(before_snap["current_top_rel_y"][0]) != pytest.approx(
            float(before_snap["current_top_rel_y"][1])
        )

        msl_binding.alloc_reset()
        msl_binding.debug_copy_colldata(handle, 0, 0, 1)
        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0

        colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
        contacts = np.zeros((1, int(sizes["collision_contacts"])), dtype=np.uint8)
        compare = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.debug_write_colldata_ecb(handle, colldata)
        msl_binding.debug_write_collision_contacts(handle, contacts)
        msl_binding.write_compare(handle, compare)
        snap = colldata.view(colldata_dtype).reshape((1,))[0]
        contact_snap = contacts.view(contacts_dtype).reshape((1,))[0]
        row = compare.view(COMPARE_DTYPE).reshape((1,))[0]

        for field in (
            "current_bottom_rel_y",
            "current_top_rel_y",
            "current_left_rel_x",
            "current_right_rel_x",
            "prev_bottom_rel_y",
            "prev_top_rel_y",
            "desired_bottom_rel_y",
            "desired_top_rel_y",
            "substep_prev_pos_x",
            "substep_prev_pos_y",
            "last_pos_x",
            "last_pos_y",
        ):
            assert float(snap[field][1]) == pytest.approx(float(snap[field][0]))
        assert int(snap["floor_skip_valid"][1]) == 1
        assert int(snap["floor_skip_segment_id"][1]) == 2
        assert int(snap["joint_id_skip"][1]) == 5
        assert int(snap["joint_id_only"][1]) == 8
        assert int(contact_snap["coll_env_flags"][1]) == int(contact_snap["coll_env_flags"][0])
        assert int(row["action_id"][1]) == ACT_WAIT
        assert int(row["action_id"][0]) == ACT_FALL
        assert float(row["pos_x"][1]) == pytest.approx(float(row["pos_x"][0]))
        assert float(row["pos_y"][1]) == pytest.approx(float(row["pos_y"][0]))

        msl_binding.debug_set_player_root(handle, 0, 0, 70.0, 80.0, 1)
        msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, -1, -1)
        msl_binding.debug_write_colldata_ecb(handle, colldata)
        msl_binding.write_compare(handle, compare)
        after_mutate = colldata.view(colldata_dtype).reshape((1,))[0]
        row_after = compare.view(COMPARE_DTYPE).reshape((1,))[0]
        assert float(row_after["pos_x"][0]) == pytest.approx(70.0)
        assert float(row_after["pos_x"][1]) == pytest.approx(float(row["pos_x"][1]))
        assert int(after_mutate["joint_id_skip"][0]) == -1
        assert int(after_mutate["joint_id_skip"][1]) == 5
        assert int(after_mutate["joint_id_only"][1]) == 8
        assert float(after_mutate["last_pos_x"][0]) == pytest.approx(70.0)
        assert float(after_mutate["last_pos_x"][1]) == pytest.approx(float(snap["last_pos_x"][1]))
    finally:
        msl_binding.destroy(handle)


def test_reseed_clears_debug_joint_filters_before_normal_step() -> None:
    # Joint filters are not serialized in MslSeed. A debug-mutated filter must not survive into a
    # later reseed on a reused handle.
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    colldata_dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == colldata_dtype.itemsize

    floor_seg = msl_binding.stage_floor_segment(3, 34)
    assert floor_seg is not None
    joint_id = int(floor_seg["joint_id"])

    seed = _seed_base(3, ACT_WAIT, SM_WAIT1_0, 0.0, 1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["speed_y_self"][0, 0] = np.float32(-5.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, joint_id, -1)
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.alloc_reset()
        msl_binding.step_input(handle, inp, inp)
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(colldata_dtype).reshape((1,))[0]
        stats = msl_binding.alloc_stats()

        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
        assert int(snap["joint_id_skip"][0]) == -1
        assert int(snap["joint_id_only"][0]) == -1
        assert int(snap["floor_result_valid"][0]) == 1
        assert int(snap["floor_result_segment_id"][0]) == 34
    finally:
        msl_binding.destroy(handle)


def test_colldata_last_pos_initializes_from_root_without_action_routing() -> None:
    # Source mpColl wrappers keep CollData.last_pos as callback-local root state. Reseed/debug root
    # helpers initialize the modeled lane from the current root without routing a fighter action.
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCollEnd}
    import msl_binding

    sizes = msl_binding.sizes()
    dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == dtype.itemsize
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FALL, SM_FALL, -12.0, 34.0)
    handle = msl_binding.init(batch_size=1, num_players=2)
    out = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(dtype).reshape((1,))[0]
        assert float(snap["last_pos_x"][0]) == pytest.approx(-12.0)
        assert float(snap["last_pos_y"][0]) == pytest.approx(34.0)

        msl_binding.debug_set_player_root(handle, 0, 0, 22.5, 44.5, 1)
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(dtype).reshape((1,))[0]
        assert float(snap["last_pos_x"][0]) == pytest.approx(22.5)
        assert float(snap["last_pos_y"][0]) == pytest.approx(44.5)
    finally:
        msl_binding.destroy(handle)


def test_colldata_debug_exports_cliff_and_specialhi_source_provenance() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == dtype.itemsize

    seed = _seed_base(STAGE_YOSHI, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 34.0, -15.0)
    seed["ledge_cooldown"][0, 0] = np.uint8(21)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(6)
    seed["specialhi_rotate_model_valid_u8"][0, 0] = np.uint8(0)
    handle = msl_binding.init(batch_size=1, num_players=2)
    out = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(dtype).reshape((1,))[0]
        assert int(snap["ledge_cooldown"][0]) == 21
        assert int(snap["cliff_ledge_floor_segment_id"][0]) == 6
        assert int(snap["cliff_ledge_floor_segment_seeded"][0]) == 1

        specialhi = _seed_base(STAGE_FOD, ACT_FX_SPECIAL_HI_FALL, SM_FX_SPECIAL_HI_FALL, 0.0, -20.0)
        specialhi["action_frame"][0, 0] = np.int16(10)
        specialhi["anim_frame_f32"][0, 0] = np.float32(10.0)
        specialhi["specialhi_rotate_model_valid_u8"][0, 0] = np.uint8(1)
        specialhi["specialhi_rotate_model_f32"][0, 0] = np.float32(-0.75)
        msl_binding.reseed_seed(
            handle, specialhi.view(np.uint8).reshape((1, int(sizes["seed"])))
        )
        msl_binding.debug_write_colldata_ecb(handle, out)
        snap = out.view(dtype).reshape((1,))[0]
        assert int(snap["specialhi_rotate_model_action"][0]) == 1
        assert int(snap["specialhi_rotate_model_valid"][0]) == 1
        assert float(snap["specialhi_rotate_model"][0]) == pytest.approx(-0.75)
        assert int(snap["specialhi_collision_ecb_valid"][0]) == 1
        assert float(snap["specialhi_ecb_bottom_y"][0]) >= -20.0
        unrot_left, unrot_right, _unrot_min_y, unrot_top = msl_binding.ecb_extents_rel(
            CHAR_FOX, SM_FX_SPECIAL_HI_FALL, 10
        )
        unrot_bottom = msl_binding.ecb_bottom_rel_y(CHAR_FOX, SM_FX_SPECIAL_HI_FALL, 10)
        # Source `mpColl_LoadECB_JObj` consumes SpecialHi's live FtPart_XRotN/JObj collision ECB,
        # not public root or fixed ECB extents. The debug packet must expose that hidden collision
        # body so scanner root-depth rows cannot be recategorized from action id alone.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_RotateModel
        # refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
        assert float(snap["current_left_rel_x"][0]) != pytest.approx(float(unrot_left))
        assert float(snap["current_right_rel_x"][0]) != pytest.approx(float(unrot_right))
        assert float(snap["current_bottom_rel_y"][0]) != pytest.approx(float(unrot_bottom))
        assert float(snap["current_top_rel_y"][0]) != pytest.approx(float(unrot_top))
    finally:
        msl_binding.destroy(handle)


def test_colldata_debug_snapshot_does_not_allocate_after_init() -> None:
    import msl_binding

    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FALL, SM_FALL, -40.0, 45.0)
    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(2)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)
    sizes = msl_binding.sizes()
    dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == dtype.itemsize
    colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.alloc_reset()
        msl_binding.debug_write_colldata_ecb(handle, colldata)
        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
    finally:
        msl_binding.destroy(handle)


def test_wall_probe_lanes_record_air_wall_commit() -> None:
    # Diagnostic wall probe (mirror of the floor probe family): a fall drifting into FD's
    # right-side wall band must leave a probe record - side, commit kind, committed segment,
    # and the push dx - so wall-pass debugging does not need throwaway printf builds.
    import msl_binding

    seed = _seed_base(STAGE_FD, ACT_FALL, SM_FALL, 88.5, -20.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.0)
    seed["speed_y_self"][0, 0] = np.float32(-0.5)

    sizes = msl_binding.sizes()
    colldata_dtype = _colldata_ecb_dtype()
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        saw_commit = False
        for _ in range(12):
            msl_binding.step_input(handle, _input_bytes(), _input_bytes())
            colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)
            msl_binding.debug_write_colldata_ecb(handle, colldata)
            snap = colldata.view(colldata_dtype).reshape((1,))[0]
            if int(snap["wall_probe_valid"][0]) and int(snap["wall_probe_commit_kind"][0]) != 0:
                assert int(snap["wall_probe_side"][0]) == 2  # MSL_WALL_RIGHT
                # The probe must record exactly the committed contact: FD's upper right
                # wall segment (data/stages/final_destination.json i=9, the vertical face
                # at x=85.5657 spanning y 0..-10.5 - the fighter's side point rides above
                # the root at y~-20), pushed rightward (positive dx).
                assert int(snap["wall_probe_segment_id"][0]) == 9
                assert float(snap["wall_probe_corr_x"][0]) > 0.0
                saw_commit = True
                break
        assert saw_commit, "no wall commit recorded by the probe lanes"
    finally:
        msl_binding.destroy(handle)
