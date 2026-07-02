from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040
BUTTON_R = 0x0020
BUTTON_Z = 0x0010
BUTTON_A = 0x0100
BUTTON_B = 0x0200

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_TURN_RUN = 0x0013
ACT_RUN = 0x0015
ACT_FALL = 0x001D
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_OFF = 0x00B4
ACT_GUARD_SET_OFF = 0x00B5
ACT_GUARD_REFLECT = 0x00B6
ACT_CATCH_DASH = 0x00D6
ACT_ESCAPE_B = 0x00EA
ACT_PASS = 0x00F4
ACT_FX_SPECIAL_LW_START = 0x0168
ACT_ESCAPE_N = 0x00EB
ACT_KNEE_BEND = 0x0018
ACT_ATTACK_HI4 = 0x003F

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_TURN_RUN = 11
SM_RUN = 13
SM_GUARD_ON = 37
SM_GUARD = 38
SM_GUARD_OFF = 39
SM_GUARD_SET_OFF = 40
SM_PASS = 209
SM_KNEE_BEND = 11

CHAR_FOX = 1
CHAR_MARTH = 18
STAGE_FD = 32
STAGE_YOSHI = 8
MAX_PLAYERS = 4


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["shield_hp"][0, 0] = np.float32(_common_attr("start_shield_health"))
    return seed


