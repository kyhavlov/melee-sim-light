from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import SEED_DTYPE
from tools.modelplay.sim_env import MATCH_CONFIG_DTYPE, build_match_config_array

ACT_WAIT = 14
ACT_ATTACK_AIR_N = 65
ACT_ATTACK_DASH = 0x0032
ACT_GUARD = 0x00B3
ACT_PASSIVE_WALL_JUMP = 0x00CB
ACT_FX_SPECIAL_AIR_S_START = 0x015E

SM_WAIT1_0 = 2
SM_ATTACK_AIR_N = 68
SM_ATTACK_DASH = 52
SM_FX_SPECIAL_AIR_S_START = 304

CHAR_FOX = 1
STAGE_FD = 32


def _require_collision_artifacts_or_skip() -> None:
    missing = [
        str(path)
        for path in (
            Path("data/anims/fox.bin"),
            Path("data/hurtcaps/fox.bin"),
            Path("data/moves/fox.json"),
        )
        if not path.exists()
    ]
    if missing:
        pytest.skip(f"missing local collision artifacts: {missing}")


def _seed(
    *,
    defender_action: int = ACT_WAIT,
    defender_anim: int = SM_WAIT1_0,
    defender_action_frame: int = 0,
    defender_anim_frame: float = 0.0,
) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8([4, 4])
    seed["char_id"][0, :2] = np.uint8([CHAR_FOX, CHAR_FOX])
    seed["action_id"][0, :2] = np.uint16([ACT_ATTACK_AIR_N, defender_action])
    seed["animation_index"][0, :2] = np.uint32([SM_ATTACK_AIR_N, defender_anim])
    seed["action_frame"][0, :2] = np.int16([3, defender_action_frame])
    seed["anim_frame_f32"][0, :2] = np.float32([3.0, defender_anim_frame])
    seed["frame_speed_mul_f32"][0, :2] = np.float32([1.0, 1.0])
    seed["fighter_scale_y"][0, :2] = np.float32([1.0, 1.0])
    seed["facing"][0, :2] = np.uint8([1, 0])
    seed["pos_y"][0, :2] = np.float32([5.0, 0.0])
    seed["instance_id"][0, :2] = np.uint16([111, 222])
    return seed


def _reseed(binding, handle, seed: np.ndarray) -> None:
    seed_stride = int(binding.sizes()["seed"])
    binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))


def _handle_with_geometry(binding, seed: np.ndarray):
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    _reseed(binding, handle, seed)
    binding.debug_refresh_combat_geometry(handle)
    return handle


def _install_hitbox_at_hurtcap_midpoint(binding, handle, cap_id: int) -> np.ndarray:
    caps, count = binding.hurtcaps_world(handle, 0, 1)
    assert int(count) > cap_id
    cap = caps[cap_id].copy()
    cx = float((cap[0] + cap[3]) * 0.5)
    cy = float((cap[1] + cap[4]) * 0.5)
    cz = float((cap[2] + cap[5]) * 0.5)
    binding.debug_clear_hitboxes_world(handle, 0, 0)
    binding.debug_set_hitbox_world(handle, 0, 0, 0, cx, cy, cz, 1.0, 5.0, 1)
    return cap


def test_cached_body_matrix_matches_invalidated_fallback() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    cap_id = 0

    handle = _handle_with_geometry(binding, _seed())
    try:
        cap = _install_hitbox_at_hurtcap_midpoint(binding, handle, cap_id)
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 1
        cached = binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id)
        assert cached > 0.0

        binding.debug_set_hurtcap_world(handle, 0, 1, cap_id, *[float(x) for x in cap])
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 0
        fallback = binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id)
        assert fallback == pytest.approx(cached, abs=1e-6)
    finally:
        binding.destroy(handle)


