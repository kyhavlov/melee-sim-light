from __future__ import annotations

from pathlib import Path

from tests.test_dolphin_engine_dump_io import _build_dump
from tools.dolphin.extract_engine_dump_rows import _collect_rows, _summary_text, Window


def test_collect_rows_v8_surfaces_guard_recoil_fields(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v8.bin", version=8)
    payload = _collect_rows(dump_path, Window(-123, -123), [1])
    assert int(payload["row_count"]) == 1

    row = payload["rows"][0]
    assert row["gr_vel"] == 1.0
    assert int(row["ecb_lock_timer"]) == 10
    assert int(row["coll_x130_flags"]) == (1 << 4)
    assert int(row["coll_x130_locked"]) == 1
    assert int(row["shield_damage_taken"]) == 3
    assert int(row["shield_int_damage"]) == 1
    assert int(row["shield_attacker_gobj"]) == 0x803F0000
    assert row["specialn_facing_dir"] == -1.0
    assert int(row["shield_hit_element"]) == 9

    text = _summary_text(payload)
    assert "gr_vel=1.000" in text
    assert "ecb_lock=10" in text
    assert "x19a4=1" in text
    assert "x19ac=-1.000" in text


def test_collect_rows_v9_surfaces_lightshield_field(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v9.bin", version=9)
    payload = _collect_rows(dump_path, Window(-123, -123), [1])
    row = payload["rows"][0]
    assert row["lightshield_amount"] == 0.5

    text = _summary_text(payload)
    assert "light=0.500" in text