def _run_seed_one_step(seed: np.ndarray, cur_input: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    prev = _mk_input_bytes(1, input_stride)
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev, cur_input)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_grounded_guard_entry_hold_exit_changes_action_and_drains_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = _seed_base()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        neutral = _mk_input_bytes(1, input_stride)
        shield = _mk_input_bytes(1, input_stride)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        msl_binding.reseed_seed(handle, seed_bytes)

        # Entry: neutral -> shield.
        msl_binding.step_input(handle, neutral, shield)
        msl_binding.write_compare(handle, out)
        out0 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()

        assert int(out0["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD)
        # In our replay-derived datasets, shield states often have `animation_index == -1`.
        if int(out0["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD):
            assert int(out0["animation_index"][0]) == 0xFFFFFFFF

        # Hold: shield -> shield for a few frames; shield_hp should drain below start.
        start_hp = float(_common_attr("start_shield_health"))
        hp = float(out0["shield_hp"][0])
        for _ in range(10):
            msl_binding.step_input(handle, shield, shield)
            msl_binding.write_compare(handle, out)
            outn = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            hp = float(outn["shield_hp"][0])
            assert int(outn["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD)
        assert hp < start_hp

        # Exit: shield -> neutral should enter GuardOff.
        msl_binding.step_input(handle, shield, neutral)
        msl_binding.write_compare(handle, out)
        out_exit = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out_exit["action_id"][0]) == ACT_GUARD_OFF
        assert int(out_exit["animation_index"][0]) == SM_GUARD_OFF

        # GuardOff anim eventually returns to Wait.
        out_last = out_exit
        for _ in range(120):
            msl_binding.step_input(handle, neutral, neutral)
            msl_binding.write_compare(handle, out)
            out_last = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(out_last["action_id"][0]) == ACT_WAIT:
                break
        assert int(out_last["action_id"][0]) == ACT_WAIT
        assert int(out_last["animation_index"][0]) == SM_WAIT1_0
    finally:
        msl_binding.destroy(handle)


def test_guard_state_does_not_reenter_guard_reflect_on_lr_edge() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["x672_input_timer"][0, 0] = np.uint8(0)
    seed["guard_x10"][0, 0] = np.uint8(3)
    seed["state_flags"][0, 0, 2] = np.uint8(0x80)  # fp+0x221B isShieldActive

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        neutral = _mk_input_bytes(1, input_stride)
        shield_edge = _mk_input_bytes(1, input_stride)
        shield_edge_view = shield_edge.view(INPUT_DTYPE).reshape((1,))
        shield_edge_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        # Decomp: powershield re-entry helper ftCo_80093694 is called from GuardOn_IASA, not
        # Guard_IASA, so a Guard frame with an LR pressed-edge should remain in Guard.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_80093694}
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, shield_edge)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD
    finally:
        msl_binding.destroy(handle)


def test_guardsetoff_hitlag_exit_clears_expired_reflect_window_while_x18_lives() -> None:
    # GuardSetOff_Anim calls ftCo_80093BC0 on the first non-hitlag callback after prio-0 hitlag
    # decrement. x221C_b1 is owned by mv.co.guard.x14 independently from x18/x221C_b2, so a row
    # exiting hitlag with x14 expired and x18 still live publishes only x221C_b2.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_SET_OFF)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD_SET_OFF)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["hitlag"][0, 0] = np.uint16(1)
    seed["state_flags"][0, 0, 3] = np.uint8(0x60)
    seed["guard_reflect_timer_x14"][0, 0] = np.uint8(0)
    seed["guard_reflect_timer_x18"][0, 0] = np.uint8(2)
    seed["guard_x10"][0, 0] = np.uint8(7)

    out = _run_seed_one_step(seed, _mk_input_bytes(1, input_stride))
    assert int(out["action_id"][0]) == ACT_GUARD_SET_OFF
    assert int(out["hitlag"][0]) == 0
    assert int(out["state_flags"][0, 3]) == 0x20


def test_guardsetoff_hitlag_exit_keeps_live_reflect_window_bit() -> None:
    # Negative boundary for the same source owner: if the x14 reflect-window timer remains live
    # after the callback tick, x221C_b1 must not be cleared just because x18 is also live.
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_SET_OFF)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD_SET_OFF)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["hitlag"][0, 0] = np.uint16(1)
    seed["state_flags"][0, 0, 3] = np.uint8(0x60)
    seed["guard_reflect_timer_x14"][0, 0] = np.uint8(2)
    seed["guard_reflect_timer_x18"][0, 0] = np.uint8(2)
    seed["guard_x10"][0, 0] = np.uint8(7)

    out = _run_seed_one_step(seed, _mk_input_bytes(1, input_stride))
    assert int(out["action_id"][0]) == ACT_GUARD_SET_OFF
    assert int(out["hitlag"][0]) == 0
    assert int(out["state_flags"][0, 3]) == 0x60


def test_guardoff_special_chain_requires_seeded_x1c_timer() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_y"][0, 0] = np.int8(-80)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        for x1c, expected_action in ((0, ACT_ESCAPE_N), (1, ACT_FX_SPECIAL_LW_START)):
            seed = _seed_base()
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
            seed["stocks"][0, 1] = np.uint8(4)
            seed["action_id"][0, 0] = np.uint16(ACT_GUARD_OFF)
            seed["action_frame"][0, 0] = np.int16(9)
            seed["animation_index"][0, 0] = np.uint32(SM_GUARD_OFF)
            seed["anim_frame_f32"][0, 0] = np.float32(9.0)
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["guard_special_enable_timer_x1c"][0, 0] = np.uint8(x1c)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)

            got = out.view(COMPARE_DTYPE).reshape((1,))[0]
            assert int(got["action_id"][0]) == expected_action
            if x1c == 0:
                assert int(got["action_id"][0]) != ACT_FX_SPECIAL_LW_START
    finally:
        msl_binding.destroy(handle)


def test_active_guard_hitlag_does_not_recharge_shield() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["hitlag"][0, 0] = np.uint8(3)
    seed["shield_hp"][0, 0] = np.float32(55.0)
    seed["state_flags"][0, 0, 2] = np.uint8(0x80)  # fp+0x221B isShieldActive

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        neutral = _mk_input_bytes(1, input_stride)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, neutral)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD
        assert int(got["hitlag"][0]) == 2
        assert float(got["shield_hp"][0]) == np.float32(55.0)
    finally:
        msl_binding.destroy(handle)


