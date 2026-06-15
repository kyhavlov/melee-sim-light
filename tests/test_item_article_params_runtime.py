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


def test_runtime_item_article_params_rejects_stale_version(tmp_path: Path) -> None:
    data_dir = tmp_path / "data"
    dst = data_dir / "items" / "articles" / "fox_falco.bin"
    dst.parent.mkdir(parents=True)
    buf = bytearray(Path("data/items/articles/fox_falco.bin").read_bytes())
    buf[8:12] = (1).to_bytes(4, "little")
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
    assert version == 3
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
