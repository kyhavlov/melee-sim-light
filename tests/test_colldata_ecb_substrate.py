from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_ESCAPE_AIR = 0x00EC

SM_WAIT1_0 = 2
SM_FALL = 20
SM_ESCAPE_AIR = 44

CHAR_FOX = 1
STAGE_FD = 32
STAGE_BATTLEFIELD = 31

FLOOR_RESULT_DIRECT = 1
FLOOR_RESULT_GROUNDED_4A908_RETRY = 2


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
            ("floor_result_valid", ("u1", (4,))),
            ("floor_result_source", ("u1", (4,))),
            ("_pad0", ("u1", (12,))),
            ("floor_result_segment_id", ("<u2", (4,))),
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
            ("desired_bottom_rel_y", ("<f4", (4,))),
            ("desired_top_rel_y", ("<f4", (4,))),
            ("desired_left_rel_x", ("<f4", (4,))),
            ("desired_right_rel_x", ("<f4", (4,))),
            ("desired_side_rel_y", ("<f4", (4,))),
            ("floor_result_contact_x", ("<f4", (4,))),
            ("floor_result_contact_y", ("<f4", (4,))),
            ("floor_result_normal_x", ("<f4", (4,))),
            ("floor_result_normal_y", ("<f4", (4,))),
            ("substep_prev_pos_x", ("<f4", (4,))),
            ("substep_prev_pos_y", ("<f4", (4,))),
            ("substep_cur_pos_x", ("<f4", (4,))),
            ("substep_cur_pos_y", ("<f4", (4,))),
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
    assert int(snap["floor_result_segment_id"][0]) == 2
    assert float(snap["floor_result_contact_y"][0]) == pytest.approx(
        float(compare["pos_y"][0]) - 0.0001,
        abs=1e-5,
    )


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
    assert int(snap["floor_result_segment_id"][0]) == 1
    assert float(snap["floor_result_contact_y"][0]) == pytest.approx(float(compare["pos_y"][0]))
