from __future__ import annotations

import hashlib
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from tools.extraction.char_registry import CHARS as CHAR_REGISTRY
from tools.extraction.extract_motion_state_tables import (
    CLASS_ATTACK_AIR,
    CLASS_DAMAGE_AIR,
    CLASS_DAMAGE_COMMON,
    CLASS_DAMAGE_FLY,
    CLASS_DAMAGE_GROUND,
    CLASS_FT80081D0C_AIR_COLL,
    CLASS_FT800827A0_EDGE_SNAP_COLL,
    CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL,
    CLASS_GROUNDED_ATTACK,
    CLASS_LANDING_AIR,
    CLASS2_CAPTURE_CONSTRAINT_CONDITIONAL_COLL,
    CLASS2_FALCON_DIVE_OWNER_CONDITIONAL_COLL,
    CLASS3_CATCH_KIND_1,
    CLASS3_CATCH_KIND_2,
    CLASS3_CATCH_TARGET_MASK_1,
    CLASS3_CATCH_TARGET_MASK_511,
    CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED,
    CLASS3_FALCON_SPECIALHI_THROW0_COLL,
    CLASS3_FALL_COLL,
    CLASS3_JUMP_COLL,
    COLL_HANDLER_AIR_ATTACK,
    COLL_HANDLER_AIR_ESCAPE,
    COLL_HANDLER_DAMAGE_COMMON,
    COLL_HANDLER_DAMAGE_FLY,
    COLL_HANDLER_GROUND_LANDING,
    COLL_HANDLER_GROUND_LANDING_AIR,
    FX_SPECIAL_KIND_VALUES,
    load_motion_state_rows,
)
from tools.slippi.motion_state_owners import (
    HEADER_BYTES,
    VERSION,
    read_mslmso01_v1,
)


OWNER_DIR = Path("data/motion_state/owners")
CHARS = tuple(CHAR_REGISTRY)


def _table(char: str):
    return read_mslmso01_v1(OWNER_DIR / f"{char}.bin")


def _actions_with(table, lane: str, bit: int) -> set[int]:
    return {i for i, value in enumerate(getattr(table, lane)) if int(value) & bit}


def _stale_owner_table_bytes() -> bytes:
    data = bytearray((OWNER_DIR / "falcon.bin").read_bytes())
    struct.pack_into("<I", data, 8, 22)
    return bytes(data)


def test_motion_state_owner_tables_preserve_raw_motion_state_lanes() -> None:
    fox = _table("fox")
    falco = _table("falco")

    assert len(fox.submotion_id) == 376
    assert len(falco.submotion_id) == 376
    assert int(fox.submotion_id[0x0041]) == 68
    assert int(fox.motion_state_word[0x00B2]) & (1 << 22)
    assert list(fox.submotion_id[:341]) == list(falco.submotion_id[:341])
    assert list(fox.x4_flags[:341]) == list(falco.x4_flags[:341])
    assert list(fox.motion_state_word[:341]) == list(falco.motion_state_word[:341])
    for lane in ("anim_cb_id", "iasa_cb_id", "phys_cb_id", "coll_cb_id", "cam_cb_id"):
        assert list(getattr(fox, lane)[:341]) == list(getattr(falco, lane)[:341])


@pytest.mark.integration
def test_common_owner_families_cover_the_complete_action_sets() -> None:
    fox = _table("fox")
    expected = {
        CLASS_ATTACK_AIR: set(range(0x0041, 0x0046)),
        CLASS_DAMAGE_COMMON: set(range(0x004B, 0x0057)) | {0x00B9, 0x00C1},
        CLASS_DAMAGE_FLY: set(range(0x0057, 0x005C)) | {0x00F7, 0x00F8},
        CLASS_LANDING_AIR: set(range(0x0046, 0x004B)),
        CLASS_GROUNDED_ATTACK: set(range(0x002C, 0x0041)),
        CLASS_DAMAGE_AIR: set(range(0x0054, 0x0057)),
        CLASS_DAMAGE_GROUND: set(range(0x004B, 0x0054)),
    }
    for bit, actions in expected.items():
        assert _actions_with(fox, "class_bits", bit) == actions


