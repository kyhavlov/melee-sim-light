from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_B = 0x0200

ACT_WAIT = 0x000E
ACT_DASH = 0x0014
ACT_FX_SPECIAL_S_START = 0x015B
ACT_FX_SPECIAL_HI_HOLD = 0x0161
ACT_FX_SPECIAL_AIR_HI = 0x0164
ACT_FX_SPECIAL_HI = 0x0163
ACT_FX_SPECIAL_S = 0x015C
ACT_FX_SPECIAL_AIR_S_START = 0x015E
ACT_FX_SPECIAL_AIR_S = 0x015F
ACT_FX_SPECIAL_S_END = 0x015D
ACT_FALL = 0x001D

SM_WAIT1_0 = 2
SM_DASH = 12
SM_FALL = 20

CHAR_FOX = 1
STAGE_FD = 32


def _fox_attr(name: str) -> float:
    return float(json.loads(Path("data/characters/fox.json").read_text(encoding="utf-8"))[name])


def _fox_special_msid(path: str) -> int:
    data = json.loads(Path("data/special_msids/fox.json").read_text(encoding="utf-8"))
    cur = data
    for key in path.split("."):
        cur = cur[key]
    assert cur is not None
    return int(cur)


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["facing_dir1"][0, :2] = np.int8(1)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    return seed


def _step_once(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def test_grounded_side_special_entry_divides_ground_speed_by_illusion_attr() -> None:
    seed = _seed_base()
    seed["speed_ground_x_self"][0, 0] = np.float32(1.2)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_S_START
    assert int(out["on_ground"][0]) == 1
    # Decomp: common ftCo_SpecialS::doEnter damps gr_vel by co_attrs.xB8 before Fox/Falco
    # ftFx_SpecialSStart_Enter divides gr_vel by x28. The same-step ftFx_SpecialSStart_Phys
    # callback then runs ft_80084F3C grounded friction before the row is observed.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialSStart_Enter,ftFx_SpecialSStart_Phys}
    damped = 1.2 * _fox_attr("side_special_ground_entry_vel_mul")
    expected = (damped / _fox_attr("illusion_ground_vel_x")) - _fox_attr("gr_friction")
    assert abs(float(out["speed_ground_x_self"][0]) - expected) <= 1e-6


def test_dash_side_special_entry_runs_dash_iasa_terminal_scalar() -> None:
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DASH)
    seed["facing"][0, 0] = np.uint8(0)
    seed["facing_dir1"][0, 0] = np.int8(-1)
    seed["speed_ground_x_self"][0, 0] = np.float32(-2.0625)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.0625)
    if "dash_x4" in seed.dtype.names:
        seed["dash_x4"][0, 0] = np.uint8(1)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_x"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_S_START
    assert int(out["on_ground"][0]) == 1
    # Source order:
    # - ftCo_Dash_IASA early branch enters Side-B through ftCo_SpecialS_CheckInput/doEnter.
    # - The Dash_IASA function then falls through to its terminal x54 gr_vel scalar.
    # - The entered SpecialSStart Phys callback runs ft_80084F3C after that scalar, clearing this
    #   small residual to zero instead of carrying Dash's root-motion velocity into Side-B.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["speed_air_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_aerial_side_special_entry_does_not_use_ground_xb8_damping() -> None:
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["pos_y"][0, 0] = np.float32(8.0)
    seed["speed_air_x_self"][0, 0] = np.float32(1.2)
    seed["speed_y_self"][0, 0] = np.float32(0.5)
    seed["jumps_left"][0, 0] = np.uint8(2)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_S_START
    assert int(out["on_ground"][0]) == 0
    # Aerial entry goes through ftCo_SpecialAir then ftFx_SpecialAirSStart_Enter; it divides
    # self_vel.x by x28 and zeroes self_vel.y, but does not run the grounded doEnter xB8 damping.
    # The same-step aerial start Phys then applies the Side-B start air friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialAirSStart_Enter,ftFx_SpecialAirSStart_Phys}
    expected = (1.2 / _fox_attr("illusion_ground_vel_x")) - _fox_attr("illusion_air_friction_start")
    assert float(out["speed_air_x_self"][0]) == pytest.approx(expected, abs=1e-6)
    assert float(out["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_grounded_specialhi_hold_entry_divides_ground_speed_by_hold_attr() -> None:
    seed = _seed_base()
    seed["speed_ground_x_self"][0, 0] = np.float32(1.6)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_y"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_HI_HOLD
    assert int(out["on_ground"][0]) == 1
    assert abs(float(out["speed_ground_x_self"][0]) - (1.6 / _fox_attr("firefox_hold_vel_x"))) <= 1e-6


def test_grounded_specialhi_hold_end_on_flat_ground_seeds_ground_launch_speed() -> None:
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_HI_HOLD)
    seed["animation_index"][0, 0] = np.uint32(_fox_special_msid("up_ground.hold.default"))
    seed["action_frame"][0, 0] = np.int16(99)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    seed["facing"][0, 0] = np.uint8(0)
    seed["facing_dir1"][0, 0] = np.int8(-1)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_x"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_HI
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) < 10.0
    assert abs(float(out["speed_ground_x_self"][0]) + _fox_attr("firefox_launch_speed")) <= 1e-6
    assert float(out["speed_y_self"][0]) == 0.0


