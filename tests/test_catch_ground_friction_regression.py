from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_CATCH = 0x00D4
SM_WAIT = 2
SM_FALL = 20
SM_CATCH = 242
STAGE_FD = 32
CHAR_FOX = 1


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _call(binding, name: str, *args):
    fn = getattr(binding, name, None)
    if fn is None:
        fn = getattr(binding, f"{name}_v0")
    return fn(*args)


def _destroy(binding, handle) -> None:
    fn = getattr(binding, "destroy", None)
    if fn is not None:
        fn(handle)


def _step_seed(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")

    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    prev_input_bytes = prev_input.view(np.uint8).reshape((1, input_stride))
    input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        _call(binding, "reseed_seed", handle, seed_bytes)
        _call(binding, "step_input", handle, prev_input_bytes, input_bytes)
        _call(binding, "write_compare", handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        _destroy(binding, handle)

    return out


def _base_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_CATCH)
    seed["animation_index"][0, 0] = np.uint32(SM_CATCH)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(0)
    seed["pos_x"][0, 1] = np.float32(20.0)
    seed["pos_y"][0, 1] = np.float32(0.0001)
    return seed


def test_catch_grounded_phys_uses_ftcommon_x64_friction() -> None:
    root = Path(__file__).resolve().parents[1]
    fox = json.loads((root / "data/characters/fox.json").read_text())
    common = json.loads((root / "data/common/ft_common_data.json").read_text())

    gr_friction = float(fox["gr_friction"])
    catch_friction_mul = float(common["catch_friction_mul"])
    seed_speed = 1.6925
    expected_speed = seed_speed - min(seed_speed, catch_friction_mul * gr_friction)

    seed = _base_seed()
    seed["speed_ground_x_self"][0, 0] = np.float32(seed_speed)
    seed["speed_air_x_self"][0, 0] = np.float32(seed_speed)

    out = _step_seed(seed)

    assert int(out["action_id"][0]) == ACT_CATCH
    assert abs(float(out["speed_ground_x_self"][0]) - expected_speed) <= 1e-5
    assert abs(float(out["pos_x"][0]) - expected_speed) <= 1e-5


def test_catch_collision_uses_b2dc_floor_edge_snap_before_floor_loss_callback() -> None:
    # Catch_Coll routes through ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC. The flags=2
    # mpColl path calls mpColl_8004A45C_Floor and snaps to the floor endpoint before invoking the
    # fn_800D8E30 floor-loss callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_Catch_Coll,fn_800D8E30}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_800827A0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    seed = _base_seed()
    seed["pos_x"][0, 0] = np.float32(-85.60087585449219)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.854806)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.854806)
    seed["facing"][0, 0] = np.uint8(0)

    out = _step_seed(seed)

    assert int(out["action_id"][0]) == ACT_CATCH
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(-85.5657, abs=0.001)


def test_catch_floor_loss_callback_still_runs_without_current_floor_owner() -> None:
    # Negative control: the edge snap above requires a valid current floor line. If the grounded
    # seed lacks that CollData floor owner, ft_800827A0 cannot accept mpColl_8004B2DC and
    # ft_800841B8 invokes fn_800D8E30 into Fall.
    seed = _base_seed()
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["animation_index"][0, 0] = np.uint32(SM_CATCH)
    seed["pos_x"][0, 0] = np.float32(-85.60087585449219)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.854806)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.854806)

    out = _step_seed(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["animation_index"][0]) == SM_FALL
    assert int(out["on_ground"][0]) == 0
