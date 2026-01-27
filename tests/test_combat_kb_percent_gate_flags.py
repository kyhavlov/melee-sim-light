from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_AERIAL = 1 << 10

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2

CHAR_FOX = 1
STAGE_FD = 32


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    return seed


def _read_compare(handle) -> np.ndarray:
    import msl_binding

    compare_stride = int(msl_binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0]


def _run_one_kb(*, dmg_x2225_b7: int, dmg_x2224_b2: int) -> float:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        # Defender (p1) in air to avoid grounded projection nuances.
        seed["on_ground"][0, 0] = np.uint8(0)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["pos_x"][0, 0] = np.float32(-1.0)
        seed["pos_x"][0, 1] = np.float32(+1.0)
        seed["percent"][0, 1] = np.float32(100.5)
        seed["dmg_x2225_b7"][0, 1] = np.uint8(dmg_x2225_b7)
        seed["dmg_x2224_b2"][0, 1] = np.uint8(dmg_x2224_b2)

        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_AERIAL))
        # Angle=0 so speed_y_attack==0 and |speed_x_attack| is proportional to kb_applied.
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 100, 0, 10)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        return float(abs(out["speed_x_attack"][1]))
    finally:
        msl_binding.destroy(handle)


def test_ftcoll_80079ab0_percent_term_gate_changes_kb_base_source() -> None:
    # When dmg_x2225_b7=0: base term is (int)percent_pre (fctiwz).
    kb_percent = _run_one_kb(dmg_x2225_b7=0, dmg_x2224_b2=0)

    # When dmg_x2225_b7=1: base term switches to p_ftCommonData->0x6D4 or 0x6D8 depending on x2224_b2.
    kb_x6d4 = _run_one_kb(dmg_x2225_b7=1, dmg_x2224_b2=0)
    kb_x6d8 = _run_one_kb(dmg_x2225_b7=1, dmg_x2224_b2=1)

    assert kb_x6d4 > 0.0
    assert kb_x6d8 > 0.0
    assert kb_percent > 0.0
    # For the chosen seed (percent=100.5, dmg=5), using the smaller common-data base terms (20/50)
    # must yield smaller knockback than using (int)percent_pre (100).
    assert kb_x6d4 < kb_x6d8 < kb_percent

