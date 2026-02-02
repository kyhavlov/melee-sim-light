from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_FALL = 20


def _step_once(seed: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)
        del handle


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)  # Final Destination
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)  # Fox
    seed["char_id"][0, 1] = np.uint8(22)  # Falco
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    return seed


def test_hitlag_freezes_action_frame_and_physics() -> None:
    seed = _seed_base()
    seed["hitlag"][0, 0] = np.uint16(2)
    seed["hitstun"][0, 0] = np.uint16(5)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)
    seed["state_flags"][0, 0, 3] = np.uint8(0x02)  # 0x221C: isHitstun (refs/slippi-ssbm-asm)

    seed["pos_x"][0, 0] = np.float32(1.25)
    seed["pos_y"][0, 0] = np.float32(100.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(3.0)

    out = _step_once(seed)

    # Timers: hitlag always decrements; hitstun does not decrement during hitlag.
    assert int(out["hitlag"][0]) == 1
    assert int(out["hitstun"][0]) == 5

    # Freeze: no action frame advancement, no motion/gravity integration.
    assert int(out["action_frame"][0]) == 10
    assert np.isclose(out["pos_x"][0], np.float32(1.25), atol=0.0, rtol=0.0)
    assert np.isclose(out["pos_y"][0], np.float32(100.0), atol=0.0, rtol=0.0)
    assert np.isclose(out["speed_y_self"][0], np.float32(3.0), atol=1e-6, rtol=0.0)


def test_hitlag_ends_then_action_and_physics_resume() -> None:
    seed = _seed_base()
    seed["hitlag"][0, 0] = np.uint16(1)
    seed["hitstun"][0, 0] = np.uint16(5)
    # Keep the seeded anim timebase within the (looping) Fall timeline so the assertion checks
    # \"hitlag ends -> advance\" rather than \"loop wrap\" behavior.
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)
    seed["state_flags"][0, 0, 3] = np.uint8(0x02)  # 0x221C: isHitstun (refs/slippi-ssbm-asm)

    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(100.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    # Use knockback X velocity so locomotion air-drift doesn't perturb this test's horizontal integration.
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_x_attack"][0, 0] = np.float32(2.0)
    seed["speed_y_self"][0, 0] = np.float32(3.0)

    out = _step_once(seed)

    # Timers: hitlag decrements to 0; hitstun begins decrementing once hitlag is 0.
    assert int(out["hitlag"][0]) == 0
    assert int(out["hitstun"][0]) == 4

    # Resume: action frame advances; position integrates; gravity applies (Fox grav=0.23).
    assert int(out["action_frame"][0]) == 4
    assert np.isclose(out["pos_x"][0], np.float32(2.0), atol=0.0, rtol=0.0)
    assert np.isclose(out["pos_y"][0], np.float32(102.77), atol=0.0, rtol=0.0)
    assert np.isclose(out["speed_y_self"][0], np.float32(2.77), atol=1e-6, rtol=0.0)


def test_hitstun_does_not_decrement_when_not_in_hitstun_flag() -> None:
    seed = _seed_base()
    seed["hitlag"][0, 0] = np.uint16(0)
    seed["hitstun"][0, 0] = np.uint16(5)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)
    seed["state_flags"][0, 0, 3] = np.uint8(0x00)  # 0x221C: not in hitstun

    out = _step_once(seed)
    assert int(out["hitstun"][0]) == 5
