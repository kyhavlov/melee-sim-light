from __future__ import annotations

import os
import subprocess
import sys
import struct
from pathlib import Path

import msl_binding
import pytest


def test_runtime_item_article_params_known_values() -> None:
    fox = msl_binding.item_article_params(1)
    falco = msl_binding.item_article_params(22)

    assert fox["blaster_shot_itkind"] == 54
    assert falco["blaster_shot_itkind"] == 55
    assert fox["side_special_illusion_itkind"] == 56
    assert falco["side_special_illusion_itkind"] == 57
    assert fox["laser_lifetime_frames"] == 35
    assert falco["laser_lifetime_frames"] == 100
    assert fox["laser_damage"] == pytest.approx(3.0)
    assert fox["shield_bounce_extra_degrees"] == pytest.approx(45.0)

    sheik = msl_binding.item_article_params(7)
    assert sheik["needle_throw_itkind"] == 79
    assert sheik["needle_held_itkind"] == 80
    assert sheik["needle_lifetime_frames"] == 30
    assert sheik["needle_bounce_lifetime_frames"] == 120
    assert sheik["needle_launch_speed"] == pytest.approx(4.0)
    assert sheik["needle_hurtbox_count"] == 1
    assert sheik["needle_hurtbox_scale"] == pytest.approx(1.0)
    assert sheik["needle_hitbox_damage"] == pytest.approx(3.0)
    assert sheik["needle_hitbox_count"] == 4
    assert sheik["needle_hitbox_damage_by_id"] == pytest.approx([3.0, 3.0, 3.0, 3.0])
    assert sheik["needle_hitbox_size"] == pytest.approx([1.953, 1.953, 1.953, 1.953])
    assert sheik["needle_hitbox_x_offset"] == pytest.approx([0.0, 0.0, 3.906, -3.906])
    assert sheik["needle_hitbox_y_offset"] == pytest.approx([0.0, 0.0, 0.0, 0.0])
    assert sheik["needle_hitbox_angle"] == [0, 70, 0, 0]
    assert sheik["needle_hitbox_kbg"] == [34, 34, 34, 34]
    assert sheik["needle_hitbox_bkb"] == [24, 24, 24, 24]
    assert sheik["needle_hitbox_element"] == [3, 3, 3, 3]
    # Extracted from state-0 item script target bits: hb0 grounded, hb1-3 aerial.
    assert sheik["needle_hitbox_flags"] == [1, 2, 2, 2]
    assert sheik["sheik_chain_itkind"] == 97
    assert sheik["sheik_chain_spawn_part_id"] == 26
    assert sheik["sheik_chain_lifetime_frames"] == 1400
    assert sheik["sheik_vanish_itkind"] == 85
    assert sheik["sheik_vanish_spawn_part_id"] == 4
    assert sheik["sheik_vanish_lifetime_frames"] == 80
    assert sheik["vanish_hitbox_count"] == 1
    assert sheik["vanish_hitbox_damage"] == pytest.approx(12.0)
    assert sheik["vanish_hitbox_size"] == pytest.approx(10.1556)
    assert sheik["vanish_hitbox_x_offset"] == pytest.approx(0.0)
    assert sheik["vanish_hitbox_y_offset"] == pytest.approx(0.0)
    assert sheik["vanish_hitbox_z_offset"] == pytest.approx(0.0)
    assert sheik["vanish_hitbox_angle"] == 90
    assert sheik["vanish_hitbox_kbg"] == 60
    assert sheik["vanish_hitbox_wsk"] == 0
    assert sheik["vanish_hitbox_bkb"] == 80
    assert sheik["vanish_hitbox_element"] == 1
    assert sheik["vanish_hitbox_shield_damage"] == -128
    assert sheik["vanish_hitbox_flags"] == 3
    assert sheik["vanish_hitbox_size_keyframe_count"] == 2
    assert sheik["vanish_hitbox_size_keyframe_frame_0"] == 7
    assert sheik["vanish_hitbox_size_keyframe_value_0"] == pytest.approx(3.9997439)
    assert sheik["vanish_hitbox_size_keyframe_frame_1"] == 11
    assert sheik["vanish_hitbox_size_keyframe_value_1"] == pytest.approx(1.999872)
    assert sheik["vanish_hitbox_remove_frame"] == 13
    # Thrown-Needle dropped/bounced motion RNG tables, transcribed by source symbol.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_803F6FA0,it_803F6FC0,it_803F7020,it_803F7040}
    assert sheik["needle_drop_min_vel_y"] == pytest.approx(
        [-2.0, -2.1, -2.2, -2.3, -2.4, -2.5, -2.6, -2.7]
    )
    assert sheik["needle_drop_gravity"] == pytest.approx(
        [-0.1, -0.12, -0.14, -0.18, -0.2, -0.22, -0.24, -0.26]
    )
    assert sheik["needle_bounce_min_vel_y"] == pytest.approx(
        [-2.0, -2.1, -2.2, -2.3, -2.4, -2.5, -2.6, -2.7]
    )
    assert sheik["needle_bounce_gravity"] == pytest.approx(
        [-0.1, -0.12, -0.14, -0.18, -0.2, -0.22, -0.24, -0.26]
    )
    # MSLITAR1 v11: SetupBounce xDD8 horizontal-drift magnitude table (it_803F7000), non-negative,
    # signed at runtime by Randi(2). refs/melee/src/melee/it/items/itseakneedlethrown.c::it_803F7000
    assert sheik["needle_bounce_x_vel"] == pytest.approx([0.0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4])
    # MSLITAR1 v12: Side-B Chain itSeakChain_Attrs (PlSk.dat article x4_specialAttributes), used by the
    # Verlet link solver. refs/melee/src/melee/it/itCharItems.h::itSeakChain_Attrs
    assert sheik["sheik_chain_link_count"] == 20
    assert sheik["sheik_chain_segment_length"] == pytest.approx(1.5)
    assert sheik["sheik_chain_friction_x10"] == pytest.approx(0.09)
    assert sheik["sheik_chain_friction_x14"] == pytest.approx(0.2)
    assert sheik["sheik_chain_gravity"] == pytest.approx(0.16)
    assert sheik["sheik_chain_decay_x34"] == pytest.approx(0.98)
    assert sheik["sheik_chain_wall_bounce_x58"] == pytest.approx(0.5)
    assert sheik["sheik_chain_attr_x54"] == pytest.approx(3.0)


