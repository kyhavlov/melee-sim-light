from __future__ import annotations

import os
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from tests.test_anim_pose import _pick_first_nonempty_anim, _read_header
from tools.extraction.extract_ecb_bottom import ECB_VERSION as ECB_BOTTOM_VERSION
from tools.extraction.extract_ecb_extents import ECB_VERSION as ECB_EXTENTS_VERSION

ROOT = Path(__file__).resolve().parents[1]


def _populate_data_overlay(dst_data_dir: Path) -> None:
    src_data_dir = ROOT / "data"
    dst_data_dir.mkdir(parents=True, exist_ok=True)
    for src in src_data_dir.rglob("*"):
        rel = src.relative_to(src_data_dir)
        dst = dst_data_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())


def _write_ecb_with_version(src: Path, dst: Path, version: int) -> None:
    buf = bytearray(src.read_bytes())
    assert buf[:8] == b"MSLECB01"
    struct.pack_into("<I", buf, 8, int(version))
    if dst.exists():
        dst.unlink()
    dst.write_bytes(bytes(buf))


def _run_ecb_loader(data_dir: Path) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["MSL_DATA_DIR"] = str(data_dir)
    return subprocess.run(
        [
            sys.executable,
            "-c",
            (
                "import msl_binding\n"
                "h = msl_binding.init(batch_size=1, num_players=2)\n"
                "try:\n"
                "    msl_binding.ecb_bottom_rel_y(1, 0, 0)\n"
                "    msl_binding.ecb_extents_rel(1, 0, 0)\n"
                "finally:\n"
                "    msl_binding.destroy(h)\n"
            ),
        ],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )


def _pick_char_anim_from_local_anims() -> tuple[int, int, int]:
    # Use local ISO-derived anim artifacts (no replay/dataset dependency).
    buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, _joint_parts = _read_header(buf)
    msid, _frame_count, _base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    return 1, int(msid), 0


def test_runtime_rejects_stale_ecb_bottom_table() -> None:
    build_dir = ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ecb-bottom-v2-stale-", dir=build_dir) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_overlay(data_dir)
        _write_ecb_with_version(
            ROOT / "data/ecb/fox_bottom.bin",
            data_dir / "ecb/fox_bottom.bin",
            ECB_BOTTOM_VERSION - 1,
        )

        proc = _run_ecb_loader(data_dir)
        assert proc.returncode != 0


def test_runtime_rejects_stale_ecb_extents_table() -> None:
    build_dir = ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ecb-extents-v3-stale-", dir=build_dir) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_overlay(data_dir)
        _write_ecb_with_version(
            ROOT / "data/ecb/fox_extents.bin",
            data_dir / "ecb/fox_extents.bin",
            ECB_EXTENTS_VERSION - 1,
        )

        proc = _run_ecb_loader(data_dir)
        assert proc.returncode != 0


def test_ecb_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_local_anims()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        y = float(msl_binding.ecb_bottom_rel_y(ch, anim, af))
        assert math.isfinite(y)
    finally:
        msl_binding.destroy(handle)


def test_ecb_extents_table_loads_and_returns_finite_values() -> None:
    import msl_binding

    ch, anim, af = _pick_char_anim_from_local_anims()

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        min_x, max_x, min_y, max_y = msl_binding.ecb_extents_rel(ch, anim, af)
        assert math.isfinite(float(min_x))
        assert math.isfinite(float(max_x))
        assert math.isfinite(float(min_y))
        assert math.isfinite(float(max_y))
        assert float(min_x) <= float(max_x)
        assert float(min_y) <= float(max_y)
    finally:
        msl_binding.destroy(handle)
