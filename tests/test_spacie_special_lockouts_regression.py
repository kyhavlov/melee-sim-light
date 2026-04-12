from __future__ import annotations

import importlib

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_KNEE_BEND = 24
ACT_FALL_SPECIAL = 35
ACT_DAMAGE_FALL = 38
ACT_FX_SPECIAL_AIR_HI_HOLD = 354
ACT_FX_SPECIAL_AIR_LW_START = 365
ACT_FX_SPECIAL_LW_START = 360

BUTTON_B = 0x0200

CHAR_FOX = 1
STAGE_FD = 32


def _stick_to_i8(v: float) -> np.int8:
  vv = max(-1.0, min(1.0, float(v)))
  return np.int8(int(np.clip(np.rint(((vv + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _mk_seed(*, action_id: int, on_ground: bool, speed_y_self: float = 0.0) -> np.ndarray:
  seed = np.zeros((1,), dtype=SEED_DTYPE)
  seed["frame_id"][0] = np.int32(0)
  seed["stage_id"][0] = np.uint32(STAGE_FD)
  seed["match_damage_ratio"][0] = np.float32(1.0)
  seed["num_players"][0] = np.uint8(2)
  seed["stocks"][0, :2] = np.uint8(4)
  seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
  seed["attack_ratio"][0, :2] = np.float32(1.0)
  seed["defense_ratio"][0, :2] = np.float32(1.0)
  seed["fighter_scale_y"][0, :2] = np.float32(1.0)
  seed["facing"][0, :2] = np.uint8(1)
  seed["action_id"][0, 0] = np.uint16(action_id)
  seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
  seed["anim_frame_f32"][0, 0] = np.float32(0.0)
  seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
  seed["on_ground"][0, :2] = np.uint8(1 if on_ground else 0)
  seed["ground_id"][0, :2] = np.uint16(0)
  seed["pos_y"][0, :2] = np.float32(5.0 if on_ground else 30.0)
  seed["speed_y_self"][0, 0] = np.float32(speed_y_self)
  return seed


def _mk_input(*, stick_x: float, stick_y: float, b: bool = True) -> np.ndarray:
  arr = np.zeros((1,), dtype=INPUT_DTYPE)
  if b:
    arr["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
  arr["p"]["main_x"][0, 0] = _stick_to_i8(stick_x)
  arr["p"]["main_y"][0, 0] = _stick_to_i8(stick_y)
  return arr


def _run_step(seed: np.ndarray, prev_input: np.ndarray, input_t: np.ndarray):
  binding = importlib.import_module("msl_binding")
  sizes = binding.sizes()
  seed_stride = int(sizes["seed"])
  input_stride = int(sizes["input"])
  compare_stride = int(sizes["compare"])

  handle = binding.init(batch_size=1, num_players=2)
  try:
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
    prev_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    binding.reseed_seed(handle, seed_bytes)
    binding.step_input(handle, prev_bytes, input_bytes)
    binding.write_compare(handle, out_compare_bytes)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
  finally:
    binding.destroy(handle)


def test_fallspecial_blocks_aerial_shine_and_bspecial_reentry() -> None:
  # Helpless FallSpecial IASA does not route through ftCo_SpecialAir_CheckInput.
  # It only checks attack/item/jump-owned branches, so Fox cannot re-enter Shine, Up-B, or Side-B
  # during the post-Illusion / post-Firefox fall before landing.
  # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_IASA
  seed = _mk_seed(action_id=ACT_FALL_SPECIAL, on_ground=False)
  prev_input = _mk_input(stick_x=0.0, stick_y=0.0, b=False)

  for stick_x, stick_y in ((0.0, -1.0), (0.0, 1.0), (1.0, 0.0)):
    out = _run_step(seed, prev_input, _mk_input(stick_x=stick_x, stick_y=stick_y))
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL


def test_kneebend_blocks_grounded_shine_and_bspecial_reentry() -> None:
  # KneeBend IASA checks Attack100, Catch, AttackHi4, then short-hop bookkeeping only.
  # It does not route through grounded special dispatch, so fresh shield-origin KneeBend / jump
  # startup cannot re-enter Shine or grounded B-specials.
  # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
  seed = _mk_seed(action_id=ACT_KNEE_BEND, on_ground=True)
  prev_input = _mk_input(stick_x=0.0, stick_y=0.0, b=False)

  for stick_x, stick_y in ((0.0, -1.0), (0.0, 1.0), (1.0, 0.0)):
    out = _run_step(seed, prev_input, _mk_input(stick_x=stick_x, stick_y=stick_y))
    assert int(out["action_id"][0]) == ACT_KNEE_BEND


def test_damagefall_downb_entry_clears_vertical_speed_on_aerial_shine() -> None:
  # Aerial Reflector entry zeroes self_vel.y immediately.
  # This is the key anti-hover owner for DamageFall -> SpecialAirLwStart.
  # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
  seed = _mk_seed(action_id=ACT_DAMAGE_FALL, on_ground=False, speed_y_self=4.0)
  prev_input = _mk_input(stick_x=0.0, stick_y=0.0, b=False)
  out = _run_step(seed, prev_input, _mk_input(stick_x=0.0, stick_y=-1.0))
  assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_LW_START
  assert float(out["speed_y_self"][0]) == 0.0
