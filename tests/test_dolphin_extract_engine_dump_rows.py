from __future__ import annotations

from pathlib import Path

import pytest

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


def test_collect_rows_v12_surfaces_hidden_fighter_lanes(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v12.bin", version=12)
    payload = _collect_rows(dump_path, Window(-123, -123), [1])
    row = payload["rows"][0]
    assert row["x670_timers_bits"] == 0x01020304
    assert row["x674_timers_bits"] == 0x05060708
    assert row["x2344_bits"] == 0x3F800000
    assert row["x2348_bits"] == 0x40000000
    assert row["x234c_bits"] == 0x40400000
    assert row["transn"] == [4.0, 5.0, 6.0]
    assert row["x1a50_bits"] == 0x090A0B0C

    text = _summary_text(payload)
    assert "x670=0x01020304" in text
    assert "x674=0x05060708" in text
    assert "x2344=0x3f800000" in text
    assert "transn=(4.000,5.000,6.000)" in text


def test_collect_rows_v12_uses_actual_active_port_ids(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v12_ports.bin", version=12, port_ids=(1, 4))

    payload = _collect_rows(dump_path, Window(-123, -123), [4])
    assert payload["port_ids"] == [1, 4]
    assert int(payload["row_count"]) == 1
    assert payload["rows"][0]["port"] == 4
    assert payload["rows"][0]["action_id"] == 101

    all_ports = _collect_rows(dump_path, Window(-123, -123), [])
    assert [row["port"] for row in all_ports["rows"]] == [1, 4]

    with pytest.raises(ValueError, match=r"dump port_ids=\[1, 4\]"):
        _collect_rows(dump_path, Window(-123, -123), [2])
