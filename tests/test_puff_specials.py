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
