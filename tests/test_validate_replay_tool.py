from __future__ import annotations

import sys
from pathlib import Path

import pytest

from tools.eval import validate_replay


def test_validate_replay_parse_ports_defaults_and_sorts() -> None:
    assert validate_replay._parse_ports(None) is None
    assert validate_replay._parse_ports("") is None
    assert validate_replay._parse_ports("2,1,2") == [1, 2]


def test_validate_replay_one_step_uses_in_memory_replay_without_temp_files(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"not a real slp; build is monkeypatched")
    calls: dict[str, object] = {}
    sentinel_dataset = object()

    def fake_build_dataset_from_slp(**kwargs):
        calls["build"] = kwargs
        return sentinel_dataset

    def fake_evaluate_dataset(**kwargs):
        calls["eval"] = kwargs
        kwargs["reporter"].print("one-step ok")
        return None

    monkeypatch.setattr(validate_replay, "build_dataset_from_slp", fake_build_dataset_from_slp)
    monkeypatch.setattr(validate_replay, "evaluate_dataset", fake_evaluate_dataset)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "validate_replay",
            "--replay",
            str(replay),
            "--mode",
            "one-step",
            "--ports",
            "2,1",
        ],
    )

    validate_replay.main()

    assert capsys.readouterr().out.strip() == "one-step ok"
    assert calls["build"] == {
        "slp_path": str(replay.resolve()),
        "ports": [1, 2],
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": False,
    }
    eval_call = calls["eval"]
    assert eval_call["dataset"] is sentinel_dataset
    assert eval_call["dataset_path"] == replay.resolve().with_suffix(".msl")
    assert sorted(p.name for p in tmp_path.iterdir()) == ["game.slp"]


def test_validate_replay_rollout_dispatches_without_writing_reports(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"not a real slp; rollout is monkeypatched")
    calls: dict[str, object] = {}

    def fake_print_rollout(**kwargs):
        calls["rollout"] = kwargs

    monkeypatch.setattr(validate_replay, "_print_rollout", fake_print_rollout)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "validate_replay",
            "--replay",
            str(replay),
            "--mode",
            "rollout",
            "--ports",
            "1,2",
            "--fields",
            "action_id,on_ground",
        ],
    )

    validate_replay.main()

    assert calls["rollout"]["replay"] == replay.resolve()
    assert calls["rollout"]["ports"] == [1, 2]
    assert calls["rollout"]["fields"] == ("action_id", "on_ground")
    assert sorted(p.name for p in tmp_path.iterdir()) == ["game.slp"]
