from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from melee_sim.raw_data import (
    RL_1_0_CHARS,
    RL_1_0_STAGES,
    RawDataError,
    raw_data_dir,
    validate_raw_data_root,
)


def _write_root(raw_dir: Path, *, data: bytes = b"retail data") -> None:
    raw_dir.mkdir(parents=True)
    (raw_dir / "PlCo.dat").write_bytes(data)
    payload = {
        "magic": "MSLRAW1",
        "version": 1,
        "profile": "rl_1_0",
        "chars": list(RL_1_0_CHARS),
        "stages": list(RL_1_0_STAGES),
        "disc": {
            "game_id": "GALE01",
            "revision": 2,
            "sha256": "0" * 64,
            "size": 1459978240,
        },
        "files": [
            {
                "iso_offset": 1,
                "iso_path": "PlCo.dat",
                "name": "PlCo.dat",
                "sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data),
            }
        ],
    }
    (raw_dir / "manifest.json").write_text(
        json.dumps(payload), encoding="utf-8"
    )


def test_raw_data_root_uses_msl_data_dir(monkeypatch, tmp_path: Path) -> None:
    root = tmp_path / "assets"
    monkeypatch.setenv("MSL_DATA_DIR", str(root))
    assert raw_data_dir() == root.resolve() / "raw"


def test_raw_data_manifest_verifies_required_files_and_hashes(tmp_path: Path) -> None:
    raw_dir = tmp_path / "raw"
    _write_root(raw_dir)

    validate_raw_data_root(raw_dir, required_names=("PlCo.dat",))

    (raw_dir / "PlCo.dat").write_bytes(b"corruption")
    with pytest.raises(RawDataError, match="wrong size|wrong SHA-256"):
        validate_raw_data_root(raw_dir, required_names=("PlCo.dat",))


def test_raw_data_manifest_rejects_partial_profile(tmp_path: Path) -> None:
    raw_dir = tmp_path / "raw"
    _write_root(raw_dir)
    manifest = raw_dir / "manifest.json"
    payload = json.loads(manifest.read_text(encoding="utf-8"))
    payload["chars"] = ["fox"]
    manifest.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(RawDataError, match="complete GALE01 revision 2 RL 1.0 profile"):
        validate_raw_data_root(raw_dir)
