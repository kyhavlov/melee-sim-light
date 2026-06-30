from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

from tools.eval import run_combined_suite_eval, run_heldout_validation, run_one_step_suite_eval
from tools.eval import run_rollout_suite_eval, run_validate_all
from tools.eval import top_float_offenders, validate_replay
from tools.eval.run_one_step_eval import EvalSummary
from tools.slippi.suite_io import repo_root


REPLAY_A = "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
REPLAY_B = "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"


def _run_main(monkeypatch, name: str, main, args: list[str]) -> None:
    monkeypatch.setattr(sys, "argv", [name, *args])
    main()


def _write_suite(path: Path, *, name: str, replays: list[str]) -> None:
    path.write_text(
        json.dumps(
            {
                "name": name,
                "ucf_enabled": True,
                "ucf_cardinals_1_0_enabled": True,
                "replays": [
                    {
                        "replay": replay,
                        "ports": [1, 2],
                        "stage_id": 32,
                        "characters": {"1": "Falco", "2": "Fox"},
                    }
                    for replay in replays
                ],
            }
        )
    )


def _fake_result(task: dict) -> dict:
    label = repo_root().joinpath(Path(str(task["slp_path"]))).resolve()
    try:
        dataset = label.relative_to(repo_root()).as_posix()
    except ValueError:
        dataset = str(label)
    return {
        "one_step_lines": ["Records: 1", "Discrete mismatches:"],
        "one_step_summary": EvalSummary(
            total_records=1,
            total_player_frames=2,
            total_state_flags=10,
            total_item_slots=0,
            mismatches={"action_id": 0},
            strict_mismatches={"action_id": 0},
            ignored_mismatches={},
            profile_name=str(task["profile"]),
            float_norm_sum=0.0,
            float_norm_count=0,
        ),
        "rollout_payload": {
            "dataset": dataset,
            "num_records": 1,
            "max_records_used": 1,
            "players": [0, 1],
            "best_len": 1,
            "best_start_record": 0,
            "best_end_record_excl": 1,
            "best_start_seed_frame_id": 0,
            "best_end_ref_frame_id_inclusive": 1,
            "streak_histogram": {1: 1},
            "first_mismatch_field_counts": {},
            "first_mismatch_field_counts_seeded": {},
            "ignored_first_mismatch_field_counts": {},
            "ignored_first_mismatch_field_counts_seeded": {},
            "first_mismatch_rows": [],
            "float_rows": {},
        },
    }


def test_combined_output_matches_existing_suite_runners_on_small_suite(tmp_path: Path, monkeypatch) -> None:
    suite = tmp_path / "small_suite.json"
    _write_suite(suite, name="small_suite", replays=[REPLAY_A])
    old_one = tmp_path / "old_one.txt"
    old_roll = tmp_path / "old_roll.txt"
    combined_one = tmp_path / "combined_one.txt"
    combined_roll = tmp_path / "combined_roll.txt"

    _run_main(
        monkeypatch,
        "run_one_step_suite_eval",
        run_one_step_suite_eval.main,
        ["--suite", str(suite), "--out", str(old_one), "--workers", "1", "--quiet"],
    )
    _run_main(
        monkeypatch,
        "run_rollout_suite_eval",
        run_rollout_suite_eval.main,
        ["--suite", str(suite), "--out", str(old_roll), "--workers", "1", "--quiet"],
    )
    _run_main(
        monkeypatch,
        "run_combined_suite_eval",
        run_combined_suite_eval.main,
        [
            "--suite",
            str(suite),
            "--one-step-out",
            str(combined_one),
            "--rollout-out",
            str(combined_roll),
            "--workers",
            "1",
        ],
    )

    assert combined_one.read_text() == old_one.read_text()
    assert combined_roll.read_text() == old_roll.read_text()


def test_combined_rollout_float_rows_do_not_call_legacy_python_collector(
    tmp_path: Path, monkeypatch
) -> None:
    suite = tmp_path / "small_suite.json"
    _write_suite(suite, name="small_suite", replays=[REPLAY_A])

    assert not hasattr(top_float_offenders, "collect_dataset_top_rollout_float_offenders")
    assert not hasattr(run_rollout_suite_eval, "collect_dataset_top_rollout_float_offenders")
    assert not hasattr(validate_replay, "collect_dataset_top_rollout_float_offenders")
    _run_main(
        monkeypatch,
        "run_combined_suite_eval",
        run_combined_suite_eval.main,
        [
            "--suite",
            str(suite),
            "--one-step-out",
            str(tmp_path / "combined_one.txt"),
            "--rollout-out",
            str(tmp_path / "combined_roll.txt"),
            "--workers",
            "1",
        ],
    )


