from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_DAMAGE_HI_2 = 0x004C

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_FALL = 20
SM_DAMAGE_HI_2 = 166
MSL_BUTTON_L = 0x0040
MSL_DAMAGE_POST_HITLAG_CB_DAMAGE_ON_EXIT = 1
MSL_STATE_FLAG_221A_IS_HITLAG = 0x20
MSL_STATE_FLAG_221A_B3 = 0x10


def _knockback_frame_decay() -> float:
    d = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(d["knockback_frame_decay"])


def _step_once(
    seed: np.ndarray, prev_inp: np.ndarray | None = None, inp: np.ndarray | None = None
) -> np.ndarray:
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
        if prev_inp is None:
            prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        else:
            assert prev_inp.shape == (1, input_stride)
        if inp is None:
            inp = np.zeros((1, input_stride), dtype=np.uint8)
        else:
            assert inp.shape == (1, input_stride)
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
    expected_kb_x = max(0.0, 2.0 - _knockback_frame_decay())
    assert np.isclose(out["pos_x"][0], np.float32(expected_kb_x), atol=0.0, rtol=0.0)
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


def test_grounded_damage_hitlag_exit_preserves_xf0_ground_kb_against_di() -> None:
    # Comparator-derived grounded DamageHi2 lock from rerun11 frame 130:
    # - vanilla exits hitlag with x8c_kb_vel=-0.771 while grounded,
    # - ftCo_Damage_OnExitHitlag can rotate x8c from DI, but Fighter_procUpdate's grounded branch
    #   decays/integrates xF0_ground_kb_vel and rebuilds x8c from that scalar.
    # - The first slide is therefore -0.771 + Falco gr_friction(0.08) = -0.691, not the smaller
    #   down-DI-rotated velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_8008E5A4}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (ground branch xF0_ground_kb_vel)
    seed = _seed_base()
    seed["char_id"][0, 0] = np.uint8(22)  # Falco gr_friction = 0.08
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_HI_2)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_HI_2)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_friction_mul"][0, 0] = np.float32(1.0)
    seed["hitlag"][0, 0] = np.uint16(1)
    seed["hitstun"][0, 0] = np.uint16(10)
    seed["damage_post_hitlag_cb_kind"][0, 0] = np.uint8(MSL_DAMAGE_POST_HITLAG_CB_DAMAGE_ON_EXIT)
    seed["state_flags"][0, 0, 3] = np.uint8(0x02)  # 0x221C: isHitstun
    seed["speed_x_attack"][0, 0] = np.float32(-0.771)
    seed["speed_y_attack"][0, 0] = np.float32(0.0)

    prev_inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)
    prev = prev_inp.view(INPUT_DTYPE).reshape((1,))
    prev["p"]["main_y"][0, 0] = np.int8(-80)
    prev["p"]["buttons"][0, 0] = np.uint16(MSL_BUTTON_L)

    out = _step_once(seed, prev_inp=prev_inp)

    assert int(out["hitlag"][0]) == 0
    assert int(out["hitstun"][0]) == 9
    assert int(out["on_ground"][0]) == 1
    assert np.isclose(float(out["pos_x"][0]), np.float32(-0.691), atol=1e-6, rtol=0.0)
    assert np.isclose(float(out["speed_x_attack"][0]), np.float32(-0.691), atol=1e-6, rtol=0.0)
    assert float(out["speed_y_attack"][0]) == np.float32(0.0)


def test_damageflyhi_hitlag_sdi_no_longer_requires_x221a_b3() -> None:
    # Current sim gate lock:
    # - ftCo_Damage_OnEveryHitlag in vanilla gates on `allow_sdi` (fp+0x221A:2), not x221A_b3.
    # - DamageFlyHi already lives in the current 2D SDI subset, so this isolates the gate owner
    #   without broadening any action families.
    # - This simulator does not yet seed/model allow_sdi independently; it currently derives the
    #   replay-visible 0x20 lane from active hitlag and uses that as the proxy gate.
    # - So this test is intentionally narrower: it proves the runtime no longer keys SDI on
    #   x221A_b3. It does not claim that true allow_sdi ownership is fully modeled yet.
    # refs/melee/src/melee/ft/types.h (fp+221A:2 allow_sdi, fp+221A:3 x221A_b3)
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnEveryHitlag,ftCo_8008EC90}
    seed = _seed_base()
    seed["char_id"][0, 0] = np.uint8(22)  # Falco
    seed["action_id"][0, 0] = np.uint16(87)  # DamageFlyHi
    seed["animation_index"][0, 0] = np.uint32(177)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["hitlag"][0, 0] = np.uint16(6)
    seed["hitstun"][0, 0] = np.uint16(50)
    seed["pos_x"][0, 0] = np.float32(4.770646)
    seed["pos_y"][0, 0] = np.float32(-1.810438)
    seed["state_flags"][0, 0, 1] = np.uint8(MSL_STATE_FLAG_221A_B3)

    sizes = INPUT_DTYPE.itemsize
    prev_inp = np.zeros((1, sizes), dtype=np.uint8)
    inp = np.zeros((1, sizes), dtype=np.uint8)
    prev = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur = inp.view(INPUT_DTYPE).reshape((1,))
    prev["p"]["main_x"][0, 0] = np.int8(80)
    prev["p"]["main_y"][0, 0] = np.int8(0)
    cur["p"]["main_x"][0, 0] = np.int8(80)
    cur["p"]["main_y"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp=prev_inp, inp=inp)

    assert int(out["hitlag"][0]) == 5
    assert int(out["action_id"][0]) == 87
    assert int(out["on_ground"][0]) == 0
    assert float(out["pos_x"][0]) > float(seed["pos_x"][0, 0])
    assert float(out["pos_y"][0]) > float(seed["pos_y"][0, 0])

    seed_no_b3 = seed.copy()
    seed_no_b3["state_flags"][0, 0, 1] = np.uint8(0)
    out_no_b3 = _step_once(seed_no_b3, prev_inp=prev_inp, inp=inp)
    assert float(out_no_b3["pos_x"][0]) > float(seed["pos_x"][0, 0])
    assert float(out_no_b3["pos_y"][0]) > float(seed["pos_y"][0, 0])
