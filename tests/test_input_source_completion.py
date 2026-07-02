from __future__ import annotations

from pathlib import Path
import re

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.validation_buffer_seed import _derive_cliff_option_stick_latch_x8


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_KNEE_BEND = 0x0018
ACT_SQUAT = 0x0027
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_LW3 = 0x0040
ACT_ATTACK_DASH = 0x0032
ACT_FX_SPECIAL_LW_START = 0x0168
ACT_OTTOTTO = 0x00F5
ACT_CLIFF_WAIT = 0x00FD
ACT_CLIFF_CLIMB_QUICK = 0x00FF
ACT_CATCH = 0x00D4
SM_WAIT = 2
SM_FALL = 20
SM_KNEE_BEND = 15
SM_SQUAT = 5
SM_ATTACK_LW3 = 67
SM_ATTACK_DASH = 52
SM_ATTACK_AIR_N = 68
SM_FX_SPECIAL_LW_START = 313
SM_OTTOTTO = 210
SM_CLIFF_WAIT = 217
SM_CLIFF_CLIMB_QUICK = 220
SM_CATCH = 242
STAGE_FINAL_DESTINATION = 32
CHAR_FOX = 1
BUTTON_A = 0x0100
BUTTON_B = 0x0200
BUTTON_Z = 0x0010


def _input_bytes() -> np.ndarray:
    import msl_binding

    return np.zeros((1, int(msl_binding.sizes()["input"])), dtype=np.uint8)


def _seed_cliff_wait(*, latch_x8: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FINAL_DESTINATION)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    seed["action_id"][0, 0] = np.uint16(ACT_CLIFF_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_CLIFF_WAIT)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(-88.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["floor_skip_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["cliff_ledge_floor_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["cliff_option_stick_latch_x8"][0, 0] = np.uint8(latch_x8)
    return seed


def _seed_action(action_id: int, animation_index: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FINAL_DESTINATION)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(animation_index)
    seed["facing"][0, :2] = np.uint8(1)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["floor_skip_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["cliff_ledge_floor_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    return seed


def _step_seed(
    seed: np.ndarray,
    *,
    prev_buttons: int = 0,
    cur_buttons: int = 0,
    prev_main_x: int = 0,
    prev_main_y: int = 0,
    cur_main_x: int = 0,
    cur_main_y: int = 0,
) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    prev_input = _input_bytes()
    input_t = _input_bytes()
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
    prev_input.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(prev_buttons)
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(cur_buttons)
    prev_input.view(INPUT_DTYPE).reshape((1,))["p"]["main_x"][0, 0] = np.int8(prev_main_x)
    prev_input.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(prev_main_y)
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_x"][0, 0] = np.int8(cur_main_x)
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(cur_main_y)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _step_cliff_wait_with_held_forward(*, latch_x8: int) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed = _seed_cliff_wait(latch_x8=latch_x8)
    prev_input = _input_bytes()
    input_t = _input_bytes()
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
    for arr in (prev_input, input_t):
        view = arr.view(INPUT_DTYPE).reshape((1,))
        view["p"]["main_x"][0, 0] = np.int8(80)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def test_cliffwait_stick_option_uses_source_x8_latch_not_previous_input_proxy() -> None:
    # Source owner:
    # - ftCo_8009A804 initializes mv.co.cliff.x8=0 on CliffWait entry.
    # - ftCo_8009AA0C latches x8 after a neutral CliffWait IASA.
    # - ftCo_8009AAFC admits climb/drop from the latched source state, even if the immediately
    #   previous visible input was already held in the option range.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{ftCo_8009AA0C,ftCo_8009AAFC}
    unlatched = _step_cliff_wait_with_held_forward(latch_x8=0)
    latched = _step_cliff_wait_with_held_forward(latch_x8=1)

    assert int(unlatched["action_id"][0]) == ACT_CLIFF_WAIT
    assert int(latched["action_id"][0]) == ACT_CLIFF_CLIMB_QUICK
    assert int(latched["animation_index"][0]) == SM_CLIFF_CLIMB_QUICK


def test_airborne_attackair_z_as_a_lane_is_common_not_jump_only() -> None:
    # Source owner:
    # - Fighter_Spaghetti maps raw Z into held HSD_PAD_A before x668 edge construction.
    # - Fall_IASA reaches ftCo_AttackAir_CheckItemThrowInput, which reads `input.x668 & HSD_PAD_A`.
    # refs/melee/src/melee/ft/fighter.c:1868-1890
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckItemThrowInput
    seed = _seed_action(ACT_FALL, SM_FALL)
    fresh_z = _step_seed(seed, cur_buttons=BUTTON_Z)
    held_z = _step_seed(seed, prev_buttons=BUTTON_Z, cur_buttons=BUTTON_Z)

    assert int(fresh_z["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(fresh_z["animation_index"][0]) == SM_ATTACK_AIR_N
    assert int(held_z["action_id"][0]) == ACT_FALL


def test_ottotto_catch_consumes_source_z_before_a_attack_guard_and_jump() -> None:
    # Source owner:
    # - Ottotto_IASA checks ftCo_Catch_CheckInput before A-attacks, guard, appeal, jump, dash,
    #   crouch, turn, and walk.
    # - Raw Z supplies the internal held LR plus x668 A lanes used by Catch_CheckInput.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    # refs/melee/src/melee/ft/fighter.c:1868-1890
    seed = _seed_action(ACT_OTTOTTO, SM_OTTOTTO)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    z_out = _step_seed(seed, cur_buttons=BUTTON_Z)
    a_out = _step_seed(seed, cur_buttons=BUTTON_A)

    assert int(z_out["action_id"][0]) == ACT_CATCH
    assert int(z_out["animation_index"][0]) == SM_CATCH
    assert int(a_out["action_id"][0]) != ACT_CATCH


def test_squat_b_down_reflector_source_order_preempts_lower_priority_consumers() -> None:
    # Source owner:
    # - Squat_IASA reaches ftCo_800D68C0 before grounded attacks/guard/jump and before the
    #   delayed platform-pass helper.
    # - Neutral-X B+down therefore enters grounded Reflector rather than AttackLw3/crouch/pass.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    seed = _seed_action(ACT_SQUAT, SM_SQUAT)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)

    b_down = _step_seed(seed, cur_buttons=BUTTON_B, cur_main_y=-100)
    a_down = _step_seed(seed, cur_buttons=BUTTON_A, cur_main_y=-100)

    assert int(b_down["action_id"][0]) == ACT_FX_SPECIAL_LW_START
    assert int(b_down["animation_index"][0]) == SM_FX_SPECIAL_LW_START
    assert int(a_down["action_id"][0]) == ACT_ATTACK_LW3
    assert int(a_down["animation_index"][0]) == SM_ATTACK_LW3


def test_attackdash_wait_delegation_specials_and_appeal_are_branch_owned() -> None:
    # Source owner:
    # - AttackDash_IASA delegates to Wait_IASA when allow_interrupt is live.
    # - Wait_IASA checks SpecialS and ftCo_800D68C0 before Squat, so B+down enters Reflector.
    # - The same branch keeps Jump below those special checks.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    seed = _seed_action(ACT_ATTACK_DASH, SM_ATTACK_DASH)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_frame"][0, 0] = np.int16(35)
    seed["anim_frame_f32"][0, 0] = np.float32(35.0)
    seed["attackdash_x0"][0, 0] = np.int16(0)

    b_down = _step_seed(seed, cur_buttons=BUTTON_B, cur_main_y=-100)
    held_b_down = _step_seed(
        seed,
        prev_buttons=BUTTON_B,
        cur_buttons=BUTTON_B,
        prev_main_y=-100,
        cur_main_y=-100,
    )

    assert int(b_down["action_id"][0]) == ACT_FX_SPECIAL_LW_START
    assert int(b_down["animation_index"][0]) == SM_FX_SPECIAL_LW_START
    assert int(held_b_down["action_id"][0]) == ACT_SQUAT


def test_cliffwait_x8_derivation_is_prefix_invariant_and_latched() -> None:
    common = {
        "lstick_deadzone_x": 0.2875,
        "lstick_deadzone_y": 0.2875,
        "cliff_option_stick_threshold": 0.25,
    }
    action = np.array(
        [ACT_WAIT, ACT_CLIFF_WAIT, ACT_CLIFF_WAIT, ACT_CLIFF_WAIT, ACT_CLIFF_WAIT],
        dtype=np.uint16,
    )
    main_x = np.array([0, 80, 0, 80, 80], dtype=np.int8)
    main_y = np.zeros(action.shape[0], dtype=np.int8)
    c_x = np.zeros(action.shape[0], dtype=np.int8)
    c_y = np.zeros(action.shape[0], dtype=np.int8)

    full = _derive_cliff_option_stick_latch_x8(
        action_id_u16=action,
        main_x_i8=main_x,
        main_y_i8=main_y,
        c_x_i8=c_x,
        c_y_i8=c_y,
        common=common,
    )

    # Entry-row option input does not latch. The following neutral CliffWait row latches x8, and
    # later held-option rows preserve the latch.
    assert full.tolist() == [0, 0, 1, 1, 1]
    for k in range(1, action.shape[0] + 1):
        got = _derive_cliff_option_stick_latch_x8(
            action_id_u16=action[:k],
            main_x_i8=main_x[:k],
            main_y_i8=main_y[:k],
            c_x_i8=c_x[:k],
            c_y_i8=c_y[:k],
            common=common,
        )
        assert np.array_equal(got, full[:k])


def test_input_system_inventory_is_closed_and_stale_proxy_wording_is_guarded() -> None:
    doc = Path("agent_docs/systems/input.md").read_text()
    progress = Path("agent_docs/systems/PROGRESS.md").read_text()
    assert "| Input | CLOSED | [input.md](input.md)" in progress
    assert "INVENTORY NEEDED" not in doc
    assert "| TODO" not in doc
    assert "| BLOCKED" not in doc

    # Keep supported input substrate and action-command consumers free of stale closure
    # contradictions. Other systems can still use these words when their own inventories explicitly
    # retain a policy; input-owned code should not describe supported command inputs as a
    # proxy/approximation.
    for path in (
        "src/input.c",
        "src/ucf.c",
        "src/ledge.c",
        "src/trigger_input.h",
        "src/input_axis.h",
        "src/jump_input.h",
    ):
        text = Path(path).read_text().lower()
        for bad in ("approximation", "proxy", "until modeled", "todo(decomp)", "scope-narrowed bridge"):
            assert bad not in text, f"{path} still contains stale input closure wording: {bad}"

    consumer_checks = {
        "src/locomotion.c": (
            r"broader synthesized x668-a provenance",
            r"local locomotion iasa approximations",
            r"remaining teeter iasa branches are still blocked",
            r"\bapproximation\b",
            r"\bapproximations\b",
            r"until\s+[^.\n]*modeled",
            r"modeled more broadly",
            r"full iasa chains",
            r"not modeled yet",
        ),
        "src/grab_flow.c": (
            r"sim inference: treat held z",
            r"slippi provides raw button bits, not the internal held_inputs/x668 representation",
        ),
        "bindings/msl_preprocess_native.c": (
            r"does not run the climb/drop x8 setter in the same proc",
        ),
    }
    for path, bad_phrases in consumer_checks.items():
        text = Path(path).read_text().lower()
        for bad in bad_phrases:
            assert re.search(bad, text) is None, (
                f"{path} still contains stale input closure wording matching: {bad}"
            )
