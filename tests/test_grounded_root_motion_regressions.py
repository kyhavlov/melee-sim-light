from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_L = 0x0040

ACT_GUARD = 0x00B3
ACT_ESCAPE_F = 0x00E9
ACT_ATTACK_S4_S = 0x003C

SM_GUARD = 0xFFFFFFFF
SM_ESCAPE_F = 42
SM_ATTACK_S4 = 62

CHAR_FOX = 1
STAGE_FD = 32


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["camera_target_point_inside_stage_cam_bounds_u8"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(1)
    return seed


def _step(handle: int, prev_inp: np.ndarray, inp: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    prev_bytes = prev_inp.view(np.uint8).reshape((1, input_stride))
    inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.step_input(handle, prev_bytes, inp_bytes)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def test_motion_state_entry_refreshes_facing_dir1_for_escapef_root_motion() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["facing"][0, 0] = np.uint8(0)  # facing left
    seed["facing_dir1"][0, 0] = np.int8(1)  # stale opposite sign from prior state

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        shield = np.zeros((1,), dtype=INPUT_DTYPE)
        roll_f = np.zeros((1,), dtype=INPUT_DTYPE)
        neutral = np.zeros((1,), dtype=INPUT_DTYPE)

        shield["p"][0, 0]["buttons"] = np.uint16(BUTTON_L)
        roll_f["p"][0, 0]["buttons"] = np.uint16(BUTTON_L)
        roll_f["p"][0, 0]["main_x"] = np.int8(-80)  # facing-left forward roll

        out0 = _step(handle, shield, roll_f)
        assert int(out0["action_id"][0]) == ACT_ESCAPE_F
        assert int(out0["animation_index"][0]) == SM_ESCAPE_F

        out_last = out0
        prev = roll_f
        cur = neutral
        for _ in range(8):
            out_last = _step(handle, prev, cur)
            prev = cur

        assert int(out_last["action_id"][0]) == ACT_ESCAPE_F
        assert float(out_last["pos_x"][0]) < float(out0["pos_x"][0])
        assert float(out_last["speed_ground_x_self"][0]) < 0.0
    finally:
        msl_binding.destroy(handle)


def test_attacks4s_uses_ft_80084fa8_root_motion() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_S4_S)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_S4)
    seed["action_frame"][0, 0] = np.int16(9)
    seed["anim_frame_f32"][0, 0] = np.float32(9.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(34.191)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["facing"][0, 0] = np.uint8(0)  # facing left
    seed["facing_dir1"][0, 0] = np.int8(-1)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1,), dtype=INPUT_DTYPE)
        out = _step(handle, neutral, neutral)

        assert int(out["action_id"][0]) == ACT_ATTACK_S4_S
        assert int(out["animation_index"][0]) == SM_ATTACK_S4
        assert float(out["pos_x"][0]) < float(seed["pos_x"][0, 0])
        assert float(out["speed_ground_x_self"][0]) < 0.0
    finally:
        msl_binding.destroy(handle)


def test_attacks4s_does_not_reapply_root_motion_when_anim_floor_is_frozen() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_S4_S)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_S4)
    seed["action_frame"][0, 0] = np.int16(7)
    seed["anim_frame_f32"][0, 0] = np.float32(7.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(60.0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing_dir1"][0, 0] = np.int8(1)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1,), dtype=INPUT_DTYPE)
        out = _step(handle, neutral, neutral)

        assert int(out["action_id"][0]) == ACT_ATTACK_S4_S
        assert int(out["animation_index"][0]) == SM_ATTACK_S4
        assert np.isclose(float(out["pos_x"][0]), 60.0, atol=1e-7)
        assert np.isclose(float(out["speed_ground_x_self"][0]), 0.0, atol=1e-7)
    finally:
        msl_binding.destroy(handle)