def test_runtime_item_article_params_rejects_missing_sheik_needle_drop_record(
    tmp_path: Path,
) -> None:
    # Drop a single needle_drop_min_vel_y_0 record (field_id 102) and prove the loader rejects the
    # whole table rather than silently zeroing a Needle drop terminal-velocity entry.
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    src = Path("data/items/articles/fox_falco.bin").read_bytes()

    header = bytearray(src[:16])
    version, count = struct.unpack_from("<II", header, 8)
    assert version == 12
    record_size = 24
    records = [src[16 + i * record_size : 16 + (i + 1) * record_size] for i in range(count)]

    def is_sheik_needle_drop_min_vel_y_0(rec: bytes) -> bool:
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", rec, 0)
        return (char_id, char_domain, value_type, field_id) == (19, 1, 3, 102)

    kept = [rec for rec in records if not is_sheik_needle_drop_min_vel_y_0(rec)]
    assert len(kept) == len(records) - 1
    struct.pack_into("<I", header, 12, len(kept))
    dst.write_bytes(bytes(header) + b"".join(kept))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_non_negative_sheik_needle_gravity(
    tmp_path: Path,
) -> None:
    # Force one needle_bounce_gravity entry (field_id 126) non-negative and prove the loader rejects
    # the artifact: SetupBounce gravities are strictly negative.
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    buf = bytearray(Path("data/items/articles/fox_falco.bin").read_bytes())

    version, count = struct.unpack_from("<II", buf, 8)
    assert version == 12
    record_size = 24
    patched = False
    for i in range(count):
        off = 16 + i * record_size
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", buf, off)
        if (char_id, char_domain, value_type, field_id) == (19, 1, 3, 126):
            struct.pack_into("<f", buf, off + 12, 0.1)
            patched = True
            break
    assert patched
    dst.write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_stale_version(tmp_path: Path) -> None:
    # A stale local artifact (e.g. a pre-needle_bounce_x_vel v10 fox_falco.bin) must be rejected by the
    # loader rather than read as current: MSLITAR1 is v11 (added needle_bounce_x_vel[8] / it_803F7000).
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    buf = bytearray(Path("data/items/articles/fox_falco.bin").read_bytes())
    buf[8:12] = (10).to_bytes(4, "little")  # the now-stale prior version
    dst.write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(1)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_missing_sheik_needle_record(tmp_path: Path) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    src = Path("data/items/articles/fox_falco.bin").read_bytes()

    header = bytearray(src[:16])
    version, count = struct.unpack_from("<II", header, 8)
    assert version == 12
    record_size = 24
    records = [src[16 + i * record_size : 16 + (i + 1) * record_size] for i in range(count)]

    def is_sheik_needle_a_offset_x(rec: bytes) -> bool:
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", rec, 0)
        return (char_id, char_domain, value_type, field_id) == (19, 1, 3, 20)

    kept = [rec for rec in records if not is_sheik_needle_a_offset_x(rec)]
    assert len(kept) == len(records) - 1
    struct.pack_into("<I", header, 12, len(kept))
    dst.write_bytes(bytes(header) + b"".join(kept))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_missing_sheik_vanish_hitbox_record(
    tmp_path: Path,
) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    src = Path("data/items/articles/fox_falco.bin").read_bytes()

    header = bytearray(src[:16])
    version, count = struct.unpack_from("<II", header, 8)
    assert version == 12
    record_size = 24
    records = [src[16 + i * record_size : 16 + (i + 1) * record_size] for i in range(count)]

    def is_sheik_vanish_damage(rec: bytes) -> bool:
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", rec, 0)
        return (char_id, char_domain, value_type, field_id) == (19, 1, 3, 82)

    kept = [rec for rec in records if not is_sheik_vanish_damage(rec)]
    assert len(kept) == len(records) - 1
    struct.pack_into("<I", header, 12, len(kept))
    dst.write_bytes(bytes(header) + b"".join(kept))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_missing_sheik_vanish_active_window_record(
    tmp_path: Path,
) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    src = Path("data/items/articles/fox_falco.bin").read_bytes()

    header = bytearray(src[:16])
    version, count = struct.unpack_from("<II", header, 8)
    assert version == 12
    record_size = 24
    records = [src[16 + i * record_size : 16 + (i + 1) * record_size] for i in range(count)]

    def is_sheik_vanish_first_size_keyframe(rec: bytes) -> bool:
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", rec, 0)
        return (char_id, char_domain, value_type, field_id) == (19, 1, 1, 95)

    kept = [rec for rec in records if not is_sheik_vanish_first_size_keyframe(rec)]
    assert len(kept) == len(records) - 1
    struct.pack_into("<I", header, 12, len(kept))
    dst.write_bytes(bytes(header) + b"".join(kept))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_zero_sheik_chain_spawn_part_id(
    tmp_path: Path,
) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    buf = bytearray(Path("data/items/articles/fox_falco.bin").read_bytes())

    version, count = struct.unpack_from("<II", buf, 8)
    assert version == 12
    record_size = 24
    patched = False
    for i in range(count):
        off = 16 + i * record_size
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", buf, off)
        if (char_id, char_domain, value_type, field_id) == (19, 1, 1, 100):
            struct.pack_into("<I", buf, off + 8, 0)
            patched = True
            break
    assert patched
    dst.write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_missing_sheik_chain_attr_record(
    tmp_path: Path,
) -> None:
    # Drop the Chain gravity attr (field_id 204) and prove the loader rejects the whole artifact:
    # the itSeakChain_Attrs required-mask must be complete or the Verlet solver would run on a zeroed
    # gravity. refs/melee/src/melee/it/itCharItems.h::itSeakChain_Attrs
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    src = Path("data/items/articles/fox_falco.bin").read_bytes()

    header = bytearray(src[:16])
    version, count = struct.unpack_from("<II", header, 8)
    assert version == 12
    record_size = 24
    records = [src[16 + i * record_size : 16 + (i + 1) * record_size] for i in range(count)]

    def is_sheik_chain_gravity(rec: bytes) -> bool:
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", rec, 0)
        return (char_id, char_domain, value_type, field_id) == (19, 1, 3, 204)

    kept = [rec for rec in records if not is_sheik_chain_gravity(rec)]
    assert len(kept) == len(records) - 1
    struct.pack_into("<I", header, 12, len(kept))
    dst.write_bytes(bytes(header) + b"".join(kept))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_item_article_params_rejects_zero_sheik_vanish_spawn_part_id(
    tmp_path: Path,
) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    buf = bytearray(Path("data/items/articles/fox_falco.bin").read_bytes())

    version, count = struct.unpack_from("<II", buf, 8)
    assert version == 12
    record_size = 24
    patched = False
    for i in range(count):
        off = 16 + i * record_size
        char_id, char_domain, value_type, field_id = struct.unpack_from("<HBBH", buf, off)
        if (char_id, char_domain, value_type, field_id) == (19, 1, 1, 101):
            struct.pack_into("<I", buf, off + 8, 0)
            patched = True
            break
    assert patched
    dst.write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    msl_binding.item_article_params(7)
except RuntimeError:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout
