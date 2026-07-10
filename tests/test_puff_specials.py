"""Jigglypuff multi-jump ladder (ftPr_MS_JumpAerialF1..F5) decomp-anchored unit tests.

Anchors: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D730C,
ftCo_800D74A4,ftCo_JumpAerialF1_*} and the fp->x2D0 stats block extracted to
data/characters/puff.json (puff_mjump_*). Replay verification: the puff suite
(replays/suites/puff.json) carries 341-345 rows in every game.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE  # noqa: E402

STAGE_FD = 32
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_FALL_AERIAL = 0x0020
SM_WAIT1 = 2
SM_FALL = 20
ACT_PR_F1 = 341
ACT_PR_F5 = 345
BTN_X = 0x0400

PUFF = 15
FOX = 1


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "puff.json").read_text())


def _mk_inputs(**axes) -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    for k, v in axes.items():
        inp["p"][0, 0][k] = v
    return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))


def _seed(pos_y: float = 300.0, facing: int = 1, jumps_left: int = 5) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(PUFF)
    seed["char_id"][0, 1] = np.uint8(FOX)
    seed["facing"][0, 0] = np.uint8(facing)
    seed["facing"][0, 1] = np.uint8(1)
    seed["pos_x"][0, 1] = np.float32(60.0)
    seed["pos_y"][0, 0] = np.float32(pos_y)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["shield_hp"][0, :2] = np.float32(60.0)
    # jumps_left counts REMAINING jumps of max_jumps=6; 5 = only the ground jump used, so the
    # next midair jump is ladder rung F1.
    seed["jumps_left"][0, 0] = np.uint8(jumps_left)
    seed["jumps_left"][0, 1] = np.uint8(2)
    seed["x676_x"][0, :2] = np.uint8(0xFE)
    for p in range(2):
        seed["action_id"][0, p] = np.uint16(ACT_WAIT)
        seed["animation_index"][0, p] = np.uint32(SM_WAIT1)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    return seed


def _run(seed: np.ndarray, frames: list[np.ndarray]) -> list[np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        outs = []
        prev = _mk_inputs()
        for inp in frames:
            msl_binding.step_input(handle, prev, inp)
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            outs.append(ob.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            prev = inp
        return outs
    finally:
        msl_binding.destroy(handle)


def test_first_midair_jump_enters_f1_with_ladder_impulse() -> None:
    a = _attrs()
    x = _mk_inputs(buttons=BTN_X, main_x=127)
    outs = _run(_seed(), [x] + [_mk_inputs()] * 2)
    o = outs[0]
    assert int(o["action_id"][0]) == ACT_PR_F1
    assert int(o["jumps_left"][0]) == 4
    # self_vel set to (stick * h_impulse, v_impulse[0]) at entry; one gravity step applies in
    # the same frame's phys.
    assert float(o["speed_y_self"][0]) == pytest.approx(
        float(a["puff_mjump_v_impulse_1"]) - float(a["grav"]), abs=1e-4
    )
    assert float(o["speed_air_x_self"][0]) > 0.0


def test_ladder_chain_waits_for_script_window_then_chains_on_held_jump() -> None:
    # F1's cmd0 window opens at script frame 28: holding X chains into F2 only once the window
    # is live (data/moves/puff.json specials_by_msid.295 set_cmd_var@28).
    a = _attrs()
    hold = _mk_inputs(buttons=BTN_X)
    outs = _run(_seed(), [hold] * 40)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_F1
    f2_first = acts.index(ACT_PR_F1 + 1)
    # Window frame 28 (anim frames advance one per step from 1 on the entry row).
    assert 26 <= f2_first <= 30, f"chained at step {f2_first}: {acts[:32]}"
    o = outs[f2_first]
    assert int(o["jumps_left"][0]) == 3
    assert float(o["speed_y_self"][0]) == pytest.approx(
        float(a["puff_mjump_v_impulse_2"]) - float(a["grav"]), abs=1e-4
    )


def test_ladder_exhausts_to_fall_aerial() -> None:
    # Chain all five rungs by holding X; after F5 (jumps_left 0) the anim end exits FallAerial.
    hold = _mk_inputs(buttons=BTN_X)
    outs = _run(_seed(), [hold] * 250)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_PR_F5 in acts
    i5 = acts.index(ACT_PR_F5)
    assert int(outs[i5]["jumps_left"][0]) == 0
    assert ACT_FALL_AERIAL in acts[i5:]
    # F5 never re-arms the chain window (no cmd0 pulse in msid 299).
    assert all(x != ACT_PR_F1 for x in acts[i5:])


def test_reverse_jump_flips_facing_at_half_window() -> None:
    # ftCo_800D74A4 arms the turnaround when the stick opposes facing beyond x4; ft_800CB6EC
    # flips the scalar facing when the countdown reaches turn_frames/2 (12 -> flip on the 6th
    # tick counting the entry tick).
    a = _attrs()
    turn_frames = int(a["puff_mjump_turn_frames"])
    x_back = _mk_inputs(buttons=BTN_X, main_x=-127)
    outs = _run(_seed(facing=1), [x_back] + [_mk_inputs(main_x=-127)] * (turn_frames + 2))
    assert int(outs[0]["action_id"][0]) == ACT_PR_F1
    faces = [int(o["facing"][0]) for o in outs]
    assert faces[0] == 1
    flip_at = faces.index(0)
    # ftCo_800D74A4 applies the FIRST ft_800CB6EC decrement on the entry frame itself, so the
    # counter reaches turn_frames/2 on the (turn_frames/2)-th decrement = 0-based output step
    # turn_frames/2 - 1 (12 -> step 5).
    assert flip_at == turn_frames // 2 - 1, f"facing flipped at step {flip_at}: {faces}"


def test_forward_jump_does_not_flip_facing() -> None:
    x_fwd = _mk_inputs(buttons=BTN_X, main_x=127)
    outs = _run(_seed(facing=1), [x_fwd] + [_mk_inputs(main_x=127)] * 14)
    assert int(outs[0]["action_id"][0]) == ACT_PR_F1
    assert all(int(o["facing"][0]) == 1 for o in outs)


def test_no_chain_without_jump_input() -> None:
    x = _mk_inputs(buttons=BTN_X)
    outs = _run(_seed(), [x] + [_mk_inputs()] * 45)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_F1
    assert all(a not in (ACT_PR_F1 + 1,) for a in acts[1:])


ACT_PR_S = 363
ACT_PR_AIR_S = 364


def test_grounded_pound_enters_and_exits_to_wait() -> None:
    seed = _seed(pos_y=0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    b_side = _mk_inputs(buttons=0x0200, main_x=127)
    outs = _run(seed, [b_side] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_S, f"side-B from Wait must enter Pound: {acts[:4]}"
    assert ACT_WAIT in acts[30:], f"grounded Pound must return to Wait: {acts[-8:]}"


def test_air_pound_impulse_angle_and_decay() -> None:
    # cmd0 pulse @12: self_vel = pound_vel * (cos, sin)(stick angle); neutral stick -> angle 0.
    # cmd1==1 phase (12..39): both lanes decay by pound_vel_decay per frame, gravity suspended.
    a = _attrs()
    vel = float(a["puff_pound_vel"])
    decay = float(a["puff_pound_vel_decay"])
    b_side = _mk_inputs(buttons=0x0200, main_x=127)
    outs = _run(_seed(pos_y=300.0), [b_side] + [_mk_inputs()] * 50)
    assert int(outs[0]["action_id"][0]) == ACT_PR_AIR_S
    # Impulse visible after the frame-12 pulse (one decay step applies the same frame).
    vx12 = float(outs[11]["speed_air_x_self"][0])
    assert vx12 == pytest.approx(vel * decay, abs=1e-3), f"vx@12 {vx12}"
    assert float(outs[11]["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-3)
    vx13 = float(outs[12]["speed_air_x_self"][0])
    assert vx13 == pytest.approx(vx12 * decay, abs=1e-3)


def test_air_pound_exits_to_fall() -> None:
    b_side = _mk_inputs(buttons=0x0200, main_x=127)
    outs = _run(_seed(pos_y=500.0), [b_side] + [_mk_inputs()] * 90)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_AIR_S
    assert ACT_FALL in acts[40:], f"air Pound must exit to Fall: {acts[-8:]}"


ACT_PR_LW_L = 369
ACT_PR_AIR_LW_L = 370
ACT_PR_LW_R = 371
ACT_PR_AIR_LW_R = 372


def test_grounded_rest_enters_facing_variant_and_exits_to_wait() -> None:
    seed = _seed(pos_y=0.0, facing=1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    b_down = _mk_inputs(buttons=0x0200, main_y=-127)
    outs = _run(seed, [b_down] + [_mk_inputs()] * 260)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_LW_R, f"down-B facing right must enter SpecialLwR: {acts[:4]}"
    assert ACT_WAIT in acts[100:], f"Rest must return to Wait: {acts[-8:]}"


def test_grounded_rest_left_variant() -> None:
    seed = _seed(pos_y=0.0, facing=0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    b_down = _mk_inputs(buttons=0x0200, main_y=-127)
    outs = _run(seed, [b_down] + [_mk_inputs()] * 2)
    assert int(outs[0]["action_id"][0]) == ACT_PR_LW_L


def test_rest_frame0_invincibility_window() -> None:
    # set_hit_status@0 -> intangible/invincible until the frame-27 restore (script-driven).
    seed = _seed(pos_y=0.0, facing=1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    b_down = _mk_inputs(buttons=0x0200, main_y=-127)
    outs = _run(seed, [b_down] + [_mk_inputs()] * 30)
    hurt = [int(o["hurtbox_state"][0]) for o in outs]
    assert hurt[2] != 0, f"Rest early frames must be invincible/intangible: {hurt[:8]}"
    assert hurt[29] == 0, f"Rest must be vulnerable after the frame-27 restore: {hurt[24:31]}"


def test_air_rest_exits_to_fall() -> None:
    b_down = _mk_inputs(buttons=0x0200, main_y=-127)
    outs = _run(_seed(pos_y=700.0, facing=1), [b_down] + [_mk_inputs()] * 260)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_AIR_LW_R
    assert ACT_FALL in acts[100:] or ACT_FALL_AERIAL in acts[100:], f"tail: {acts[-6:]}"


ACT_PR_HI_R = 367
ACT_PR_AIR_HI_R = 368


def test_grounded_sing_enters_and_exits_to_wait() -> None:
    seed = _seed(pos_y=0.0, facing=1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    b_up = _mk_inputs(buttons=0x0200, main_y=127)
    outs = _run(seed, [b_up] + [_mk_inputs()] * 260)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_HI_R, f"up-B facing right must enter SpecialHiR: {acts[:4]}"
    assert ACT_WAIT in acts[100:], f"Sing must return to Wait: {acts[-8:]}"


def test_air_sing_enters_air_variant() -> None:
    b_up = _mk_inputs(buttons=0x0200, main_y=127)
    outs = _run(_seed(pos_y=700.0, facing=1), [b_up] + [_mk_inputs()] * 3)
    assert int(outs[0]["action_id"][0]) == ACT_PR_AIR_HI_R


# ---------------------------------------------------------------------------
# Rollout (ftPr_MS_SpecialN* 346..362)
# ---------------------------------------------------------------------------
# Anchors: refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c. The charge/roll family holds
# its anim frozen at frame 0 (every internal ChangeMotionState passes anim rate 0), so all the
# exits below are stateful: charge (x2C), turn budget (x0), roll angle (x14), stick, B-release,
# and on-hit. Replay coverage carries only 346/348/350/351/362 (one grounded roll into an NHit);
# Full, End, Turn-exit, and the whole air family live here.

ACT_PR_N_START_R = 346
ACT_PR_N_START_L = 347
ACT_PR_N_LOOP = 348
ACT_PR_N_FULL = 349
ACT_PR_N_RELEASE = 350
ACT_PR_N_TURN = 351
ACT_PR_N_END_R = 352
ACT_PR_N_END_L = 353
ACT_PR_AIR_N_START_R = 354
ACT_PR_AIR_N_LOOP = 356
ACT_PR_AIR_N_FULL = 357
ACT_PR_AIR_N_RELEASE = 358
ACT_PR_AIR_N_END_R = 360
ACT_PR_AIR_N_END_L = 361
ACT_PR_N_HIT = 362
ACT_FALL_SPECIAL = 0x0023
ACT_LANDING_FALL_SPECIAL = 0x002B
BTN_B = 0x0200


def _ground_seed(facing: int = 1, pos_x: float = 0.0, fox_x: float = 60.0) -> np.ndarray:
    seed = _seed(pos_y=0.0, facing=facing)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1)
    seed["pos_x"][0, 0] = np.float32(pos_x)
    seed["pos_x"][0, 1] = np.float32(fox_x)
    return seed


def test_rollout_grounded_entry_start_then_frozen_charge_loop() -> None:
    # Neutral-B from grounded Wait -> SpecialNStartR (facing right); the 16-frame Start anim
    # hands off to the Loop held at frame 0 rate 0 with self_vel.x = facing * 1e-4, gr_vel = 0.
    # refs: ftPr_SpecialN_Enter / ftPr_SpecialNStart_Anim / ftPr_SpecialNStart_Phys
    b = _mk_inputs(buttons=BTN_B)
    outs = _run(_ground_seed(), [b] * 40)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_N_START_R, f"neutral-B must enter StartR: {acts[:4]}"
    li = acts.index(ACT_PR_N_LOOP)
    assert 10 <= li <= 20, f"Start (16f anim) must hand to Loop: first loop row {li}"
    # Frozen loop: action_frame pinned at 0, gr_vel 0, the 1e-4 facing nudge on the air lane.
    tail = outs[li + 2]
    assert int(tail["action_id"][0]) == ACT_PR_N_LOOP
    assert int(tail["action_frame"][0]) == 0
    assert float(tail["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(tail["speed_air_x_self"][0]) == pytest.approx(1e-4, abs=1e-6)


def test_rollout_full_charge_and_release_speed() -> None:
    # Charge x2C: init 50, +3/frame in Loop, Full at the 180 cap (~44 loop frames); releasing B
    # rolls at xC0 * (x2C - xB8) = 0.03 * 140 = 4.2 (flat FD, below both clamps).
    # refs: ftPr_SpecialNLoop_Anim / ftPr_SpecialNFull_IASA / ftPr_SpecialNRelease_Phys
    a = _attrs()
    hold = 16 + 50
    frames = [_mk_inputs(buttons=BTN_B)] * hold + [_mk_inputs()] * 4
    outs = _run(_ground_seed(), frames)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_PR_N_FULL in acts, f"holding B past the cap must reach Full: {acts[55:66]}"
    ri = acts.index(ACT_PR_N_RELEASE)
    v = float(outs[ri]["speed_ground_x_self"][0])
    expect = float(a["puff_rollout_release_vel_scale"]) * (
        float(a["puff_rollout_charge_max"]) - float(a["puff_rollout_charge_min_rolling"])
    )
    assert v == pytest.approx(expect, abs=1e-4), f"full-charge roll speed {v} != {expect}"


def test_rollout_release_constant_speed_and_budget_end_to_wait() -> None:
    # Minimum-hold release: charge decay xB4 is 0, so the roll speed holds constant until the
    # turn budget (90) is spent and the roll angle crosses pi -> EndR -> Wait. Facing preserved.
    # refs: ftPr_SpecialNRelease_Anim / ftPr_SpecialS_8013DA24 / ftPr_SpecialNEnd_Anim
    frames = [_mk_inputs(buttons=BTN_B)] * 18 + [_mk_inputs()] * 150
    outs = _run(_ground_seed(fox_x=-80.0), frames)
    acts = [int(o["action_id"][0]) for o in outs]
    ri = acts.index(ACT_PR_N_RELEASE)
    v0 = float(outs[ri]["speed_ground_x_self"][0])
    assert v0 > 0.0
    assert float(outs[ri + 20]["speed_ground_x_self"][0]) == pytest.approx(v0, abs=1e-5)
    ei = acts.index(ACT_PR_N_END_R)
    assert 90 <= ei - ri <= 120, f"budget-90 roll must end shortly after: release {ri} end {ei}"
    assert int(outs[ei]["facing"][0]) == 1
    assert ACT_WAIT in acts[ei:], f"End must hand to Wait: {acts[-8:]}"


def test_rollout_turn_decel_and_reversal_back_to_release() -> None:
    # Holding the stick opposite the roll -> SpecialNTurn: gr_vel += xC4 * (-0.05 * pre) per
    # frame on flat ground, crosses zero, and exits back to Release past |pre * xD0| with the
    # latched facing flip; the Release Phys then snaps to the charge speed in the new direction.
    # refs: ftPr_SpecialNRelease_IASA / ftPr_SpecialNTurn_Phys / setFacingDir
    a = _attrs()
    frames = (
        [_mk_inputs(buttons=BTN_B)] * 30
        + [_mk_inputs()] * 5
        + [_mk_inputs(main_x=-127)] * 60
    )
    outs = _run(_ground_seed(fox_x=-80.0), frames)
    acts = [int(o["action_id"][0]) for o in outs]
    ri = acts.index(ACT_PR_N_RELEASE)
    ti = acts.index(ACT_PR_N_TURN)
    pre = float(outs[ti - 1]["speed_ground_x_self"][0])
    d1 = pre - float(outs[ti]["speed_ground_x_self"][0])
    expect_step = float(a["puff_rollout_turn_accel"]) * 0.05 * pre
    assert d1 == pytest.approx(expect_step, abs=1e-4), f"turn decel {d1} != {expect_step}"
    # Back to Release, reversed: facing flips and the speed recomputes from charge.
    rj = ti + next(i for i, x in enumerate(acts[ti:]) if x == ACT_PR_N_RELEASE)
    assert int(outs[rj]["facing"][0]) == 0, "post-turn facing must flip left"
    v_after = float(outs[rj + 1]["speed_ground_x_self"][0])
    assert v_after == pytest.approx(-pre, abs=2e-2), f"reversed roll speed {v_after} vs {-pre}"


def test_rollout_air_entry_charge_fall_and_release_landing_transfer() -> None:
    # Air neutral-B -> AirNStartR -> frozen AirChargeLoop falling at the rollout gravity/terminal
    # (x3C/x40); releasing B -> AirChargeRelease with the x58 decel toward the x5C floor; ground
    # contact lands into grounded Release at the preserved frame with gr_vel = |self_vel.x| * dir.
    # refs: ftPr_SpecialAirN_Enter / ftPr_SpecialAirNChargeLoop_Phys /
    #   ftPr_SpecialAirNChargeRelease_{Phys,Coll}
    a = _attrs()
    seed = _seed(pos_y=30.0)
    seed["pos_x"][0, 1] = np.float32(-80.0)
    frames = [_mk_inputs(buttons=BTN_B)] * 21 + [_mk_inputs()] * 30
    outs = _run(seed, frames)
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_PR_AIR_N_START_R
    li = acts.index(ACT_PR_AIR_N_LOOP)
    vy = float(outs[li + 2]["speed_y_self"][0]) - float(outs[li + 3]["speed_y_self"][0])
    term = float(a["puff_rollout_air_terminal_vel"])
    at_term = float(outs[li + 3]["speed_y_self"][0]) == pytest.approx(-term, abs=1e-4)
    assert at_term or vy == pytest.approx(float(a["puff_rollout_air_grav"]), abs=1e-4)
    ri = acts.index(ACT_PR_AIR_N_RELEASE)
    assert float(outs[ri]["speed_air_x_self"][0]) > 0.0
    gi = acts.index(ACT_PR_N_RELEASE)
    assert gi > ri, "air release must land into grounded Release"
    assert int(outs[gi]["on_ground"][0]) == 1
    assert float(outs[gi]["speed_ground_x_self"][0]) > 0.0


def test_rollout_air_budget_end_freefall_special() -> None:
    # A full air roll: budget end -> AirNEnd (facing letter by roll dir), whose anim end enters
    # the ftCo_80096900 freefall with the xD8=30 landing lag (FallSpecial), landing into
    # LandingFallSpecial.
    # refs: ftPr_SpecialAirNChargeRelease_Anim / ftPr_SpecialAirNEnd_Anim
    seed = _seed(pos_y=280.0)
    seed["pos_x"][0, 0] = np.float32(-40.0)
    seed["pos_x"][0, 1] = np.float32(-80.0)
    frames = [_mk_inputs(buttons=BTN_B)] * 18 + [_mk_inputs()] * 340
    outs = _run(seed, frames)
    acts = [int(o["action_id"][0]) for o in outs]
    ri = acts.index(ACT_PR_AIR_N_RELEASE)
    ei = next(i for i, x in enumerate(acts) if x in (ACT_PR_AIR_N_END_R, ACT_PR_AIR_N_END_L))
    assert 90 <= ei - ri <= 130, f"air roll must end after the 90-frame budget: {ri}..{ei}"
    fi = next((i for i, x in enumerate(acts[ei:]) if x == ACT_FALL_SPECIAL), None)
    assert fi is not None and fi <= 40, f"AirNEnd must exit to FallSpecial: {acts[ei:ei+40]}"
    assert ACT_LANDING_FALL_SPECIAL in acts[ei:], f"freefall must land with lag: {acts[-8:]}"


def test_rollout_on_hit_enters_nhit_with_backward_hop() -> None:
    # Rolling into a grounded opponent: the deal_dmg cb (8013D764) exits to SpecialNHit at the
    # preserved frame with self_vel.x = gr_vel * specialn_vel.x (-0.13 backward hop) and
    # self_vel.y = specialn_vel.y (+1.6); landing takes the xD8=30 LandingFallSpecial lag.
    # refs: ftPr_SpecialS_8013D764 / ftPr_SpecialNHit_Coll
    a = _attrs()
    frames = [_mk_inputs(buttons=BTN_B)] * 46 + [_mk_inputs()] * 120
    outs = _run(_ground_seed(fox_x=30.0), frames)
    acts = [int(o["action_id"][0]) for o in outs]
    ri = acts.index(ACT_PR_N_RELEASE)
    hi = acts.index(ACT_PR_N_HIT)
    assert hi > ri, "the roll must connect and exit to NHit"
    roll_v = float(outs[hi - 1]["speed_ground_x_self"][0])
    o = outs[hi]
    assert int(o["on_ground"][0]) == 0, "NHit is airborne"
    assert float(o["speed_air_x_self"][0]) == pytest.approx(
        roll_v * float(a["puff_rollout_hit_vel_x_mul"]), abs=2e-2
    )
    assert float(o["speed_y_self"][0]) == pytest.approx(
        float(a["puff_rollout_hit_vel_y"]), abs=0.1
    )
    assert ACT_LANDING_FALL_SPECIAL in acts[hi:], f"NHit landing takes the special lag: {acts[-8:]}"


# ---------------------------------------------------------------------------
# DamageSong victim family (297..299; Sing's sleep effect)
# ---------------------------------------------------------------------------
# Anchors: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageSong.c and the ftCo_8008E908
# element-6/7 dispatch. No suite replay carries a landed Sing; these tests own the family.

ACT_DAMAGE_SONG = 297
ACT_DAMAGE_SONG_WAIT = 298
ACT_DAMAGE_SONG_RV = 299


def _common() -> dict:
    return json.loads((ROOT / "data" / "common" / "ft_common_data.json").read_text())


def _mk_inputs2(p0: dict | None = None, p1: dict | None = None) -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    for k, v in (p0 or {}).items():
        inp["p"][0, 0][k] = v
    for k, v in (p1 or {}).items():
        inp["p"][0, 1][k] = v
    return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))


def _sing_seed(fox_percent: float = 0.0, fox_air: bool = False, handicap: int = 9) -> np.ndarray:
    seed = _ground_seed(facing=1, pos_x=0.0, fox_x=8.0)
    seed["percent"][0, 1] = np.float32(fox_percent)
    seed["handicap"][0, :2] = np.uint8(handicap)
    if fox_air:
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["pos_y"][0, 1] = np.float32(500.0)
        seed["action_id"][0, 1] = np.uint16(ACT_FALL)
        seed["animation_index"][0, 1] = np.uint32(SM_FALL)
    return seed


def _song_timer_expect(c: dict, percent: float, victim_slot: int, handicap: int) -> float:
    return (
        c["damagesong_timer_base"]
        + c["damagesong_timer_handicap_mul"] * (c["damagesong_timer_handicap_base"] - handicap)
        + c["damagesong_timer_slot_mul"] * (c["damagesong_timer_slot_base"] - victim_slot)
        + percent * c["damagesong_timer_percent_mul"]
    )


def test_sing_puts_grounded_victim_to_sleep_for_formula_duration() -> None:
    # Element-6 grounded hit -> DamageSong with grab_timer = the inlineA0 duration; the start
    # anim hands to the sleep loop; expiry -> DamageSongRv -> Wait.
    c = _common()
    b_up = _mk_inputs2(p0={"buttons": BTN_B, "main_y": 127})
    idle = _mk_inputs2()
    outs = _run(_sing_seed(), [b_up] + [idle] * 320)
    acts1 = [int(o["action_id"][1]) for o in outs]
    si = acts1.index(ACT_DAMAGE_SONG)
    assert 27 <= si <= 34, f"Sing's frame-28 hitbox must sleep grounded fox: first sleep row {si}"
    assert ACT_DAMAGE_SONG_WAIT in acts1[si:], "start anim must hand to the sleep loop"
    ri = acts1.index(ACT_DAMAGE_SONG_RV)
    dur = ri - si
    expect = _song_timer_expect(c, 0.0, victim_slot=2, handicap=9)
    assert abs(dur - expect) <= 3, f"sleep duration {dur} vs formula {expect}"
    assert ACT_WAIT in acts1[ri:], f"DamageSongRv must exit to Wait: {acts1[-6:]}"


def test_sing_sleep_duration_scales_with_percent() -> None:
    c = _common()
    b_up = _mk_inputs2(p0={"buttons": BTN_B, "main_y": 127})
    idle = _mk_inputs2()
    outs = _run(_sing_seed(fox_percent=50.0), [b_up] + [idle] * 420)
    acts1 = [int(o["action_id"][1]) for o in outs]
    si = acts1.index(ACT_DAMAGE_SONG)
    ri = acts1.index(ACT_DAMAGE_SONG_RV)
    expect = _song_timer_expect(c, 50.0, victim_slot=2, handicap=9)
    assert abs((ri - si) - expect) <= 3, f"sleep duration {ri - si} vs formula {expect}"


def test_sing_mash_shortens_sleep() -> None:
    # ftCommon_GrabMash: each stick sign flip past the mash threshold takes x640 (7) extra
    # frames off the timer.
    b_up = _mk_inputs2(p0={"buttons": BTN_B, "main_y": 127})
    idle = _mk_inputs2()
    mash = [
        _mk_inputs2(p1={"main_x": 127 if (i % 2 == 0) else -127}) for i in range(320)
    ]
    outs_idle = _run(_sing_seed(), [b_up] + [idle] * 320)
    outs_mash = _run(_sing_seed(), [b_up] + mash)
    a_idle = [int(o["action_id"][1]) for o in outs_idle]
    a_mash = [int(o["action_id"][1]) for o in outs_mash]
    d_idle = a_idle.index(ACT_DAMAGE_SONG_RV) - a_idle.index(ACT_DAMAGE_SONG)
    d_mash = a_mash.index(ACT_DAMAGE_SONG_RV) - a_mash.index(ACT_DAMAGE_SONG)
    assert d_mash < d_idle - 20, f"mash must shorten sleep: idle {d_idle} mash {d_mash}"


def test_sing_does_not_sleep_airborne_victim() -> None:
    # The Sing hitbox payload is hit_grounded-only (hit_aerial False); an airborne fox never
    # enters the sleep family.
    b_up = _mk_inputs2(p0={"buttons": BTN_B, "main_y": 127})
    idle = _mk_inputs2()
    # Keep fox airborne through the whole frame-28..126 hitbox window.
    outs = _run(_sing_seed(fox_air=True), [b_up] + [idle] * 130)
    acts1 = [int(o["action_id"][1]) for o in outs]
    assert ACT_DAMAGE_SONG not in acts1, f"airborne fox must not sleep: {sorted(set(acts1))}"


# ---------------------------------------------------------------------------
# Cliff catch during air Sing / Rollout air release / the multijump ladder
# ---------------------------------------------------------------------------
# Coll ledge modes: the multijump ladder shares ftCo_JumpAerialF1_Coll (ft_80082F28,
# facing-direction ledge boxes); air Sing passes CLIFFCATCH_BOTH (0) into
# ft_CheckGroundAndLedge so BOTH ledge sides are live regardless of facing; Rollout
# AirChargeRelease passes the roll direction (mv x34.x) into ft_8008239C. Air Pound/Rest use
# ft_80081D0C (no ledge).
# refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_JumpAerialF1_Coll
# refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialHi.c::ftPr_SpecialAirHi_Coll
# refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
# refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialLw.c::ftPr_SpecialAirLw_Coll

from tests.stage_metadata_helpers import fd_stage_segments  # noqa: E402

ACT_CLIFF_CATCH = 0x00FC
ACT_PR_AIR_HI_L = 366
SM_PR_AIR_HI_L = 320
SM_PR_AIR_HI_R = 322
ACT_PR_AIR_LW_R = 372
SM_PR_AIR_LW_R = 326
SM_PR_AIR_N_RELEASE = 312
SM_PR_JUMP_F1 = 295


def _fd_left_ledge() -> tuple[float, float]:
    best_x = float("inf")
    pt = None
    for seg in fd_stage_segments():
        if seg.get("kind") != "floor" or bool(seg.get("platform")) or not bool(seg.get("ledge")):
            continue
        for k in (("x0", "y0"), ("x1", "y1")):
            x, y = float(seg[k[0]]), float(seg[k[1]])
            if x < best_x:
                best_x = x
                pt = (x, y)
    assert pt is not None
    return pt


def _ledge_seed(action: int, msid: int, *, facing: int, vx: float = 0.0,
                frame: float = 10.0) -> np.ndarray:
    lx, ly = _fd_left_ledge()
    a = _attrs()
    seed = _seed(pos_y=float(ly - a["ledge_snap_y"]), facing=facing)
    seed["pos_x"][0, 0] = np.float32(lx - 1.0)
    seed["action_id"][0, 0] = np.uint16(action)
    seed["action_frame"][0, 0] = np.int16(int(frame))
    seed["anim_frame_f32"][0, 0] = np.float32(frame)
    seed["animation_index"][0, 0] = np.uint32(msid)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)
    seed["speed_air_x_self"][0, 0] = np.float32(vx)
    return seed


def test_air_sing_catches_ledge_both_facings() -> None:
    # CLIFFCATCH_BOTH: the left-ledge box is live even when facing away from stage.
    for action, msid, facing in (
        (ACT_PR_AIR_HI_R, SM_PR_AIR_HI_R, 1),
        (ACT_PR_AIR_HI_L, SM_PR_AIR_HI_L, 0),
    ):
        outs = _run(_ledge_seed(action, msid, facing=facing), [_mk_inputs()])
        assert int(outs[0]["action_id"][0]) == ACT_CLIFF_CATCH, (
            f"air Sing {action} facing {facing} must ledge-catch"
        )


def test_air_rest_does_not_ledge_catch() -> None:
    # ftPr_SpecialAirLw_Coll uses ft_80081D0C: no cliff catch site.
    outs = _run(_ledge_seed(ACT_PR_AIR_LW_R, SM_PR_AIR_LW_R, facing=1), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) != ACT_CLIFF_CATCH


def test_multijump_ledge_catch_is_facing_gated() -> None:
    # ftCo_JumpAerialF1_Coll (ft_80082F28): facing-direction ledge boxes, so the left ledge
    # only catches while facing into stage (+X).
    outs = _run(_ledge_seed(ACT_PR_F1, SM_PR_JUMP_F1, facing=1), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_CLIFF_CATCH
    outs = _run(_ledge_seed(ACT_PR_F1, SM_PR_JUMP_F1, facing=0), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) != ACT_CLIFF_CATCH


def test_rollout_air_release_ledge_catch_follows_roll_direction() -> None:
    # ft_8008239C(dir = x34.x): the ledge boxes key on the roll direction, not facing. The
    # frozen release row reconstructs dir from the velocity sign, so rolling toward the stage
    # (+X at the left ledge) catches even facing away, and rolling away never catches.
    # Small +X speed keeps the post-integration ECB bottom short of the ledge x (the
    # mpColl_80044164 `bottom.x < edge.x` gate).
    outs = _run(
        _ledge_seed(ACT_PR_AIR_N_RELEASE, SM_PR_AIR_N_RELEASE, facing=0, vx=0.5, frame=0.0),
        [_mk_inputs()],
    )
    assert int(outs[0]["action_id"][0]) == ACT_CLIFF_CATCH, "roll toward stage must catch"
    outs = _run(
        _ledge_seed(ACT_PR_AIR_N_RELEASE, SM_PR_AIR_N_RELEASE, facing=1, vx=-0.5, frame=0.0),
        [_mk_inputs()],
    )
    assert int(outs[0]["action_id"][0]) != ACT_CLIFF_CATCH, "roll away from stage must not catch"


ACT_PR_AIR_N_START_TURN = 359
SM_PR_N_TURN = 305


def _turn_edge_seed(gr_vel: float) -> np.ndarray:
    lx, ly = _fd_left_ledge()
    seed = _ground_seed(facing=1, pos_x=float(lx + 0.5))
    seed["pos_y"][0, 0] = np.float32(ly)
    seed["action_id"][0, 0] = np.uint16(ACT_PR_N_TURN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["animation_index"][0, 0] = np.uint32(SM_PR_N_TURN)
    seed["speed_ground_x_self"][0, 0] = np.float32(gr_vel)
    return seed


def test_rollout_turn_slow_coll_edge_snaps_and_stays_grounded() -> None:
    # ftPr_SpecialNTurn_Coll at |gr_vel| <= x74: ft_80082978's mpColl_8004A45C_Floor endpoint
    # snap pins the turn to the floor edge instead of losing the floor.
    # refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNTurn_Coll
    lx, _ = _fd_left_ledge()
    outs = _run(_turn_edge_seed(-2.0), [_mk_inputs()])
    o = outs[0]
    assert int(o["action_id"][0]) == ACT_PR_N_TURN, "slow turn must stay grounded at the edge"
    assert int(o["on_ground"][0]) == 1
    assert float(o["pos_x"][0]) >= lx - 1e-3

def test_rollout_turn_fast_coll_loses_floor_to_air_turn() -> None:
    # Above x74 the ft_80082888 wrapper lets the floor loss fire: grounded Turn swaps to
    # SpecialAirNStartTurn at the preserved frame.
    a = _attrs()
    thr = float(a["puff_rollout_turn_coll_vel_threshold"])
    outs = _run(_turn_edge_seed(-(thr + 2.0)), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_PR_AIR_N_START_TURN
    assert int(outs[0]["on_ground"][0]) == 0


def test_sing_contact_applies_no_hitlag_hitstun_or_facing_flip() -> None:
    # ftCo_8008E908 element-6 arm -> ftCo_800C318C: the 0-damage sleep contact starts no hitlag
    # on either side (Fighter_ProcessHit gates hitlag on nonzero applied damage), never reaches
    # ftCo_8008DCE0 (no hitstun, no victim facing flip), and 800C318C does not run the post-entry
    # anim tick (the entry row publishes action frame 0).
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageSong.c::ftCo_800C318C
    b_up = _mk_inputs2(p0={"buttons": BTN_B, "main_y": 127})
    idle = _mk_inputs2()
    seed = _sing_seed()
    # Victim standing to the RIGHT of Puff facing right (fox_x=8): fox facing right too, so a
    # generic damage entry would flip fox to face the attacker (left). Sleep must not.
    seed["facing"][0, 1] = np.uint8(1)
    outs = _run(seed, [b_up] + [idle] * 40)
    acts1 = [int(o["action_id"][1]) for o in outs]
    si = acts1.index(ACT_DAMAGE_SONG)
    o = outs[si]
    assert int(o["action_frame"][1]) == 0, "800C318C entry row publishes frame 0 (no entry tick)"
    assert int(o["hitlag"][1]) == 0, "0-damage sleep contact must not start victim hitlag"
    assert int(o["hitlag"][0]) == 0, "0-damage sleep contact must not start attacker hitlag"
    assert int(o["hitstun"][1]) == 0, "sleep entry bypasses ftCo_8008DCE0 hitstun"
    assert int(o["facing"][1]) == 1, "sleep entry must not flip victim facing"


ACT_ATTACK_AIR_LW = 69
SM_ATTACK_AIR_LW = 72
ACT_LANDING_AIR_LW = 74


def test_dair_posed_ecb_bottom_below_root_does_not_land_early() -> None:
    # Puff's dair pose dips the ECB source joints ~2 units below TransN, but
    # mpColl_LoadECB_JObj clamps the desired bottom to the root (`bottom_y < 0 -> 0`), so landing
    # only fires when the ROOT sweep crosses the floor.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
    def dair_seed(y0: float) -> np.ndarray:
        seed = _seed(pos_y=y0)
        seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_LW)
        seed["action_frame"][0, 0] = np.int16(12)
        seed["anim_frame_f32"][0, 0] = np.float32(12.0)
        seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_LW)
        seed["speed_y_self"][0, 0] = np.float32(-1.0)
        return seed

    # Root ends the step above the FD floor (y=0): the posed bottom (-2.15 at frame 13) would
    # cross, the clamped bottom must not.
    outs = _run(dair_seed(1.5), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_ATTACK_AIR_LW, "clamped bottom must not land early"
    assert int(outs[0]["on_ground"][0]) == 0
    # Root crossing the floor lands normally.
    outs = _run(dair_seed(0.5), [_mk_inputs()])
    assert int(outs[0]["action_id"][0]) == ACT_LANDING_AIR_LW
    assert int(outs[0]["on_ground"][0]) == 1
