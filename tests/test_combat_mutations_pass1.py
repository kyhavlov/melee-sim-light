from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

# HitElement ids (GALE01): refs/melee/src/melee/lb/forward.h::HitElement
HIT_ELEMENT_NORMAL = 0
HIT_ELEMENT_INERT = 11

# Slippi post-frame `state_flags`: refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04

# Hitlist cd value for "indefinite" (rehit_frames==0).
# src/hitlist.h (MSL_HITLIST_CD_INDEFINITE).
HITLIST_CD_INDEFINITE = 0xFFFF

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_REBIRTH_WAIT = 0x000D
ACT_WAIT = 0x000E
ACT_DAMAGE_AIR1 = 0x0054
ACT_SQUAT = 0x0027
ACT_SQUAT_WAIT = 0x0028
ACT_GUARD_REFLECT = 0x00B6
ACT_GUARD_ON = 0x00B2
ACT_GUARD_SET_OFF = 0x00B5
ACT_DAMAGE_N1 = 0x004E
ACT_ATTACK_AIR_B = 0x0043
ACT_DAMAGE_FLY_HI = 0x0057
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DAMAGE_FLY_ROLL = 0x005B

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_DAMAGE_N1 = 168
SM_DAMAGE_AIR1 = 174

CHAR_FOX = 1
STAGE_FD = 32

TRIGGER_FULL = np.uint8(255)


def _common_attr(name: str) -> float:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


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
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
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
        # Grounded percent-only/no-KB ProcessHit runs ftCommon_800804FC after percent add, which
        # clears source owner instead of preserving the fresh attacker port.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
        assert int(out["last_hit_by"][1]) == 6
        assert int(out["last_attack_landed"][0]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_overlap_defender_invincible_applies_attacker_hitlag_only() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["percent"][0, 1] = np.float32(12.0)
        seed["instance_hit_by"][0, 1] = np.uint16(999)
        seed["last_hit_by"][0, 1] = np.uint8(1)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # Defender is invincible (opcode 26 domain): select contact, but do not apply percent/KB/hitstun.
        msl_binding.debug_set_hit_status_override(handle, 0, 1, 1)
        _, selected = _read_selected_body_hits(handle)
        assert selected == 1

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(5) * hitlag_dmg_mul + hitlag_base)

        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == 0

        assert float(out["percent"][1]) == pytest.approx(12.0)
        assert float(out["speed_x_attack"][1]) == pytest.approx(0.0)
        assert float(out["speed_y_attack"][1]) == pytest.approx(0.0)
        assert int(out["hitstun"][1]) == 0

        # No "hit attribution" updates on invincible contacts.
        assert int(out["instance_hit_by"][1]) == 999
        assert int(out["last_hit_by"][1]) == 1

        # No damage-state entry.
        assert int(out["action_id"][1]) == ACT_WAIT
        assert int(out["animation_index"][1]) == SM_WAIT1_0
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
        # Decomp: ftCo_8008DCE0 does Fighter_ChangeMotionState + immediate ftAnim_8006EBA4,
        # so entry-frame Damage* snapshots are action_frame==1 (not 0).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        assert int(out["action_frame"][1]) == 1
        timebase = msl_binding.debug_timebase(handle, 0)
        # The same explicit ftAnim call interprets the live JObj before later contact priorities;
        # numeric action time and collision pose therefore publish the same advanced frame.
        # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
        assert float(timebase[1, 5]) == 1.0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_damageflyroll_percent_gate_reads_committed_current_hit_percent() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["percent"][0, 1] = np.float32(91.54)
        # After the two source effect draws this seed admits DamageFlyRoll once the current hit is
        # committed before ftCo_8008DCE0's >=100% gate.
        seed["frame_pre_random_seed"][0] = np.uint32(2959522329)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 14.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_AERIAL))
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 45, 105, 0, 10)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 2)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["percent"][1]) > _common_attr("damagefly_roll_percent_threshold")
        assert int(out["action_id"][1]) == ACT_DAMAGE_FLY_ROLL
    finally:
        msl_binding.destroy(handle)
        del handle


