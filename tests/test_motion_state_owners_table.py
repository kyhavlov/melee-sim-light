from __future__ import annotations

import struct
import os
import subprocess
import sys
from pathlib import Path

import pytest

from tools.extraction.extract_motion_state_owners import (
    CLASS_ATTACK_AIR,
    CLASS_ATTACK_S3,
    CLASS_ATTACK_S4,
    CLASS_COMMON_AIR_COLL,
    CLASS_COMMON_AIR_PHYS,
    CLASS_COMMON_AIR_WALLJUMP_COLL,
    CLASS_DAMAGE_AIR,
    CLASS_DAMAGE_COMMON,
    CLASS_DAMAGE_COMMON_COLL,
    CLASS_DAMAGE_FALL_COLL,
    CLASS_DAMAGE_FLY,
    CLASS_DAMAGE_FLY_COLL,
    CLASS_DAMAGE_GROUND,
    CLASS_ESCAPE_AIR_COLL,
    CLASS_GUARDON_FRAME_START_X672_IASA,
    CLASS_FT80081D0C_AIR_COLL,
    CLASS_FT800827A0_EDGE_SNAP_COLL,
    CLASS_FT80083090_PLATFORM_PASS_COLL,
    CLASS_FT80082B1C_BASIC_LANDING_COLL,
    CLASS_FT80083F88_GROUND_TO_AIR_COLL,
    CLASS2_COMMON_AIRBORNE_COLL,
    CLASS2_COMMON_GROUNDED_B2DC_COLL,
    CLASS2_COMMON_GROUNDED_B4B0_COLL,
    CLASS2_COMMON_GROUNDED_B108_COLL,
    CLASS2_COMMON_GROUNDED_COLL,
    CLASS2_CATCH_START_FLOOR_LOSS,
    CLASS2_CLIFF_HOLD_PHYS_SNAP,
    CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE,
    CLASS2_FALL_LIKE_ACTION,
    CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA,
    CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL,
    CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST,
    CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS,
    CLASS2_GUARD_STATE,
    CLASS2_LANDING_ROOT_FLOOR_SNAP,
    CLASS2_WALK_ACTION,
    CLASS3_PHASE4_ATTACK_AIR_COLL,
    FX_SPECIAL_KIND_BY_SYMBOL,
    FX_SPECIAL_KIND_VALUES,
    CLASS3_PHASE4_DAMAGE_COMMON_COLL,
    CLASS3_PHASE4_DAMAGE_FALL_COLL,
    CLASS3_PHASE4_DAMAGE_FLY_COLL,
    CLASS3_PHASE4_ESCAPE_AIR_COLL,
    CLASS3_ORDINARY_WALLJUMP_COLL,
    CLASS3_WALLTECH_COLL,
    ORDINARY_WALLJUMP_COLL_CBS,
    WALLTECH_COLL_CBS,
    CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD,
    CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION,
    CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS,
    CLASS_FX_SPECIALS_GROUND_B108_COLL,
    CLASS_GROUNDED_ATTACK,
    CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL,
    CLASS_LANDING_AIR,
    CLASS_LANDING_AIR_COLL,
    CLASS_LANDING_COLL,
    CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL,
    CLASS_SPECIALHI,
)
from tools.slippi.motion_state_owners import (
    HEADER_BYTES,
    VERSION,
    read_callback_manifest,
    read_mslmso01_v1,
)


FOX = Path("data/motion_state/owners/fox.bin")
FALCO = Path("data/motion_state/owners/falco.bin")
MANIFEST = Path("data/motion_state/owners/callback_symbols.json")
MARTH = Path("data/motion_state/owners/marth.bin")
FALCON = Path("data/motion_state/owners/falcon.bin")
SHEIK = Path("data/motion_state/owners/sheik.bin")
SOURCE_ARTIFACT_OWNERS = Path("tools/extraction/source_artifacts/motion_state/owners")


def _data_manifest_chars() -> list[str]:
    # The owner tables share one callback-id namespace across every extracted character, so
    # regeneration stability must be checked with the same character set the data tree was
    # built with (data/manifest.json), not a hardcoded fox,falco pair.
    import json as _json

    try:
        return list(_json.loads(Path("data/manifest.json").read_text()).get("chars") or ["fox", "falco"])
    except OSError:
        return ["fox", "falco"]


