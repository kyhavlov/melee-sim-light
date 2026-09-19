from __future__ import annotations

from pathlib import Path

import pytest

from tools.data.extract import _disc_files, _main_dol, _profile
from tools.data.raw import DataError, raw_dir, validate_raw_dir


def test_repository_iso_contains_complete_supported_profile() -> None:
    iso = Path("SSBM.iso")
    if not iso.is_file():
        pytest.skip("local SSBM.iso is unavailable")
    with iso.open("rb") as source:
        profile = _profile(_disc_files(source))
        dol = _main_dol(source)
    assert len(profile) == 218
    assert {"PlMt.dat", "PlMtAJ.dat"} <= profile.keys()
    assert {"PlGw.dat", "PlGwAJ.dat", "PlGwNr.dat"} <= profile.keys()
    assert {"PlFe.dat", "PlFeAJ.dat", "EfFeData.dat"} <= profile.keys()
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
    assert len(manifest["files"]) == 219