def test_coll_handler_kinds_are_explicit_and_stable() -> None:
    fox = _table("fox")
    for action in range(0x0041, 0x0046):
        assert int(fox.coll_handler_kind[action]) == COLL_HANDLER_AIR_ATTACK
    assert int(fox.coll_handler_kind[0x00EC]) == COLL_HANDLER_AIR_ESCAPE
    assert int(fox.coll_handler_kind[0x004B]) == COLL_HANDLER_DAMAGE_COMMON
    for action in (0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x00F7, 0x00F8):
        assert int(fox.coll_handler_kind[action]) == COLL_HANDLER_DAMAGE_FLY
    assert int(fox.coll_handler_kind[0x002A]) == COLL_HANDLER_GROUND_LANDING
    assert int(fox.coll_handler_kind[0x0046]) == COLL_HANDLER_GROUND_LANDING_AIR


def test_character_special_collision_owners_are_pointer_derived() -> None:
    marth = _table("marth")
    falcon = _table("falcon")
    sheik = _table("sheik")
    zelda = _table("zelda")

    for action in (0x015D, 0x015E, 0x0160, 0x0163):
        assert int(marth.class_bits[action]) & CLASS_FT80081D0C_AIR_COLL
        assert int(marth.class_bits[action]) & CLASS_FT800827A0_EDGE_SNAP_COLL
    for action in (0x0166, 0x0167, 0x0168):
        assert int(sheik.class_bits[action]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    for action in (0x0160, 0x0161, 0x0162):
        assert int(zelda.class_bits[action]) & CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL

    assert int(falcon.class2_bits[0x0163]) & CLASS2_FALCON_DIVE_OWNER_CONDITIONAL_COLL
    assert int(falcon.class3_bits[0x0164]) & CLASS3_FALCON_SPECIALHI_THROW0_COLL
    assert (
        int(_table("fox").class2_bits[0x0113])
        & CLASS2_CAPTURE_CONSTRAINT_CONDITIONAL_COLL
    )


def test_preprocessing_callback_consumers_have_explicit_owner_bits() -> None:
    for char in CHARS:
        table = _table(char)
        assert _actions_with(table, "class3_bits", CLASS3_JUMP_COLL) == {0x0019, 0x001A}
        assert _actions_with(table, "class3_bits", CLASS3_FALL_COLL) == {
            0x001D,
            0x001E,
            0x001F,
            0x0021,
            0x0022,
        }
    assert _actions_with(
        _table("falcon"), "class3_bits", CLASS3_FALCON_SPECIALHI_THROW0_COLL
    ) == {0x0164}
    for char in ("fox", "falco", "marth", "sheik", "zelda"):
        assert not _actions_with(
            _table(char), "class3_bits", CLASS3_FALCON_SPECIALHI_THROW0_COLL
        )


def test_motion_state_entry_ledgers_remain_action_owned() -> None:
    common_expected = {
        CLASS3_CATCH_TARGET_MASK_1: {0x00B8, 0x00B9, 0x00C0, 0x00C1},
        CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED: set(range(0x00DB, 0x00DF)),
        CLASS3_CATCH_KIND_1: {0x00D4, 0x00D6},
    }
    for char in CHARS:
        table = _table(char)
        for bit, actions in common_expected.items():
            assert _actions_with(table, "class3_bits", bit) == actions
    falcon = _table("falcon")
    assert int(falcon.class3_bits[0x0161]) & CLASS3_CATCH_KIND_2
    assert int(falcon.class3_bits[0x0162]) & CLASS3_CATCH_KIND_2
    assert int(falcon.class3_bits[0x0163]) & CLASS3_CATCH_TARGET_MASK_511


def test_fx_special_kind_is_pointer_owned_and_dense() -> None:
    fox = _table("fox")
    falco = _table("falco")
    marth = _table("marth")
    assert list(fox.fx_special_kind) == list(falco.fx_special_kind)
    assert list(fox.fx_special_kind[341:370]) == list(range(1, 30))
    assert not any(int(value) for value in marth.fx_special_kind)
    assert sorted(FX_SPECIAL_KIND_VALUES.values()) == list(range(1, 30))


def test_fx_special_kind_c_enum_matches_extractor_values() -> None:
    header = Path("src/motion_state_owners.h").read_text(encoding="utf-8")
    match = re.search(
        r"typedef enum MslMsFxSpecialKind \{(?P<body>.*?)\} MslMsFxSpecialKind;",
        header,
        re.S,
    )
    assert match is not None
    values = {
        item.group(1): int(item.group(2))
        for item in re.finditer(
            r"MSL_FX_KIND_([A-Z_0-9]+) = (\d+),", match.group("body")
        )
    }
    assert values.pop("NONE") == 0
    assert values.pop("COUNT") == 30
    assert values == FX_SPECIAL_KIND_VALUES


def test_motion_state_semantic_lanes_match_reviewed_digest() -> None:
    digest = hashlib.sha256()
    for char in CHARS:
        digest.update(char.encode("ascii") + b"\0")
        table = _table(char)
        for lane in (
            "class_bits",
            "class2_bits",
            "class3_bits",
            "fx_special_kind",
            "coll_handler_kind",
        ):
            digest.update(getattr(table, lane).tobytes())
    assert (
        digest.hexdigest()
        == "aa92ff31ffe2bf8aa3ceb5aa22ee57b1e75cf038b6afff452eb0c08625f7cf66"
    )


@pytest.mark.integration
def test_iso_motion_state_extractor_matches_generated_semantic_lanes() -> None:
    dol = Path("_iso/main.dol")
    if not dol.is_file():
        pytest.skip("run make bootstrap to extract _iso/main.dol")
    extracted = load_motion_state_rows(dol)
    for char, rows in extracted.items():
        table = _table(char)
        assert len(rows) == len(table.submotion_id)
        for index, row in enumerate(rows):
            assert row.submotion_id == int(table.submotion_id[index])
            assert row.x4_flags == int(table.x4_flags[index])
            assert row.motion_state_word == int(table.motion_state_word[index])
            assert row.class_bits == int(table.class_bits[index])
            assert row.class2_bits == int(table.class2_bits[index])
            assert row.class3_bits == int(table.class3_bits[index])
            assert row.fx_special_kind == int(table.fx_special_kind[index])
            assert row.coll_handler_kind == int(table.coll_handler_kind[index])


def test_motion_state_owner_reader_rejects_stale_versions(tmp_path: Path) -> None:
    stale = tmp_path / "falcon_v22.bin"
    stale.write_bytes(_stale_owner_table_bytes())
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
    path.write_bytes(buf)
    with pytest.raises(ValueError, match="MSLMSO01 table too small"):
        read_mslmso01_v1(path)


def test_motion_state_owner_reader_rejects_table_offsets_inside_header(
    tmp_path: Path,
) -> None:
    path = tmp_path / "bad-offset.bin"
    buf = bytearray((OWNER_DIR / "fox.bin").read_bytes())
    struct.pack_into("<I", buf, 16, HEADER_BYTES - 4)
    path.write_bytes(buf)
    with pytest.raises(ValueError, match="MSLMSO01 bad table offset"):
        read_mslmso01_v1(path)


def test_runtime_rejects_stale_motion_state_owner_tables(tmp_path: Path) -> None:
    data_dir = tmp_path / "data"
    for src in Path("data").rglob("*"):
        if not src.is_file():
            continue
        rel = src.relative_to("data")
        dst = data_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if rel.parts[:2] == ("motion_state", "owners") and rel.name in {
            "fox.bin",
            "falco.bin",
        }:
            continue
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())
    for char in ("fox", "falco"):
        stale = data_dir / "motion_state" / "owners" / f"{char}.bin"
        stale.parent.mkdir(parents=True, exist_ok=True)
        stale.write_bytes(_stale_owner_table_bytes())

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
    proc = subprocess.run(
        [sys.executable, "-c", code], env=env, text=True, capture_output=True
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout
