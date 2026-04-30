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
    CLASS_DAMAGE_COMMON,
    CLASS_DAMAGE_FLY,
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
    assert int(fox.class_bits[0x0041]) & CLASS_ATTACK_AIR

    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
    assert cb_name(0x0163, "anim") == "ftFx_SpecialHi_Anim"
    assert cb_name(0x0163, "coll") == "ftFx_SpecialHi_Coll"
    assert int(fox.class_bits[0x0163]) & CLASS_SPECIALHI

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

    for action_id in range(max_action):
        assert both_have(action_id, CLASS_ATTACK_AIR) == (action_id in attack_air)
        assert both_have(action_id, CLASS_ATTACK_S3) == (action_id in attack_s3)
        assert both_have(action_id, CLASS_ATTACK_S4) == (action_id in attack_s4)
        assert both_have(action_id, CLASS_DAMAGE_COMMON) == (action_id in common_damage)
        assert both_have(action_id, CLASS_DAMAGE_FLY) == (action_id in damage_fly)


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