@pytest.mark.parametrize(
    "damagefly_action",
    [ACT_DAMAGE_FLY_HI, ACT_DAMAGE_FLY_TOP, ACT_DAMAGE_FLY_ROLL],
)
def test_combat_resolve_tiny_phantom_overlap_applies_in_damagefly_without_full_damage(
    damagefly_action: int,
) -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    def run_case(*, hurt_x: float) -> np.ndarray:
        handle = msl_binding.init(batch_size=1, num_players=2)
        try:
            seed = _seed_base()
            seed["on_ground"][0, :2] = np.uint8(0)
            seed["action_id"][0, 1] = np.uint16(damagefly_action)
            seed["hitstun"][0, 1] = np.uint16(10)
            seed["percent"][0, 1] = np.float32(20.0)
            seed["last_hit_by"][0, 1] = np.uint8(6)
            seed["instance_hit_by"][0, 1] = np.uint16(0)
            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)

            msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
            msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 15.0, 1)
            msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_AERIAL))
            msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
            msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
            msl_binding.debug_set_hurtcap_world(
                handle, 0, 1, 0, hurt_x, 0.0, 0.0, hurt_x, 0.0, 0.0, 0.5
            )
            overlap = msl_binding.debug_body_matrix_overlap(handle, 0, 0, 0, 1, 0)
            msl_binding.debug_combat_resolve(handle)
            out = _read_compare(handle).copy()
            return np.array((overlap,), dtype=np.float32), out
        finally:
            msl_binding.destroy(handle)

    # Scalar debug capsules use the direct lbColl radius sum: 1.0 + 0.5. Place the point just
    # inside that source boundary so the overlap is positive but below the phantom threshold.
    tiny_overlap, tiny = run_case(hurt_x=1.49)
    assert 0.0 < float(tiny_overlap[0]) < _common_attr("phantom_overlap_max_x7a8")
    assert float(tiny["percent"][1]) == pytest.approx(20.0)
    assert int(tiny["action_id"][1]) == damagefly_action
    assert int(tiny["hitstun"][1]) == 10
    assert int(tiny["hitlag"][0]) == 0
    assert int(tiny["hitlag"][1]) > 0
    assert int(tiny["instance_hit_by"][1]) == 111

    full_overlap, full = run_case(hurt_x=1.0)
    assert float(full_overlap[0]) > _common_attr("phantom_overlap_max_x7a8")
    assert float(full["percent"][1]) == pytest.approx(35.0)
    assert int(full["hitlag"][0]) > 0
    assert int(full["hitlag"][1]) > 0


def test_combat_resolve_damage_calcvel_merges_after_x18ac_window() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    def run_case(handle, *, x18ac: int, cur_kb_x: float) -> np.ndarray:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed["speed_x_attack"][0, 1] = np.float32(cur_kb_x)
        seed["damage_time_since_hit_x18ac"][0, 1] = np.int16(x18ac)
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
        return _read_compare(handle)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        merge_window = int(_common_attr("kb_vel_merge_since_hit_frames"))
        cur_kb_x = -0.125
        out_replace = run_case(handle, x18ac=merge_window - 1, cur_kb_x=cur_kb_x)
        out_merge = run_case(handle, x18ac=merge_window, cur_kb_x=cur_kb_x)

        replace_x = float(out_replace["speed_x_attack"][1])
        merge_x = float(out_merge["speed_x_attack"][1])

        assert replace_x > 0.0
        # Decomp ftCo_Damage_CalcVel: after xFC frames, opposite-sign x components add instead of
        # replacing the existing fp->x8c_kb_vel.x.
        assert abs(merge_x - (replace_x + cur_kb_x)) < 1e-6
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_body_overlap_applies_kb_multiplier_chain_to_velocity_only() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # Baseline (all multipliers 1.0).
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        # Simple low-KB horizontal hit: angle=0, KBG=0 => kb_applied ~= BKB.
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 10)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)
        v1 = float(out1["speed_x_attack"][1])
        p1 = float(out1["percent"][1])

        # Apply a nontrivial collision KB multiplier chain: gm_8016B248 * attack_ratio * defense_ratio.
        seed2 = _seed_base()
        seed2["pos_x"][0, 0] = np.float32(0.0)
        seed2["pos_x"][0, 1] = np.float32(1.0)
        seed2["match_damage_ratio"][0] = np.float32(2.0)
        seed2["attack_ratio"][0, 0] = np.float32(1.0)
        seed2["defense_ratio"][0, 1] = np.float32(1.0)
        seed_bytes2 = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes2)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 10)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)
        v2 = float(out2["speed_x_attack"][1])
        p2 = float(out2["percent"][1])

        assert p1 == 5.0
        assert p2 == 5.0
        assert abs(v2 - 2.0 * v1) < 1e-6
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
        # Hold shield via analog trigger (avoid entering GuardReflect via digital press).
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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


