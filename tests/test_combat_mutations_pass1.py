from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

# HitElement ids (GALE01): refs/melee/src/melee/lb/forward.h::HitElement
HIT_ELEMENT_NORMAL = 0
HIT_ELEMENT_INERT = 11

# Slippi post-frame `state_flags`: refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_SQUAT = 0x0027
ACT_SQUAT_WAIT = 0x0028
ACT_GUARD_SET_OFF = 0x00B5
ACT_DAMAGE_N1 = 0x004E
ACT_DAMAGE_AIR1 = 0x0054

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_DAMAGE_N1 = 168
SM_DAMAGE_AIR1 = 174

CHAR_FOX = 1
STAGE_FD = 32


def _common_attr(name: str) -> float:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["shield_hp"][0, :2] = np.float32(_common_attr("start_shield_health"))
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


def _read_selected_body_hits(handle, *, batch_index: int = 0, max_contacts: int = 16):
    import msl_binding

    raw, count = msl_binding.debug_combat_select_body_hits(handle, batch_index, max_contacts)
    assert raw.shape[1] > 0

    contact_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("defender", "u1"),
            ("hitbox_id", "u1"),
            ("hurtcap_id", "u1"),
            ("attacker_msid", "<u2"),
            ("attacker_action_frame", "<i2"),
            ("hitbox_x", "<f4"),
            ("hitbox_y", "<f4"),
            ("hitbox_z", "<f4"),
            ("hitbox_radius", "<f4"),
            ("hitbox_damage", "<f4"),
            ("hurtcap_ax", "<f4"),
            ("hurtcap_ay", "<f4"),
            ("hurtcap_az", "<f4"),
            ("hurtcap_bx", "<f4"),
            ("hurtcap_by", "<f4"),
            ("hurtcap_bz", "<f4"),
            ("hurtcap_radius", "<f4"),
        ],
        align=False,
    )
    assert raw.shape[1] == contact_dtype.itemsize
    contacts = raw.reshape(-1).view(contact_dtype)[:count]
    return contacts, int(count)


def _read_contacts_filtered(handle, *, batch_index: int = 0, max_contacts: int = 256):
    import msl_binding

    raw, count = msl_binding.debug_combat_contacts_filtered(handle, batch_index, max_contacts)
    assert raw.shape[1] > 0

    contact_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("defender", "u1"),
            ("hitbox_id", "u1"),
            ("hurtcap_id", "u1"),
            ("attacker_msid", "<u2"),
            ("attacker_action_frame", "<i2"),
            ("hitbox_x", "<f4"),
            ("hitbox_y", "<f4"),
            ("hitbox_z", "<f4"),
            ("hitbox_radius", "<f4"),
            ("hitbox_damage", "<f4"),
            ("hurtcap_ax", "<f4"),
            ("hurtcap_ay", "<f4"),
            ("hurtcap_az", "<f4"),
            ("hurtcap_bx", "<f4"),
            ("hurtcap_by", "<f4"),
            ("hurtcap_bz", "<f4"),
            ("hurtcap_radius", "<f4"),
        ],
        align=False,
    )
    assert raw.shape[1] == contact_dtype.itemsize
    contacts = raw.reshape(-1).view(contact_dtype)[:count]
    return contacts, int(count)


