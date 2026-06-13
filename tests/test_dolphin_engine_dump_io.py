from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.dolphin import engine_dump_io as io
from tools.dolphin.dolphin_engine_dump import _write_dolphin_ini


def _build_dump(path: Path, *, version: int, port_ids: tuple[int, ...] | None = None) -> Path:
    frame_count = 1
    port_count = len(port_ids) if port_ids is not None else 2
    total_items = 1 if version >= 10 else 0

    frames = np.zeros((frame_count,), dtype=io.FRAME_DTYPE)
    frames["frame_index"][0] = -123

    inputs = np.zeros((frame_count * port_count,), dtype=io.INPUT_DTYPE)
    fighter_dtype = (
        io.FIGHTER_DTYPE_V12
        if version == 12
        else io.FIGHTER_DTYPE_V7
        if version == 10
        else io.FIGHTER_DTYPE_V9
        if version >= 9
        else io.FIGHTER_DTYPE_V8
        if version >= 8
        else io.FIGHTER_DTYPE_V7
    )
    fighters = np.zeros((frame_count * port_count,), dtype=fighter_dtype)
    for slot in range(port_count):
        fighters["action_state"][slot] = np.uint16(100 + slot)
    items = np.zeros((total_items,), dtype=io.ITEM_DTYPE)
    hitboxes = np.zeros((frame_count * port_count * 4,), dtype=io.HITBOX_DTYPE)
    hurtboxes = np.zeros((frame_count * port_count * 15,), dtype=io.HURTBOX_DTYPE)
    hitlists = np.zeros((frame_count * port_count * 4,), dtype=io.HITLIST_DTYPE)
    item_hitlists = np.zeros((total_items * 4,), dtype=io.HITLIST_DTYPE)

    if version >= 7:
        hitlists["group"][0] = 7
        hitlists["victims1_cursor"][0] = 3
        hitlists["victims2_cursor"][0] = 5
        hitlists["owner_gobj"][0] = 0x80453080
        hitlists["victims1_ptr"][0][0] = 0x81000000
        hitlists["victims1_cooldown"][0][0] = 12
        hitlists["victims2_ptr"][0][1] = 0x82000000
        hitlists["victims2_cooldown"][0][1] = 4
    if version >= 8 and version not in (10, 12):
        fighters["gr_vel_bits"][0] = np.uint32(0x3f800000)
        if version >= 9:
            fighters["lightshield_amount_bits"][0] = np.uint32(0x3f000000)
        fighters["ecb_lock_timer"][0] = np.uint8(10)
        fighters["coll_x130_flags"][0] = np.uint32(1 << 4)
        fighters["shield_damage_taken"][0] = np.uint32(3)
        fighters["shield_int_damage"][0] = np.uint32(1)
        fighters["shield_attacker_gobj"][0] = np.uint32(0x803f0000)
        fighters["specialn_facing_dir_bits"][0] = np.uint32(0xbf800000)
        fighters["shield_hit_element"][0] = np.uint32(9)
    if version >= 10:
        items["item_id"][0] = np.uint32(0x81234567)
        items["kind"][0] = np.uint16(54)
        items["state"][0] = np.uint16(1)
        items["owner_port_i8"][0] = np.int8(1)
        items["damage"][0] = np.uint32(3)
        items["xC34_damage_dealt"][0] = np.int32(7)
        items["xCA8_callback_damage"][0] = np.int32(7)
        items["xCBC_hitlag_bits"][0] = np.uint32(0x40000000)
        items["xDA8_short"][0] = np.uint16(1234)
        items["xDCE_flags"][0] = np.uint8(0x20)
        items["xDD4_laser_scale_bits"][0] = np.uint32(0x3F000000)
        item_hitlists["group"][0] = 2
        item_hitlists["victims1_cursor"][0] = 1
        item_hitlists["victims1_ptr"][0][0] = 0x81230000
        item_hitlists["victims1_cooldown"][0][0] = 16
    if version == 12:
        fighters["x670_timers_bits"][0] = np.uint32(0x01020304)
        fighters["x674_timers_bits"][0] = np.uint32(0x05060708)
        fighters["x2344_bits"][0] = np.uint32(0x3F800000)
        fighters["x2348_bits"][0] = np.uint32(0x40000000)
        fighters["x234c_bits"][0] = np.uint32(0x40400000)
        fighters["transn_x_bits"][0] = np.uint32(0x40800000)
        fighters["transn_y_bits"][0] = np.uint32(0x40A00000)
        fighters["transn_z_bits"][0] = np.uint32(0x40C00000)
        fighters["x1a50_bits"][0] = np.uint32(0x090A0B0C)

    frames_offset = io.HEADER_DTYPE.itemsize
    inputs_offset = frames_offset + frames.nbytes
    fighters_offset = inputs_offset + inputs.nbytes
    items_offset = fighters_offset + fighters.nbytes
    hitboxes_offset = items_offset + items.nbytes
    hurtboxes_offset = hitboxes_offset + hitboxes.nbytes
    if version >= 7:
        hitlists_offset = hurtboxes_offset + hurtboxes.nbytes
        item_hitlists_offset = hitlists_offset + hitlists.nbytes if version >= 10 else 0
        total_size = (
            item_hitlists_offset + item_hitlists.nbytes
            if version >= 10
            else hitlists_offset + hitlists.nbytes
        )
    else:
        hitlists_offset = 0
        item_hitlists_offset = 0
        total_size = hurtboxes_offset + hurtboxes.nbytes

    blob = bytearray(total_size)

    header = np.zeros((1,), dtype=io.HEADER_DTYPE)
    header["magic"][0] = io.ENGINE_DUMP_MAGIC
    header["version"][0] = version
    header["endian_tag"][0] = 0x01020304
    header["frame_count"][0] = frame_count
    header["port_count"][0] = port_count
    if port_ids is not None:
        for slot, port in enumerate(port_ids):
            header["port_ids"][0][slot] = np.uint8(port)
    header["stage_id"][0] = 0x20
    header["is_teams"][0] = 0
    header["frames_offset"][0] = frames_offset
    header["inputs_offset"][0] = inputs_offset
    header["fighters_offset"][0] = fighters_offset
    header["items_offset"][0] = items_offset
    header["hitboxes_offset"][0] = hitboxes_offset
    header["hurtboxes_offset"][0] = hurtboxes_offset
    header["total_items"][0] = total_items
    header["hitlists_offset"][0] = hitlists_offset
    header["item_hitlists_offset"][0] = item_hitlists_offset

    blob[: io.HEADER_DTYPE.itemsize] = header.tobytes()
    blob[frames_offset : frames_offset + frames.nbytes] = frames.tobytes()
    blob[inputs_offset : inputs_offset + inputs.nbytes] = inputs.tobytes()
    blob[fighters_offset : fighters_offset + fighters.nbytes] = fighters.tobytes()
    blob[items_offset : items_offset + items.nbytes] = items.tobytes()
    blob[hitboxes_offset : hitboxes_offset + hitboxes.nbytes] = hitboxes.tobytes()
    blob[hurtboxes_offset : hurtboxes_offset + hurtboxes.nbytes] = hurtboxes.tobytes()
    if version >= 7:
        blob[hitlists_offset : hitlists_offset + hitlists.nbytes] = hitlists.tobytes()
    if version >= 10:
        blob[item_hitlists_offset : item_hitlists_offset + item_hitlists.nbytes] = item_hitlists.tobytes()

    path.write_bytes(bytes(blob))
    return path


