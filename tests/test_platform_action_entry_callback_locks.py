from __future__ import annotations

import math

import numpy as np
import pytest

from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype
from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_L = 0x0040

ACT_ESCAPE_AIR = 236
ACT_JUMP_AERIAL_F = 27
ACT_WAIT = 14
SM_ESCAPE_AIR = 44
SM_JUMP_AERIAL_F = 18
SM_WAIT1_0 = 2
CHAR_FOX = 1
STAGE_FD = 32
STAGE_FOD = 2
ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR = 3


def _blank_input(input_stride: int) -> np.ndarray:
    return np.zeros((1, input_stride), dtype=np.uint8)


def _synthetic_air_seed(
    *, stage_id: int, action_id: int, animation_index: int, x: float, y: float
) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["ground_id"][0, 1] = np.uint16(1)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(animation_index)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


def test_escapeair_floor_producer_runtime_clears_on_jumpaerial_before_later_escapeair() -> None:
    # The runtime authority lane belongs to EscapeAir_Coll only. Even if debug tooling arms it
    # before a JumpAerial frame, the non-EscapeAir callback boundary must clear it before a later
    # air-dodge entry can observe it.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()

    seed = _synthetic_air_seed(
        stage_id=STAGE_FD,
        action_id=ACT_JUMP_AERIAL_F,
        animation_index=SM_JUMP_AERIAL_F,
        x=0.0,
        y=12.0,
    )
    seed["ground_id"][0, 0] = np.uint16(1)
    neutral = _blank_input(input_stride)
    airdodge = _blank_input(input_stride)
    airdodge.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, seed.view("u1").reshape(1, seed_stride).copy())
        binding.debug_set_escapeair_floor_producer_runtime(
            handle, 0, 0, 1, ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR
        )

        binding.step_input(handle, neutral, neutral)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
        after_jump = colldata_bytes.view(colldata_dtype).reshape((1,))[0].copy()
        assert int(after_jump["escapeair_floor_producer_runtime"][0]) == 0

        binding.step_input(handle, neutral, airdodge)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    after_escapeair = colldata_bytes.view(colldata_dtype).reshape((1,))[0]
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(after_escapeair["escapeair_floor_producer_runtime"][0]) == 0
    assert int(after_escapeair["desired_locked_owner"][0]) == 0


def test_escapeair_live_hard_floor_projection_publishes_sloped_line_normal() -> None:
    # The live carried hard-floor path is generic over fighter-solid non-platform floor lines.
    # When it publishes a sloped source line, the CollData floor result must carry that line's
    # normal rather than a flat fallback normal.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 segment 4
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()

    line = binding.stage_floor_segment(STAGE_FOD, 4)
    assert line is not None
    assert int(line["is_platform"]) == 0
    assert int(line["is_ledge"]) == 0
    assert int(line["fighter_solid"]) == 1
    x = (float(line["x0"]) + float(line["x1"])) * 0.5
    dx = float(line["x1"]) - float(line["x0"])
    dy = float(line["y1"]) - float(line["y0"])
    floor_y = float(line["y0"]) + (dy * ((x - float(line["x0"])) / dx))
    length = math.hypot(dx, dy)
    expected_nx = -dy / length
    expected_ny = dx / length
    assert abs(expected_nx) > 0.01
    assert expected_ny != pytest.approx(1.0)

    seed = _synthetic_air_seed(
        stage_id=STAGE_FOD,
        action_id=ACT_ESCAPE_AIR,
        animation_index=SM_ESCAPE_AIR,
        x=x,
        y=floor_y - 0.25,
    )
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(4)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(floor_y + 1.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)

    neutral = _blank_input(input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, seed.view("u1").reshape(1, seed_stride).copy())
        binding.debug_set_escapeair_floor_producer_runtime(
            handle, 0, 0, 1, ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR
        )
        binding.step_input(handle, neutral, neutral)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    dbg = colldata_bytes.view(colldata_dtype).reshape((1,))[0]
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4
    assert int(dbg["floor_result_segment_id"][0]) == 4
    assert float(dbg["floor_result_normal_x"][0]) == pytest.approx(expected_nx, abs=1e-6)
    assert float(dbg["floor_result_normal_y"][0]) == pytest.approx(expected_ny, abs=1e-6)
