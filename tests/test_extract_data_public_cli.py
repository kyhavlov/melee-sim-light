from __future__ import annotations

import hashlib
import json

import pytest

from melee_sim import extract_data
from melee_sim.iso import DiscIdentity, IsoFile
from tools.extraction import build_data
from tools.extraction.char_registry import CHARS
from tools.extraction.extract_stage_item_objects import _audit_stage_dat_path


def test_extract_data_extracts_dol_and_does_not_require_decomp(tmp_path, monkeypatch):
    iso = tmp_path / "GALE01.iso"
    iso.write_bytes(b"fake")
    out_dir = tmp_path / "data"
    raw = out_dir / "raw"
    calls: list[list[str]] = []

    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(extract_data, "_default_melee_decomp", lambda: None)
    monkeypatch.setattr(extract_data, "read_disc_identity", lambda _iso: DiscIdentity("GALE01", 2))
    monkeypatch.setattr(extract_data, "list_files", lambda _iso: [])

    def fake_find_files(_files, pattern):
        name = pattern.replace("*", "")
        return [IsoFile(path=name, offset=len(name), size=1)]

    def fake_extract_files(_iso, entries, out_dir):
        for name in entries:
            (out_dir / name).write_bytes(b"x")
        return {name: hashlib.sha256(b"x").hexdigest() for name in entries}

    def fake_extract_dol(_iso, out):
        out.write_bytes(b"dol")

    def fake_run(cmd, check):
        calls.append([str(x) for x in cmd])
        assert check is True
        generated = out_dir / "common" / "fixture.bin"
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.write_bytes(b"generated")
        (out_dir / "manifest.json").write_text(
            json.dumps(
                {
                    "magic": "MSLDATA1",
                    "version": 1,
                    "chars": list(CHARS),
                    "stages": ["grnla", "grnba", "griz", "grps", "grst", "grop"],
                    "schemas": build_data.DATA_SCHEMA_VERSIONS,
                    "raw_manifest_sha256": extract_data.raw_manifest_digest(raw),
                    "files": [
                        {
                            "path": "common/fixture.bin",
                            "sha256": hashlib.sha256(b"generated").hexdigest(),
                            "size": len(b"generated"),
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

    monkeypatch.setattr(extract_data, "find_files", fake_find_files)
    monkeypatch.setattr(extract_data, "extract_files", fake_extract_files)
    monkeypatch.setattr(extract_data, "extract_main_dol", fake_extract_dol)
    monkeypatch.setattr(extract_data.subprocess, "run", fake_run)

    extract_data.main(["--iso", str(iso)])

    assert (raw / "main.dol").read_bytes() == b"dol"
    assert (raw / "PlCo.dat").exists()
    assert (raw / "PdPm.dat").exists()
    assert (raw / "ItCo.usd").exists()
    assert (raw / "EfFxData.dat").exists()
    assert len(calls) == 1
    assert "--raw-manifest" in calls[0]
    assert "--melee-decomp" not in calls[0]

    extract_data.main(["--iso", str(iso)])
    assert len(calls) == 1

    (out_dir / "common" / "fixture.bin").unlink()
    extract_data.main(["--iso", str(iso)])
    assert len(calls) == 2


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


def test_extract_data_rejects_partial_runtime_stage_roots(tmp_path, monkeypatch):
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

    with pytest.raises(SystemExit, match="full stage registry"):
        extract_data.main(
            [
                "--iso",
                str(iso),
                "--out-dir",
                str(out_dir),
                "--stages",
                "grnla",
            ]
        )
    assert not out_dir.exists()


def test_extract_data_rejects_wrong_disc_before_fst_scan(tmp_path, monkeypatch):
    iso = tmp_path / "wrong.iso"
    iso.write_bytes(b"fake")
    monkeypatch.setattr(
        extract_data,
        "read_disc_identity",
        lambda _iso: DiscIdentity("GALP01", 0),
    )
    monkeypatch.setattr(
        extract_data,
        "list_files",
        lambda _iso: pytest.fail("wrong-region rejection must happen before FST scan"),
    )

    with pytest.raises(SystemExit, match="GALE01 revision 2"):
        extract_data.main(["--iso", str(iso)])


def test_stage_item_audit_paths_do_not_depend_on_data_root(tmp_path):
    assert _audit_stage_dat_path(tmp_path / "one" / "GrSt.dat") == "GrSt.dat"
    assert _audit_stage_dat_path(tmp_path / "a-longer-root" / "GrSt.dat") == "GrSt.dat"


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
    raw_manifest = iso_dir / "manifest.json"
    raw_manifest.write_bytes(b"raw manifest")
    stale = out_dir / "common" / "obsolete.bin"
    stale.parent.mkdir(parents=True)
    stale.write_bytes(b"obsolete")
    preserved = out_dir / "stages" / "slippi_neutral_spawns.json"
    preserved.parent.mkdir(parents=True)
    preserved.write_bytes(b"source-authored")

    commands: list[list[str]] = []
    validated: list[tuple[object, set[str], bool]] = []

    def fake_run(mod, argv, timings=None):
        commands.append([mod, *[str(x) for x in argv]])

    monkeypatch.setattr(build_data, "_run", fake_run)
    monkeypatch.setattr(
        build_data,
        "validate_raw_data_root",
        lambda root, *, required_names, verify_hashes: validated.append(
            (root, set(required_names), verify_hashes)
        ),
    )
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
            "--raw-manifest",
            str(raw_manifest),
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
    assert not stale.exists()
    assert preserved.read_bytes() == b"source-authored"
    assert validated == [
        (
            iso_dir.resolve(),
            {
                "main.dol",
                "PlCo.dat",
                "ItCo.dat",
                "EfCoData.dat",
                "GrNLa.dat",
                *(info.pl_dat for info in CHARS.values()),
                *(info.aj_dat for info in CHARS.values()),
                *(info.costume_dat for info in CHARS.values()),
            },
            True,
        )
    ]

    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["chars"] == list(CHARS)
    assert manifest["schemas"] == build_data.DATA_SCHEMA_VERSIONS
