from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 14
SM_WAIT = 2
CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_POKEMON_STADIUM = 3
ITEM_FALCO_LASER = 55


def _step_seed(seed: np.ndarray, *, steps: int = 1) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        for _ in range(steps):
            binding.step_input(handle, inp, inp)
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _seed_with_laser(*, x: float, y: float, vel_x: float, vel_y: float) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_POKEMON_STADIUM)
    seed["num_players"][0] = np.uint8(2)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8([CHAR_FOX, CHAR_FALCO])
    seed["facing"][0, :2] = np.uint8([1, 0])
    seed["facing_dir1"][0, :2] = np.int8([1, -1])
    seed["pos_x"][0, :2] = np.float32([150.0, -150.0])
    seed["pos_y"][0, :2] = np.float32([80.0, 80.0])
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    seed["ground_id"][0, :2] = np.uint16(0xFFFF)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["ground_friction_mul"][0, :2] = np.float32(1.0)
    seed["shield_hp"][0, :2] = np.float32(60.0)

    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_FALCO_LASER)
    item["owner"] = np.int8(1)
    item["state"] = np.uint8(0)
    item["instance_id"] = np.uint16(1)
    item["spawn_id"] = np.uint32(1)
    item["direction"] = np.float32(1.0 if vel_x >= 0.0 else -1.0)
    item["pos_x"] = np.float32(x)
    item["pos_y"] = np.float32(y)
    item["vel_x"] = np.float32(vel_x)
    item["vel_y"] = np.float32(vel_y)
    item["timer"] = np.float32(99.0)
    return seed


@pytest.mark.integration
def test_pokemon_laser_ignores_inactive_transformation_wall_lines() -> None:
    # Source owner: it_8029C4D4 -> it_8026E9A4 tests active mpLib collision lines. Frozen Pokemon
    # Stadium keeps transformation geometry in MSLSTG01 for debug/data, but marks it
    # fighter_solid=0 because the runtime active line set excludes it. A left-moving Falco laser at
    # this height crosses transformation segment 95 in the extracted graph and should not be deleted
    # by that invisible wall.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
    # refs/melee/src/melee/it/it_266F.c::it_8026E9A4
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # data/stages/pokemon_stadium.json::segments[95]
    out = _step_seed(_seed_with_laser(x=-42.84, y=13.75, vel_x=-5.0, vel_y=0.0), steps=4)
    item = out["items"][0]
    assert int(item["exists"]) == 1
    assert float(item["timer"]) == pytest.approx(95.0)
    assert float(item["pos_x"]) == pytest.approx(-62.84)


@pytest.mark.integration
def test_pokemon_laser_still_hits_active_main_floor() -> None:
    # Negative boundary: runtime-solid lines remain laser collision owners.
    out = _step_seed(_seed_with_laser(x=0.0, y=5.0, vel_x=0.0, vel_y=-10.0))
    item = out["items"][0]
    assert int(item["exists"]) == 1
    assert float(item["timer"]) == pytest.approx(1.0)
