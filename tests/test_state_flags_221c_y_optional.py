from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _populate_data_overlay_without_state_flags_221c_y(dst_data_dir: Path) -> None:
    src_data_dir = ROOT / "data"
    dst_data_dir.mkdir(parents=True, exist_ok=True)
    exclude = {
        Path("state_flags_221c_y/fox.bin"),
        Path("state_flags_221c_y/falco.bin"),
    }

    for src in src_data_dir.rglob("*"):
        rel = src.relative_to(src_data_dir)
        if rel in exclude:
            continue
        dst = dst_data_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())


def test_init_succeeds_without_state_flags_221c_y_tables() -> None:
    pytest.importorskip("msl_binding")

    build_dir = ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build_dir) as td:
        data_dir = Path(td) / "data"
        # Keep required runtime artifacts present, but remove only opcode-52 x221C_u16_y bins to
        # lock optional-init behavior for this table specifically.
        _populate_data_overlay_without_state_flags_221c_y(data_dir)

        env = os.environ.copy()
        env["MSL_DATA_DIR"] = str(data_dir)
        code = (
            "import msl_binding\n"
            "h=msl_binding.init(batch_size=1,num_players=2)\n"
            "assert h is not None\n"
            "msl_binding.destroy(h)\n"
        )
        proc = subprocess.run(
            [sys.executable, "-c", code],
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
        )
        assert proc.returncode == 0, f"stdout={proc.stdout}\nstderr={proc.stderr}"
