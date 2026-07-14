from __future__ import annotations

import json
from types import SimpleNamespace

import pytest

from melee_sim import extract_data
from tools.extraction import build_data
from tools.extraction.char_registry import CHARS


def test_extract_data_extracts_dol_and_does_not_require_decomp(tmp_path, monkeypatch):
    iso = tmp_path / "GALE01.iso"
    iso.write_bytes(b"fake")
    out_dir = tmp_path / "data"
    calls: list[list[str]] = []

    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(extract_data, "_default_melee_decomp", lambda: None)
    monkeypatch.setattr(extract_data, "list_files", lambda _iso: object())

    def fake_find_files(_files, pattern):
        name = pattern.strip("*")
        return [SimpleNamespace(path=name, size=1)]

    def fake_extract_file(_iso, match, out):
        out.write_bytes(match.path.encode("ascii"))

    def fake_extract_dol(_iso, out):
        out.write_bytes(b"dol")

    def fake_run(cmd, check):
        calls.append([str(x) for x in cmd])
        assert check is True

    monkeypatch.setattr(extract_data, "find_files", fake_find_files)
    monkeypatch.setattr(extract_data, "extract_file", fake_extract_file)
    monkeypatch.setattr(extract_data, "extract_main_dol", fake_extract_dol)
    monkeypatch.setattr(extract_data.subprocess, "run", fake_run)

    extract_data.main(["--iso", str(iso)])

    assert (out_dir / "_iso" / "main.dol").read_bytes() == b"dol"
    assert (out_dir / "_iso" / "PlCo.dat").exists()
    assert calls
    assert "--melee-decomp" not in calls[0]


def test_extract_data_rejects_partial_runtime_character_roots(tmp_path, monkeypatch):
    iso = tmp_path / "GALE01.iso"
    iso.write_bytes(b"fake")
    out_dir = tmp_path / "msl"
    monkeypatch.setattr(
        extract_data,
        "list_files",
        lambda _iso: pytest.fail(
            "partial-root rejection must happen before ISO scanning"
        ),
    )

    with pytest.raises(
        SystemExit, match="runtime data roots require the full character registry"
    ):
        extract_data.main(
            ["--iso", str(iso), "--out-dir", str(out_dir), "--chars", "fox,falco"]
        )
    assert not out_dir.exists()


def test_build_data_rejects_partial_runtime_character_roots(tmp_path):
    with pytest.raises(
        SystemExit, match="runtime data roots require the full character registry"
    ):
        build_data.main(
            [
                "--iso-dir",
                str(tmp_path / "iso"),
                "--out-dir",
                str(tmp_path / "data"),
                "--chars",
                "fox,falco",
            ]
        )


@pytest.mark.parametrize("missing_name", ["PlFxAJ.dat", "PlFxNr.dat", "PlCaAJ.dat"])
def test_build_data_preflights_target_and_donor_animation_dats(
    tmp_path, monkeypatch, missing_name
):
    iso_dir = tmp_path / "iso"
    iso_dir.mkdir()
    required = {"main.dol", "PlCo.dat", "ItCo.dat", "EfCoData.dat"}
    for info in CHARS.values():
        required.update((info.pl_dat, info.aj_dat, info.costume_dat))
    for name in sorted(required - {missing_name}):
        (iso_dir / name).write_bytes(b"fake")
    monkeypatch.setattr(
        build_data,
        "_run",
        lambda *_args, **_kwargs: pytest.fail(
            "DAT preflight must finish before generators run"
        ),
    )

    with pytest.raises(SystemExit, match=missing_name):
        build_data.main(
            [
                "--iso-dir",
                str(iso_dir),
                "--out-dir",
                str(tmp_path / "data"),
                "--chars",
                ",".join(CHARS),
            ]
        )


def test_build_data_routes_motion_state_outputs_through_iso_dol(tmp_path, monkeypatch):
    iso_dir = tmp_path / "iso"
    out_dir = tmp_path / "data"
    missing_decomp = tmp_path / "missing_decomp"
    iso_dir.mkdir()
    required = {"main.dol", "PlCo.dat", "ItCo.dat", "EfCoData.dat", "GrNLa.dat"}
    for info in CHARS.values():
        required.update((info.pl_dat, info.aj_dat, info.costume_dat))
    for name in sorted(required):
        (iso_dir / name).write_bytes(b"fake")

    commands: list[list[str]] = []

    def fake_run(mod, argv, timings=None):
        commands.append([mod, *[str(x) for x in argv]])

    monkeypatch.setattr(build_data, "_run", fake_run)
    build_data.main(
        [
            "--iso-dir",
            str(iso_dir),
            "--out-dir",
            str(out_dir),
            "--stages",
            "grnla",
            "--chars",
            ",".join(reversed(CHARS)),
            "--melee-decomp",
            str(missing_decomp),
        ]
    )

    generator = next(
        cmd
        for cmd in commands
        if cmd[0] == "tools.extraction.extract_motion_state_tables"
    )
    assert generator[generator.index("--dol") + 1] == str(iso_dir / "main.dol")
    assert generator[generator.index("--out-root") + 1] == str(out_dir)
    assert not any("source_artifacts" in token for cmd in commands for token in cmd)
    assert not any(
        cmd[0] == "tools.extraction.extract_staling_move_id" for cmd in commands
    )
    assert not any(
        cmd[0] == "tools.extraction.extract_attack_id_move_id" for cmd in commands
    )
    assert not any(
        cmd[0] == "tools.extraction.extract_motion_state_owners" for cmd in commands
    )

    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["chars"] == list(CHARS)
    assert manifest["schemas"] == build_data.DATA_SCHEMA_VERSIONS
