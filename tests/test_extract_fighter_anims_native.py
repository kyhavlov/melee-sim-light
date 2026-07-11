from __future__ import annotations

from pathlib import Path
import struct

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
        iso_dir / "PlCa.dat",
        iso_dir / "PlCaAJ.dat",
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


@pytest.mark.integration
def test_capture_captain_cross_bakes_falcon_donor_on_fox_skeleton(tmp_path: Path) -> None:
    required = [
        Path("_iso/PlFx.dat"),
        Path("_iso/PlFxNr.dat"),
        Path("_iso/PlFxAJ.dat"),
        Path("_iso/PlCa.dat"),
        Path("_iso/PlCaAJ.dat"),
        Path("_iso/PlCo.dat"),
        Path("data/moves/fox.json"),
    ]
    missing = [p for p in required if not p.exists()]
    if missing:
        pytest.skip("missing local extraction inputs: " + ", ".join(str(p) for p in missing))

    from tools.extraction.extract_fighter_anims import _msid_anim_entry_full, extract_one_character

    donor_entry = _msid_anim_entry_full("falcon", 276)
    assert donor_entry is not None
    assert donor_entry[-2:] == (1, 0x21), (
        "CaptureCaptain enables the common skeleton's inserted TransN2 part and uses "
        "FTKIND_NONE as its FigaTree namespace"
    )

    extract_one_character(
        character="fox",
        moves_path=Path("data/moves/fox.json"),
        out_dir=tmp_path,
        native=True,
        msids=[276],  # ftCo_SM_CaptureCaptain
    )

    buf = (tmp_path / "fox.bin").read_bytes()
    joint_count, anim_count = struct.unpack_from("<HH", buf, 12)
    assert anim_count == 1
    off = 16 + joint_count
    msid, frame_count = struct.unpack_from("<HH", buf, off)
    assert msid == 276
    assert frame_count == 17, "Falcon's 16-frame donor FigaTree must be baked for the Fox victim"

    parts = list(buf[16 : 16 + joint_count])
    assert 71 in parts, "Fox ftParts_GetBoneIndex(FtPart_TransN2) must be present"
    anchor_i = parts.index(71)
    matrix_off = off + 4 + anchor_i * 12 * 4
    anchor = struct.unpack_from("<12f", buf, matrix_off)
    # Falcon's node 52 is authored in FTKIND_NONE's semantic common skeleton. Its source track must
    # land on Fox raw part 71, not raw part 52 and not Falcon's raw TransN2 part 61. These values are
    # the resulting PlCaAJ.dat donor transform on Fox's PlFxNr.dat hierarchy at frame 0.
    assert (anchor[3], anchor[7], anchor[11]) == pytest.approx(
        (-1.9774169921875, 16.91259765625, 11.54571533203125), abs=1e-6
    )