def test_combat_resolve_earlier_body_hitbox_precedes_later_shield_candidate() -> None:
    # Source ordering: ftColl_80078C70 resolves shield/BODY per HitCapsule. A lower-index BODY hit
    # commits before a higher-index shield candidate can enter GuardSetOff.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_80076CBC}
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)

        body_x = shx + shr + 4.0
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(
            handle, 0, 1, 0, body_x - 0.25, shy, shz, body_x + 0.25, shy, shz, 0.5
        )

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, body_x, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, shx, shy, shz, 1.0, 5.0, 2)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["shield_hp"][1]) == pytest.approx(hp0, abs=1e-6)
        assert int(out["action_id"][1]) != ACT_GUARD_SET_OFF
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_hit_does_not_pairwide_suppress_later_distinct_body() -> None:
    # Source ordering: ftColl_80078C70 resolves shield/BODY per HitCapsule. A shield hit registers
    # that HitCapsule's hit_group, but does not terminate later distinct hit_groups in the same
    # attacker->defender pair.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076808
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)

        body_x = shx + shr + 4.0
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(
            handle, 0, 1, 0, body_x - 0.25, shy, shz, body_x + 0.25, shy, shz, 0.5
        )

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, body_x, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 1)

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["shield_hp"][1]) < hp0
        assert float(out["percent"][1]) == pytest.approx(5.0)
        assert int(out["instance_hit_by"][1]) == 111
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_hit_group_register_suppresses_later_same_group_body() -> None:
    # Shield contact calls ftColl_80076808(..., type=1, ...), registering every active HitCapsule
    # with the same hit_group. A later same-group BODY candidate must be rejected by lbColl_8000ACFC.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL
        msl_binding.step_input(handle, neutral, shield)

        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy, shz, shr = (
            float(bubbles[1, 0]),
            float(bubbles[1, 1]),
            float(bubbles[1, 2]),
            float(bubbles[1, 3]),
        )
        assert shr > 0.0

        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)

        body_x = shx + shr + 4.0
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(
            handle, 0, 1, 0, body_x - 0.25, shy, shz, body_x + 0.25, shy, shz, 0.5
        )

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, body_x, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 0)

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["shield_hp"][1]) < hp0
        assert float(out["percent"][1]) == pytest.approx(0.0)
        assert int(out["instance_hit_by"][1]) == 0
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_distinct_group_body_contacts_accumulate_percent_and_max_hitlag() -> None:
    # Source ftColl_80078C70 continues to later HitCapsules after a BODY hit. ftColl_80076ED8
    # accumulates percentTemp across accepted distinct groups while x183C_applied keeps the max
    # getEnvDmg value consumed once by Fighter_ProcessHit.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,inlineB2}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 3.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 1)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 1, 0, 0, 0, 0)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(5) * hitlag_dmg_mul + hitlag_base)

        assert float(out["percent"][1]) == pytest.approx(8.0)
        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl
        assert int(out["action_id"][1]) == ACT_WAIT
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_same_group_body_contact_registers_before_later_body() -> None:
    # ftColl_80076ED8 calls inlineB0/lbColl_80008688 immediately, so later same-group BODY
    # candidates in the same ftColl_80078C70 pass are rejected by lbColl_8000ACFC.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB0}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 3.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, 1.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 1, 0, 0, 0, 0)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(3) * hitlag_dmg_mul + hitlag_base)

        assert float(out["percent"][1]) == pytest.approx(3.0)
        assert int(out["hitlag"][0]) == exp_hl
        assert int(out["hitlag"][1]) == exp_hl
        assert int(out["action_id"][1]) == ACT_WAIT
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_multi_body_best_kb_ignores_later_lower_kb_log() -> None:
    # ftColl_80076ED8 logs every accepted BODY contact, but ftColl_8007A06C applies knockback and
    # damage-entry fields from the highest-KB log after all percentTemp accumulation. A later lower-KB
    # distinct group must not override the earlier winner.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    def run(*, include_late_low: bool) -> np.void:
        handle = msl_binding.init(batch_size=1, num_players=2)
        try:
            seed = _seed_base()
            seed["pos_x"][0, 0] = np.float32(0.0)
            seed["pos_x"][0, 1] = np.float32(1.0)
            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)

            msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
            msl_binding.debug_set_hurtcap_world(
                handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5
            )
            msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

            msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
            msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 3.0, 1)
            msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
            msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
            msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 80)
            if include_late_low:
                msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, 1.0, 0.0, 0.0, 1.0, 1.0, 1)
                msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
                msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 1)
                msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 1, 0, 0, 0, 1)

            msl_binding.debug_combat_resolve(handle)
            return _read_compare(handle).copy()
        finally:
            msl_binding.destroy(handle)

    high_only = run(include_late_low=False)
    high_plus_low = run(include_late_low=True)

    assert float(high_only["percent"][1]) == pytest.approx(3.0)
    assert float(high_plus_low["percent"][1]) == pytest.approx(4.0)
    assert int(high_plus_low["action_id"][1]) != ACT_WAIT
    # The late low-BKB log contributes percentTemp but does not own KB/hitstun/writeback.
    assert int(high_plus_low["hitstun"][1]) >= int(high_only["hitstun"][1])
    assert float(high_plus_low["speed_x_attack"][1]) >= float(high_only["speed_x_attack"][1])