def test_validate_all_evaluates_duplicate_replay_once_and_emits_each_report(tmp_path: Path, monkeypatch) -> None:
    suite_a = tmp_path / "suite_a.json"
    suite_b = tmp_path / "suite_b.json"
    _write_suite(suite_a, name="suite_a", replays=[REPLAY_A])
    _write_suite(suite_b, name="suite_b", replays=[REPLAY_A])
    calls: list[str] = []

    def fake_task(task: dict) -> dict:
        calls.append(str(task["slp_path"]))
        return _fake_result(task)

    monkeypatch.setattr(run_combined_suite_eval, "_combined_dataset_task", fake_task)
    one_a = tmp_path / "one_a.txt"
    roll_a = tmp_path / "roll_a.txt"
    one_b = tmp_path / "one_b.txt"
    roll_b = tmp_path / "roll_b.txt"
    _run_main(
        monkeypatch,
        "run_validate_all",
        run_validate_all.main,
        [
            "--suite",
            str(suite_a),
            "--agg-suite",
            str(suite_b),
            "--one-step-out",
            str(one_a),
            "--rollout-out",
            str(roll_a),
            "--agg-one-step-out",
            str(one_b),
            "--agg-rollout-out",
            str(roll_b),
            "--workers",
            "1",
        ],
    )

    assert len(calls) == 1
    assert "suite: suite_a  replays: 1" in one_a.read_text()
    assert "suite: suite_b  replays: 1" in one_b.read_text()
    assert REPLAY_A in one_a.read_text()
    assert REPLAY_A in one_b.read_text()
    assert REPLAY_A in roll_a.read_text()
    assert REPLAY_A in roll_b.read_text()


def test_validate_all_keeps_one_step_suite_order_and_rollout_canonical_order(
    tmp_path: Path, monkeypatch
) -> None:
    suite = tmp_path / "ordered_suite.json"
    empty = tmp_path / "empty_suite.json"
    _write_suite(suite, name="ordered_suite", replays=[REPLAY_B, REPLAY_A])
    _write_suite(empty, name="empty_suite", replays=[])
    monkeypatch.setattr(run_combined_suite_eval, "_combined_dataset_task", _fake_result)
    one = tmp_path / "one.txt"
    roll = tmp_path / "roll.txt"

    _run_main(
        monkeypatch,
        "run_validate_all",
        run_validate_all.main,
        [
            "--suite",
            str(suite),
            "--agg-suite",
            str(empty),
            "--one-step-out",
            str(one),
            "--rollout-out",
            str(roll),
            "--agg-one-step-out",
            str(tmp_path / "empty_one.txt"),
            "--agg-rollout-out",
            str(tmp_path / "empty_roll.txt"),
            "--workers",
            "1",
        ],
    )

    one_text = one.read_text()
    roll_text = roll.read_text()
    assert one_text.index(REPLAY_B) < one_text.index(REPLAY_A)
    # Existing rollout summaries are canonicalized by dataset path, not emitted in suite order.
    assert roll_text.index(REPLAY_A) < roll_text.index(REPLAY_B)


def _write_fake_heldout_reports(args: list[str]) -> subprocess.CompletedProcess[str]:
    suite_path = Path(args[args.index("--suite") + 1])
    if not suite_path.is_absolute():
        suite_path = repo_root() / suite_path
    suite_name = json.loads(suite_path.read_text())["name"]
    one_step_out = Path(args[args.index("--one-step-out") + 1])
    rollout_out = Path(args[args.index("--rollout-out") + 1])
    one_step_out.write_text(
        "\n".join(
            [
                f"suite: {suite_name}",
                "Records: 10",
                "overall.discrete_mismatch: 0 / 100",
                "overall.strict_discrete_mismatch: 0 / 100",
                "",
            ]
        )
    )
    rollout_out.write_text(
        "\n".join(
            [
                f"suite: {suite_name}",
                "overall.rollout.first_mismatch_total: 0",
                "overall.rollout.first_mismatch_seeded_total: 0",
                "",
            ]
        )
    )
    if suite_name == "first_suite":
        time.sleep(0.05)
    return subprocess.CompletedProcess(args=args, returncode=0, stdout="", stderr="")


def test_heldout_summary_order_is_index_order_under_concurrency(tmp_path: Path, monkeypatch) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    _write_suite(first, name="first_suite", replays=[REPLAY_A])
    _write_suite(second, name="second_suite", replays=[REPLAY_B])
    index = tmp_path / "heldout.json"
    index.write_text(json.dumps({"suites": [str(first), str(second)]}))
    summary = tmp_path / "summary.txt"

    monkeypatch.setattr(run_heldout_validation, "_run_subprocess", _write_fake_heldout_reports)
    _run_main(
        monkeypatch,
        "run_heldout_validation",
        run_heldout_validation.main,
        [
            "--index",
            str(index),
            "--out-dir",
            str(tmp_path / "heldout_reports"),
            "--summary-out",
            str(summary),
            "--suite-workers",
            "2",
            "--workers",
            "1",
        ],
    )

    lines = [line for line in summary.read_text().splitlines() if line and not line.startswith("#")]
    assert lines[1].startswith("first_suite\t")
    assert lines[2].startswith("second_suite\t")
