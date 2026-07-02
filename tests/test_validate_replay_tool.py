from __future__ import annotations

import sys
import json
from dataclasses import dataclass
from types import SimpleNamespace
from pathlib import Path

import pytest

from tools.eval import validate_replay
from tools.eval import run_one_step_suite_eval
from tools.eval.one_step_report import EvalSummary


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
    sentinel_buffers = object()

    def fake_build_validation_buffers_from_slp(**kwargs):
        calls["build"] = kwargs
        return sentinel_buffers

    def fake_evaluate_validation_buffers(**kwargs):
        calls["eval"] = kwargs
        kwargs["reporter"].print("one-step ok")
        return None

    monkeypatch.setattr(validate_replay, "build_validation_buffers_from_slp", fake_build_validation_buffers_from_slp)
    monkeypatch.setattr(validate_replay, "evaluate_validation_buffers", fake_evaluate_validation_buffers)
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
    assert eval_call["buffers"] is sentinel_buffers
    assert eval_call["dataset_path"] == replay.resolve()
    assert sorted(p.name for p in tmp_path.iterdir()) == ["game.slp"]


def test_one_step_suite_uses_replay_identity_for_reports_and_io(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"not a real slp; suite eval is monkeypatched")
    suite_path = tmp_path / "suite.json"
    suite_path.write_text(
        json.dumps(
            {
                "name": "direct_replay_test",
                "ucf_enabled": True,
                "ucf_cardinals_1_0_enabled": False,
                "replays": [
                    {
                        "replay": str(replay),
                        "ports": [1, 2],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    calls: dict[str, object] = {}
    sentinel_buffers = SimpleNamespace(num_records=3, num_players=2)

    def fake_build_validation_buffers_from_slp(**kwargs):
        calls["build"] = kwargs
        return sentinel_buffers

    def fake_evaluate_validation_buffers(**kwargs):
        calls["eval"] = kwargs
        kwargs["reporter"].print("suite one-step ok")
        return EvalSummary(
            total_records=0,
            total_player_frames=0,
            total_state_flags=0,
            total_item_slots=0,
            mismatches={},
            strict_mismatches={},
            ignored_mismatches={},
            profile_name="rl1_gameplay",
            float_norm_sum=0.0,
            float_norm_count=0,
        )

    monkeypatch.setattr(
        run_one_step_suite_eval,
        "build_validation_buffers_from_slp",
        fake_build_validation_buffers_from_slp,
        raising=False,
    )
    monkeypatch.setattr(run_one_step_suite_eval, "evaluate_validation_buffers", fake_evaluate_validation_buffers)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "run_one_step_suite_eval",
            "--suite",
            str(suite_path),
            "--workers",
            "1",
            "--quiet",
        ],
    )

    run_one_step_suite_eval.main()

    assert calls["build"]["slp_path"] == str(replay.resolve())
    assert not str(calls["build"]["slp_path"]).endswith(".slpz")
    eval_call = calls["eval"]
    assert eval_call["buffers"] is sentinel_buffers
    assert Path(eval_call["dataset_path"]) == replay.resolve()
    assert not replay.with_suffix(".slpz").exists()


def test_validate_replay_legacy_slp_path_resolves_to_slpz(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    replay = tmp_path / "game.slp"
    compressed_replay = tmp_path / "game.slpz"
    compressed_replay.write_bytes(b"not a real slpz; build is monkeypatched")
    calls: dict[str, object] = {}

    def fake_build_validation_buffers_from_slp(**kwargs):
        calls["build"] = kwargs
        return object()

    def fake_evaluate_validation_buffers(**kwargs):
        kwargs["reporter"].print("legacy path ok")
        return None

    monkeypatch.setattr(validate_replay, "build_validation_buffers_from_slp", fake_build_validation_buffers_from_slp)
    monkeypatch.setattr(validate_replay, "evaluate_validation_buffers", fake_evaluate_validation_buffers)
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "validate_replay",
            "--replay",
            str(replay),
            "--mode",
            "one-step",
        ],
    )

    validate_replay.main()

    assert capsys.readouterr().out.strip() == "legacy path ok"
    assert calls["build"]["slp_path"] == str(compressed_replay.resolve())


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
        ],
    )

    validate_replay.main()

    assert calls["rollout"]["replay"] == replay.resolve()
    assert calls["rollout"]["ports"] == [1, 2]
    assert sorted(p.name for p in tmp_path.iterdir()) == ["game.slp"]
