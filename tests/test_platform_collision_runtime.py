from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_SQUAT_WAIT = 0x0028
ACT_DAMAGE_FALL = 0x0026
ACT_GUARD = 0x00B3
ACT_PASS = 0x00F4

SM_WAIT1_0 = 2
SM_FALL = 20
SM_DAMAGE_FALL = 33
SM_PASS = 209

CHAR_FOX = 1
BUTTON_L = 0x0040
BUTTON_B = 0x0200

ACT_FX_SPECIAL_AIR_LW_START = 0x016D


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 0, 0.0, 1.125),  # FoD source-local platform; live moving transforms are residual
        (31, 2, -40.0, 27.200000762939453),  # Battlefield left platform
        (8, 4, 0.0, 42.0),  # Yoshi's Story top platform
        (28, 2, 0.0, 51.42530059814453),  # Dream Land top platform
        (3, 35, -40.0, 25.0),  # frozen Pokemon Stadium left platform
        (3, 36, 40.0, 25.0),  # frozen Pokemon Stadium right platform
    ],
)
def test_static_platform_lines_are_debug_visible_but_not_in_filtered_graph(
    stage_id: int, line_id: int, x: float, y: float
) -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seg = msl_binding.stage_floor_segment(stage_id, line_id)
        assert seg is not None
        assert int(seg["is_platform"]) == 1
        assert float(seg["x0"]) <= x <= float(seg["x1"])
        assert float(seg["y0"]) == pytest.approx(y)

        # The filtered graph remains a hard-floor-only view; runtime platform collision uses the
        # full graph plus Pass/floor-skip gating.
        assert msl_binding.stage_fighter_floor_segment(stage_id, line_id) is None
    finally:
        msl_binding.destroy(handle)


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
    seed["anim_frame_f32"][0, :2] = seed["action_frame"][0, :2].astype(np.float32)
    return seed


def _step_once(seed: np.ndarray, prev_input: np.ndarray | None = None, input_t: np.ndarray | None = None):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    if prev_input is None:
        prev_input = _input_bytes()
    if input_t is None:
        input_t = _input_bytes()
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 0, 0.0, 1.125),
        (31, 2, -40.0, 27.200000762939453),
        (8, 4, 0.0, 42.0),
        (28, 2, 0.0, 51.42530059814453),
        (3, 35, -40.0, 25.0),
        (3, 36, 40.0, 25.0),
    ],
)
def test_grounded_fighter_stays_on_static_platform(stage_id: int, line_id: int, x: float, y: float) -> None:
    seed = _seed_base(stage_id, ACT_WAIT, SM_WAIT1_0, x, y)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(line_id)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == line_id
    assert float(out["pos_y"][0]) == pytest.approx(y + 0.0001, abs=1e-5)


def test_guard_on_yoshi_platform_stays_grounded() -> None:
    seed = _seed_base(8, ACT_GUARD, 0, 0.0, 42.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4
    assert int(out["action_id"][0]) != ACT_FALL


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 0, 0.0, 1.125),
        (31, 2, -40.0, 27.200000762939453),
        (8, 4, 0.0, 42.0),
        (28, 2, 0.0, 51.42530059814453),
        (3, 35, -40.0, 25.0),
        (3, 36, 40.0, 25.0),
    ],
)
def test_damagefall_lands_on_static_platform(stage_id: int, line_id: int, x: float, y: float) -> None:
    seed = _seed_base(stage_id, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, x, y + 8.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(y + 14.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == line_id


def test_jump_up_through_platform_does_not_ground_from_below() -> None:
    seed = _seed_base(31, ACT_FALL, SM_FALL, -40.0, 25.0)
    seed["speed_y_self"][0, 0] = np.float32(3.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF


def test_frozen_pokemon_stadium_ignores_transformation_platform_line() -> None:
    # The GrPs collision artifact contains transformation-object platforms as source data, but the
    # current runtime domain is frozen Stadium. The source frozen toggle makes Stadium take the
    # no-transformation branch; line 11 remains debug-visible but is not fighter-solid.
    #
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # refs/melee/src/melee/gr/grpstadium.c::{grStadium_OnInit,grStadium_801D10F0}
    seed = _seed_base(3, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, 40.0, 34.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)

    out = _step_once(seed)

    assert int(out["ground_id"][0]) != 11


def test_squat_down_input_enters_pass_and_skips_source_platform() -> None:
    seed = _seed_base(31, ACT_SQUAT_WAIT, 0, -40.0, 27.200000762939453)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["tilt_timer_y"][0, 0] = np.uint8(1)

    prev_input = _input_bytes()
    input_t = _input_bytes()
    prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
    cur_view = input_t.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_y"][0, 0] = np.int8(-80)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_input, input_t)

    assert int(out["action_id"][0]) == ACT_PASS
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 2

    seed2 = _seed_base(31, ACT_PASS, SM_PASS, -40.0, 27.0)
    seed2["seed_prev_action_id"][0, 0] = np.uint16(ACT_PASS)
    seed2["ground_id"][0, 0] = np.uint16(2)
    seed2["speed_y_self"][0, 0] = np.float32(-0.5)
    seed2["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed2["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed2["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(28.0)

    out2 = _step_once(seed2)

    assert int(out2["action_id"][0]) == ACT_PASS
    assert int(out2["on_ground"][0]) == 0

    seed3 = _seed_base(31, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, -40.0, 35.0)
    seed3["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed3["ground_id"][0, 0] = np.uint16(2)
    seed3["speed_y_self"][0, 0] = np.float32(-10.0)
    seed3["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed3["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed3["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)

    out3 = _step_once(seed3)

    assert int(out3["action_id"][0]) != ACT_PASS
    assert int(out3["on_ground"][0]) == 1
    assert int(out3["ground_id"][0]) == 2


def test_pass_iasa_can_enter_aerial_shine() -> None:
    seed = _seed_base(31, ACT_PASS, SM_PASS, -40.0, 26.5)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["speed_y_self"][0, 0] = np.float32(-0.67)

    input_t = _input_bytes()
    cur_view = input_t.view(INPUT_DTYPE).reshape((1,))
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_once(seed, _input_bytes(), input_t)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_LW_START


@pytest.mark.integration
def test_guard_drop_through_replay_real_enters_pass_on_frozen_ps_platform() -> None:
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[90]
    p = 1

    assert int(row["seed_t"]["stage_id"]) == 3
    assert int(row["seed_t"]["action_id"][p]) == 178  # GuardOn
    assert int(row["seed_t"]["ground_id"][p]) == 36
    assert int(row["ref_t1"]["action_id"][p]) == ACT_PASS

    out = _step_one_replay_row(ds, 90)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == ACT_PASS
    assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 36
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)


def _step_one_replay_row(ds, record: int):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    row = ds.samples[record]
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed(
            handle,
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride),
        )
        msl_binding.step_input(
            handle,
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride),
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride),
        )
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)
