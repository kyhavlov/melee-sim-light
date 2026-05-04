from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01)
ACT_PASSIVE_STAND_F = 0x00C8
ACT_PASSIVE_STAND_B = 0x00C9
ACT_PASSIVE_CEIL = 0x00CC
ACT_DAMAGE_FLY_N = 0x0058
ACT_FALL = 0x001D

# Submotion ids
SM_PASSIVE_STAND_F = 200
SM_PASSIVE_STAND_B = 201
SM_PASSIVE_CEIL = 204
SM_DAMAGE_FLY_N = 178
SM_FALL = 20

CHAR_FOX = 1
STAGE_FD = 32

# Collision env flags
COLLIDE_CEILING_HUG = 0x00004000

# Fox model_scaling from data/characters/fox.json
FOX_MODEL_SCALING = 0.9599999785423279


def _seed_2p_fd() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.array([CHAR_FOX, CHAR_FOX], dtype=np.uint8)
    seed["team_id"][0, :2] = np.array([0, 1], dtype=np.uint8)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.array([1, 0], dtype=np.uint8)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(1)
    seed["pos_x"][0, :2] = np.array([0.0, 10.0], dtype=np.float32)
    seed["pos_y"][0, :2] = np.array([0.0, 0.0], dtype=np.float32)
    seed["action_id"][0, :2] = np.uint16(ACT_PASSIVE_STAND_F)
    seed["action_frame"][0, :2] = np.int16(1)
    seed["anim_frame_f32"][0, :2] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["animation_index"][0, :2] = np.uint32(SM_PASSIVE_STAND_F)
    seed["shield_hp"][0, :2] = np.float32(60.0)
    seed["instance_id"][0, :2] = np.array([101, 102], dtype=np.uint16)
    return seed


def _make_input() -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    return inp


@pytest.mark.integration
def test_passive_stand_b_root_motion_drives_ground_velocity_and_position() -> None:
    """
    PassiveStandB uses TransN root-motion physics (ft_80085030 via down_roll_apply_phys_transn).
    Seed at frame 1 so the frame-2 vs frame-1 TransN finite difference is non-zero.
    After one step, speed_ground_x_self and pos_x must reflect the root-motion displacement.

    Note: PassiveStandF has zero TransN displacement in the extracted Fox data, so it would
    produce zero velocity even with uses_root_motion=1. PassiveStandB has clear backward
    displacement (negative TransN Z), which with facing=0 (facing_dir=-1) yields positive
    ground velocity.
    """
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_2p_fd()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["facing"][0, 0] = np.uint8(0)  # facing_dir = -1.0f, so backward root motion -> +X
    seed["action_id"][0, 0] = np.uint16(ACT_PASSIVE_STAND_B)
    seed["animation_index"][0, 0] = np.uint32(SM_PASSIVE_STAND_B)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["action_frame"][0, 0] = np.int16(1)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        inp = _make_input()
        inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, inp_bytes, inp_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    # PassiveStandB should remain grounded
    assert int(out["on_ground"][0]) == 1
    # Root motion should have produced non-zero ground velocity
    assert float(out["speed_ground_x_self"][0]) > 0.5
    # Position should have moved in the +X direction because facing=0 flips the sign
    assert float(out["pos_x"][0]) > 0.5


@pytest.mark.integration
def test_passive_stand_f_root_motion_zero_displacement_produces_zero_velocity() -> None:
    """
    PassiveStandF has uses_root_motion=1 but zero TransN displacement in the extracted Fox data.
    This verifies the gating path is taken (not the friction fallback) and correctly yields zero
    velocity when the animation data has no root motion.
    """
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_2p_fd()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["facing"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_PASSIVE_STAND_F)
    seed["animation_index"][0, 0] = np.uint32(SM_PASSIVE_STAND_F)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["action_frame"][0, 0] = np.int16(1)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        inp = _make_input()
        inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, inp_bytes, inp_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["on_ground"][0]) == 1
    # Zero displacement -> zero velocity, but still on the root-motion path
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-4)
    assert float(out["pos_x"][0]) == pytest.approx(0.0, abs=1e-4)


@pytest.mark.integration
def test_passive_ceil_entry_from_damage_fly_via_post_collision() -> None:
    """
    PassiveCeil entry is triggered in knockdown_update_post_collision when:
    - fighter is airborne (was_ground=0, now_ground=0)
    - action is damage-fly
    - tech is available
    - coll_env_flags has Collide_CeilingHug

    We drive the collision state via debug hooks after a normal pre-combat step,
    then re-run knockdown_update_post_collision to assert the entry path.
    """
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_2p_fd()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["pos_y"][0, 0] = np.float32(50.0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_N)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FLY_N)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["hitstun"][0, 0] = np.uint16(10)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_x_attack"][0, 0] = np.float32(0.0)
    seed["speed_y_attack"][0, 0] = np.float32(0.0)
    # Tech available: x680 < tech_window_frames, x684 >= tech_lr_debounce_frames
    seed["x680"][0, 0] = np.uint8(0)
    seed["x684"][0, 0] = np.uint8(255)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        inp = _make_input()
        inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        # Run the frame up to (but not including) combat. This advances anim, physics,
        # stage collision, and the first knockdown_update_post_collision pass.
        # Because there is no real ceiling on FD, PassiveCeil is NOT entered yet.
        binding.debug_step_input_pre_combat(handle, inp_bytes, inp_bytes)

        # Now arm the ceiling contact bits that the real collision pass would have set.
        binding.debug_set_coll_env_flags(handle, 0, 0, COLLIDE_CEILING_HUG)
        binding.debug_set_ceiling_contact(handle, 0, 0, 60.0)

        # Re-run the knockdown post-collision path with the armed ceiling metadata.
        binding.debug_run_knockdown_post_collision(handle)

        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == ACT_PASSIVE_CEIL
    assert int(out["animation_index"][0]) == SM_PASSIVE_CEIL
    # Hitstun should be cleared
    assert int(out["hitstun"][0]) == 0
    # Velocities should be cleared
    assert float(out["speed_air_x_self"][0]) == pytest.approx(0.0, abs=1e-4)
    assert float(out["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-4)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-4)
    # pos_y snap: ceiling_contact_y + (PassiveCeil frame-0 TransN.y * model_scaling)
    # PassiveCeil frame-0 ty = -14.8583984375 (from fox.bin), model_scaling = 0.96
    expected_pos_y = 60.0 + (-14.8583984375 * FOX_MODEL_SCALING)
    assert float(out["pos_y"][0]) == pytest.approx(expected_pos_y, abs=1e-3)


@pytest.mark.integration
def test_passive_ceil_anim_end_enters_fall() -> None:
    """
    PassiveCeil anim-end transitions to Fall via enter_fall in
    knockdown_update_pre_physics.
    """
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_2p_fd()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["pos_y"][0, 0] = np.float32(50.0)
    seed["action_id"][0, 0] = np.uint16(ACT_PASSIVE_CEIL)
    seed["animation_index"][0, 0] = np.uint32(SM_PASSIVE_CEIL)
    # Use a very large frame number to guarantee anim_is_finished returns true.
    seed["anim_frame_f32"][0, 0] = np.float32(1000.0)
    seed["action_frame"][0, 0] = np.int16(1000)
    seed["hitstun"][0, 0] = np.uint16(0)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        inp = _make_input()
        inp_bytes = inp.view(np.uint8).reshape((1, input_stride))
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, inp_bytes, inp_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["animation_index"][0]) == SM_FALL
