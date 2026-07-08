from __future__ import annotations

from types import SimpleNamespace

from melee_sim import extract_data
from tools.extraction import build_data
from tools.slippi.motion_state_owners import read_callback_manifest, read_mslmso01_v1


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
    for name in (
        "PlCo.dat",
        "ItCo.dat",
        "PlFx.dat",
        "PlFc.dat",
        "PlMs.dat",
        "PlCa.dat",
        "PlSk.dat",
        "PlZd.dat",
        "GrNLa.dat",
    ):
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
            "fox,falco,marth,falcon,sheik,zelda",
            "--melee-decomp",
            str(missing_decomp),
        ]
    )

    assert (out_dir / "staling/move_id/fox.bin").exists()
    assert (out_dir / "attack_id/move_id/falco.bin").exists()
    assert (out_dir / "staling/move_id/marth.bin").exists()
    assert (out_dir / "attack_id/move_id/marth.bin").exists()
    assert (out_dir / "motion_state/owners/marth.bin").exists()
    assert (out_dir / "staling/move_id/falcon.bin").exists()
    assert (out_dir / "attack_id/move_id/falcon.bin").exists()
    assert (out_dir / "motion_state/owners/falcon.bin").exists()
    assert (out_dir / "staling/move_id/sheik.bin").exists()
    assert (out_dir / "attack_id/move_id/sheik.bin").exists()
    assert (out_dir / "motion_state/owners/sheik.bin").exists()
    assert (out_dir / "staling/move_id/zelda.bin").exists()
    assert (out_dir / "attack_id/move_id/zelda.bin").exists()
    assert (out_dir / "motion_state/owners/zelda.bin").exists()
    assert (out_dir / "motion_state/owners/callback_symbols.json").exists()
    manifest = out_dir / "manifest.json"
    assert manifest.exists()
    from tools.extraction.extract_attack_id_move_id import FORMAT_VERSION as ATTACK_ID_MOVE_ID_VERSION
    from tools.extraction.extract_motion_state_owners import FORMAT_VERSION as MOTION_STATE_OWNER_VERSION

    assert f'"motion_state_owners": {MOTION_STATE_OWNER_VERSION}' in manifest.read_text(encoding="utf-8")
    # The manifest alone is not enough: the packaged no-decomp artifacts must THEMSELVES be
    # at the runtime version, or Python preflight passes and native init then rejects the
    # bins (the v16-packaged/v17-runtime skew class). Check the copied headers.
    import json as _json
    import struct as _struct

    for ch in ("fox", "falco", "marth", "falcon", "sheik", "zelda"):
        b = (out_dir / "staling" / "move_id" / f"{ch}.bin").read_bytes()
        assert b[:8] == b"MSLSTID1"
        assert _struct.unpack_from("<I", b, 8)[0] == 1, (
            f"packaged {ch}.bin is not MSLSTID1 v1 - regenerate "
            "tools/extraction/source_artifacts/staling/move_id/"
        )
        b = (out_dir / "attack_id" / "move_id" / f"{ch}.bin").read_bytes()
        assert b[:8] == b"MSLACID1"
        assert _struct.unpack_from("<I", b, 8)[0] == ATTACK_ID_MOVE_ID_VERSION, (
            f"packaged {ch}.bin is not MSLACID1 v{ATTACK_ID_MOVE_ID_VERSION} - regenerate "
            "tools/extraction/source_artifacts/attack_id/move_id/"
        )
        b = (out_dir / "motion_state" / "owners" / f"{ch}.bin").read_bytes()
        assert b[:8] == b"MSLMSO01"
        assert _struct.unpack_from("<I", b, 8)[0] == MOTION_STATE_OWNER_VERSION, (
            f"packaged {ch}.bin is not MSLMSO01 v{MOTION_STATE_OWNER_VERSION} - regenerate "
            "tools/extraction/source_artifacts/motion_state/owners/"
        )
    symbols = _json.loads(
        (out_dir / "motion_state" / "owners" / "callback_symbols.json").read_text(encoding="utf-8")
    )
    assert int(symbols["version"]) == MOTION_STATE_OWNER_VERSION
    callback_names = read_callback_manifest(out_dir / "motion_state" / "owners" / "callback_symbols.json")

    def cb_name(ch: str, action_id: int, lane: str) -> str:
        owners = read_mslmso01_v1(out_dir / "motion_state" / "owners" / f"{ch}.bin")
        cb_id = int(getattr(owners, f"{lane}_cb_id")[action_id])
        return callback_names[cb_id]

    # The callback manifest and every copied source-artifact bin share one generated namespace.
    # Header checks alone miss stale old-character bins decoded under a newly regenerated manifest
    # (for example old Fox/Falco/Marth id 975 becoming ftSk_* after adding Sheik). Spot-check
    # high-id character-special callbacks from each packaged char.
    assert cb_name("fox", 0x0163, "anim") == "ftFx_SpecialHi_Anim"
    assert cb_name("falco", 0x0163, "coll") == "ftFx_SpecialHi_Coll"
    assert cb_name("marth", 0x015D, "coll") == "ftMs_SpecialAirS1_Coll"
    assert cb_name("falcon", 0x0161, "coll") == "ftCa_SpecialHi_Coll"
    assert cb_name("sheik", 0x0168, "coll") == "ftSk_SpecialAirHi_Coll"
    assert not any(cmd[0] == "tools.extraction.extract_staling_move_id" for cmd in commands)
    assert not any(cmd[0] == "tools.extraction.extract_attack_id_move_id" for cmd in commands)
    assert not any(cmd[0] == "tools.extraction.extract_motion_state_owners" for cmd in commands)
    script_cmds = [cmd for cmd in commands if cmd[0] == "tools.extraction.extract_fighter_script_timeline"]
    assert script_cmds
    assert all("--character" in cmd for cmd in script_cmds)