def test_combat_resolve_body_overlap_sets_hitlag_and_attribution() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["last_hit_by"][0, 1] = np.uint8(1)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        contacts, count = _read_contacts_filtered(handle)
        assert count == 1
        assert int(contacts["attacker"][0]) == 0
        assert int(contacts["defender"][0]) == 1
        assert int(contacts["hitbox_id"][0]) == 0
        assert int(contacts["hurtcap_id"][0]) == 0

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(5) * hitlag_dmg_mul + hitlag_base)

        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl
        assert int(out["instance_hit_by"][1]) == 111
        assert int(out["last_hit_by"][1]) == 0
        assert int(out["last_attack_landed"][0]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_overlap_applies_squat_hitlag_mul() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        # Defender is in squat; decomp ftCommon_CalcHitlag applies hitlag_squat_mul when
        # motion_id in [ftCo_MS_Squat, ftCo_MS_SquatWait].
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
        seed["action_id"][0, 1] = np.uint16(ACT_SQUAT)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 6.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        hitlag_squat_mul = _common_attr("hitlag_squat_mul")

        base = int(int(6) * hitlag_dmg_mul + hitlag_base)
        exp_attacker = base
        exp_defender = int(float(base) * hitlag_squat_mul)

        assert int(out["hitlag"][0]) == exp_attacker
        assert int(out["hitlag"][1]) == exp_defender
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_overlap_applies_percent_knockback_hitstun_and_enters_damage() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Overlap + grounded eligibility.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        # Low-KB horizontal hit: angle=0, KBG=0 => kb_applied ~= BKB.
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 20)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        # Mid hurt height => DamageN* group.
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["percent"][1]) == 5.0
        assert float(out["speed_x_attack"][1]) > 0.0
        assert float(out["speed_y_attack"][1]) == 0.0
        assert int(out["hitstun"][1]) > 0
        assert int(out["action_id"][1]) == ACT_DAMAGE_N1
        assert int(out["animation_index"][1]) == SM_DAMAGE_N1
        assert int(out["action_frame"][1]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_rehit_suppression_prevents_double_apply() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 20)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)

        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)

        assert float(out1["percent"][1]) == 5.0
        assert float(out2["percent"][1]) == 5.0
        assert int(out2["hitstun"][1]) == int(out1["hitstun"][1])
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_damage_entry_differs_ground_vs_air() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    # Grounded victim => DamageN1 (mid height).
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 20)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        assert int(out["action_id"][1]) == ACT_DAMAGE_N1
        assert int(out["animation_index"][1]) == SM_DAMAGE_N1
    finally:
        msl_binding.destroy(handle)
        del handle

    # Airborne victim => DamageAir1.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_AERIAL))
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 20)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        assert int(out["action_id"][1]) == ACT_DAMAGE_AIR1
        assert int(out["animation_index"][1]) == SM_DAMAGE_AIR1
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_overlap_uses_get_env_dmg_for_low_damage() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Overlap with a hitbox whose float damage would truncate to 0 if we used (int)damage.
        # Decomp converts float->int via getEnvDmg, which yields 1 for nonzero float with (int)==0.
        # refs/melee/src/melee/ft/ftcoll.c::inlineA0/inlineA1
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 0.5, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(1) * hitlag_dmg_mul + hitlag_base)

        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_shield_precedence_blocks_body_selection() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Reset hitlag (step_input may have decremented timers; keep this test surgical).
        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)

        # Precedence: even if a hurtcap overlaps, shield contact suppresses BODY selection.
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, shx - 0.5, shy, shz, shx + 0.5, shy, shz, 0.5)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        _, count = _read_selected_body_hits(handle)
        assert count == 0

        # Baseline (after shield bubble exists, before combat resolve).
        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        # Combat now resolves shield hits (mutating shield_hp / hitlag), while still preventing BODY
        # selection for the overlapping hitbox.
        assert float(out["shield_hp"][1]) < hp0
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
        assert int(out["action_id"][1]) == ACT_GUARD_SET_OFF
        assert int(out["last_attack_landed"][0]) == 0
        assert int(out["instance_hit_by"][1]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_overlap_reduces_shield_hp_by_decomp_formula() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        # Force a shield overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        hp1 = float(out["shield_hp"][1])

        # Decomp (GALE01):
        # shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        trig_deadzone = _common_attr("trigger_deadzone")
        shield_hit_damage_mul = _common_attr("shield_hit_damage_mul")
        shield_hit_damage_base = _common_attr("shield_hit_damage_base")
        shield_hit_ls_min = _common_attr("shield_hit_lightshield_min")
        shield_hit_ls_max = _common_attr("shield_hit_lightshield_max")

        # Digital L is treated as fully pressed (trig=1.0), so lightshield_amount==1.0.
        # refs/melee/src/melee/ft/fighter.c and :2019-2050 (x650 update)
        assert trig_deadzone < 1.0
        light = 1.0
        ls = light * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min
        exp_depletion = shield_hit_damage_mul * (float(int(5)) * (1.0 - ls)) + shield_hit_damage_base

        assert np.isclose(hp1, hp0 - exp_depletion, atol=1e-5)
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_hitlag_uses_get_env_dmg_semantics() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Force a shield overlap with sub-integer damage.
        # Decomp (GALE01): getEnvDmg returns 1 when dmg!=0 and (int)dmg==0.
        # refs/melee/src/melee/ft/ftcoll.c (inlineA0/inlineA1 and ftColl_80076CBC).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 0.5, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(1) * hitlag_dmg_mul + hitlag_base)

        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_hit_uses_max_damage_for_hitlag_but_first_for_hp_in_pass1() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        # Force a shield overlap for two hitboxes this frame.
        #
        # Pass 1 is simplified: it applies shield HP / GuardSetOff from the first eligible overlap,
        # but hitlag uses the max int damage over all eligible shield overlaps (decomp-shaped).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 3.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, shx, shy, shz, 1.0, 7.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        hp1 = float(out["shield_hp"][1])

        # Hitlag uses decomp getEnvDmg semantics (int damage) and uses the max over shield overlaps.
        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(7) * hitlag_dmg_mul + hitlag_base)
        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl

        trig_deadzone = _common_attr("trigger_deadzone")
        shield_hit_damage_mul = _common_attr("shield_hit_damage_mul")
        shield_hit_damage_base = _common_attr("shield_hit_damage_base")
        shield_hit_ls_min = _common_attr("shield_hit_lightshield_min")
        shield_hit_ls_max = _common_attr("shield_hit_lightshield_max")

        assert trig_deadzone < 1.0
        light = 1.0
        ls = light * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min
        exp_depletion = shield_hit_damage_mul * (float(int(3)) * (1.0 - ls)) + shield_hit_damage_base

        assert np.isclose(hp1, hp0 - exp_depletion, atol=1e-5)
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_powershield_blocks_shield_hp_depletion_but_keeps_hitlag() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        # 0x221C bit 0x20: powershield active.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        seed["state_flags"][0, 1, 3] = np.uint8(0x20)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        # Force a shield overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        hp1 = float(out["shield_hp"][1])

        assert np.isclose(hp1, hp0, atol=1e-6)
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
        assert int(out["action_id"][1]) == ACT_GUARD_SET_OFF
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_non_inert_shield_overlap_does_not_set_detect_hitbox_flag() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Force a shield overlap using a non-inert hitbox.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_element(handle, 0, 0, 0, int(HIT_ELEMENT_NORMAL))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        flags_221c_p0 = int(out["state_flags"][0, 3])
        flags_221c_p1 = int(out["state_flags"][1, 3])
        assert (flags_221c_p0 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
        assert (flags_221c_p1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_inert_shield_overlap_sets_detect_hitbox_flag() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Force a shield overlap using an inert hitbox (detection only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 0.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_element(handle, 0, 0, 0, int(HIT_ELEMENT_INERT))

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        flags_221c_p0 = int(out["state_flags"][0, 3])
        flags_221c_p1 = int(out["state_flags"][1, 3])
        assert (flags_221c_p0 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
        assert (flags_221c_p1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) != 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_inert_shield_overlap_does_not_apply_shield_hit_mutations() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])
        a0 = int(out0["action_id"][1])
        hl0_a = int(out0["hitlag"][0])
        hl0_d = int(out0["hitlag"][1])

        # Force a shield overlap using an inert hitbox, but give it positive damage to ensure the
        # shield branch split is keyed on HitElement, not damage.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_element(handle, 0, 0, 0, int(HIT_ELEMENT_INERT))

        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)

        # Inert overlap sets only the Slippi post-frame flag (x221C_b5 / 0x04).
        flags_221c_p0 = int(out1["state_flags"][0, 3])
        flags_221c_p1 = int(out1["state_flags"][1, 3])
        assert (flags_221c_p0 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
        assert (flags_221c_p1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) != 0

        # ...and does not apply normal shield-hit mutations (HP depletion, GuardSetOff, hitlag).
        hp1 = float(out1["shield_hp"][1])
        assert np.isclose(hp1, hp0, atol=1e-6)
        assert int(out1["action_id"][1]) == a0
        assert int(out1["action_id"][1]) != ACT_GUARD_SET_OFF
        assert int(out1["hitlag"][0]) == hl0_a
        assert int(out1["hitlag"][1]) == hl0_d
        assert int(out1["hitlag"][0]) == 0
        assert int(out1["hitlag"][1]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_detect_hitbox_flag_is_cleared_on_next_combat_pass() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # First combat pass: set the flag via inert overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 0.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_element(handle, 0, 0, 0, int(HIT_ELEMENT_INERT))
        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)
        flags_221c_p0_1 = int(out1["state_flags"][0, 3])
        flags_221c_p1_1 = int(out1["state_flags"][1, 3])
        assert (flags_221c_p0_1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
        assert (flags_221c_p1_1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) != 0

        # Second combat pass (no overlaps): the decomp-shaped ProcessHit consume should clear it.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)
        flags_221c_p0_2 = int(out2["state_flags"][0, 3])
        flags_221c_p1_2 = int(out2["state_flags"][1, 3])
        assert (flags_221c_p0_2 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
        assert (flags_221c_p1_2 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_step_input_clears_detect_hitbox_flag_next_frame() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # Seed the bit as if it had been set by a previous frame's collision pass.
        seed = _seed_base()
        seed["state_flags"][0, 1, 3] = np.uint8(STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)

        # Frame N: the decomp-shaped "ProcessHit consume" runs at the start of the frame and
        # clears x221C_b5/0x04 before the post-step state is written.
        msl_binding.step_input(handle, neutral, neutral)
        out1 = _read_compare(handle)
        flags_221c_p1_1 = int(out1["state_flags"][1, 3])
        assert (flags_221c_p1_1 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0

        # Frame N+1: remains clear in the normal step path when there are no new inert overlaps.
        msl_binding.step_input(handle, neutral, neutral)
        out2 = _read_compare(handle)
        flags_221c_p1_2 = int(out2["state_flags"][1, 3])
        assert (flags_221c_p1_2 & STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_hitlag_gating_prevents_multiple_shield_hits() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)

        # Step once to compute shield bubble world geometry for defender P1.
        msl_binding.step_input(handle, neutral, shield)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        # Force a shield overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)
        hp1 = float(out1["shield_hp"][1])
        assert int(out1["hitlag"][0]) > 0
        assert int(out1["hitlag"][1]) > 0

        # Resolve again without decrementing hitlag: hitlag gating should prevent a second shield HP drain.
        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)
        hp2 = float(out2["shield_hp"][1])
        assert np.isclose(hp2, hp1, atol=1e-6)
    finally:
        msl_binding.destroy(handle)
        del handle

def test_debug_select_body_hits_uses_pos_z_in_world_geometry() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    def _seed_attack11_overlap(*, p0_z: float, p1_z: float) -> np.ndarray:
        seed = _seed_base()
        # Keep action_id out of locomotion codepaths so animation_index remains seeded.
        seed["action_id"][0, :2] = np.uint16(0xFFFF)
        # step_input advances action_frame by +1 before hitbox/hurtcap refresh.
        seed["action_frame"][0, :2] = np.int16(1)
        seed["anim_frame_f32"][0, :2] = np.float32(1.0)
        # ftCo_SM_Attack11 (Fox jab1): data/moves/fox.json -> submotion_id 46 (hitbox spawn at frame 2).
        seed["animation_index"][0, :2] = np.uint32(46)
        seed["pos_x"][0, :2] = np.float32(0.0)
        seed["pos_y"][0, :2] = np.float32(0.0)
        seed["pos_z"][0, 0] = np.float32(p0_z)
        seed["pos_z"][0, 1] = np.float32(p1_z)
        return seed

    neutral = np.zeros((1, input_stride), dtype=np.uint8)

    # Sanity: overlap in BODY when both players share the same z plane.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_attack11_overlap(p0_z=0.0, p1_z=0.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, neutral)

        _, hb_count = msl_binding.hitboxes_world(handle, 0, 0)
        assert int(hb_count) > 0

        _, cap_count = msl_binding.hurtcaps_world(handle, 0, 1)
        assert int(cap_count) > 0

        _, count = _read_contacts_filtered(handle)
        assert count > 0
    finally:
        msl_binding.destroy(handle)
        del handle

    # With large z separation, BODY selection should find no overlaps.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_attack11_overlap(p0_z=0.0, p1_z=100.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, neutral, neutral)

        _, count = _read_contacts_filtered(handle)
        assert count == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_disabled_hurtcap_does_not_select_body() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        contacts, count = _read_selected_body_hits(handle)
        assert count == 1

        # Disable the only overlapping capsule: contact must no longer be selected.
        msl_binding.debug_set_hurtcap_enabled(handle, 0, 1, 0, 0)
        contacts, count = _read_selected_body_hits(handle)
        assert count == 0
        assert contacts.size == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_defender_intangible_hit_status_blocks_body_selection() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # Override defender hit status to intangible (opcode 26 domain): selection must be skipped.
        msl_binding.debug_set_hit_status_override(handle, 0, 1, 2)
        _, c0 = _read_selected_body_hits(handle)
        assert c0 == 0

        # Clear override: selection returns.
        msl_binding.debug_set_hit_status_override(handle, 0, 1, -1)
        _, c1 = _read_selected_body_hits(handle)
        assert c1 == 1
    finally:
        msl_binding.destroy(handle)
        del handle


def test_debug_select_body_hits_rehit_suppression_blocks_repeat_until_clear() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force stable overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # First resolve applies hitlag and latches rehit.
        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)
        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(5) * hitlag_dmg_mul + hitlag_base)
        assert int(out1["hitlag"][0]) == exp_hl
        assert int(out1["hitlag"][1]) == exp_hl

        # Clear hitlag (so hitlag gating doesn't mask the rehit latch), then resolve again:
        # latch should suppress the repeat hit and leave hitlag at 0.
        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)
        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)
        assert int(out2["hitlag"][0]) == 0
        assert int(out2["hitlag"][1]) == 0

        # Clear hitboxes (approximates ClearHitboxes), then re-enable: hit can apply again.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_combat_resolve(handle)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)
        msl_binding.debug_combat_resolve(handle)
        out3 = _read_compare(handle)
        assert int(out3["hitlag"][0]) == exp_hl
        assert int(out3["hitlag"][1]) == exp_hl
    finally:
        msl_binding.destroy(handle)
        del handle
