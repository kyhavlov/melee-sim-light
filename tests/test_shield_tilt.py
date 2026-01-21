from __future__ import annotations

import numpy as np

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_GUARD = 0x00B3
ACT_WAIT = 0x000E

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2

CHAR_FOX = 1
STAGE_FD = 32


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_guard(*, facing_p1: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)  # P0 right
    seed["facing"][0, 1] = np.uint8(int(facing_p1) & 1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["animation_index"][0, 1] = np.uint32(0xFFFF_FFFF)
    seed["shield_hp"][0, :2] = np.float32(_common_attr("start_shield_health"))
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    return seed


def test_shield_bubble_center_moves_with_guard_tilt_and_mirrors_with_facing() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # Facing right: tilt right vs tilt up should change center.
        seed = _seed_guard(facing_p1=1)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        neutral = _mk_input_bytes(1, input_stride)

        tilt_right = _mk_input_bytes(1, input_stride)
        tilt_right_view = tilt_right.view(INPUT_DTYPE).reshape((1,))
        tilt_right_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        # Keep below escape (roll) threshold (~0.7) so Guard IASA doesn't transition to EscapeF/B.
        tilt_right_view["p"]["main_x"][0, 1] = np.int8(50)
        tilt_right_view["p"]["main_y"][0, 1] = np.int8(0)

        msl_binding.step_input(handle, neutral, tilt_right)
        b1 = msl_binding.debug_shield_bubbles_world(handle, 0)
        x1, y1, z1, r1 = (float(b1[1, 0]), float(b1[1, 1]), float(b1[1, 2]), float(b1[1, 3]))
        assert r1 > 0.0

        tilt_up = _mk_input_bytes(1, input_stride)
        tilt_up_view = tilt_up.view(INPUT_DTYPE).reshape((1,))
        tilt_up_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        tilt_up_view["p"]["main_x"][0, 1] = np.int8(0)
        tilt_up_view["p"]["main_y"][0, 1] = np.int8(80)

        msl_binding.step_input(handle, tilt_right, tilt_up)
        b2 = msl_binding.debug_shield_bubbles_world(handle, 0)
        x2, y2, z2, r2 = (float(b2[1, 0]), float(b2[1, 1]), float(b2[1, 2]), float(b2[1, 3]))
        assert r2 > 0.0

        assert (abs(x1 - x2) + abs(y1 - y2) + abs(z1 - z2)) > 1.0e-5

        # Mirror: facing left + forward tilt should flip X offset relative to facing right + forward tilt.
        seed_r = _seed_guard(facing_p1=1)
        msl_binding.reseed_seed(handle, seed_r.view(np.uint8).reshape((1, seed_stride)))

        forward_r = _mk_input_bytes(1, input_stride)
        fwd_r_view = forward_r.view(INPUT_DTYPE).reshape((1,))
        fwd_r_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        fwd_r_view["p"]["main_x"][0, 1] = np.int8(50)
        fwd_r_view["p"]["main_y"][0, 1] = np.int8(0)
        msl_binding.step_input(handle, neutral, forward_r)
        br = msl_binding.debug_shield_bubbles_world(handle, 0)
        dx_r = float(br[1, 0])  # pos_x is 0 in this seed
        dy_r = float(br[1, 1])

        seed_l = _seed_guard(facing_p1=0)
        msl_binding.reseed_seed(handle, seed_l.view(np.uint8).reshape((1, seed_stride)))

        forward_l = _mk_input_bytes(1, input_stride)
        fwd_l_view = forward_l.view(INPUT_DTYPE).reshape((1,))
        fwd_l_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        fwd_l_view["p"]["main_x"][0, 1] = np.int8(-50)
        fwd_l_view["p"]["main_y"][0, 1] = np.int8(0)
        msl_binding.step_input(handle, neutral, forward_l)
        bl = msl_binding.debug_shield_bubbles_world(handle, 0)
        dx_l = float(bl[1, 0])
        dy_l = float(bl[1, 1])

        assert abs(dx_r + dx_l) < 1.0e-4
        assert abs(dy_r - dy_l) < 1.0e-4
    finally:
        msl_binding.destroy(handle)