def test_seeded_kneebend_still_allows_attack_hi4_iasa() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_KNEE_BEND)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEE_BEND)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        cur_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Decomp: ftCo_KneeBend_IASA still allows AttackHi4 interrupt on a seeded KneeBend row
        # through ftCo_AttackHi4_CheckInputNoD0 / ftCo_800DF2D8.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_CheckInputNoD0
        cur_view["p"]["c_y"][0, 0] = np.int8(80)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_ATTACK_HI4
    finally:
        msl_binding.destroy(handle)


def test_guard_snapshot_with_held_shield_and_no_release_latch_stays_guard() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["guard_x10"][0, 0] = np.uint8(0)
    seed["guard_release_latched_xc"][0, 0] = np.uint8(0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        cur_view = inp.view(INPUT_DTYPE).reshape((1,))
        cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD
    finally:
        msl_binding.destroy(handle)


def test_guard_snapshot_without_held_shield_still_enters_guardoff() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["guard_x10"][0, 0] = np.uint8(0)
    seed["guard_release_latched_xc"][0, 0] = np.uint8(1)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        got = out.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(got["action_id"][0]) == ACT_GUARD_OFF
    finally:
        msl_binding.destroy(handle)


def test_guardsetoff_iasa_does_not_platform_pass_while_guardon_can() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    cur = _mk_input_bytes(1, input_stride)
    cur_view = cur.view(INPUT_DTYPE).reshape((1,))
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    # Use a down value that satisfies pass-through (`x464`) but not spotdodge (`x314`), so this
    # positive isolates ftCo_8009A080 instead of the earlier ftCo_8009980C branch.
    cur_view["p"]["main_y"][0, 0] = np.int8(-55)

    seed = _seed_base()
    seed["stage_id"][0] = np.uint32(STAGE_YOSHI)
    seed["ground_id"][0, 0] = np.uint16(4)  # Yoshi top soft platform; data/stages/yoshis_story.json
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(60.0)
    seed["tilt_timer_y"][0, 0] = np.uint8(0)

    guard_on_seed = seed.copy()
    guard_on_seed["action_id"][0, 0] = np.uint16(ACT_GUARD_ON)
    guard_on_seed["animation_index"][0, 0] = np.uint32(SM_GUARD_ON)
    guard_on_seed["action_frame"][0, 0] = np.int16(2)
    guard_on_seed["anim_frame_f32"][0, 0] = np.float32(2.0)

    # Source positive: when earlier GuardOn IASA branches are absent, ftCo_8009A080 enters Pass
    # while L/R is held and the current CollData floor is a platform.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    out_guard_on = _run_seed_one_step(guard_on_seed, cur)
    assert int(out_guard_on["action_id"][0]) == ACT_PASS
    assert int(out_guard_on["animation_index"][0]) == SM_PASS
    assert int(out_guard_on["on_ground"][0]) == 0

    analog_cur = _mk_input_bytes(1, input_stride)
    analog_view = analog_cur.view(INPUT_DTYPE).reshape((1,))
    analog_view["p"]["r"][0, 0] = np.uint8(96)
    analog_view["p"]["main_y"][0, 0] = np.int8(-55)

    # HSD_PAD_LR is the synthesized held-input lane, not only the digital button bits. Analog
    # trigger-held shield-drop inputs therefore reach ftCo_8009A080 too.
    # refs/melee/src/melee/ft/fighter.c:1868-1890
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    out_guard_on_analog = _run_seed_one_step(guard_on_seed, analog_cur)
    assert int(out_guard_on_analog["action_id"][0]) == ACT_PASS
    assert int(out_guard_on_analog["animation_index"][0]) == SM_PASS

    no_shield_down = _mk_input_bytes(1, input_stride)
    no_shield_down.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-55)
    out_no_shield = _run_seed_one_step(guard_on_seed, no_shield_down)
    assert int(out_no_shield["action_id"][0]) != ACT_PASS

    guard_setoff_seed = seed.copy()
    guard_setoff_seed["action_id"][0, 0] = np.uint16(ACT_GUARD_SET_OFF)
    guard_setoff_seed["animation_index"][0, 0] = np.uint32(SM_GUARD_SET_OFF)
    guard_setoff_seed["action_frame"][0, 0] = np.int16(2)
    guard_setoff_seed["anim_frame_f32"][0, 0] = np.float32(2.0)

    # Source negative: GuardSetOff_IASA is empty, so the same held shield+down platform input must
    # not enter Pass from GuardSetOff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_IASA
    out_guard_setoff = _run_seed_one_step(guard_setoff_seed, cur)
    assert int(out_guard_setoff["action_id"][0]) == ACT_GUARD_SET_OFF
    assert int(out_guard_setoff["on_ground"][0]) == 1


