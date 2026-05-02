from __future__ import annotations

import json
import sys
from pathlib import Path

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval
from tools.slippi.make_dataset_from_slp import write_dataset_from_slp
from tools.slippi.suite_io import dataset_path_for_suite_replay


def _write_tiny_suite(path: Path) -> dict:
    suite = {
        "name": "suite_eval_parity_smoke",
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True,
        "replays": [
            {
                "replay": "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp",
                "ports": [1, 2],
                "stage_id": 32,
                "characters": {"1": "Falco", "2": "Fox"},
            }
        ],
    }
    path.write_text(json.dumps(suite))
    return suite


def _run_main(monkeypatch, main, argv: list[str]) -> None:
    monkeypatch.setattr(sys, "argv", argv)
    main()


def test_suite_eval_cached_and_in_memory_outputs_match(tmp_path: Path, monkeypatch) -> None:
    suite_path = tmp_path / "suite.json"
    suite = _write_tiny_suite(suite_path)
    datasets_dir = tmp_path / "datasets"
    replay = suite["replays"][0]["replay"]
    cached_path = dataset_path_for_suite_replay(
        suite_name=suite["name"],
        replay_rel_path=replay,
        datasets_dir=datasets_dir,
    )
    cached_path.parent.mkdir(parents=True, exist_ok=True)
    write_dataset_from_slp(
        slp_path=replay,
        out_path=str(cached_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    one_step_in_memory = tmp_path / "one_step_in_memory.txt"
    one_step_cached = tmp_path / "one_step_cached.txt"
    common_one_step_args = [
        "run_one_step_suite_eval",
        "--suite",
        str(suite_path),
        "--datasets-dir",
        str(datasets_dir),
        "--chunk",
        "512",
        "--workers",
        "1",
        "--quiet",
    ]
    _run_main(
        monkeypatch,
        run_one_step_suite_eval.main,
        [*common_one_step_args, "--out", str(one_step_in_memory)],
    )
    _run_main(
        monkeypatch,
        run_one_step_suite_eval.main,
        [*common_one_step_args, "--cached-datasets", "--out", str(one_step_cached)],
    )
    assert one_step_in_memory.read_text() == one_step_cached.read_text()

    rollout_in_memory = tmp_path / "rollout_in_memory.txt"
    rollout_cached = tmp_path / "rollout_cached.txt"
    common_rollout_args = [
        "run_rollout_suite_eval",
        "--suite",
        str(suite_path),
        "--datasets-dir",
        str(datasets_dir),
        "--max-records",
        "512",
        "--workers",
        "1",
        "--quiet",
    ]
    _run_main(
        monkeypatch,
        run_rollout_suite_eval.main,
        [*common_rollout_args, "--out", str(rollout_in_memory)],
    )
    _run_main(
        monkeypatch,
        run_rollout_suite_eval.main,
        [*common_rollout_args, "--cached-datasets", "--out", str(rollout_cached)],
    )
    assert rollout_in_memory.read_text() == rollout_cached.read_text()
