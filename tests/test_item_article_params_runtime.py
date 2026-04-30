from __future__ import annotations

import os
import subprocess
import sys
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