def test_guardsetoff_platform_edge_floor_loss_preempts_destination_guard_roll() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    cstick_roll_back = _mk_input_bytes(1, input_stride)
    cstick_roll_back.view(INPUT_DTYPE).reshape((1,))["p"]["c_x"][0, 0] = np.int8(-80)

    seed = _seed_base()
    seed["stage_id"][0] = np.uint32(STAGE_YOSHI)
    seed["ground_id"][0, 0] = np.uint16(4)  # Yoshi top soft platform; scaled right endpoint x=15.75.
    seed["pos_y"][0, 0] = np.float32(42.0001)
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_SET_OFF)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD_SET_OFF)
    seed["action_frame"][0, 0] = np.int16(17)
    seed["anim_frame_f32"][0, 0] = np.float32(18.0)
    seed["speed_air_x_self"][0, 0] = np.float32(1.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)
    seed["guard_x10"][0, 0] = np.uint8(7)

    edge_seed = seed.copy()
    edge_seed["pos_x"][0, 0] = np.float32(15.25)

    # GuardSetOff_Coll still owns the source callback pass when shieldstun ends. If post-Phys
    # ground motion carries the root past the soft-platform endpoint, ft_80084104/ft_800845B4
    # enters Fall before the destination Guard IASA roll can publish.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_GuardSetOff_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
    out_edge = _run_seed_one_step(edge_seed, cstick_roll_back)
    assert int(out_edge["action_id"][0]) == ACT_FALL
    assert int(out_edge["on_ground"][0]) == 0

    center_seed = seed.copy()
    center_seed["pos_x"][0, 0] = np.float32(0.0)
    out_center = _run_seed_one_step(center_seed, cstick_roll_back)
    assert int(out_center["action_id"][0]) != ACT_FALL
    assert int(out_center["on_ground"][0]) == 1


def test_guardreflect_spotdodge_preempts_platform_pass() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["stage_id"][0] = np.uint32(STAGE_YOSHI)
    seed["ground_id"][0, 0] = np.uint16(4)  # Yoshi top soft platform; data/stages/yoshis_story.json
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(60.0)
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_REFLECT)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["tilt_timer_y"][0, 0] = np.uint8(0)
    seed["guard_reflect_timer_x14"][0, 0] = np.uint8(2)
    seed["guard_reflect_timer_x18"][0, 0] = np.uint8(2)

    down_spotdodge = _mk_input_bytes(1, input_stride)
    down_spotdodge_view = down_spotdodge.view(INPUT_DTYPE).reshape((1,))
    down_spotdodge_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    down_spotdodge_view["p"]["main_y"][0, 0] = np.int8(-80)

    # GuardReflect_IASA checks spotdodge/roll/catch/jump before ftCo_8009A080 platform pass.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    out_spotdodge = _run_seed_one_step(seed, down_spotdodge)
    assert int(out_spotdodge["action_id"][0]) == ACT_ESCAPE_N
    assert int(out_spotdodge["on_ground"][0]) == 1

    down_pass_only = _mk_input_bytes(1, input_stride)
    down_pass_view = down_pass_only.view(INPUT_DTYPE).reshape((1,))
    down_pass_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    down_pass_view["p"]["main_y"][0, 0] = np.int8(-55)

    out_pass = _run_seed_one_step(seed, down_pass_only)
    assert int(out_pass["action_id"][0]) == ACT_PASS
    assert int(out_pass["animation_index"][0]) == SM_PASS
    assert int(out_pass["on_ground"][0]) == 0


