from __future__ import annotations

from pathlib import Path

import pytest


@pytest.mark.integration
def test_extract_fighter_anims_native_matches_python(tmp_path: Path) -> None:
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
    try:
        import msl_binding  # noqa: F401
    except Exception:
        pytest.skip("msl_binding extension not available")
    import msl_binding

    if not hasattr(msl_binding, "anim_bake_ssanim01"):
        pytest.skip("msl_binding missing anim_bake_ssanim01 (native helper)")

    from tools.extraction.extract_fighter_anims import extract_one_character
    from tools.extraction.extract_fighter_anims import _write_anim_blend_data

    moves_path = Path("data/moves/fox.json")
    if not moves_path.exists():
        pytest.skip("missing local moves file data/moves/fox.json")

    out_py = tmp_path / "py"
    out_native = tmp_path / "native"
    out_py.mkdir()
    out_native.mkdir()

    # Pick a stable, common submotion id (Wait) and compare full serialized outputs.
    msids = [2]

    extract_one_character(character="fox", moves_path=moves_path, out_dir=out_py, native=False, msids=msids)
    extract_one_character(character="fox", moves_path=moves_path, out_dir=out_native, native=True, msids=msids)

    _write_anim_blend_data("fox", out_py)
    _write_anim_blend_data("fox", out_native)

    for suffix in (".bin", ".locals.bin", ".tracks.bin", ".blend.bin", ".dyn.bin"):
        a = (out_py / f"fox{suffix}").read_bytes()
        b = (out_native / f"fox{suffix}").read_bytes()
        assert a == b, f"mismatch for {suffix}"
