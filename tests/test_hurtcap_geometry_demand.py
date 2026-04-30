from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE

ACT_WAIT = 14
ACT_DAMAGE_HI_1 = 85
ACT_ATTACK_AIR_N = 65
ACT_CATCH = 212
ACT_CATCH_PULL = 213
ACT_CAPTURE_PULLED_HI = 223
ACT_REBIRTH_WAIT = 13

SM_WAIT1_0 = 2
SM_ATTACK_AIR_N = 68
SM_CATCH = 242

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32


def _require_collision_artifacts_or_skip() -> None:
    required = [
        Path("data/anims/fox.bin"),
        Path("data/anims/falco.bin"),
        Path("data/hurtcaps/fox.bin"),
        Path("data/hurtcaps/falco.bin"),
        Path("data/moves/fox.json"),
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        pytest.skip(f"missing local collision artifacts: {missing}")


def _base_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8([4, 4])
    seed["char_id"][0, :2] = np.uint8([CHAR_FOX, CHAR_FALCO])
    seed["action_id"][0, :2] = np.uint16([ACT_WAIT, ACT_WAIT])
    seed["animation_index"][0, :2] = np.uint32([SM_WAIT1_0, SM_WAIT1_0])
    seed["frame_speed_mul_f32"][0, :2] = np.float32([1.0, 1.0])
    seed["fighter_scale_y"][0, :2] = np.float32([1.0, 1.0])
    seed["facing"][0, :2] = np.uint8([1, 0])
    return seed


def _step_seed(binding, seed: np.ndarray):
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    input_t = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
    binding.step_input(handle, prev_input, input_t)
    binding.write_compare(handle, out_compare)
    compare = out_compare.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    return handle, compare


def test_no_contact_demand_skips_endpoints_but_debug_materializes() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    seed = _base_seed()
    seed["pos_x"][0, :2] = np.float32([-40.0, 40.0])

    handle, _compare = _step_seed(binding, seed)
    try:
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 0) == 0
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 1) == 0

        hurtcaps, count = binding.hurtcaps_world(handle, 0, 0)
        assert int(count) > 0
        assert np.any(hurtcaps[: int(count), :6] != np.float32(0.0))
        assert np.any(hurtcaps[: int(count), 6] > np.float32(0.0))
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 0) == 1
    finally:
        binding.destroy(handle)


def test_active_body_hitbox_demands_defender_geometry_and_hits() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    seed = _base_seed()
    seed["action_id"][0, :2] = np.uint16([ACT_ATTACK_AIR_N, ACT_WAIT])
    seed["animation_index"][0, :2] = np.uint32([SM_ATTACK_AIR_N, SM_WAIT1_0])
    seed["action_frame"][0, :2] = np.int16([3, 0])
    seed["anim_frame_f32"][0, :2] = np.float32([3.0, 0.0])
    seed["pos_x"][0, :2] = np.float32([0.0, 0.0])
    seed["pos_y"][0, :2] = np.float32([5.0, 0.0])

    handle, compare = _step_seed(binding, seed)
    try:
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 0) == 0
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 1) == 1
        _hitboxes, hitbox_count = binding.hitboxes_world_full(handle, 0, 0)
        assert int(hitbox_count) > 0
        assert int(compare["action_id"][1]) == ACT_DAMAGE_HI_1
        assert float(compare["percent"][1]) == pytest.approx(12.0, abs=1e-4)
        assert int(compare["hitlag"][1]) > 0
    finally:
        binding.destroy(handle)


def test_active_catch_hitbox_demands_defender_geometry_and_grabs() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    seed = _base_seed()
    seed["action_id"][0, :2] = np.uint16([ACT_CATCH, ACT_WAIT])
    seed["animation_index"][0, :2] = np.uint32([SM_CATCH, SM_WAIT1_0])
    seed["action_frame"][0, :2] = np.int16([5, 0])
    seed["anim_frame_f32"][0, :2] = np.float32([5.0, 0.0])
    seed["pos_x"][0, :2] = np.float32([0.0, 5.0])

    handle, compare = _step_seed(binding, seed)
    try:
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 1) == 1
        _hitboxes, hitbox_count = binding.hitboxes_world_full(handle, 0, 0)
        assert int(hitbox_count) > 0
        assert int(compare["action_id"][0]) == ACT_CATCH_PULL
        assert int(compare["action_id"][1]) == ACT_CAPTURE_PULLED_HI
    finally:
        binding.destroy(handle)


@pytest.mark.parametrize("item_type", [54, 56], ids=["fox_laser", "fox_illusion"])
def test_relevant_item_article_demands_geometry_before_item_collision(item_type: int) -> None:
    _require_collision_artifacts_or_skip()
    if item_type == 54 and not Path("data/items/lasers.bin").exists():
        pytest.skip("missing local laser artifact: data/items/lasers.bin")
    binding = pytest.importorskip("msl_binding")

    seed = _base_seed()
    seed["pos_x"][0, :2] = np.float32([-100.0, 100.0])
    seed["items"]["exists"][0, 0] = np.uint8(1)
    seed["items"]["type"][0, 0] = np.uint16(item_type)
    seed["items"]["state"][0, 0] = np.uint8(1)
    seed["items"]["owner"][0, 0] = np.int8(0)
    seed["items"]["instance_id"][0, 0] = np.uint16(123)
    seed["items"]["pos_x"][0, 0] = np.float32(1000.0)
    seed["items"]["pos_y"][0, 0] = np.float32(1000.0)
    seed["item_reflect_damage_mul"][0, 0] = np.float32(1.0)

    handle, _compare = _step_seed(binding, seed)
    try:
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 0) == 1
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 1) == 1
    finally:
        binding.destroy(handle)


def test_rebirth_wait_collision_skip_keeps_zero_geometry_and_visible_state() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    seed = _base_seed()
    seed["stocks"][0, :2] = np.uint8([3, 4])
    seed["action_id"][0, :2] = np.uint16([ACT_REBIRTH_WAIT, ACT_WAIT])
    seed["animation_index"][0, :2] = np.uint32([SM_WAIT1_0, SM_WAIT1_0])
    seed["action_frame"][0, :2] = np.int16([120, 0])
    seed["anim_frame_f32"][0, :2] = np.float32([120.0, 0.0])
    seed["pos_x"][0, :2] = np.float32([16.0, 60.0])
    seed["pos_y"][0, :2] = np.float32([45.0, 0.0])
    seed["match_flow_timer"][0, 0] = np.uint8(240)

    handle, compare = _step_seed(binding, seed)
    try:
        assert int(compare["action_id"][0]) == ACT_REBIRTH_WAIT
        assert int(compare["action_frame"][0]) == 120
        hurtcaps, count = binding.hurtcaps_world(handle, 0, 0)
        assert int(count) == 0
        assert not np.any(hurtcaps)
        assert binding.debug_hurtcap_geometry_valid(handle, 0, 0) == 0
    finally:
        binding.destroy(handle)
