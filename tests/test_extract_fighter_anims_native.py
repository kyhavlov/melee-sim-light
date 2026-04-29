from __future__ import annotations

from pathlib import Path

import pytest


@pytest.mark.integration
def test_extract_fighter_anims_python_path_is_disabled(tmp_path: Path) -> None:
    iso_dir = Path("_iso")
    required = [
        iso_dir / "PlFx.dat",  # ftData for msid table + blend bytes + model scaling
        iso_dir / "PlFxAJ.dat",  # animation archive
        iso_dir / "PlCo.dat",  # parts tables
        iso_dir / "PlFxNr.dat",  # costume joint tree
    ]
    missing = [p for p in required if not p.exists()]
    if missing:
        pytest.skip("missing local _iso assets: " + ", ".join(str(p) for p in missing))
    from tools.extraction.extract_fighter_anims import extract_one_character

    moves_path = Path("data/moves/fox.json")
    if not moves_path.exists():
        pytest.skip("missing local moves file data/moves/fox.json")

    out_py = tmp_path / "disabled_py"
    out_py.mkdir()

    with pytest.raises(RuntimeError, match="pure-Python fighter animation extraction is disabled"):
        extract_one_character(character="fox", moves_path=moves_path, out_dir=out_py, native=False, msids=[2])


@pytest.mark.integration
def test_ecb_anim_superset_matches_default_extract_for_current_msids(tmp_path: Path) -> None:
    iso_dir = Path("_iso")
    required = [
        iso_dir / "PlFx.dat",
        iso_dir / "PlFxAJ.dat",
        iso_dir / "PlCo.dat",
        iso_dir / "PlFxNr.dat",
    ]
    missing = [p for p in required if not p.exists()]
    if missing:
        pytest.skip("missing local _iso assets: " + ", ".join(str(p) for p in missing))
    try:
        import msl_binding  # noqa: F401
    except Exception:
        pytest.skip("msl_binding extension not available")

    from tools.extraction.extract_fighter_anims import extract_one_character
    from tools.extraction.extract_fighter_anims import _write_anim_blend_data

    moves_path = Path("data/moves/fox.json")
    if not moves_path.exists():
        pytest.skip("missing local moves file data/moves/fox.json")

    out_default = tmp_path / "default"
    out_superset = tmp_path / "superset"
    out_default.mkdir()
    out_superset.mkdir()

    extract_one_character(character="fox", moves_path=moves_path, out_dir=out_default, native=True)
    _write_anim_blend_data("fox", out_default)
    extract_one_character(
        character="fox",
        moves_path=moves_path,
        out_dir=out_superset,
        native=True,
        add_msids=[175, 176],
    )
    _write_anim_blend_data("fox", out_superset)

    for suffix in (".bin", ".locals.bin", ".tracks.bin", ".blend.bin", ".dyn.bin"):
        a = (out_default / f"fox{suffix}").read_bytes()
        b = (out_superset / f"fox{suffix}").read_bytes()
        assert a == b, f"mismatch for {suffix}"
