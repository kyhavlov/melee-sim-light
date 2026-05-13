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
    CLASS_DAMAGE_COMMON,
    CLASS_DAMAGE_COMMON_COLL,
    CLASS_DAMAGE_FALL_COLL,
    CLASS_DAMAGE_FLY,
    CLASS_DAMAGE_FLY_COLL,
    CLASS_GUARDON_FRAME_START_X672_IASA,
    CLASS_FT80081D0C_AIR_COLL,
    CLASS_GROUNDED_ATTACK,
    CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL,
    CLASS_LANDING_AIR,
    CLASS_LANDING_AIR_COLL,
    CLASS_LANDING_COLL,
    CLASS_SIDEB_AIR_GROUND_LEDGE_COLL,
    CLASS_SPECIALHI,
)
from tools.slippi.motion_state_owners import VERSION, read_callback_manifest, read_mslmso01_v1


FOX = Path("data/motion_state/owners/fox.bin")
FALCO = Path("data/motion_state/owners/falco.bin")
MANIFEST = Path("data/motion_state/owners/callback_symbols.json")


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

    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
    assert cb_name(0x0163, "anim") == "ftFx_SpecialHi_Anim"
    assert cb_name(0x0163, "coll") == "ftFx_SpecialHi_Coll"
    assert int(fox.class_bits[0x0163]) & CLASS_SPECIALHI

    assert cb_name(0x015E, "coll") == "ftFx_SpecialAirSStart_Coll"
    assert cb_name(0x015F, "coll") == "ftFx_SpecialAirS_Coll"
    assert cb_name(0x0160, "coll") == "ftFx_SpecialAirSEnd_Coll"
    assert int(fox.class_bits[0x015E]) & CLASS_SIDEB_AIR_GROUND_LEDGE_COLL
    assert int(fox.class_bits[0x015F]) & CLASS_SIDEB_AIR_GROUND_LEDGE_COLL
    assert int(fox.class_bits[0x0160]) & CLASS_SIDEB_AIR_GROUND_LEDGE_COLL

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

    # Cliff ledgedash floor-owner consumer:
    # EscapeAir_Coll is the generated MotionState callback consumed by
    # src/mpcoll_ground.c's Cliff/CollData ledge floor owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    assert cb_name(0x00EC, "coll") == "ftCo_EscapeAir_Coll"
    assert int(fox.coll_cb_id[0x00EC]) == 339

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
    assert cb_name(0x0026, "coll") == "ftCo_DamageFall_Coll"  # DamageFall
    assert int(fox.class_bits[0x0026]) & CLASS_DAMAGE_FALL_COLL
    assert cb_name(0x005B, "coll") == "ftCo_DamageFlyRoll_Coll"  # DamageFlyRoll
    assert int(fox.class_bits[0x005B]) & CLASS_DAMAGE_FLY_COLL

    # GuardOn carries x9_b1 in the raw MotionState +0x8 word, matching MSLACID1 continuity.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    assert int(fox.motion_state_word[0x00B2]) & (1 << 22)


@pytest.mark.integration
def test_motion_state_class_equivalence_for_migrated_predicates() -> None:
    fox = read_mslmso01_v1(FOX)
    falco = read_mslmso01_v1(FALCO)
    max_action = min(len(fox.class_bits), len(falco.class_bits))

    def both_have(action_id: int, bit: int) -> bool:
        return bool(int(fox.class_bits[action_id]) & bit) and bool(int(falco.class_bits[action_id]) & bit)

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
    }
    grounded_attack = {*range(0x002C, 0x0041)}
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
    sideb_air_ground_ledge_coll = {0x015E, 0x015F, 0x0160}

    for action_id in range(max_action):
        assert both_have(action_id, CLASS_ATTACK_AIR) == (action_id in attack_air)
        assert both_have(action_id, CLASS_ATTACK_S3) == (action_id in attack_s3)
        assert both_have(action_id, CLASS_ATTACK_S4) == (action_id in attack_s4)
        assert both_have(action_id, CLASS_DAMAGE_COMMON) == (action_id in common_damage)
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
        assert both_have(action_id, CLASS_FT80081D0C_AIR_COLL) == (
            action_id in ft80081d0c_air_coll
        )
        assert both_have(action_id, CLASS_SIDEB_AIR_GROUND_LEDGE_COLL) == (
            action_id in sideb_air_ground_ledge_coll
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


def test_motion_state_owner_reader_rejects_stale_versions(tmp_path: Path) -> None:
    stale = tmp_path / "fox.bin"
    buf = bytearray(52)
    buf[0:8] = b"MSLMSO01"
    struct.pack_into("<I", buf, 8, VERSION - 1)
    struct.pack_into("<H", buf, 12, 1)
    stale.write_bytes(bytes(buf))

    with pytest.raises(ValueError, match="unsupported MSLMSO01 version"):
        read_mslmso01_v1(stale)


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
        buf = bytearray(52)
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
            "fox,falco",
        ],
        check=True,
    )
    for rel in ("fox.bin", "falco.bin", "callback_symbols.json"):
        assert (out_dir / rel).read_bytes() == (Path("data/motion_state/owners") / rel).read_bytes()
