from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval import top_float_offenders
from tools.eval.dataset import HEADER_DTYPE, MAGIC, SAMPLE_DTYPE, Dataset


def test_rollout_python_collector_production_symbol_is_removed() -> None:
    assert not hasattr(top_float_offenders, "collect_dataset_top_rollout_float_offenders")


def test_rollout_suite_float_offenders_use_native_standard_scan(
    monkeypatch, tmp_path: Path
) -> None:
    suite_path = tmp_path / "suite.json"
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"fake slp")
    suite_path.write_text(
        json.dumps(
            {
                "name": "tiny",
                "ucf_enabled": True,
                "ucf_cardinals_1_0_enabled": False,
                "replays": [{"replay": str(replay), "ports": [1, 2]}],
            }
        ),
        encoding="utf-8",
    )
    samples = np.zeros(1, dtype=SAMPLE_DTYPE)
    header = np.zeros((), dtype=HEADER_DTYPE)
    header["magic"] = MAGIC
    header["record_size"] = SAMPLE_DTYPE.itemsize
    header["num_records"] = 1
    header["num_players"] = 2
    dataset = Dataset(header=header, samples=samples)
    calls: dict[str, object] = {}

    def fake_build_dataset_from_slp(**kwargs):
        calls["build"] = kwargs
        return dataset

    def fake_scan(**kwargs):
        calls["scan"] = kwargs
        return None, {
            "pos_y": [
                {
                    "field": "pos_y",
                    "abs_err": 0.5,
                    "dataset": kwargs["float_dataset_label"],
                    "record": 7,
                    "p": 1,
                    "seed_frame": 6,
                    "ref_frame": 8,
                    "seed": 0.0,
                    "out": 0.5,
                    "ref": 0.0,
                    "seed_action_id": 20,
                    "out_action_id": 20,
                    "ref_action_id": 20,
                    "seed_action_frame": 1,
                    "out_action_frame": 2,
                    "ref_action_frame": 2,
                    "attempt": "free_run",
                    "seeded_retry": False,
                    "discrete_state_matches": True,
                    "streak_start_record": 0,
                    "streak_len": 7,
                }
            ]
        }

    monkeypatch.setattr(top_float_offenders, "build_dataset_from_slp", fake_build_dataset_from_slp)
    monkeypatch.setattr(top_float_offenders, "_scan_dataset_streaks_with_native_float_rows", fake_scan)

    payload = top_float_offenders.collect_suite_top_float_offenders(
        suite=suite_path,
        fields=("pos_y",),
        chunk=16,
        top=3,
        mode="rollout",
    )

    assert payload["mode"] == "rollout"
    assert calls["build"]["slp_path"] == str(replay.resolve())
    assert calls["scan"]["ds"] is dataset
    assert calls["scan"]["fields"] == top_float_offenders.DEFAULT_DISCRETE_FIELDS
    assert payload["top_rows"]["pos_y"][0]["record"] == 7
