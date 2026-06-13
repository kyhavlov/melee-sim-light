from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

import pytest

from tools.dolphin import slp_scenario_probe
from tools.dolphin.dolphin_engine_dump import CaptureResult
from tools.dolphin.patch_slp_preframe_window import (
    EVENT_PAYLOADS,
    GAME_START,
    PRE_FRAME,
    PRE_FRAME_OFFSETS,
    _f32,
    _i32,
    _load_ubjson_module,
)


def _tiny_slp_raw() -> bytearray:
    event_payloads = bytes(
        [
            EVENT_PAYLOADS,
            7,
            GAME_START,
            0,
            2,
            PRE_FRAME,
            0,
            0x43,
        ]
    )
    game_start = bytes([GAME_START, 0xAA, 0xBB])
    pre = bytearray(0x44)
    pre[0] = PRE_FRAME
    struct.pack_into(">i", pre, PRE_FRAME_OFFSETS["frame"], 12)
    pre[PRE_FRAME_OFFSETS["player"]] = 0
    struct.pack_into(">H", pre, PRE_FRAME_OFFSETS["action"], 20)
    struct.pack_into(">f", pre, PRE_FRAME_OFFSETS["x"], 1.0)
    struct.pack_into(">f", pre, PRE_FRAME_OFFSETS["y"], 2.0)
    struct.pack_into(">f", pre, PRE_FRAME_OFFSETS["joystickX"], 0.0)
    return bytearray(event_payloads + game_start + pre)


def _write_tiny_slp(path: Path) -> None:
    ubjson = _load_ubjson_module()
    with path.open("wb") as f:
        ubjson.dump({"raw": bytes(_tiny_slp_raw())}, f)


def test_slp_scenario_probe_runs_baseline_and_patched_windows(tmp_path: Path, monkeypatch) -> None:
    source = tmp_path / "source.slp"
    source.write_bytes(b"fake slp")
    scenario = tmp_path / "scenario.json"
    scenario.write_text(
        json.dumps(
            {
                "source_replay": str(source),
                "patches": [
                    {
                        "frame": 12,
                        "player": 0,
                        "input": {"joystickX": -1.0},
                    }
                ],
                "windows": [
                    {
                        "name": "after_patch",
                        "start_frame": 20,
                        "end_frame": 22,
                        "ports": [1, 4],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    out_dir = tmp_path / "out"
    captures: list[tuple[str, int, int]] = []

    def fake_write_patched_replay(source_slp: Path, _patch_spec: Path, out_slp: Path) -> list[int]:
        assert source_slp.read_bytes() == b"fake slp"
        out_slp.write_bytes(b"patched slp")
        return [1]

    def fake_capture_engine_dump(**kwargs):
        replay = Path(kwargs["replay"])
        captures.append((replay.name, int(kwargs["start_frame"]), int(kwargs["end_frame"])))
        out_bin = Path(kwargs["out_bin"])
        out_bin.parent.mkdir(parents=True, exist_ok=True)
        out_bin.write_bytes(b"dump")
        return CaptureResult(
            returncode=0,
            out_bin=out_bin,
            stdout_log=out_bin.with_suffix(".stdout.log"),
            stderr_log=out_bin.with_suffix(".stderr.log"),
            elapsed_sec=0.25,
            frame_count=3,
            first_frame=20,
            last_frame=22,
            error=None,
        )

    def fake_extract_to_dir(**kwargs):
        assert kwargs["ports"] == [1, 4]
        dst = Path(kwargs["out_dir"])
        dst.mkdir(parents=True, exist_ok=True)
        json_path = dst / "engine_dump_rows.json"
        txt_path = dst / "engine_dump_rows.txt"
        json_path.write_text(json.dumps({"row_count": 6}) + "\n", encoding="utf-8")
        txt_path.write_text("", encoding="utf-8")
        return json_path, txt_path

    monkeypatch.setattr(slp_scenario_probe, "_write_patched_replay", fake_write_patched_replay)
    monkeypatch.setattr(slp_scenario_probe, "capture_engine_dump", fake_capture_engine_dump)
    monkeypatch.setattr(slp_scenario_probe, "extract_to_dir", fake_extract_to_dir)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "slp_scenario_probe",
            "--scenario",
            str(scenario),
            "--dolphin",
            str(tmp_path / "dolphin"),
            "--iso",
            str(tmp_path / "SSBM.iso"),
            "--out-dir",
            str(out_dir),
            "--baseline",
        ],
    )

    assert slp_scenario_probe.main() == 0
    assert captures == [("source.slp", 20, 22), ("scenario.slp", 20, 22)]
    summary = json.loads((out_dir / "scenario_summary.json").read_text())
    assert summary["patch_match_counts"] == [1]
    assert json.loads((out_dir / "scenario_input.json").read_text())["source_replay"] == str(source)
    assert summary["windows"]["baseline"][0]["row_count"] == 6
    assert summary["windows"]["scenario"][0]["row_count"] == 6


def test_slp_scenario_probe_rejects_unsafe_window_names(tmp_path: Path) -> None:
    base = {
        "source_replay": str(tmp_path / "source.slp"),
        "patches": [{"frame": 12, "player": 0, "input": {"a": True}}],
    }
    for name in ["../escape", "/abs", "nested/name", r"nested\\name", ".", ".."]:
        spec = dict(base)
        spec["windows"] = [{"name": name, "start_frame": 20, "end_frame": 21}]
        path = tmp_path / f"scenario_{len(name)}.json"
        path.write_text(json.dumps(spec), encoding="utf-8")
        with pytest.raises(ValueError, match="unsafe name"):
            slp_scenario_probe._read_scenario(path)


def test_write_patched_replay_applies_real_preframe_patch(tmp_path: Path) -> None:
    source = tmp_path / "source.slp"
    patched = tmp_path / "patched.slp"
    patch_spec = tmp_path / "patch.json"
    _write_tiny_slp(source)
    patch_spec.write_text(
        json.dumps(
            {
                "patches": [
                    {
                        "frame": 12,
                        "player": 0,
                        "input": {"joystickX": -1.0, "a": True},
                        "pre_state": {"x": 7.5, "action": 14},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    assert slp_scenario_probe._write_patched_replay(source, patch_spec, patched) == [1]

    ubjson = _load_ubjson_module()
    with patched.open("rb") as f:
        raw = bytearray(ubjson.load(f)["raw"])
    pre_offset = 8 + 3
    assert raw[pre_offset] == PRE_FRAME
    assert _i32(raw, pre_offset + PRE_FRAME_OFFSETS["frame"]) == 12
    assert struct.unpack_from(">H", raw, pre_offset + PRE_FRAME_OFFSETS["action"])[0] == 14
    assert _f32(raw, pre_offset + PRE_FRAME_OFFSETS["x"]) == pytest.approx(7.5)
    assert _f32(raw, pre_offset + PRE_FRAME_OFFSETS["joystickX"]) == pytest.approx(-1.0)
    processed = struct.unpack_from(">I", raw, pre_offset + PRE_FRAME_OFFSETS["processed_buttons"])[0]
    physical = struct.unpack_from(">H", raw, pre_offset + PRE_FRAME_OFFSETS["physical_buttons"])[0]
    assert processed & 0x0100
    assert physical & 0x0100
