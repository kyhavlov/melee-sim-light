from __future__ import annotations

import sys
import json
from dataclasses import dataclass
from types import SimpleNamespace
from pathlib import Path

import pytest

from tools.eval import validate_replay
from tools.eval import run_one_step_suite_eval
from tools.eval.run_one_step_eval import EvalSummary


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
    sentinel_dataset = SimpleNamespace(header={"num_records": 3, "num_players": 2})

    class FakeRuntime:
        def close(self) -> None:
            calls["closed"] = True

    def fake_build_dataset_from_slp(**kwargs):
        calls["build"] = kwargs
        return sentinel_dataset

    def fake_create_runtime(**kwargs):
        calls["runtime"] = kwargs
        return FakeRuntime()

    def fake_evaluate_dataset(**kwargs):
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

    import tools.slippi.make_dataset_from_slp as make_dataset_from_slp

    monkeypatch.setattr(make_dataset_from_slp, "build_dataset_from_slp", fake_build_dataset_from_slp)
    monkeypatch.setattr(run_one_step_suite_eval, "create_one_step_eval_runtime", fake_create_runtime)
    monkeypatch.setattr(run_one_step_suite_eval, "evaluate_dataset", fake_evaluate_dataset)
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
    assert not str(calls["build"]["slp_path"]).endswith(".msl")
    eval_call = calls["eval"]
    assert eval_call["dataset"] is sentinel_dataset
    assert Path(eval_call["dataset_path"]) == replay.resolve()
    assert not replay.with_suffix(".msl").exists()


def test_validate_replay_legacy_slp_path_resolves_to_slpz(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    replay = tmp_path / "game.slp"
    compressed_replay = tmp_path / "game.slpz"
    compressed_replay.write_bytes(b"not a real slpz; build is monkeypatched")
    calls: dict[str, object] = {}

    def fake_build_dataset_from_slp(**kwargs):
        calls["build"] = kwargs
        return object()

    def fake_evaluate_dataset(**kwargs):
        kwargs["reporter"].print("legacy path ok")
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
            "--fields",
            "action_id,on_ground",
        ],
    )

    validate_replay.main()

    assert calls["rollout"]["replay"] == replay.resolve()
    assert calls["rollout"]["ports"] == [1, 2]
    assert calls["rollout"]["fields"] == ("action_id", "on_ground")
    assert sorted(p.name for p in tmp_path.iterdir()) == ["game.slp"]


def test_validate_replay_rollout_prints_report_overlay_fields(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"not a real slp; rollout is monkeypatched")
    exceptions = tmp_path / "validation_exceptions.json"
    exceptions.write_text(
        json.dumps(
            {
                "version": 1,
                "rollout_first_mismatch_exceptions": [
                    {
                        "dataset": str(replay.resolve()),
                        "record": 12,
                        "player": 0,
                        "field": "action_id",
                        "subindex": -1,
                        "seed": 21,
                        "out": 91,
                        "ref": 88,
                        "seeded_break": False,
                        "category": "open_magnify_camera",
                        "reason": "reviewed package boundary",
                    }
                ],
                "rollout_float_annotations": [
                    {
                        "dataset": str(replay.resolve()),
                        "record": 20,
                        "player": 0,
                        "field": "percent",
                        "category": "open_magnify",
                        "reason": "known open owner",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )

    @dataclass(frozen=True)
    class LocateRow:
        dataset: str
        record: int
        player: int
        field: str
        subindex: int
        seed: int
        out: int
        ref: int
        seeded_break: bool

    fake_dataset = SimpleNamespace(header={"num_players": 2})
    fake_streaks = SimpleNamespace(
        dataset=replay.resolve(),
        num_records=30,
        max_records_used=30,
        players=(0, 1),
        best_len=12,
        best_start_record=0,
        best_end_record_excl=12,
        best_start_seed_frame_id=-123,
        best_end_ref_frame_id_inclusive=-112,
        streak_histogram={12: 1},
        first_mismatch_field_counts={"action_id": 1},
        first_mismatch_field_counts_seeded={},
        ignored_first_mismatch_field_counts={},
        ignored_first_mismatch_field_counts_seeded={},
    )

    def fake_build_dataset_from_slp(**_kwargs):
        return fake_dataset

    def fake_scan_dataset_streaks_with_native_float_rows(**kwargs):
        return fake_streaks, {
            "percent": [
                {
                    "field": "percent",
                    "abs_err": 1.0,
                    "dataset": str(kwargs["float_dataset_label"]),
                    "record": 20,
                    "p": 0,
                    "seed_frame": -100,
                    "ref_frame": -99,
                    "seed": 89.0,
                    "out": 89.0,
                    "ref": 90.0,
                    "seed_action_id": 38,
                    "out_action_id": 38,
                    "ref_action_id": 38,
                    "seed_action_frame": 1,
                    "out_action_frame": 2,
                    "ref_action_frame": 2,
                    "attempt": "free_run",
                    "seeded_retry": False,
                    "discrete_state_matches": True,
                    "streak_start_record": 0,
                    "streak_len": 12,
                }
            ]
        }

    def fake_locate_dataset_rollout_desyncs(**kwargs):
        return [
            LocateRow(
                dataset=str(kwargs["dataset_label"]),
                record=12,
                player=0,
                field="action_id",
                subindex=-1,
                seed=21,
                out=91,
                ref=88,
                seeded_break=False,
            )
        ]

    monkeypatch.setattr(validate_replay, "build_dataset_from_slp", fake_build_dataset_from_slp)
    monkeypatch.setattr(
        validate_replay,
        "_scan_dataset_streaks_with_native_float_rows",
        fake_scan_dataset_streaks_with_native_float_rows,
    )
    monkeypatch.setattr(
        validate_replay,
        "_locate_dataset_rollout_desyncs",
        fake_locate_dataset_rollout_desyncs,
    )
    monkeypatch.setattr(validate_replay, "_float_compare_fields", lambda: ("percent",))

    validate_replay._print_rollout(
        replay=replay.resolve(),
        ports=None,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
        fields=("action_id", "on_ground"),
        players_csv=None,
        max_records=0,
        profile="rl1_gameplay",
        exceptions_path=exceptions,
        float_top=3,
    )

    out = capsys.readouterr().out
    assert "rollout.status: ACCEPTED-CLEAN" in out
    assert "rollout.approved_exception_total: 1" in out
    assert "rollout.approved_exceptions: rec=12 p=0 seeded=0 action_id[-1]" in out
    assert "rollout.float_status: FLOAT-CLEAN" in out
    assert "rollout.approved_float_exception_total: 1" in out
    assert "rollout.float_top_errors:" in out
    assert "percent max_err=1 rec=20 p=0" in out