def test_combat_resolve_distinct_body_logs_update_combo_from_collision_owner() -> None:
    # ftColl_80076ED8 calls ftColl_8007891C for each accepted regular BODY log, so stale/combo
    # bookkeeping is owned by the collision log path even though KB writeback is selected later by
    # ftColl_8007A06C.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C,ftColl_8007A06C}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(1.0)
        seed["attack_id"][0, 0] = np.uint16(7)
        seed["attack_instance"][0, 0] = np.uint16(44)
        seed["combo_victim_port"][0, 0] = np.uint8(0xFF)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, 0.5, 0.0, 0.0, 1.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 1.0, 0.0, 0.0, 1.0, 2.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, 1.0, 0.0, 0.0, 1.0, 2.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 1)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 0, 1, 0, 0, 0, 0)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert float(out["percent"][1]) > 0.0
        assert int(out["action_id"][1]) == ACT_WAIT
        assert int(out["last_attack_landed"][0]) == 7
        assert int(out["combo_count"][0]) == 2
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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


def test_combat_resolve_same_group_shield_contact_registers_before_later_shield() -> None:
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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

        # Force shield overlap for two same-group hitboxes this frame.
        #
        # Source ftColl_80076CBC calls ftColl_80076808 immediately for an accepted shield contact,
        # registering the victim across every active HitCapsule with the same hit_group. The later
        # same-group shield candidate is therefore rejected by lbColl_8000ACFC before it can raise
        # x19A4 or x19A0.
        # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
        # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 3.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, shx, shy, shz, 1.0, 7.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 0)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        hp1 = float(out["shield_hp"][1])

        # Same-group rehit suppression leaves the first accepted shield contact as the only source
        # for hitlag and shieldDamageTaken.
        hitlag_dmg_mul = _common_attr("hitlag_dmg_mul")
        hitlag_base = _common_attr("hitlag_base")
        exp_hl = int(int(3) * hitlag_dmg_mul + hitlag_base)
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


