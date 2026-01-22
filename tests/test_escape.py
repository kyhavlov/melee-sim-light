from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.seed_history import load_shield_tilt_table_meta


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD = 0x00B3
ACT_ESCAPE_F = 0x00E9
ACT_ESCAPE_B = 0x00EA
ACT_ESCAPE_N = 0x00EB

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_ESCAPE_N = 41
SM_ESCAPE_F = 42
SM_ESCAPE_B = 43

CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_guard_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(0)
    # In our replay-derived datasets, shield states often have `animation_index == -1`.
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["shield_hp"][0, 0] = np.float32(_common_attr("start_shield_health"))

    neutral, _frame_max = load_shield_tilt_table_meta()[CHAR_FOX]
    seed["guard_tilt_x8"][0, 0] = np.uint16(neutral)
    seed["guard_tilt_x4"][0, 0] = np.float32(0.0)
    return seed


def test_shield_spotdodge_oos_enters_escape_n_and_eventually_returns_to_wait() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = _seed_guard_base()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        shield = _mk_input_bytes(1, input_stride)
        down = _mk_input_bytes(1, input_stride)
        neutral = _mk_input_bytes(1, input_stride)

        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        down_view = down.view(INPUT_DTYPE).reshape((1,))
        down_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        down_view["p"]["main_y"][0, 0] = np.int8(-80)

        msl_binding.reseed_seed(handle, seed_bytes)

        # Spotdodge: shield-hold + fresh down flick.
        msl_binding.step_input(handle, shield, down)
        msl_binding.write_compare(handle, out)
        out0 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()

        assert int(out0["action_id"][0]) == ACT_ESCAPE_N
        assert int(out0["animation_index"][0]) == SM_ESCAPE_N

        # EscapeN should not immediately snap back to Wait.
        msl_binding.step_input(handle, down, neutral)
        msl_binding.write_compare(handle, out)
        out1 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out1["action_id"][0]) == ACT_ESCAPE_N

        # EscapeN anim eventually returns to Wait.
        out_last = out1
        for _ in range(240):
            msl_binding.step_input(handle, neutral, neutral)
            msl_binding.write_compare(handle, out)
            out_last = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(out_last["action_id"][0]) == ACT_WAIT:
                break
        assert int(out_last["action_id"][0]) == ACT_WAIT
        assert int(out_last["animation_index"][0]) == SM_WAIT1_0
    finally:
        msl_binding.destroy(handle)


def test_shield_roll_oos_direction_selects_escape_f_vs_escape_b() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_guard_base()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        shield = _mk_input_bytes(1, input_stride)
        right = _mk_input_bytes(1, input_stride)
        left = _mk_input_bytes(1, input_stride)

        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        right_view = right.view(INPUT_DTYPE).reshape((1,))
        right_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        right_view["p"]["main_x"][0, 0] = np.int8(+80)

        left_view = left.view(INPUT_DTYPE).reshape((1,))
        left_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        left_view["p"]["main_x"][0, 0] = np.int8(-80)

        # Facing right: +X is roll forward, -X is roll backward.
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, shield, right)
        msl_binding.write_compare(handle, out)
        out_f = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out_f["action_id"][0]) == ACT_ESCAPE_F
        assert int(out_f["animation_index"][0]) == SM_ESCAPE_F

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, shield, left)
        msl_binding.write_compare(handle, out)
        out_b = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out_b["action_id"][0]) == ACT_ESCAPE_B
        assert int(out_b["animation_index"][0]) == SM_ESCAPE_B
    finally:
        msl_binding.destroy(handle)


def test_shield_hold_without_escape_input_stays_in_guard() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_guard_base()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        shield = _mk_input_bytes(1, input_stride)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        msl_binding.reseed_seed(handle, seed_bytes)
        for _ in range(20):
            msl_binding.step_input(handle, shield, shield)
            msl_binding.write_compare(handle, out)
            outn = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            assert int(outn["action_id"][0]) == ACT_GUARD
            assert int(outn["animation_index"][0]) == 0xFFFFFFFF
    finally:
        msl_binding.destroy(handle)


def test_spotdodge_lstick_requires_fresh_flick_but_cstick_ignores_flick_timer() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    spot_max = int(_common_attr("spotdodge_flick_tilt_max_frames"))
    assert spot_max > 0

    seed = _seed_guard_base()
    # Simulate "stick already held" long enough to fail the flick timer gate.
    seed["tilt_timer_y"][0, 0] = np.uint8(spot_max)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        held_down = _mk_input_bytes(1, input_stride)
        held_down_view = held_down.view(INPUT_DTYPE).reshape((1,))
        held_down_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        held_down_view["p"]["main_y"][0, 0] = np.int8(-80)

        # L-stick held (no fresh flick window) should not trigger spotdodge.
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, held_down, held_down)
        msl_binding.write_compare(handle, out)
        out0 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out0["action_id"][0]) == ACT_GUARD

        # C-stick down should still trigger spotdodge, regardless of tilt timer.
        c_down = _mk_input_bytes(1, input_stride)
        c_down_view = c_down.view(INPUT_DTYPE).reshape((1,))
        c_down_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        c_down_view["p"]["c_y"][0, 0] = np.int8(-80)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, c_down, c_down)
        msl_binding.write_compare(handle, out)
        out1 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out1["action_id"][0]) == ACT_ESCAPE_N
    finally:
        msl_binding.destroy(handle)


def test_roll_lstick_requires_fresh_flick_but_cstick_ignores_flick_timer() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    roll_max = int(_common_attr("escape_flick_tilt_max_frames"))
    assert roll_max > 0

    seed = _seed_guard_base()
    seed["tilt_timer_x"][0, 0] = np.uint8(roll_max)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        held_right = _mk_input_bytes(1, input_stride)
        held_right_view = held_right.view(INPUT_DTYPE).reshape((1,))
        held_right_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        held_right_view["p"]["main_x"][0, 0] = np.int8(+80)

        # L-stick held (no fresh flick window) should not trigger roll.
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, held_right, held_right)
        msl_binding.write_compare(handle, out)
        out0 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out0["action_id"][0]) == ACT_GUARD

        # C-stick should still trigger roll, regardless of tilt timer.
        c_right = _mk_input_bytes(1, input_stride)
        c_right_view = c_right.view(INPUT_DTYPE).reshape((1,))
        c_right_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
        c_right_view["p"]["c_x"][0, 0] = np.int8(+80)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, c_right, c_right)
        msl_binding.write_compare(handle, out)
        out1 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out1["action_id"][0]) == ACT_ESCAPE_F
    finally:
        msl_binding.destroy(handle)