@pytest.mark.integration
def test_motion_state_owner_tables_cover_known_callbacks_and_flags() -> None:
    fox = read_mslmso01_v1(FOX)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(fox, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    # refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
    assert cb_name(0x004B, "anim") == "ftCo_Damage_Anim"  # DamageHi1
    assert cb_name(0x004B, "phys") == "ftCo_Damage_Phys"
    assert int(fox.class_bits[0x004B]) & CLASS_DAMAGE_COMMON

    assert cb_name(0x0041, "anim") == "ftCo_AttackAir_Anim"  # AttackAirN
    assert cb_name(0x0041, "iasa") == "ftCo_AttackAirN_IASA"
    assert cb_name(0x0041, "coll") == "ftCo_AttackAir_Coll"
    assert int(fox.class_bits[0x0041]) & CLASS_ATTACK_AIR
    assert int(fox.class_bits[0x0041]) & CLASS_FT80081D0C_AIR_COLL
    assert int(fox.class3_bits[0x0041]) & CLASS3_PHASE4_ATTACK_AIR_COLL

    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
    assert cb_name(0x0163, "anim") == "ftFx_SpecialHi_Anim"
    assert cb_name(0x0163, "coll") == "ftFx_SpecialHi_Coll"
    assert int(fox.class_bits[0x0163]) & CLASS_SPECIALHI

    assert cb_name(0x015E, "coll") == "ftFx_SpecialAirSStart_Coll"
    assert cb_name(0x015F, "coll") == "ftFx_SpecialAirS_Coll"
    assert cb_name(0x0160, "coll") == "ftFx_SpecialAirSEnd_Coll"
    assert int(fox.class_bits[0x015E]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    assert int(fox.class_bits[0x015F]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    assert int(fox.class_bits[0x0160]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    assert int(fox.class_bits[0x0166]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    assert int(fox.class_bits[0x00F4]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    assert int(fox.class_bits[0x00FB]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL

    assert cb_name(0x0019, "phys") == "ftCo_Jump_Phys"  # JumpF
    assert cb_name(0x0019, "coll") == "ftCo_Jump_Coll"
    assert int(fox.class_bits[0x0019]) & CLASS_COMMON_AIR_PHYS
    assert int(fox.class_bits[0x0019]) & CLASS_COMMON_AIR_COLL
    assert int(fox.class_bits[0x0019]) & CLASS_COMMON_AIR_WALLJUMP_COLL

    assert cb_name(0x00CA, "coll") == "ftCo_PassiveWall_Coll"
    assert cb_name(0x00CB, "coll") == "ftCo_PassiveWall_Coll"
    assert int(fox.class_bits[0x00CA]) & CLASS_COMMON_AIR_WALLJUMP_COLL
    assert int(fox.class_bits[0x00CB]) & CLASS_COMMON_AIR_WALLJUMP_COLL

    assert cb_name(0x0023, "phys") == "ftCo_FallSpecial_Phys"  # FallSpecial
    assert cb_name(0x0023, "coll") == "ftCo_FallSpecial_Coll"
    assert int(fox.class_bits[0x0023]) & CLASS_COMMON_AIR_PHYS
    assert int(fox.class_bits[0x0023]) & CLASS_COMMON_AIR_COLL
    assert not int(fox.class_bits[0x0023]) & CLASS_COMMON_AIR_WALLJUMP_COLL
    assert int(fox.class_bits[0x0023]) & CLASS_FT80082B1C_BASIC_LANDING_COLL
    assert cb_name(0x00FB, "coll") == "ftCo_MissFoot_Coll"
    assert int(fox.class_bits[0x00FB]) & CLASS_FT80082B1C_BASIC_LANDING_COLL

    # Cliff ledgedash floor-owner consumer:
    # EscapeAir_Coll is the generated MotionState callback consumed by
    # src/mpcoll_ground.c's Cliff/CollData ledge floor owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    assert cb_name(0x00EC, "coll") == "ftCo_EscapeAir_Coll"
    # Callback ids renumber by design when a character is added (falcon port: 339 -> 400).
    assert int(fox.coll_cb_id[0x00EC]) == 400
    assert int(fox.class3_bits[0x00EC]) & CLASS3_PHASE4_ESCAPE_AIR_COLL

    assert cb_name(0x002A, "coll") == "ftCo_Landing_Coll"  # Landing
    assert int(fox.class_bits[0x002A]) & CLASS_LANDING_COLL
    assert int(fox.class_bits[0x002A]) & CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL
    assert cb_name(0x0046, "coll") == "ftCo_LandingAir_Coll"  # LandingAirN
    assert int(fox.class_bits[0x0046]) & CLASS_LANDING_AIR
    assert int(fox.class_bits[0x0046]) & CLASS_LANDING_AIR_COLL
    assert int(fox.class_bits[0x0046]) & CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL
    assert cb_name(0x00B8, "coll") == "ftCo_DownWait_Coll"
    assert not int(fox.class_bits[0x00B8]) & CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL
    assert cb_name(0x00C7, "coll") == "ftCo_Passive_Coll"
    assert not int(fox.class_bits[0x00C7]) & CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL
    assert int(fox.class_bits[0x00C7]) & CLASS_FT80083F88_GROUND_TO_AIR_COLL
    assert cb_name(0x0018, "coll") == "ftCo_KneeBend_Coll"
    assert int(fox.class_bits[0x0018]) & CLASS_FT80083F88_GROUND_TO_AIR_COLL
    assert cb_name(0x0026, "coll") == "ftCo_DamageFall_Coll"  # DamageFall
    assert int(fox.class_bits[0x0026]) & CLASS_DAMAGE_FALL_COLL
    assert int(fox.class3_bits[0x0026]) & CLASS3_PHASE4_DAMAGE_FALL_COLL
    assert cb_name(0x005B, "coll") == "ftCo_DamageFlyRoll_Coll"  # DamageFlyRoll
    assert int(fox.class_bits[0x005B]) & CLASS_DAMAGE_FLY_COLL
    assert int(fox.class3_bits[0x005B]) & CLASS3_PHASE4_DAMAGE_FLY_COLL

    # GuardOn carries x9_b1 in the raw MotionState +0x8 word, matching MSLACID1 continuity.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    assert int(fox.motion_state_word[0x00B2]) & (1 << 22)


@pytest.mark.integration
def test_motion_state_class_equivalence_for_migrated_predicates() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)
    symbols = read_callback_manifest(MANIFEST)
    max_action = min(len(fox.class_bits), len(falco.class_bits))

    def both_have(action_id: int, bit: int) -> bool:
        return bool(int(fox.class_bits[action_id]) & bit) and bool(int(falco.class_bits[action_id]) & bit)

    def both_coll(action_id: int, symbol: str) -> bool:
        return (
            symbols[int(fox.coll_cb_id[action_id])] == symbol
            and symbols[int(falco.coll_cb_id[action_id])] == symbol
        )

    attack_air = {0x0041, 0x0042, 0x0043, 0x0044, 0x0045}
    attack_s3 = {0x0033, 0x0034, 0x0035, 0x0036, 0x0037}
    attack_s4 = {0x003A, 0x003B, 0x003C, 0x003D, 0x003E}
    common_damage = {
        0x004B,
        0x004C,
        0x004D,
        0x004E,
        0x004F,
        0x0050,
        0x0051,
        0x0052,
        0x0053,
        0x0054,
        0x0055,
        0x0056,
        0x00B9,
        0x00C1,
    }
    damage_air = {0x0054, 0x0055, 0x0056}
    damage_ground = {
        0x004B,
        0x004C,
        0x004D,
        0x004E,
        0x004F,
        0x0050,
        0x0051,
        0x0052,
        0x0053,
    }
    damage_fly = {0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x00F7, 0x00F8}
    landing_air = {0x0046, 0x0047, 0x0048, 0x0049, 0x004A}
    common_air_phys = {
        0x0019,
        0x001A,
        0x001B,
        0x001C,
        0x001D,
        0x001E,
        0x001F,
        0x0020,
        0x0021,
        0x0022,
        0x0023,
        0x0024,
        0x0025,
    }
    common_air_coll = set(common_air_phys)
    common_air_walljump_coll = {
        0x0019,
        0x001A,
        0x001B,
        0x001C,
        0x001D,
        0x001E,
        0x001F,
        0x0020,
        0x0021,
        0x0022,
        0x00CA,
        0x00CB,
    }
    landing_coll = {0x002A, 0x002B}
    damagefall_coll = {0x0026}
    grounded_stage_object_carry = {
        0x000E,
        0x000F,
        0x0010,
        0x0011,
        0x0012,
        0x0013,
        0x0014,
        0x0015,
        0x0016,
        0x0017,
        0x0027,
        0x0028,
        0x0029,
        0x002A,
        0x002B,
        *range(0x002C, 0x0041),
        *range(0x0046, 0x004B),
        0x00B2,
        0x00B3,
        0x00B4,
        0x00B5,
        0x00B6,
        0x00BB,
        0x00BC,
        0x00BD,
        0x00C3,
        0x00C4,
        0x00C5,
        0x00C8,
        0x00C9,
        0x00D4,
        0x00D5,
        0x00D6,
        0x00D7,
        0x00D8,
        0x00D9,
        0x00DA,
        0x00DB,
        0x00DC,
        0x00DD,
        0x00DE,
    }
    grounded_attack = {*range(0x002C, 0x0041)}
    grounded_attack_wait_iasa_specials = {
        *range(0x0033, 0x0039),
        *range(0x003A, 0x0041),
    }
    grounded_attack_wait_iasa_locomotion = {
        0x002C,
        0x002D,
        0x002E,
        0x0032,
        *range(0x0033, 0x0041),
    }
    grounded_attack_wait_iasa_catch_guard = {
        0x002E,
        *range(0x0033, 0x0038),
        0x003F,
        0x0040,
    }
    guardon_frame_start_x672_iasa = {
        0x000E,
        0x000F,
        0x0010,
        0x0011,
        0x0012,
        0x0014,
        0x0015,
        0x0016,
        0x0027,
        0x0028,
        0x0029,
    }
    fresh_guardon_item_shielddesc_iasa = {
        0x000E,
        0x000F,
        0x0010,
        0x0011,
        0x0012,
        0x0014,
        0x0015,
        0x0016,
        0x0027,
        0x0028,
        0x0029,
        0x002A,
        0x002B,
    }
    ft80081d0c_air_coll = {
        *range(0x0041, 0x0046),
        0x00CD,
        0x00CE,
        0x00EC,
        0x0115,
        0x0137,
        0x013A,
        0x013C,
        0x013E,
        0x0140,
    }
    ft_check_ground_ledge_air_coll = {
        0x00F4,
        0x00FB,
        0x015E,
        0x015F,
        0x0160,
        0x0162,
        0x0164,
        0x0166,
    }
    ft80083f88_ground_to_air_coll = {
        0x0012,
        0x0018,
        0x0027,
        0x0028,
        0x0029,
        0x00B7,
        0x00B8,
        0x00BA,
        0x00BE,
        0x00BF,
        0x00C0,
        0x00C2,
        0x00C6,
        0x00C7,
        0x00CF,
        0x00D0,
        0x00D1,
        0x00D2,
        0x00D3,
        0x00EE,
        0x0155,
        0x0156,
        0x0157,
    }
    ft80083090_platform_pass_coll = {
        0x0019,
        0x001A,
        0x001B,
        0x001C,
        0x001D,
        0x001E,
        0x001F,
        0x0020,
        0x0021,
        0x0022,
        0x0023,
        0x0024,
        0x0025,
        0x00CA,
        0x00CB,
        0x00CC,
        0x0105,
        0x0107,
    }
    ft80082b1c_basic_landing_coll = {
        0x0019,
        0x001A,
        0x001B,
        0x001C,
        0x001D,
        0x001E,
        0x001F,
        0x0020,
        0x0021,
        0x0022,
        0x0023,
        0x0024,
        0x0025,
        0x00E6,
        0x00FB,
        0x0105,
        0x0107,
        0x0158,
        0x0159,
        0x015A,
    }
    ft800827a0_edge_snap_coll = {
        *range(0x002C, 0x0041),
        0x00BB,
        0x00BC,
        0x00BD,
        0x00C3,
        0x00C4,
        0x00C5,
        0x00C8,
        0x00C9,
        0x00D4,
        0x00D5,
        0x00D6,
        0x00D7,
        0x00D8,
        0x00D9,
        0x00DA,
        0x00DB,
        0x00DC,
        0x00DD,
        0x00DE,
        0x00E9,
        0x00EA,
        0x00EB,
        0x0108,
        0x0109,
        0x015D,
    }
    escape_air_coll = {0x00EC}
    fx_specials_ground_b108_coll = {0x015B, 0x015C}

    for action_id in range(max_action):
        assert both_have(action_id, CLASS_ATTACK_AIR) == (action_id in attack_air)
        assert both_have(action_id, CLASS_ATTACK_AIR) == both_coll(
            action_id, "ftCo_AttackAir_Coll"
        )
        assert both_have(action_id, CLASS_ATTACK_S3) == (action_id in attack_s3)
        assert both_have(action_id, CLASS_ATTACK_S4) == (action_id in attack_s4)
        assert both_have(action_id, CLASS_DAMAGE_COMMON) == (action_id in common_damage)
        assert both_have(action_id, CLASS_DAMAGE_AIR) == (action_id in damage_air)
        assert both_have(action_id, CLASS_DAMAGE_GROUND) == (action_id in damage_ground)
        assert both_have(action_id, CLASS_DAMAGE_FLY) == (action_id in damage_fly)
        assert both_have(action_id, CLASS_LANDING_AIR) == (action_id in landing_air)
        assert both_have(action_id, CLASS_COMMON_AIR_PHYS) == (action_id in common_air_phys)
        assert both_have(action_id, CLASS_COMMON_AIR_COLL) == (action_id in common_air_coll)
        assert both_have(action_id, CLASS_COMMON_AIR_WALLJUMP_COLL) == (
            action_id in common_air_walljump_coll
        )
        assert both_have(action_id, CLASS_LANDING_COLL) == (action_id in landing_coll)
        assert both_have(action_id, CLASS_LANDING_AIR_COLL) == (action_id in landing_air)
        assert both_have(action_id, CLASS_DAMAGE_COMMON_COLL) == (action_id in common_damage)
        assert both_have(action_id, CLASS_DAMAGE_FLY_COLL) == (action_id in damage_fly)
        assert both_have(action_id, CLASS_DAMAGE_FALL_COLL) == (action_id in damagefall_coll)
        assert both_have(action_id, CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL) == (
            action_id in grounded_stage_object_carry
        )
        assert both_have(action_id, CLASS_GROUNDED_ATTACK) == (action_id in grounded_attack)
        assert both_have(action_id, CLASS_GUARDON_FRAME_START_X672_IASA) == (
            action_id in guardon_frame_start_x672_iasa
        )
        assert (
            bool(int(fox.class2_bits[action_id]) & CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)
            and bool(int(falco.class2_bits[action_id]) & CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)
        ) == (action_id in fresh_guardon_item_shielddesc_iasa)
        assert both_have(action_id, CLASS_FT80081D0C_AIR_COLL) == (
            action_id in ft80081d0c_air_coll
        )
        assert both_have(action_id, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) == (
            action_id in ft_check_ground_ledge_air_coll
        )
        assert both_have(action_id, CLASS_FT80083F88_GROUND_TO_AIR_COLL) == (
            action_id in ft80083f88_ground_to_air_coll
        )
        assert both_have(action_id, CLASS_FT80083090_PLATFORM_PASS_COLL) == (
            action_id in ft80083090_platform_pass_coll
        )
        assert both_have(action_id, CLASS_FT80082B1C_BASIC_LANDING_COLL) == (
            action_id in ft80082b1c_basic_landing_coll
        )
        assert both_have(action_id, CLASS_FT800827A0_EDGE_SNAP_COLL) == (
            action_id in ft800827a0_edge_snap_coll
        )
        assert both_have(action_id, CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS) == (
            action_id in grounded_attack_wait_iasa_specials
        )
        assert both_have(action_id, CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION) == (
            action_id in grounded_attack_wait_iasa_locomotion
        )
        assert both_have(action_id, CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD) == (
            action_id in grounded_attack_wait_iasa_catch_guard
        )
        assert both_have(action_id, CLASS_ESCAPE_AIR_COLL) == (action_id in escape_air_coll)
        assert both_have(action_id, CLASS_FX_SPECIALS_GROUND_B108_COLL) == (
            action_id in fx_specials_ground_b108_coll
        )


@pytest.mark.integration
def test_stage_object_carry_class_tracks_grounded_floor_persistence_owner() -> None:
    fox = read_mslmso01_v1(FOX)
    symbols = read_callback_manifest(MANIFEST)

    def has_carry(action_id: int) -> bool:
        return bool(int(fox.class_bits[action_id]) & CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL)

    def coll_name(action_id: int) -> str:
        return symbols[int(fox.coll_cb_id[action_id])]

    # Landing/LandingAir share the floor-persistence owner that should inherit Randall's stage
    # object motion while already attached to its moving floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Coll
    assert coll_name(0x002A) == "ftCo_Landing_Coll"
    assert coll_name(0x0046) == "ftCo_LandingAir_Coll"
    assert has_carry(0x002A)
    assert has_carry(0x0046)

    # These downed/passive-family callbacks route through ft_80084104 like grounded attacks, so they
    # should inherit the same attached-floor stage-object carry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_DownAttack_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Coll
    assert coll_name(0x00BC) == "ftCo_Down_Coll"
    assert coll_name(0x00BB) == "ftCo_DownAttack_Coll"
    assert coll_name(0x00C8) == "ftCo_PassiveStand_Coll"
    assert has_carry(0x00BC)
    assert has_carry(0x00BB)
    assert has_carry(0x00C8)

    # Catch-family grounded callbacks use ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC, so they
    # share the same attached-floor persistence owner as other B2DC grounded states.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_Catch_Coll,ftCo_CatchDash_Coll,ftCo_CatchPull_Coll,ftCo_CatchWait_Coll,
    #   ftCo_CatchAttack_Coll,ftCo_CatchCut_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_800827A0}
    assert coll_name(0x00D4) == "ftCo_Catch_Coll"
    assert coll_name(0x00D6) == "ftCo_CatchDash_Coll"
    assert coll_name(0x00D5) == "ftCo_CatchPull_Coll"
    assert coll_name(0x00D7) == "ftCo_CatchPull_Coll"
    assert coll_name(0x00D8) == "ftCo_CatchWait_Coll"
    assert coll_name(0x00D9) == "ftCo_CatchAttack_Coll"
    assert coll_name(0x00DA) == "ftCo_CatchCut_Coll"
    assert has_carry(0x00D4)
    assert has_carry(0x00D6)
    assert has_carry(0x00D5)
    assert has_carry(0x00D7)
    assert has_carry(0x00D8)
    assert has_carry(0x00D9)
    assert has_carry(0x00DA)

    # Grounded Throw* callbacks also branch to ft_800841B8 -> ft_800827A0 while ground-owned.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
    #   ftCo_ThrowF_Coll,ftCo_ThrowB_Coll,ftCo_ThrowHi_Coll,ftCo_ThrowLw_Coll}
    assert coll_name(0x00DB) == "ftCo_ThrowF_Coll"
    assert coll_name(0x00DC) == "ftCo_ThrowB_Coll"
    assert coll_name(0x00DD) == "ftCo_ThrowHi_Coll"
    assert coll_name(0x00DE) == "ftCo_ThrowLw_Coll"
    assert has_carry(0x00DB)
    assert has_carry(0x00DC)
    assert has_carry(0x00DD)
    assert has_carry(0x00DE)

    # Other downed/passive states are separately owned in source and must not borrow the
    # Landing/Wait stage-object carry class.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownWait.c::ftCo_DownWait_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
    assert coll_name(0x00B8) == "ftCo_DownWait_Coll"
    assert coll_name(0x00C7) == "ftCo_Passive_Coll"
    assert coll_name(0x00B9) == "ftCo_DownDamage_Coll"
    assert not has_carry(0x00B8)
    assert not has_carry(0x00C7)
    assert not has_carry(0x00B9)

    # Airborne floor-search callbacks can land on Randall, but they are not already-grounded
    # floor-persistence riders and must not receive the pre-projection stage-object carry.
    assert coll_name(0x001D) == "ftCo_Fall_Coll"
    assert not has_carry(0x001D)


@pytest.mark.integration
def test_mpcoll_wrapper_phase_classes_cover_source_phase_overlaps() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)

    def both_have(action_id: int, bit: int) -> bool:
        return bool(int(fox.class_bits[action_id]) & bit) and bool(
            int(falco.class_bits[action_id]) & bit
        )

    # Source wrapper phase groups consumed by src/mpcoll_ground.c. Some states intentionally carry
    # multiple phase bits because the source wrapper stack layers helpers, e.g. grounded Attack11
    # preserves CollData.floor through the grounded carry owner and can still enter the
    # ft_800827A0/mpColl_8004B2DC edge-snap phase.
    # - ft_80083090/ft_800831CC/ft_800835B0 pass ftCo_80096CC8 for platform-pass ownership.
    # - ft_80081D0C / ft_CheckGroundAndLedge use the direct airborne floor wrapper.
    # - ft_80081DD4 selects DamageFly/Damage/DamageFall collision wrappers.
    # - ft_80082708 / ft_8004B108 owns allow-ground-to-air grounded callbacks.
    # - ft_800827A0 / ft_8004B2DC owns grounded edge-snap callbacks.
    # refs/melee/src/melee/ft/ft_081B.c
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_800473CC,mpColl_800477E0,mpColl_8004B108,mpColl_8004B2DC}
    assert both_have(0x0019, CLASS_FT80083090_PLATFORM_PASS_COLL)  # JumpF
    assert both_have(0x0023, CLASS_FT80083090_PLATFORM_PASS_COLL)  # FallSpecial
    assert not both_have(0x0041, CLASS_FT80083090_PLATFORM_PASS_COLL)  # AttackAirN

    assert both_have(0x0041, CLASS_FT80081D0C_AIR_COLL)  # AttackAirN
    assert both_have(0x00EC, CLASS_FT80081D0C_AIR_COLL)  # EscapeAir
    assert not both_have(0x0019, CLASS_FT80081D0C_AIR_COLL)  # JumpF

    assert both_have(0x015E, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL)  # SpecialAirSStart
    assert both_have(0x0166, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL)  # SpecialHiFall
    assert not both_have(0x0041, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL)  # AttackAirN

    assert both_have(0x004B, CLASS_DAMAGE_COMMON_COLL)  # DamageHi1
    assert both_have(0x005B, CLASS_DAMAGE_FLY_COLL)  # DamageFlyRoll
    assert both_have(0x0026, CLASS_DAMAGE_FALL_COLL)  # DamageFall
    assert not both_have(0x0041, CLASS_DAMAGE_COMMON_COLL)  # AttackAirN

    assert both_have(0x0018, CLASS_FT80083F88_GROUND_TO_AIR_COLL)  # KneeBend
    assert both_have(0x015B, CLASS_FX_SPECIALS_GROUND_B108_COLL)  # SpecialSStart
    assert not both_have(0x002A, CLASS_FT80083F88_GROUND_TO_AIR_COLL)  # Landing

    assert both_have(0x0030, CLASS_FT800827A0_EDGE_SNAP_COLL)  # Attack11
    assert both_have(0x0030, CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL)
    assert both_have(0x00D4, CLASS_FT800827A0_EDGE_SNAP_COLL)  # Catch
    assert both_have(0x00D4, CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL)
    assert not both_have(0x00B8, CLASS_FT800827A0_EDGE_SNAP_COLL)  # DownWait


def test_motion_state_owner_phase3_common_owner_classes_exclude_later_families() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)

    def both_have2(action_id: int, bit: int) -> bool:
        return bool(int(fox.class2_bits[action_id]) & bit) and bool(
            int(falco.class2_bits[action_id]) & bit
        )

    # Phase 3 grounded common owners:
    # refs/melee/src/melee/ft/ft_081B.c common grounded wrappers and ft_80083F88.
    assert both_have2(0x000E, CLASS2_COMMON_GROUNDED_COLL)  # Wait
    assert both_have2(0x000F, CLASS2_COMMON_GROUNDED_COLL)  # WalkSlow
    assert both_have2(0x000F, CLASS2_WALK_ACTION)
    assert both_have2(0x0010, CLASS2_WALK_ACTION)
    assert both_have2(0x0011, CLASS2_WALK_ACTION)
    assert both_have2(0x0014, CLASS2_COMMON_GROUNDED_COLL)  # Dash
    assert both_have2(0x0014, CLASS2_COMMON_GROUNDED_B108_COLL)
    assert both_have2(0x0018, CLASS2_COMMON_GROUNDED_B108_COLL)  # KneeBend
    assert both_have2(0x0027, CLASS2_COMMON_GROUNDED_COLL)  # Squat
    assert both_have2(0x0027, CLASS2_COMMON_GROUNDED_B108_COLL)
    assert both_have2(0x002A, CLASS2_COMMON_GROUNDED_COLL)  # Landing
    assert both_have2(0x002A, CLASS2_COMMON_GROUNDED_B4B0_COLL)
    assert both_have2(0x000E, CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)  # Wait IASA
    assert both_have2(0x002A, CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)  # Landing IASA
    assert not both_have2(0x002A, CLASS2_COMMON_GROUNDED_B108_COLL)
    assert both_have2(0x00B3, CLASS2_COMMON_GROUNDED_COLL)  # Guard
    assert both_have2(0x00B3, CLASS2_COMMON_GROUNDED_B108_COLL)
    assert both_have2(0x00B2, CLASS2_GUARD_STATE)  # GuardOn
    assert both_have2(0x00B6, CLASS2_GUARD_STATE)  # GuardReflect
    assert both_have2(0x00F5, CLASS2_COMMON_GROUNDED_COLL)  # Ottotto
    assert both_have2(0x00F5, CLASS2_COMMON_GROUNDED_B2DC_COLL)
    assert both_have2(0x00D4, CLASS2_CATCH_START_FLOOR_LOSS)  # Catch
    assert both_have2(0x00D6, CLASS2_CATCH_START_FLOOR_LOSS)  # CatchDash
    assert not both_have2(0x00D5, CLASS2_CATCH_START_FLOOR_LOSS)  # CatchPull
    for action_id in (0x000F, 0x0012, 0x0014, 0x0018, 0x00B2, 0x00EB, 0x00D4, 0x00D6):
        assert both_have2(action_id, CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST), hex(
            action_id
        )
    assert not both_have2(0x000E, CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST)  # Wait
    assert not both_have2(0x0030, CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST)  # Attack100Loop

    for action_id in (0x000E, 0x000F, 0x002A, 0x0046, 0x002C, 0x003A, 0x003F, 0x00B6):
        assert both_have2(action_id, CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS), hex(action_id)
    assert not both_have2(0x0030, CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS)  # Attack100Loop
    assert not both_have2(0x00D4, CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS)  # Catch
    assert not both_have2(0x0041, CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS)  # AttackAirN

    # Phase 3 airborne common owners:
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083090,ft_800831CC,ft_800835B0}.
    assert both_have2(0x0019, CLASS2_COMMON_AIRBORNE_COLL)  # JumpF
    assert both_have2(0x001B, CLASS2_COMMON_AIRBORNE_COLL)  # JumpAerialF
    assert both_have2(0x001D, CLASS2_COMMON_AIRBORNE_COLL)  # Fall
    assert both_have2(0x001D, CLASS2_FALL_LIKE_ACTION)  # Fall
    assert both_have2(0x0020, CLASS2_FALL_LIKE_ACTION)  # FallAerial
    assert not both_have2(0x0023, CLASS2_FALL_LIKE_ACTION)  # FallSpecial
    assert both_have2(0x0023, CLASS2_COMMON_AIRBORNE_COLL)  # FallSpecial
    assert both_have2(0x00F4, CLASS2_COMMON_AIRBORNE_COLL)  # Pass
    assert both_have2(0x00FB, CLASS2_COMMON_AIRBORNE_COLL)  # MissFoot
    assert both_have2(0x0105, CLASS2_COMMON_AIRBORNE_COLL)  # CliffJump2Slow1
    for action_id in (0x00FC, 0x00FD, 0x001D, 0x001E, 0x001F, 0x0019, 0x001B, 0x00EC):
        assert both_have2(action_id, CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE), hex(action_id)
    assert not both_have2(0x0020, CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE)  # FallAerial
    assert not both_have2(0x0100, CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE)  # CliffAttackSlow
    for action_id in (0x000E, 0x002A, 0x002B, 0x0046, 0x004A):
        assert both_have2(action_id, CLASS2_LANDING_ROOT_FLOOR_SNAP), hex(action_id)
    assert not both_have2(0x001D, CLASS2_LANDING_ROOT_FLOOR_SNAP)  # Fall

    # Hard Phase 3 exclusions: AttackAir, EscapeAir, Damage, item/projectile, catch/throw/capture,
    # and Fox/Falco bespoke special callbacks must not enter the narrow Phase 3 class word.
    excluded = [
        0x0030,  # Attack11
        0x0041,  # AttackAirN
        0x004B,  # DamageHi1
        0x005E,  # LightThrowF
        0x00D4,  # Catch
        0x00DB,  # ThrowF
        0x00E1,  # CaptureDamageHi
        0x00EC,  # EscapeAir
        0x0091,  # ItemParasolFall
        0x0158,  # Fox/Falco SpecialAirNStart
        0x015B,  # Fox/Falco SpecialSStart
        0x015E,  # Fox/Falco SpecialAirSStart
        0x0163,  # Fox/Falco SpecialHi
        0x016D,  # Fox/Falco SpecialAirLwStart
    ]
    for action_id in excluded:
        assert not both_have2(action_id, CLASS2_COMMON_GROUNDED_COLL)
        assert not both_have2(action_id, CLASS2_COMMON_GROUNDED_B108_COLL)
        assert not both_have2(action_id, CLASS2_COMMON_GROUNDED_B2DC_COLL)
        assert not both_have2(action_id, CLASS2_COMMON_GROUNDED_B4B0_COLL)
        assert not both_have2(action_id, CLASS2_COMMON_AIRBORNE_COLL)
        assert not both_have2(action_id, CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA)


@pytest.mark.integration
def test_sheik_vanish_motion_state_callbacks_publish_source_collision_owners() -> None:
    sheik = read_mslmso01_v1(SHEIK)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(sheik, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    def has(action_id: int, bit: int) -> bool:
        return bool(int(sheik.class_bits[action_id]) & bit)

    # Sheik Vanish aerial start/travel/end callbacks call ft_CheckGroundAndLedge and can publish
    # LandingFallSpecial or CliffCatch from their collision callbacks. Keep this generated from the
    # decomp MotionState callback table rather than a local action-id list in runtime collision.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialAirHi_Coll}
    for action_id, coll_cb in (
        (0x0166, "ftSk_SpecialAirHiStart_0_Coll"),
        (0x0167, "ftSk_SpecialAirHiStart_1_Coll"),
        (0x0168, "ftSk_SpecialAirHi_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL)
        assert int(sheik.fx_special_kind[action_id]) == 0

    # The grounded Vanish end callback calls ft_800827A0 before converting to the aerial fall
    # state; the start callbacks use ft_80082708 and must not inherit this edge-snap owner.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialHiStart_0_Coll,ftSk_SpecialHiStart_1_Coll,ftSk_SpecialHi_Coll}
    assert cb_name(0x0165, "coll") == "ftSk_SpecialHi_Coll"
    assert has(0x0165, CLASS_FT800827A0_EDGE_SNAP_COLL)
    assert not has(0x0163, CLASS_FT800827A0_EDGE_SNAP_COLL)
    assert not has(0x0164, CLASS_FT800827A0_EDGE_SNAP_COLL)


@pytest.mark.integration
def test_falcon_raptor_motion_state_callbacks_publish_source_collision_owners() -> None:
    falcon = read_mslmso01_v1(FALCON)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(falcon, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    def has(action_id: int, bit: int) -> bool:
        return bool(int(falcon.class_bits[action_id]) & bit)

    # Raptor Boost start switches source helpers from cmd_vars[2]:
    # cmd2 live uses ft_80082708/B108; cmd2 clear uses ft_80084104/B2DC edge snap. The generated
    # table publishes the superset and runtime narrows it from the extracted script timeline.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Coll
    # data/moves/falcon.json::specials_by_msid.303 set_cmd_var(idx=2,value={1,0})
    assert cb_name(0x015D, "coll") == "ftCa_SpecialSStart_Coll"
    assert has(0x015D, CLASS_FX_SPECIALS_GROUND_B108_COLL)
    assert has(0x015D, CLASS_FT800827A0_EDGE_SNAP_COLL)

    # Raptor Boost hit-punch does not branch on cmd_vars[2]; it always calls ft_80082708.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialS_Coll
    assert cb_name(0x015E, "coll") == "ftCa_SpecialS_Coll"
    assert has(0x015E, CLASS_FX_SPECIALS_GROUND_B108_COLL)
    assert not has(0x015E, CLASS_FT800827A0_EDGE_SNAP_COLL)

    # Aerial Raptor Start/Hit call ft_80081D0C directly. This owner also controls whether a live
    # ftCommon ECB lock can preserve desired.bottom through the collision callback.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
    #   ftCa_SpecialAirSStart_Coll,ftCa_SpecialAirS_Coll}
    for action_id, coll_cb in (
        (0x015F, "ftCa_SpecialAirSStart_Coll"),
        (0x0160, "ftCa_SpecialAirS_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_FT80081D0C_AIR_COLL)


@pytest.mark.integration
def test_falcon_kick_landing_end_publishes_source_collision_owner() -> None:
    falcon = read_mslmso01_v1(FALCON)
    symbols = read_callback_manifest(MANIFEST)

    action_id = 0x0168
    coll_cb = symbols[int(falcon.coll_cb_id[action_id])]
    assert coll_cb == "ftCa_SpecialAirLwEnd_Coll"
    # ftCa_SpecialAirLwEnd_Coll -> ft_80084104 -> ft_800827A0/mpColl_8004B2DC.
    # This is the grounded landing-skid state; the adjacent airborne backflip state has its own
    # doColl landing callback and must not inherit the endpoint-snap owner.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    #   ftCa_SpecialAirLwEnd_Coll,ftCa_SpecialAirLwEndAir_Coll}
    assert int(falcon.class_bits[action_id]) & CLASS_FT800827A0_EDGE_SNAP_COLL
    assert not int(falcon.class_bits[0x0169]) & CLASS_FT800827A0_EDGE_SNAP_COLL


@pytest.mark.integration
def test_falcon_dive_motion_state_callbacks_publish_source_collision_owners() -> None:
    falcon = read_mslmso01_v1(FALCON)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(falcon, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    def has(action_id: int, bit: int) -> bool:
        return bool(int(falcon.class_bits[action_id]) & bit)

    def has2(action_id: int, bit: int) -> bool:
        return bool(int(falcon.class2_bits[action_id]) & bit)

    # Falcon Dive ground/air Coll callbacks run doAirColl on the airborne branch:
    # ft_CheckGroundAndLedge -> mpColl_800473CC. Keep this generated so floor/wall/ceiling
    # publication uses the same callback-local CollData owner as other SpecialHi families.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
    #   ftCa_SpecialHi_Coll,ftCa_SpecialAirHi_Coll}
    for action_id, coll_cb in (
        (0x0161, "ftCa_SpecialHi_Coll"),
        (0x0162, "ftCa_SpecialAirHi_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_SPECIALHI)
        assert has(action_id, CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL)
        assert has2(action_id, CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL)

    # Throw0 uses the direct airborne floor wrapper after its Anim callback refreshes the
    # five-frame common ECB lock.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{
    #   ftCa_SpecialHiThrow0_Anim,ftCa_SpecialHiThrow0_Coll}
    assert cb_name(0x0164, "coll") == "ftCa_SpecialHiThrow0_Coll"
    assert has(0x0164, CLASS_FT80081D0C_AIR_COLL)


@pytest.mark.integration
def test_falcon_direct_air_special_callbacks_publish_source_collision_owner() -> None:
    falcon = read_mslmso01_v1(FALCON)
    symbols = read_callback_manifest(MANIFEST)

    for action_id, coll_cb in (
        (0x015C, "ftCa_SpecialAirN_Coll"),
        (0x0165, "ftCa_SpecialLw_Coll"),
        (0x0166, "ftCa_SpecialLwEnd_Coll"),
        (0x0167, "ftCa_SpecialAirLw_Coll"),
        (0x0169, "ftCa_SpecialAirLwEndAir_Coll"),
        (0x016A, "ftCa_SpecialLwEndAir_Coll"),
    ):
        callback_id = int(falcon.coll_cb_id[action_id])
        assert symbols[callback_id] == coll_cb
        assert int(falcon.class_bits[action_id]) & CLASS_FT80081D0C_AIR_COLL


@pytest.mark.integration
def test_sheik_chain_motion_state_callbacks_publish_source_collision_owners() -> None:
    sheik = read_mslmso01_v1(SHEIK)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(sheik, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    def has(action_id: int, bit: int) -> bool:
        return bool(int(sheik.class_bits[action_id]) & bit)

    # Sheik Chain aerial Start/Active/End callbacks call ft_80081D0C directly. Ground contact is
    # therefore the generated CheckGroundOnly owner, not the common-air platform-pass owner.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialAirSStart_Coll,ftSk_SpecialAirS_Coll,ftSk_SpecialAirSEnd_Coll}
    for action_id, coll_cb in (
        (0x0160, "ftSk_SpecialAirSStart_Coll"),
        (0x0161, "ftSk_SpecialAirS_Coll"),
        (0x0162, "ftSk_SpecialAirSEnd_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_FT80081D0C_AIR_COLL)
        assert not has(action_id, CLASS_FT80083090_PLATFORM_PASS_COLL)

    # Grounded Chain Start/Active/End callbacks call ft_800827A0 before their source handoff.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialSStart_Coll,ftSk_SpecialS_Coll,ftSk_SpecialSEnd_Coll}
    for action_id, coll_cb in (
        (0x015D, "ftSk_SpecialSStart_Coll"),
        (0x015E, "ftSk_SpecialS_Coll"),
        (0x015F, "ftSk_SpecialSEnd_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_FT800827A0_EDGE_SNAP_COLL)


@pytest.mark.integration
def test_sheik_needle_motion_state_callbacks_publish_source_collision_owners() -> None:
    sheik = read_mslmso01_v1(SHEIK)
    symbols = read_callback_manifest(MANIFEST)

    def cb_name(action_id: int, lane: str) -> str:
        cb_id = getattr(sheik, f"{lane}_cb_id")[action_id]
        return symbols[int(cb_id)]

    def has(action_id: int, bit: int) -> bool:
        return bool(int(sheik.class_bits[action_id]) & bit)

    # Aerial Needle Start/Loop/Cancel callbacks call ft_80081D0C directly before their grounded
    # handoff; End uses ft_80082708 and is therefore a separate landing owner.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialAirNStart_Coll,ftSk_SpecialAirNLoop_Coll,ftSk_SpecialAirNCancel_Coll,
    #   ftSk_SpecialAirNEnd_Coll}
    for action_id, coll_cb in (
        (0x0159, "ftSk_SpecialAirNStart_Coll"),
        (0x015A, "ftSk_SpecialAirNLoop_Coll"),
        (0x015B, "ftSk_SpecialAirNCancel_Coll"),
    ):
        assert cb_name(action_id, "coll") == coll_cb
        assert has(action_id, CLASS_FT80081D0C_AIR_COLL)
        assert not has(action_id, CLASS_FT80083090_PLATFORM_PASS_COLL)

    assert cb_name(0x015C, "coll") == "ftSk_SpecialAirNEnd_Coll"
    assert not has(0x015C, CLASS_FT80081D0C_AIR_COLL)


def test_motion_state_owner_phase3_class2_matches_source_callbacks_for_all_actions() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)
    symbols = read_callback_manifest(MANIFEST)

    common_grounded = {
        "ftCo_Wait_Coll",
        "ftCo_Walk_Coll",
        "ftCo_Turn_Coll",
        "ftCo_TurnRun_Coll",
        "ftCo_Dash_Coll",
        "ftCo_Run_Coll",
        "ftCo_RunDirect_Coll",
        "ftCo_RunBrake_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_Landing_Coll",
        "ftCo_GuardOn_Coll",
        "ftCo_Guard_Coll",
        "ftCo_GuardOff_Coll",
        "ftCo_GuardSetOff_Coll",
        "ftCo_Ottotto_Coll",
        "ftCo_OttottoWait_Coll",
    }
    common_grounded_b108 = {
        "ftCo_KneeBend_Coll",
        "ftCo_Turn_Coll",
        "ftCo_Dash_Coll",
        "ftCo_Run_Coll",
        "ftCo_RunDirect_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_GuardOn_Coll",
        "ftCo_Guard_Coll",
        "ftCo_GuardOff_Coll",
        "ftCo_GuardSetOff_Coll",
    }
    common_grounded_b2dc = {
        "ftCo_TurnRun_Coll",
        "ftCo_Ottotto_Coll",
        "ftCo_OttottoWait_Coll",
    }
    common_grounded_b4b0 = {
        "ftCo_Wait_Coll",
        "ftCo_Walk_Coll",
        "ftCo_RunBrake_Coll",
        "ftCo_Landing_Coll",
    }
    common_airborne = {
        "ftCo_Fall_Coll",
        "ftCo_FallAerial_Coll",
        "ftCo_FallSpecial_Coll",
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_CliffJump2_Coll",
        "ftCo_MissFoot_Coll",
        "ftCo_Pass_Coll",
    }

    fresh_guardon_item_shielddesc_iasa = {
        "ftCo_Wait_IASA",
        "ftCo_Walk_IASA",
        "ftCo_Turn_IASA",
        "ftCo_Dash_IASA",
        "ftCo_Run_IASA",
        "ftCo_RunDirect_IASA",
        "ftCo_Squat_IASA",
        "ftCo_SquatWait_IASA",
        "ftCo_SquatRv_IASA",
        "ftCo_Landing_IASA",
    }

    def expected_bits(anim_cb: str, iasa_cb: str, phys_cb: str, coll_cb: str) -> int:
        bits = 0
        if iasa_cb in fresh_guardon_item_shielddesc_iasa:
            bits |= CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA
        if coll_cb in common_grounded:
            bits |= CLASS2_COMMON_GROUNDED_COLL
        if coll_cb in common_grounded_b108:
            bits |= CLASS2_COMMON_GROUNDED_B108_COLL
        if coll_cb in common_grounded_b2dc:
            bits |= CLASS2_COMMON_GROUNDED_B2DC_COLL
        if coll_cb in common_grounded_b4b0:
            bits |= CLASS2_COMMON_GROUNDED_B4B0_COLL
        if coll_cb in common_airborne:
            bits |= CLASS2_COMMON_AIRBORNE_COLL
        if anim_cb == "ftCo_Walk_Anim" and iasa_cb == "ftCo_Walk_IASA" and coll_cb == "ftCo_Walk_Coll":
            bits |= CLASS2_WALK_ACTION
        if anim_cb in {"ftCo_Fall_Anim", "ftCo_FallAerial_Anim"} and iasa_cb in {
            "ftCo_Fall_IASA",
            "ftCo_FallAerial_IASA",
        }:
            bits |= CLASS2_FALL_LIKE_ACTION
        if (
            anim_cb.startswith("ftCo_Guard")
            and iasa_cb.startswith("ftCo_Guard")
            and coll_cb.startswith("ftCo_Guard")
        ):
            bits |= CLASS2_GUARD_STATE
        if anim_cb in {"ftCo_Catch_Anim", "ftCo_CatchDash_Anim"} and coll_cb in {
            "ftCo_Catch_Coll",
            "ftCo_CatchDash_Coll",
        }:
            bits |= CLASS2_CATCH_START_FLOOR_LOSS
        if (
            bits
            & (
                CLASS2_COMMON_GROUNDED_COLL
                | CLASS2_COMMON_GROUNDED_B108_COLL
                | CLASS2_COMMON_GROUNDED_B2DC_COLL
                | CLASS2_COMMON_GROUNDED_B4B0_COLL
            )
            or coll_cb == "ftCo_LandingAir_Coll"
            or coll_cb in {"ftCo_EscapeF_Coll", "ftCo_EscapeB_Coll", "ftCo_EscapeN_Coll"}
            or anim_cb in {
                "ftCo_Attack11_Anim",
                "ftCo_Attack12_Anim",
                "ftCo_Attack13_Anim",
                "ftCo_AttackDash_Anim",
                "ftCo_AttackS3_Anim",
                "ftCo_AttackHi3_Anim",
                "ftCo_AttackLw3_Anim",
                "ftCo_AttackS4_Anim",
                "ftCo_AttackHi4_Anim",
                "ftCo_AttackLw4_Anim",
            }
            or anim_cb == "ftCo_GuardReflect_Anim"
        ):
            bits |= CLASS2_GROUND_LOCOMOTION_FLOOR_LOSS
        if (
            anim_cb in {"ftCo_CliffCatch_Anim", "ftCo_CliffWait_Anim"}
            or (anim_cb == "ftCo_Fall_Anim" and iasa_cb == "ftCo_Fall_IASA")
            or anim_cb in {"ftCo_Jump_Anim", "ftCo_JumpAerial_Anim", "ftCo_EscapeAir_Anim"}
        ):
            bits |= CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE
        if phys_cb in {"ftCo_CliffCatch_Phys", "ftCo_CliffJump1_Phys", "ftCo_CliffWait_Phys"}:
            bits |= CLASS2_CLIFF_HOLD_PHYS_SNAP
        if coll_cb in {
            "ftFx_SpecialAirHi_Coll",
            "ftFx_SpecialHiFall_Coll",
            "ftCa_SpecialHi_Coll",
            "ftCa_SpecialAirHi_Coll",
        }:
            bits |= CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL
        if (
            anim_cb == "ftCo_Wait_Anim"
            or anim_cb == "ftCo_Landing_Anim"
            or anim_cb == "ftCo_LandingAir_Anim"
        ):
            bits |= CLASS2_LANDING_ROOT_FLOOR_SNAP
        if bits & CLASS2_WALK_ACTION or anim_cb in {
            "ftCo_Turn_Anim",
            "ftCo_Dash_Anim",
            "ftCo_Squat_Anim",
            "ftCo_KneeBend_Anim",
            "ftCo_GuardOn_Anim",
            "ftCo_EscapeN_Anim",
            "ftCo_Catch_Anim",
            "ftCo_CatchDash_Anim",
        }:
            bits |= CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST
        return bits

    # Exhaustive source-callback boundary for Phase 3 routing. Later-owner callbacks can still
    # carry older broad class_bits, but they must not enter this narrow common owner word.
    # refs/melee/src/melee/ft/ft_081B.c common grounded/airborne wrappers.
    for label, table in (("fox", fox), ("falco", falco)):
        for action_id in range(len(table.class2_bits)):
            iasa_cb = symbols[int(table.iasa_cb_id[action_id])]
            phys_cb = symbols[int(table.phys_cb_id[action_id])]
            coll_cb = symbols[int(table.coll_cb_id[action_id])]
            anim_cb = symbols[int(table.anim_cb_id[action_id])]
            assert int(table.class2_bits[action_id]) == expected_bits(anim_cb, iasa_cb, phys_cb, coll_cb), (
                label,
                action_id,
                anim_cb,
                iasa_cb,
                phys_cb,
                coll_cb,
            )


def test_motion_state_owner_class3_matches_source_callbacks_for_all_actions() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)
    marth = read_mslmso01_v1(MARTH)
    falcon = read_mslmso01_v1(FALCON)
    sheik = read_mslmso01_v1(SHEIK)
    zelda = read_mslmso01_v1(Path("data/motion_state/owners/zelda.bin"))
    symbols = read_callback_manifest(MANIFEST)

    def expected_bits(coll_cb: str) -> int:
        bits = 0
        if coll_cb == "ftCo_AttackAir_Coll":
            bits |= CLASS3_PHASE4_ATTACK_AIR_COLL
        if coll_cb == "ftCo_EscapeAir_Coll":
            bits |= CLASS3_PHASE4_ESCAPE_AIR_COLL
        if coll_cb in {"ftCo_Damage_Coll", "ftCo_DownDamage_Coll"}:
            bits |= CLASS3_PHASE4_DAMAGE_COMMON_COLL
        if coll_cb in {"ftCo_DamageFly_Coll", "ftCo_DamageFlyRoll_Coll", "ftCo_FlyReflect_Coll"}:
            bits |= CLASS3_PHASE4_DAMAGE_FLY_COLL
        if coll_cb == "ftCo_DamageFall_Coll":
            bits |= CLASS3_PHASE4_DAMAGE_FALL_COLL
        if coll_cb in ORDINARY_WALLJUMP_COLL_CBS:
            bits |= CLASS3_ORDINARY_WALLJUMP_COLL
        if coll_cb in WALLTECH_COLL_CBS:
            bits |= CLASS3_WALLTECH_COLL
        return bits

    # Exhaustive class3 source-callback boundary. The phase-4 bits route later floor owners, while
    # the walljump bits classify the two PassiveWall entry producers across common and character
    # callbacks.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_Coll,ftCo_DamageFly_Coll,ftCo_DamageFlyRoll_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::ftCo_FlyReflect_Coll
    for label, table in (
        ("fox", fox),
        ("falco", falco),
        ("marth", marth),
        ("falcon", falcon),
        ("sheik", sheik),
        ("zelda", zelda),
    ):
        for action_id in range(len(table.class3_bits)):
            coll_cb = symbols[int(table.coll_cb_id[action_id])]
            assert int(table.class3_bits[action_id]) == expected_bits(coll_cb), (
                label,
                action_id,
                coll_cb,
            )

    def both_have3(action_id: int, bit: int) -> bool:
        return bool(int(fox.class3_bits[action_id]) & bit) and bool(
            int(falco.class3_bits[action_id]) & bit
        )

    assert both_have3(0x0041, CLASS3_PHASE4_ATTACK_AIR_COLL)  # AttackAirN
    assert both_have3(0x00EC, CLASS3_PHASE4_ESCAPE_AIR_COLL)  # EscapeAir
    assert both_have3(0x0054, CLASS3_PHASE4_DAMAGE_COMMON_COLL)  # DamageAir1
    assert both_have3(0x005A, CLASS3_PHASE4_DAMAGE_FLY_COLL)  # DamageFlyTop
    assert both_have3(0x0026, CLASS3_PHASE4_DAMAGE_FALL_COLL)  # DamageFall
    for action_id in (0x0026, 0x00CC, 0x00DA, 0x00E5, 0x00F4, 0x00FA, 0x00FB, 0x0105, 0x0107):
        assert both_have3(action_id, CLASS3_ORDINARY_WALLJUMP_COLL)
    for action_id in (0x0058, 0x005B, 0x00B9, 0x00F7):
        assert both_have3(action_id, CLASS3_WALLTECH_COLL)
    assert int(marth.class3_bits[367]) & CLASS3_ORDINARY_WALLJUMP_COLL
    assert int(marth.class3_bits[368]) & CLASS3_ORDINARY_WALLJUMP_COLL

    excluded = [
        0x001D,  # Fall, Phase 3 common airborne
        0x002A,  # Landing, Phase 3 common grounded
        0x00CD,  # AirCatch
        0x0115,  # ItemThrowAirF
        0x0137,  # HammerFall
        0x013A,  # CargoFall
        0x013C,  # CargoThrowF
        0x0140,  # YoshiEgg
        0x015E,  # Fox/Falco SpecialAirSStart
        0x016D,  # Fox/Falco SpecialAirLwStart
    ]
    for action_id in excluded:
        assert not int(fox.class3_bits[action_id]) & (
            CLASS3_PHASE4_ATTACK_AIR_COLL
            | CLASS3_PHASE4_ESCAPE_AIR_COLL
            | CLASS3_PHASE4_DAMAGE_COMMON_COLL
            | CLASS3_PHASE4_DAMAGE_FLY_COLL
            | CLASS3_PHASE4_DAMAGE_FALL_COLL
        )
        assert not int(falco.class3_bits[action_id]) & (
            CLASS3_PHASE4_ATTACK_AIR_COLL
            | CLASS3_PHASE4_ESCAPE_AIR_COLL
            | CLASS3_PHASE4_DAMAGE_COMMON_COLL
            | CLASS3_PHASE4_DAMAGE_FLY_COLL
            | CLASS3_PHASE4_DAMAGE_FALL_COLL
        )


def test_motion_state_owner_reader_rejects_stale_versions(tmp_path: Path) -> None:
    stale = tmp_path / "fox.bin"
    buf = bytearray(HEADER_BYTES)
    buf[0:8] = b"MSLMSO01"
    struct.pack_into("<I", buf, 8, VERSION - 1)
    struct.pack_into("<H", buf, 12, 1)
    stale.write_bytes(bytes(buf))

    with pytest.raises(ValueError, match="unsupported MSLMSO01 version"):
        read_mslmso01_v1(stale)


@pytest.mark.parametrize("size", [60, 63])
def test_motion_state_owner_reader_rejects_truncated_current_header(
    tmp_path: Path, size: int
) -> None:
    path = tmp_path / "truncated.bin"
    buf = bytearray(size)
    buf[0:8] = b"MSLMSO01"
    struct.pack_into("<I", buf, 8, VERSION)
    path.write_bytes(bytes(buf))

    with pytest.raises(ValueError, match="MSLMSO01 table too small"):
        read_mslmso01_v1(path)


def test_motion_state_owner_reader_rejects_table_offsets_inside_header(tmp_path: Path) -> None:
    path = tmp_path / "bad-offset.bin"
    buf = bytearray(FOX.read_bytes())
    struct.pack_into("<I", buf, 16, HEADER_BYTES - 4)
    path.write_bytes(bytes(buf))

    with pytest.raises(ValueError, match="MSLMSO01 bad table offset"):
        read_mslmso01_v1(path)


def test_runtime_rejects_stale_motion_state_owner_tables(tmp_path: Path) -> None:
    data_dir = tmp_path / "data"
    src_root = Path("data")
    for src in src_root.rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to(src_root)
        dst = data_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if rel.parts[:2] == ("motion_state", "owners") and rel.name in {"fox.bin", "falco.bin"}:
            continue
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())

    for ch in ("fox", "falco"):
        stale = data_dir / "motion_state" / "owners" / f"{ch}.bin"
        stale.parent.mkdir(parents=True, exist_ok=True)
        buf = bytearray(HEADER_BYTES)
        buf[0:8] = b"MSLMSO01"
        struct.pack_into("<I", buf, 8, VERSION - 1)
        struct.pack_into("<H", buf, 12, 1)
        stale.write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    h = msl_binding.init(batch_size=1, num_players=2)
except Exception:
    raise SystemExit(0)
if h:
    msl_binding.destroy(h)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_motion_state_owner_extractor_regenerates_stable_artifacts(tmp_path: Path) -> None:
    out_dir = tmp_path / "owners"
    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_motion_state_owners",
            "--melee_decomp",
            "refs/melee",
            "--out_dir",
            str(out_dir),
            "--chars",
            ",".join(_data_manifest_chars()),
        ],
        check=True,
    )
    for rel in [f"{ch}.bin" for ch in _data_manifest_chars()] + ["callback_symbols.json"]:
        assert (out_dir / rel).read_bytes() == (Path("data/motion_state/owners") / rel).read_bytes()


def test_motion_state_owner_source_artifacts_match_generated_data() -> None:
    # build_data's no-decomp path copies these tracked source artifacts directly. Keep their
    # manifest class maps and shared callback-id namespace byte-identical with the generated data
    # tree so a regenerated runtime table cannot silently drift from the packaged fallback.
    rels = [f"{ch}.bin" for ch in _data_manifest_chars()] + ["callback_symbols.json"]
    for rel in rels:
        assert (SOURCE_ARTIFACT_OWNERS / rel).read_bytes() == (
            Path("data/motion_state/owners") / rel
        ).read_bytes(), rel


def test_motion_state_owner_partial_extraction_uses_packaged_callback_namespace(
    tmp_path: Path,
) -> None:
    out_dir = tmp_path / "owners"
    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_motion_state_owners",
            "--melee_decomp",
            "refs/melee",
            "--out_dir",
            str(out_dir),
            "--chars",
            "fox,falco",
        ],
        check=True,
    )
    for rel in ("fox.bin", "falco.bin", "callback_symbols.json"):
        assert (out_dir / rel).read_bytes() == (SOURCE_ARTIFACT_OWNERS / rel).read_bytes(), rel


def test_fx_special_kind_matches_anim_callback_symbols() -> None:
    # fx_special_kind (v18) is generated 1:1 from each row's ANIM callback symbol; the
    # is_spacie/MSL_ACT_FX_* predicate migration consumes it. Exhaustive parity: every
    # action's kind equals the extractor table's mapping for its anim callback, fox and
    # falco agree (clone shares the ftFx machines), and non-Fox specials carry kind 0 everywhere
    # (their same-numbered actions have character-local callbacks).
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)
    marth = read_mslmso01_v1(MARTH)
    sheik = read_mslmso01_v1(SHEIK)
    symbols = read_callback_manifest(MANIFEST)

    for label, table in (("fox", fox), ("falco", falco)):
        for action_id in range(len(table.fx_special_kind)):
            anim_cb = symbols[int(table.anim_cb_id[action_id])]
            expected = FX_SPECIAL_KIND_BY_SYMBOL.get(anim_cb, 0)
            assert int(table.fx_special_kind[action_id]) == expected, (label, action_id, anim_cb)

    assert list(fox.fx_special_kind) == list(falco.fx_special_kind)
    assert not any(int(k) for k in marth.fx_special_kind), "marth must carry no fx kinds"
    assert not any(int(k) for k in sheik.fx_special_kind), "sheik must carry no fx kinds"
    # The kind value space is dense 1..N with no gaps (C enum parity).
    assert sorted(FX_SPECIAL_KIND_VALUES.values()) == list(range(1, len(FX_SPECIAL_KIND_VALUES) + 1))


def test_fx_special_kind_c_enum_matches_extractor_values() -> None:
    # The runtime consumes fx_special_kind through the hand-written MslMsFxSpecialKind C
    # enum (src/motion_state_owners.h); the bytes are generated from FX_SPECIAL_KIND_VALUES.
    # Parse the C enum and assert exact name/value parity so the generated lane and the
    # runtime vocabulary cannot drift while tests stay green.
    import re as _re

    header = Path("src/motion_state_owners.h").read_text(encoding="utf-8")
    m = _re.search(r"typedef enum MslMsFxSpecialKind \{(?P<body>.*?)\} MslMsFxSpecialKind;",
                   header, _re.S)
    assert m is not None, "MslMsFxSpecialKind enum not found in motion_state_owners.h"
    c_values: dict[str, int] = {}
    for em in _re.finditer(r"MSL_FX_KIND_([A-Z_0-9]+) = (\d+),", m.group("body")):
        c_values[em.group(1)] = int(em.group(2))
    assert c_values.pop("NONE") == 0
    # COUNT is the C-side array-size sentinel (one past the last kind), not a kind row.
    assert c_values.pop("COUNT") == max(FX_SPECIAL_KIND_VALUES.values()) + 1
    assert c_values == FX_SPECIAL_KIND_VALUES, (
        "C MslMsFxSpecialKind diverged from the extractor's FX_SPECIAL_KIND_VALUES"
    )