def test_grounded_specialhi_hold_end_with_up_input_enters_air_launch() -> None:
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_HI_HOLD)
    seed["animation_index"][0, 0] = np.uint32(_fox_special_msid("up_ground.hold.default"))
    seed["action_frame"][0, 0] = np.int16(99)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_y"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(out["on_ground"][0]) == 0
    assert float(out["pos_y"][0]) >= 0.0
    assert abs(float(out["speed_air_x_self"][0])) <= 1e-5
    assert abs(float(out["speed_y_self"][0]) - _fox_attr("firefox_launch_speed")) <= 1e-4


def _step_twice(seed: np.ndarray, prev0: np.ndarray, inp0: np.ndarray, prev1: np.ndarray, inp1: np.ndarray):
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
      seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
      out0 = np.zeros((1, compare_stride), dtype=np.uint8)
      out1 = np.zeros((1, compare_stride), dtype=np.uint8)
      binding.reseed_seed(handle, seed_bytes)
      binding.step_input(handle, prev0, inp0)
      binding.write_compare(handle, out0)
      binding.step_input(handle, prev1, inp1)
      binding.write_compare(handle, out1)
      return out0.view(COMPARE_DTYPE).reshape((1,))[0].copy(), out1.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
      binding.destroy(handle)


def test_grounded_side_special_walkoff_promotes_to_air_side_special_next_frame() -> None:
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_S)
    seed["animation_index"][0, 0] = np.uint32(_fox_special_msid("side_ground.main.default"))
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["pos_x"][0, 0] = np.float32(90.0)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    z = _mk_input_bytes(1, input_stride)
    out0, out1 = _step_twice(seed, z, z, z, z)
    assert int(out0["on_ground"][0]) == 0
    assert int(out1["action_id"][0]) == ACT_FX_SPECIAL_AIR_S


def test_grounded_side_special_end_edge_snap_stays_grounded_next_frame() -> None:
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_S_END)
    seed["animation_index"][0, 0] = np.uint32(_fox_special_msid("side_ground.end.default"))
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["pos_x"][0, 0] = np.float32(90.0)

    binding = pytest.importorskip("msl_binding")
    input_stride = int(binding.sizes()["input"])
    z = _mk_input_bytes(1, input_stride)
    out0, out1 = _step_twice(seed, z, z, z, z)
    # Decomp: grounded Fox/Falco SpecialSEnd_Coll uses ft_800827A0 -> mpColl_8004B2DC, whose
    # mpColl_8004A45C_Floor edge fallback can keep this endpoint case grounded instead of
    # converting directly to Fall.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800827A0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    assert int(out0["on_ground"][0]) == 1
    assert int(out0["action_id"][0]) == ACT_FX_SPECIAL_S_END
    assert int(out1["on_ground"][0]) == 1
    assert int(out1["action_id"][0]) == ACT_FX_SPECIAL_S_END
