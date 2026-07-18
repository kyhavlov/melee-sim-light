from __future__ import annotations

import struct
from pathlib import Path

import pytest

from tools.data.extract import _disc_files, _main_dol, _profile
from tools.data.gameplay_parts import emit_gameplay_parts
from tools.data.raw import DataError, raw_dir, validate_raw_dir


def test_repository_iso_contains_complete_supported_profile() -> None:
    iso = Path("SSBM.iso")
    if not iso.is_file():
        pytest.skip("local SSBM.iso is unavailable")
    with iso.open("rb") as source:
        profile = _profile(_disc_files(source))
        dol = _main_dol(source)
    assert len(profile) == 80
    assert dol.path == "sys/main.dol"
    assert dol.size > 0


def test_configured_raw_data_profile_is_valid() -> None:
    path = raw_dir()
    if not (path / "manifest.json").is_file():
        pytest.skip("extracted game data is unavailable")
    try:
        manifest = validate_raw_dir(path, verify_hashes=False)
    except DataError as exc:
        pytest.fail(str(exc))
    assert len(manifest["files"]) == 81


def test_gameplay_part_artifacts_are_deterministic(tmp_path: Path) -> None:
    emit_gameplay_parts(tmp_path)
    artifacts = sorted((tmp_path / "model_parts").glob("*.bin"))
    assert [path.stem for path in artifacts] == [
        "falco",
        "falcon",
        "fox",
        "marth",
        "peach",
        "puff",
        "sheik",
        "zelda",
    ]
    for path in artifacts:
        payload = path.read_bytes()
        assert payload[:8] == b"MSLPART1"
        version, _character, part_count, anchor_count, reserved = struct.unpack_from(
            "<IHHHH", payload, 8
        )
        assert (version, anchor_count, reserved) == (1, 0, 0)
        assert len(payload) == 20 + part_count * 12
