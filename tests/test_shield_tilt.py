from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import INPUT_DTYPE, SEED_DTYPE
from tools.slippi.seed_history import load_shield_tilt_table_meta

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_GUARD = 0x00B3
ACT_GUARD_SET_OFF = 0x00B5
ACT_WAIT = 0x000E

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_GUARD_DAMAGE = 40

CHAR_FOX = 1
STAGE_FD = 32
STICK_MAX_I8 = 80  # src/input_axis.h::MSL_STICK_MAX_I8


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _up_tilt_without_tap_jump_i8() -> np.int8:
    # Guard tilt tests require "up" input that remains below jump entry.
    # Decomp jump gate: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
    tap_jump_thresh = _common_attr("tap_jump_threshold")
    v = int(np.floor(tap_jump_thresh * float(STICK_MAX_I8))) - 1
    if v < 0:
        v = 0
    if v > STICK_MAX_I8:
        v = STICK_MAX_I8
    return np.int8(v)


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
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # fp+0x221B_b0: live ShieldDesc
    # P0 is only a dummy opponent for the shield-pose query; keep it out of the common grounded
    # player-overlap nudge lane so this test isolates shield tilt.
    seed["pos_x"][0, 0] = np.float32(-20.0)

    neutral, _frame_max = load_shield_tilt_table_meta()[CHAR_FOX]
    seed["guard_tilt_x8"][0, :2] = np.uint16(neutral)
    seed["guard_tilt_x4"][0, :2] = np.float32(0.0)
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
        tilt_up_view["p"]["main_y"][0, 1] = _up_tilt_without_tap_jump_i8()

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

def test_guard_tilt_has_inertia_and_converges_over_multiple_frames() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_guard(facing_p1=1)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        neutral = _mk_input_bytes(1, input_stride)
        neutral_view = neutral.view(INPUT_DTYPE).reshape((1,))
        neutral_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        up = _mk_input_bytes(1, input_stride)
        up_view = up.view(INPUT_DTYPE).reshape((1,))
        up_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        up_view["p"]["main_x"][0, 1] = np.int8(0)
        up_view["p"]["main_y"][0, 1] = _up_tilt_without_tap_jump_i8()

        # Baseline (neutral): bubble at neutral.
        msl_binding.step_input(handle, neutral, neutral)
        b0 = msl_binding.debug_shield_bubbles_world(handle, 0)
        p0 = np.array([float(b0[1, 0]), float(b0[1, 1]), float(b0[1, 2])], dtype=np.float64)

        # Current input is published after the priority-1 Guard Anim callback, so the first up
        # sample is intentionally deferred for one frame.
        msl_binding.step_input(handle, neutral, up)
        b_deferred = msl_binding.debug_shield_bubbles_world(handle, 0)
        p_deferred = np.array(
            [float(b_deferred[1, 0]), float(b_deferred[1, 1]), float(b_deferred[1, 2])],
            dtype=np.float64,
        )

        # On the next frame, Anim consumes the previous up sample and begins the source recurrence.
        msl_binding.step_input(handle, up, up)
        b1 = msl_binding.debug_shield_bubbles_world(handle, 0)
        p1 = np.array([float(b1[1, 0]), float(b1[1, 1]), float(b1[1, 2])], dtype=np.float64)

        # Hold: converge towards the final up-tilt pose.
        for _ in range(23):
            msl_binding.step_input(handle, up, up)
        bN = msl_binding.debug_shield_bubbles_world(handle, 0)
        pN = np.array([float(bN[1, 0]), float(bN[1, 1]), float(bN[1, 2])], dtype=np.float64)

        assert np.array_equal(p_deferred, p0)

        d01 = float(np.linalg.norm(p1 - p0))
        d0N = float(np.linalg.norm(pN - p0))
        d1N = float(np.linalg.norm(pN - p1))

        assert d01 > 1.0e-7
        assert d0N > d01
        assert d1N < d0N
    finally:
        msl_binding.destroy(handle)


def test_guardsetoff_shielddesc_uses_live_descriptor_and_guarddamage_joint() -> None:
    import json
    from pathlib import Path

    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    attrs = json.loads(Path("data/characters/fox.json").read_text())
    shield_part = int(attrs["grab_capture_anchor_part_id"])
    model_scale = float(attrs["model_scaling"])

    seed = _seed_guard(facing_p1=1)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_SET_OFF)
    seed["action_frame"][0, 1] = np.int16(5)
    seed["animation_index"][0, 1] = np.uint32(SM_GUARD_DAMAGE)
    seed["anim_frame_f32"][0, 1] = np.float32(5.0)
    seed["hitlag"][0, 1] = np.uint8(2)
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # fp+0x221B_b0: live ShieldDesc

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        neutral = _mk_input_bytes(1, input_stride)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, neutral, neutral)
        bubble = msl_binding.debug_shield_bubbles_world(handle, 0)[1]

        # GuardSetOff's motion-state row owns ftCo_SM_GuardDamage, while ftCo_80092450 keeps the
        # live ShieldDesc attached to ftData.x8->x11. Its center must therefore follow this JObj,
        # not the steady Guard tilt table.
        # refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardSetOff
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092450
        matrix = msl_binding.anim_pose_collision_matrix_f32(
            CHAR_FOX, SM_GUARD_DAMAGE, 5.0, shield_part
        )
        expected_x = float(matrix[11]) * model_scale
        expected_y = float(matrix[7]) * model_scale
        assert float(bubble[0]) == pytest.approx(expected_x, abs=2.0e-3)
        assert float(bubble[1]) == pytest.approx(expected_y, abs=2.0e-3)
        assert float(bubble[3]) > 0.0

        # Descriptor identity, not GuardSetOff's action id, controls collision availability.
        seed["state_flags"][0, 1, 2] = np.uint8(0)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, neutral, neutral)
        inactive = msl_binding.debug_shield_bubbles_world(handle, 0)[1]
        assert float(inactive[3]) == 0.0
    finally:
        msl_binding.destroy(handle)