def test_debug_clear_and_set_invalidate_stale_hurtcap_matrix() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    cap_id = 0

    handle = _handle_with_geometry(binding, _seed())
    try:
        cap = _install_hitbox_at_hurtcap_midpoint(binding, handle, cap_id)
        binding.debug_poison_hurtcap_matrix(handle, 0, 1, cap_id)
        assert binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id) == 0.0

        binding.debug_clear_hurtcaps_world(handle, 0, 1)
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 0

        binding.debug_poison_hurtcap_matrix(handle, 0, 1, cap_id)
        binding.debug_set_hurtcap_world(handle, 0, 1, cap_id, *[float(x) for x in cap])
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 0
        assert binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id) > 0.0
    finally:
        binding.destroy(handle)


def test_reseed_clears_runtime_hurtcap_matrix_cache() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    seed = _seed()
    cap_id = 0

    handle = _handle_with_geometry(binding, seed)
    try:
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 1
        _reseed(binding, handle, seed)
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 0
    finally:
        binding.destroy(handle)


def test_masked_match_init_only_clears_selected_lane_hurtcap_matrix_cache() -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    seed_stride = int(binding.sizes()["seed"])
    cap_id = 0

    seeds = np.zeros((2,), dtype=SEED_DTYPE)
    seeds[0] = _seed()[0]
    seeds[1] = _seed()[0]

    handle = binding.init(batch_size=2, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seeds.view(np.uint8).reshape((2, seed_stride)))
        binding.debug_refresh_combat_geometry(handle)
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 1
        assert binding.debug_hurtcap_matrix_valid(handle, 1, 1, cap_id) == 1

        row0 = build_match_config_array(
            char_ids=(CHAR_FOX, CHAR_FOX), facing=(1, 0), stocks=4, frame_id=-123
        )[0]
        row1 = build_match_config_array(
            char_ids=(CHAR_FOX, CHAR_FOX), facing=(1, 0), stocks=4, frame_id=-123
        )[0]
        configs = np.zeros((2,), dtype=MATCH_CONFIG_DTYPE)
        configs[0] = row0
        configs[1] = row1
        mask = np.array([0, 1], dtype=np.uint8)

        binding.init_match_masked(handle, configs.view(np.uint8).reshape((2, -1)), mask)
        assert binding.debug_hurtcap_matrix_valid(handle, 0, 1, cap_id) == 1
        assert binding.debug_hurtcap_matrix_valid(handle, 1, 1, cap_id) == 0
    finally:
        binding.destroy(handle)


@pytest.mark.parametrize(
    "seed",
    [
        pytest.param(
            _seed(defender_action=ACT_ATTACK_DASH, defender_anim=SM_ATTACK_DASH,
                  defender_action_frame=34, defender_anim_frame=34.0),
            id="attackdash-post-hitbox",
        ),
        pytest.param(
            _seed(defender_action=ACT_GUARD, defender_anim=0xFFFFFFFF,
                  defender_action_frame=-1, defender_anim_frame=-1.0),
            id="guard-tilt-live-body",
        ),
        pytest.param(
            _seed(defender_action=ACT_FX_SPECIAL_AIR_S_START,
                  defender_anim=SM_FX_SPECIAL_AIR_S_START, defender_action_frame=1,
                  defender_anim_frame=1.0),
            id="sideb-start-pre-anim",
        ),
    ],
)
def test_body_pose_fallbacks_ignore_poisoned_hurtcap_matrix(seed: np.ndarray) -> None:
    _require_collision_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    cap_id = 0

    if int(seed["action_id"][0, 1]) == ACT_GUARD:
        seed["pos_z"][0, 1] = np.float32(2.0)
        seed["guard_tilt_x4"][0, 1] = np.float32(1.0)
    if int(seed["action_id"][0, 1]) == ACT_FX_SPECIAL_AIR_S_START:
        seed["seed_prev_action_id"][0, 1] = np.uint16(ACT_PASSIVE_WALL_JUMP)
        seed["seed_prev_action_frame"][0, 1] = np.int16(1)

    handle = _handle_with_geometry(binding, seed)
    try:
        _install_hitbox_at_hurtcap_midpoint(binding, handle, cap_id)
        fallback = binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id)
        assert fallback > 0.0

        binding.debug_poison_hurtcap_matrix(handle, 0, 1, cap_id)
        poisoned = binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, cap_id)
        assert poisoned == pytest.approx(fallback, abs=1e-6)
    finally:
        binding.destroy(handle)
