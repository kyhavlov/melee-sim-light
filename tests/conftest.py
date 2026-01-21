from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# Make the repo root importable so `tools.*` modules can be imported in tests.
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def _ensure_ecb_bottom_tables() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "ecb" / f"{ch}_bottom.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                    _anim_count = int.from_bytes(f.read(2), "little", signed=False)
                    stride = int.from_bytes(f.read(2), "little", signed=False)  # reserved in v2
                if magic == b"MSLECB01" and ver == 2:
                    # Bottom tables use reserved=0 for historical compatibility.
                    if stride == 0:
                        continue
            except OSError:
                pass
        anims = ROOT / "data" / "anims" / f"{ch}.bin"
        attrs = ROOT / "data" / "characters" / f"{ch}.json"
        if not anims.exists():
            raise RuntimeError(f"missing required anims file for tests: {anims}")
        if not attrs.exists():
            raise RuntimeError(f"missing required attrs file for tests: {attrs}")
        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_ecb_bottom",
                "--character",
                ch,
                "--anims",
                str(anims),
                "--attrs",
                str(attrs),
                "--out",
                str(out),
            ],
            check=True,
        )


def _ensure_ecb_extents_tables() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "ecb" / f"{ch}_extents.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                    _anim_count = int.from_bytes(f.read(2), "little", signed=False)
                    stride = int.from_bytes(f.read(2), "little", signed=False)
                if magic == b"MSLECB01" and ver == 2 and stride == 16:
                    continue
            except OSError:
                pass
        anims = ROOT / "data" / "anims" / f"{ch}.bin"
        attrs = ROOT / "data" / "characters" / f"{ch}.json"
        if not anims.exists():
            raise RuntimeError(f"missing required anims file for tests: {anims}")
        if not attrs.exists():
            raise RuntimeError(f"missing required attrs file for tests: {attrs}")
        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_ecb_extents",
                "--character",
                ch,
                "--anims",
                str(anims),
                "--attrs",
                str(attrs),
                "--out",
                str(out),
            ],
            check=True,
        )


def pytest_sessionstart(session) -> None:  # type: ignore[no-untyped-def]
    _ensure_ecb_bottom_tables()
    _ensure_ecb_extents_tables()
