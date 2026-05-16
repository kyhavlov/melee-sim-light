from __future__ import annotations

import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from tools.dolphin import forensic_row_dump
from tools.slippi.slpz import EVENT_PAYLOADS, GAME_START, RAW_HEADER, compress_slpz


def _fake_slp() -> bytes:
    event_payloads = bytes([EVENT_PAYLOADS, 4, GAME_START, 0, 2])
    game_start = bytes([GAME_START, 0xAA, 0xBB])
    raw = event_payloads + game_start
    return RAW_HEADER + len(raw).to_bytes(4, "big") + raw + b"{U\x08metadata"


def test_forensic_row_dump_infers_validation_slpz_from_dataset_path(tmp_path: Path) -> None:
    dataset = tmp_path / "datasets" / "suite" / "replays" / "validation" / "set" / "Game.msl"
    dataset.parent.mkdir(parents=True)
    dataset.write_bytes(b"dataset placeholder")
    replay = tmp_path / "replays" / "validation" / "set" / "Game.slpz"
    replay.parent.mkdir(parents=True)
    replay.write_bytes(compress_slpz(_fake_slp()))

    assert forensic_row_dump._infer_replay_from_dataset(tmp_path, dataset) == replay.resolve()


def test_forensic_row_dump_decompresses_slpz_for_capture(
    tmp_path: Path, monkeypatch
) -> None:
    dataset = tmp_path / "datasets" / "suite" / "replays" / "validation" / "set" / "Game.msl"
    dataset.parent.mkdir(parents=True)
    dataset.write_bytes(b"dataset placeholder")
    replay = tmp_path / "replays" / "validation" / "set" / "Game.slpz"
    replay.parent.mkdir(parents=True)
    replay.write_bytes(compress_slpz(_fake_slp()))

    samples = np.zeros(
        1,
        dtype=[
            ("seed_t", [("frame_id", np.int32)]),
            ("ref_t1", [("frame_id", np.int32)]),
        ],
    )
    samples["seed_t"]["frame_id"][0] = 100
    samples["ref_t1"]["frame_id"][0] = 101

    captured: dict[str, Path] = {}

    def fake_read_dataset(_path: str):
        return SimpleNamespace(samples=samples, header={"num_players": 2})

    def fake_capture_engine_dump(**kwargs):
        capture_replay = Path(kwargs["replay"])
        assert capture_replay.exists()
        assert capture_replay.suffix == ".slp"
        assert capture_replay.read_bytes() == _fake_slp()
        captured["replay"] = capture_replay
        return 0, Path(kwargs["out_bin"])

    def fake_extract_to_dir(**kwargs):
        out_dir = Path(kwargs["out_dir"])
        out_dir.mkdir(parents=True, exist_ok=True)
        json_path = out_dir / "rows.json"
        txt_path = out_dir / "rows.txt"
        json_path.write_text("{}\n", encoding="utf-8")
        txt_path.write_text("", encoding="utf-8")
        return json_path, txt_path

    monkeypatch.setattr(forensic_row_dump, "repo_root", lambda: tmp_path)
    monkeypatch.setattr(forensic_row_dump, "read_dataset", fake_read_dataset)
    monkeypatch.setattr(forensic_row_dump, "capture_engine_dump", fake_capture_engine_dump)
    monkeypatch.setattr(forensic_row_dump, "extract_to_dir", fake_extract_to_dir)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "forensic_row_dump",
            "--row",
            f"{dataset}:0:1",
            "--dolphin",
            str(tmp_path / "dolphin"),
            "--out-dir",
            str(tmp_path / "out"),
        ],
    )

    assert forensic_row_dump.main() == 0
    assert captured["replay"].suffix == ".slp"
    assert not captured["replay"].exists()
