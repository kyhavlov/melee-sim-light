from __future__ import annotations

from types import SimpleNamespace

from melee_sim import extract_data
from tools.extraction import build_data


def test_extract_data_does_not_require_decomp_before_iso_extract(tmp_path, monkeypatch):
    iso = tmp_path / "GALE01.iso"
    iso.write_bytes(b"fake")
    out_dir = tmp_path / "msl"
    calls: list[list[str]] = []

    monkeypatch.setattr(extract_data, "_default_melee_decomp", lambda: None)
    monkeypatch.setattr(extract_data, "list_files", lambda _iso: object())

    def fake_find_files(_files, pattern):
        name = pattern.strip("*")
        return [SimpleNamespace(path=name, size=1)]

    def fake_extract_file(_iso, match, out):
        out.write_bytes(match.path.encode("ascii"))

    def fake_run(cmd, check):
        calls.append([str(x) for x in cmd])
        assert check is True

    monkeypatch.setattr(extract_data, "find_files", fake_find_files)
    monkeypatch.setattr(extract_data, "extract_file", fake_extract_file)
    monkeypatch.setattr(extract_data.subprocess, "run", fake_run)

    extract_data.main(["--iso", str(iso), "--out-dir", str(out_dir)])

    assert (out_dir / "_iso" / "PlCo.dat").exists()
    assert calls
    assert "--melee-decomp" not in calls[0]


def test_build_data_uses_packaged_source_artifacts_without_decomp(tmp_path, monkeypatch):
    iso_dir = tmp_path / "iso"
    out_dir = tmp_path / "data"
    missing_decomp = tmp_path / "missing_decomp"
    iso_dir.mkdir()
    for name in ("PlCo.dat", "ItCo.dat", "PlFx.dat", "PlFc.dat", "GrNLa.dat"):
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
            "fox,falco",
            "--melee-decomp",
            str(missing_decomp),
        ]
    )

    assert (out_dir / "staling/move_id/fox.bin").exists()
    assert (out_dir / "attack_id/move_id/falco.bin").exists()
    assert (out_dir / "motion_state/owners/callback_symbols.json").exists()
    manifest = out_dir / "manifest.json"
    assert manifest.exists()
    assert '"motion_state_owners": 15' in manifest.read_text(encoding="utf-8")
    assert not any(cmd[0] == "tools.extraction.extract_staling_move_id" for cmd in commands)
    assert not any(cmd[0] == "tools.extraction.extract_attack_id_move_id" for cmd in commands)
    assert not any(cmd[0] == "tools.extraction.extract_motion_state_owners" for cmd in commands)
    script_cmds = [cmd for cmd in commands if cmd[0] == "tools.extraction.extract_fighter_script_timeline"]
    assert script_cmds
    assert all("--character" in cmd for cmd in script_cmds)