def test_combat_resolve_distinct_group_shield_contacts_accumulate_damage_taken() -> None:
    # Source ftColl_80076CBC accumulates x19A0 shieldDamageTaken across accepted shield contacts
    # while x19A4 keeps the max int damage for shieldstun/hitlag.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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

        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 3.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, shx, shy, shz, 1.0, 7.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 1, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 1, 1)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)
        hp1 = float(out["shield_hp"][1])

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
        shield_damage_taken = float(int(3) + int(7))
        exp_depletion = shield_hit_damage_mul * (shield_damage_taken * (1.0 - ls)) + shield_hit_damage_base

        assert np.isclose(hp1, hp0 - exp_depletion, atol=1e-5)
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_shield_rehit_suppression_blocks_repeat_after_guard_set_off_entry() -> None:
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
        # Hold shield via analog trigger (avoid entering GuardReflect via digital press).
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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

        # Force stable shield overlap.
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, shx, shy, shz, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_set_hitbox_element(handle, 0, 0, 0, int(HIT_ELEMENT_NORMAL))

        out0 = _read_compare(handle)
        hp0 = float(out0["shield_hp"][1])
        iid0 = int(out0["instance_id"][1])

        # First resolve applies the shield hit and enters GuardSetOff.
        msl_binding.debug_combat_resolve(handle)
        out1 = _read_compare(handle)
        hp1 = float(out1["shield_hp"][1])
        iid1 = int(out1["instance_id"][1])
        assert int(out1["action_id"][1]) == ACT_GUARD_SET_OFF
        assert int(out1["hitlag"][0]) > 0
        assert int(out1["hitlag"][1]) > 0

        # Ensure this exercises the GuardSetOff entry path that bumps instance_id (decomp-shaped
        # motion-state entry bundle via msl_anim_timebase_enter()).
        assert hp1 < hp0
        assert iid1 != iid0

        # Clear hitlag (so hitlag gating doesn't mask the rehit latch), then resolve again:
        # rehit suppression should prevent a second shield hit from re-entering GuardSetOff.
        msl_binding.debug_set_hitlag(handle, 0, 0, 0)
        msl_binding.debug_set_hitlag(handle, 0, 1, 0)
        msl_binding.debug_combat_resolve(handle)
        out2 = _read_compare(handle)
        assert float(out2["shield_hp"][1]) == pytest.approx(hp1)
        assert int(out2["hitlag"][0]) == 0
        assert int(out2["hitlag"][1]) == 0
        assert int(out2["instance_id"][1]) == iid1
    finally:
        msl_binding.destroy(handle)
        del handle


def test_combat_resolve_rehit_suppression_clears_on_instance_id_change() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # Seed a stale hitlist entry for (attacker=0, hit_group=0, victim=1), then change the
        # victim instance_id to simulate death/respawn. Rehit suppression must NOT carry over to
        # the new instance identity key.
        seed = _seed_base()
        seed["combat_hitlist_cd"][0, 0, 0, 1] = np.uint16(HITLIST_CD_INDEFINITE)
        seed["combat_hitlist_victim_iid"][0, 0, 0, 1] = np.uint16(222)
        seed["instance_id"][0, 1] = np.uint16(333)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        # If the stale hitlist entry incorrectly carried over to the new instance_id, hitlag would
        # remain 0. Correct behavior: iid mismatch clears the entry and allows the hit.
        assert int(out["hitlag"][0]) > 0
        assert int(out["hitlag"][1]) > 0
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
        # Decomp-shaped powershield-active setup: GuardReflect owner state + x18 timer.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093BC0}
        seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
        seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
        seed["guard_reflect_timer_x14"][0, 1] = np.uint8(2)
        seed["guard_reflect_timer_x18"][0, 1] = np.uint8(2)
        # Direct locomotion GuardReflect owns both ShieldDesc and ReflectDesc.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
        seed["state_flags"][0, 1, 1] = np.uint8(0x01)
        seed["state_flags"][0, 1, 2] = np.uint8(0x80)
        # Slippi post-frame packing parity for x221C_b2.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        seed["state_flags"][0, 1, 3] = np.uint8(0x20)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        neutral = np.zeros((1, input_stride), dtype=np.uint8)
        shield = np.zeros((1, input_stride), dtype=np.uint8)
        shield_view = shield.view(INPUT_DTYPE).reshape((1,))
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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
        shield_view["p"]["l"][0, 1] = TRIGGER_FULL

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

    # Sanity: overlap in BODY when debug primitives share the same z plane.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        _, count = _read_contacts_filtered(handle)
        assert count > 0
    finally:
        msl_binding.destroy(handle)
        del handle

    # With large z separation, BODY selection should find no overlaps. This directly protects the
    # debug/contact geometry path without being masked by the decomp grounded Z-nudge owner, which
    # pulls seeded grounded `pos_z` back toward the engine's narrow depth lane before refresh.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(
            handle, 0, 1, 0, -0.5, 0.0, 100.0, 0.5, 0.0, 100.0, 0.5
        )

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