def test_dolphin_ini_defaults_to_uncapped_jit_null_backends(tmp_path: Path) -> None:
    _write_dolphin_ini(tmp_path)
    ini = (tmp_path / "Config" / "Dolphin.ini").read_text()
    assert "GFXBackend = Null" in ini
    assert "CPUCore = 1" in ini
    assert "EmulationSpeed = 0.000" in ini
    assert "DSPHLE = True" in ini
    assert "Backend = NullSound" in ini


def test_dolphin_ini_keeps_jit_for_bounded_probe_hooks(tmp_path: Path) -> None:
    _write_dolphin_ini(tmp_path)
    ini = (tmp_path / "Config" / "Dolphin.ini").read_text()
    assert "CPUCore = 1" in ini
    assert "CPUCore = 0" not in ini
    assert "EmulationSpeed = 0.000" in ini


def test_read_engine_dump_v6_backward_compat(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v6.bin", version=6)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 6
    assert int(d.frames[0]["frame_index"]) == -123
    assert d.hitlists.size == 0


def test_read_engine_dump_v7_hitlist_fields(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v7.bin", version=7)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 7
    assert d.hitlists.size == 8
    hb0 = d.hitlists[0]
    assert int(hb0["group"]) == 7
    assert int(hb0["victims1_cursor"]) == 3
    assert int(hb0["victims2_cursor"]) == 5
    assert int(hb0["owner_gobj"]) == 0x80453080
    assert int(hb0["victims1_ptr"][0]) == 0x81000000
    assert int(hb0["victims1_cooldown"][0]) == 12
    assert int(hb0["victims2_ptr"][1]) == 0x82000000
    assert int(hb0["victims2_cooldown"][1]) == 4


def test_read_engine_dump_v12_port_ids(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v12_ports.bin", version=12, port_ids=(1, 4))
    d = io.read_engine_dump(dump_path)
    assert int(d.header["port_count"]) == 2
    assert [int(p) for p in d.header["port_ids"].tolist()[:2]] == [1, 4]


def test_read_engine_dump_rejects_unknown_version(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "bad.bin", version=99)
    with pytest.raises(ValueError, match="unsupported engine dump version"):
        io.read_engine_dump(dump_path)


def test_read_engine_dump_v8_guard_recoil_fields(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v8.bin", version=8)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 8
    ft0 = d.fighters[0]
    assert int(ft0["ecb_lock_timer"]) == 10
    assert int(ft0["coll_x130_flags"]) == (1 << 4)
    assert int(ft0["shield_damage_taken"]) == 3
    assert int(ft0["shield_int_damage"]) == 1
    assert int(ft0["shield_attacker_gobj"]) == 0x803F0000
    assert io.f32_from_bits(int(ft0["specialn_facing_dir_bits"])) == pytest.approx(-1.0)
    assert int(ft0["shield_hit_element"]) == 9


def test_read_engine_dump_v9_lightshield_field(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v9.bin", version=9)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 9
    ft0 = d.fighters[0]
    assert io.f32_from_bits(int(ft0["lightshield_amount_bits"])) == pytest.approx(0.5)


def test_read_engine_dump_v10_item_callback_and_hitlist_fields(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v10.bin", version=10)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 10
    assert d.items.size == 1
    item = d.items[0]
    assert int(item["kind"]) == 54
    assert int(item["state"]) == 1
    assert int(item["owner_port_i8"]) == 1
    assert int(item["xC34_damage_dealt"]) == 7
    assert int(item["xCA8_callback_damage"]) == 7
    assert io.f32_from_bits(int(item["xCBC_hitlag_bits"])) == pytest.approx(2.0)
    assert int(item["xDA8_short"]) == 1234
    assert int(item["xDCE_flags"]) == 0x20
    assert io.f32_from_bits(int(item["xDD4_laser_scale_bits"])) == pytest.approx(0.5)
    assert d.item_hitlists.size == 4
    hl0 = d.item_hitlists[0]
    assert int(hl0["group"]) == 2
    assert int(hl0["victims1_cursor"]) == 1
    assert int(hl0["victims1_ptr"][0]) == 0x81230000
    assert int(hl0["victims1_cooldown"][0]) == 16


def test_read_engine_dump_v12_hidden_fighter_lanes(tmp_path: Path) -> None:
    dump_path = _build_dump(tmp_path / "v12.bin", version=12)
    d = io.read_engine_dump(dump_path)
    assert int(d.header["version"]) == 12
    ft0 = d.fighters[0]
    assert int(ft0["x670_timers_bits"]) == 0x01020304
    assert int(ft0["x674_timers_bits"]) == 0x05060708
    assert io.f32_from_bits(int(ft0["x2344_bits"])) == pytest.approx(1.0)
    assert io.f32_from_bits(int(ft0["x2348_bits"])) == pytest.approx(2.0)
    assert io.f32_from_bits(int(ft0["x234c_bits"])) == pytest.approx(3.0)
    assert io.f32_from_bits(int(ft0["transn_x_bits"])) == pytest.approx(4.0)
    assert io.f32_from_bits(int(ft0["transn_y_bits"])) == pytest.approx(5.0)
    assert io.f32_from_bits(int(ft0["transn_z_bits"])) == pytest.approx(6.0)
    assert int(ft0["x1a50_bits"]) == 0x090A0B0C