def test_guardoff_anim_end_wait_destination_guard_entry_and_precedence() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    shield = _mk_input_bytes(1, input_stride)
    shield_view = shield.view(INPUT_DTYPE).reshape((1,))
    shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_R)
    shield_view["p"]["r"][0, 0] = np.uint8(255)

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_OFF)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD_OFF)
    seed["action_frame"][0, 0] = np.int16(14)
    seed["anim_frame_f32"][0, 0] = np.float32(14.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["x672_input_timer"][0, 0] = np.uint8(16)

    # GuardOff_Anim enters Wait when the shield-drop animation ends; the same frame's destination
    # Wait_IASA then reaches ftCo_80091A4C and enters shield on held/pressed shield.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    out_guard = _run_seed_one_step(seed, shield)
    assert int(out_guard["action_id"][0]) in (ACT_GUARD_ON, ACT_GUARD_REFLECT)
    assert int(out_guard["animation_index"][0]) == 0xFFFFFFFF

    # Negative coverage: Wait_IASA checks attack input before ftCo_80091A4C, so this retained
    # destination callback must not broaden A+shield into GuardOn.
    attack_shield = shield.copy()
    attack_shield.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(
        BUTTON_R | BUTTON_A
    )
    out_attack_priority = _run_seed_one_step(seed, attack_shield)
    assert int(out_attack_priority["action_id"][0]) not in (ACT_GUARD_ON, ACT_GUARD_REFLECT)


def test_marth_turnrun_iasa_does_not_admit_guard_entry_synthetic_control() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["char_id"][0, 0] = np.uint8(CHAR_MARTH)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN_RUN)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN_RUN)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_TURN_RUN)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.8)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.8)
    seed["facing"][0, 0] = np.uint8(0)

    shield = _mk_input_bytes(1, input_stride)
    shield_view = shield.view(INPUT_DTYPE).reshape((1,))
    shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    shield_view["p"]["l"][0, 0] = np.uint8(255)
    shield_view["p"]["main_x"][0, 0] = np.int8(80)

    # Character-specific control for the same common callback owner: Marth's TurnRun uses the
    # common ftCo_TurnRun_IASA jump-only path and must not inherit Run/Wait guard admission.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_IASA
    out_row = _run_seed_one_step(seed, shield)
    assert int(out_row["action_id"][0]) == ACT_TURN_RUN
    assert int(out_row["animation_index"][0]) == SM_TURN_RUN


def test_run_iasa_still_admits_guard_entry_spacie_control() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["action_id"][0, 0] = np.uint16(ACT_RUN)
    seed["animation_index"][0, 0] = np.uint32(SM_RUN)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.5)
    seed["speed_air_x_self"][0, 0] = np.float32(1.5)

    shield = _mk_input_bytes(1, input_stride)
    shield_view = shield.view(INPUT_DTYPE).reshape((1,))
    shield_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    shield_view["p"]["l"][0, 0] = np.uint8(255)

    # Run_IASA calls ftCo_80091A4C after attack/CatchDash checks, unlike TurnRun_IASA. With only
    # shield held and no grab edge, this remains the adjacent guard control for Run's pre-guard
    # CatchDash helper.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    out_row = _run_seed_one_step(seed, shield)
    assert int(out_row["action_id"][0]) != ACT_CATCH_DASH
    assert int(out_row["action_id"][0]) in (ACT_GUARD_ON, ACT_GUARD_REFLECT)