def test_debug_select_body_hits_rebirthwait_collision_skip_blocks_vulnerable_platform_target() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base()
        seed["action_id"][0, 1] = np.uint16(ACT_REBIRTH_WAIT)
        seed["hurtbox_state"][0, 1] = np.uint8(0)
        seed["colanim_hit_status_x198c"][0, 1] = np.uint8(0)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives. Visible hurtbox_state is vulnerable, but RebirthWait owns
        # x2219_b1, so vanilla skips the defender's common collision pass:
        # refs/melee/src/melee/ft/ft_0D4D.c::ftCo_800D5600
        # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        _, c0 = _read_selected_body_hits(handle)
        assert c0 == 0

        seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        _, c1 = _read_selected_body_hits(handle)
        assert c1 == 1
    finally:
        msl_binding.destroy(handle)
        del handle


def test_falco_laser_rebirthwait_collision_skip_keeps_platform_target_unhit() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    def run(defender_action: int) -> np.void:
        handle = msl_binding.init(batch_size=1, num_players=2)
        try:
            seed = _seed_base()
            seed["action_id"][0, 1] = np.uint16(defender_action)
            seed["hurtbox_state"][0, 1] = np.uint8(0)
            seed["colanim_hit_status_x198c"][0, 1] = np.uint8(0)
            seed["pos_x"][0, 0] = np.float32(-10.0)
            seed["pos_x"][0, 1] = np.float32(0.0)
            seed["pos_y"][0, :2] = np.float32(45.0)
            seed["on_ground"][0, :2] = np.uint8(0)
            seed["ground_id"][0, :2] = np.uint16(0xFFFF)
            seed["match_flow_timer"][0, 1] = np.uint16(200)
            item = seed["items"][0, 0]
            item["exists"] = np.uint8(1)
            item["type"] = np.uint16(55)  # Falco laser.
            item["state"] = np.uint8(0)
            item["owner"] = np.int8(0)
            item["instance_id"] = np.uint16(333)
            item["attack_id"] = np.uint16(55)
            item["attack_instance"] = np.uint16(333)
            item["direction"] = np.float32(1.0)
            item["pos_x"] = np.float32(-1.0)
            item["pos_y"] = np.float32(45.0)
            item["vel_x"] = np.float32(0.0)
            item["vel_y"] = np.float32(0.0)
            item["damage"] = np.uint16(3)
            item["timer"] = np.float32(30.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            neutral = np.zeros((1, input_stride), dtype=np.uint8)
            out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, neutral, neutral)
            msl_binding.write_compare(handle, out_bytes)
            return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        finally:
            msl_binding.destroy(handle)

    # Positive control: the same seeded laser hits a normal vulnerable Wait target.
    wait_out = run(ACT_WAIT)
    assert int(wait_out["items"][0]["exists"]) == 0
    assert float(wait_out["percent"][1]) == pytest.approx(3.0)
    assert int(wait_out["hitlag"][1]) > 0
    assert int(wait_out["action_id"][1]) == ACT_DAMAGE_AIR1

    # RebirthWait owns x2219_b1, so item collision rejects it despite visible vulnerable status.
    rebirth_out = run(ACT_REBIRTH_WAIT)
    assert int(rebirth_out["items"][0]["exists"]) == 1
    assert float(rebirth_out["percent"][1]) == pytest.approx(0.0)
    assert int(rebirth_out["hitlag"][1]) == 0
    assert int(rebirth_out["action_id"][1]) == ACT_REBIRTH_WAIT


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
