from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_GUARD_REFLECT = 0x00B6

# Slippi state_flags byte0 (fp+0x2218) reflect-active bit (0x10).
# refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
STATE_FLAG_2218_REFLECT_ACTIVE = 0x10


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def test_guard_reflect_timer_counts_down_and_clears_reflect_active_bit() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    internals_stride = int(sizes["internals"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (mv.co.guard.x14=x2A4)
    # Seed/state uses +1 bias (x14+1) so we can expire at 0.
    t0 = int(common["powershield_reflect_frames"]) + 1
    assert t0 > 0

    # Packed MslDebugInternals view for reading the timer (struct is packed; align=False).
    DEBUG_INTERNALS_DTYPE = np.dtype(
        [
            ("tilt_timer_x", ("u1", (4,))),
            ("turn_frames_to_turn", ("u1", (4,))),
            ("turn_has_turned", ("u1", (4,))),
            ("guard_reflect_timer_x14", ("u1", (4,))),
            ("attack_id", ("<u2", (4,))),
            ("attack_instance", ("<u2", (4,))),
            ("attack_identity_last_action_id", ("<u2", (4,))),
            ("instance_id", ("<u2", (4,))),
            ("instance_id_x2073", ("u1", (4,))),
            ("instance_identity_last_action_id", ("<u2", (4,))),
            ("instance_id_counter", "<u2"),
            ("throw_pulse_consumed", ("u1", (4,))),
            ("throw_pulse_crossed_prev_frame", ("u1", (4,))),
        ],
        align=False,
    )
    assert int(DEBUG_INTERNALS_DTYPE.itemsize) == internals_stride

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)  # Final Destination
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(1)  # Fox
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    # Player 0 is idle (Wait).
    seed["action_id"][0, 0] = np.uint16(0x000E)
    seed["animation_index"][0, 0] = np.uint32(2)  # Wait1_0

    # Player 1 starts in GuardReflect with a finite reflect window.
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(t0)
    # Keep the guard animation from finishing and transitioning out of GuardReflect during the test.
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 1] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Hold shield via analog trigger so GuardReflect doesn't immediately exit.
        inp_view["p"]["l"][0, 1] = np.uint8(255)

        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        out_int = np.zeros((1, internals_stride), dtype=np.uint8)

        # Step 1: timer should tick down by 1; reflect-active bit stays set.
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
        msl_binding.debug_write_internals(handle, out_int)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
        it0 = out_int.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0]
        assert int(cmp0["action_id"][1]) == ACT_GUARD_REFLECT
        assert int(it0["guard_reflect_timer_x14"][1]) == t0 - 1
        assert (int(cmp0["state_flags"][1, 0]) & STATE_FLAG_2218_REFLECT_ACTIVE) != 0

        # Step 2: timer reaches 0; reflect-active bit clears even if action remains GuardReflect.
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
        msl_binding.debug_write_internals(handle, out_int)
        cmp1 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
        it1 = out_int.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0]
        assert int(cmp1["action_id"][1]) == ACT_GUARD_REFLECT
        assert int(it1["guard_reflect_timer_x14"][1]) == 0
        assert (int(cmp1["state_flags"][1, 0]) & STATE_FLAG_2218_REFLECT_ACTIVE) == 0
    finally:
        msl_binding.destroy(handle)
