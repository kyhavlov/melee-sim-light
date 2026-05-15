from __future__ import annotations

import json
from pathlib import Path

from tools.eval.run_rollout_suite_eval import _exception_probe_limit, _float_summary
from tools.eval.validation_exceptions import (
    classify_float_row,
    load_validation_exceptions,
    match_rollout_first_exceptions,
)


def test_rollout_first_exception_requires_exact_observed_shape(tmp_path: Path) -> None:
    path = tmp_path / "validation_exceptions.json"
    path.write_text(
        json.dumps(
            {
                "version": 1,
                "rollout_first_mismatch_exceptions": [
                    {
                        "dataset": "Game.msl",
                        "record": 12,
                        "player": 0,
                        "field": "action_id",
                        "subindex": -1,
                        "seed": 21,
                        "out": 91,
                        "ref": 88,
                        "seeded_break": False,
                        "category": "open_magnify_camera",
                        "reason": "reviewed display-only exception",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    exceptions = load_validation_exceptions(path)

    accepted, stale = match_rollout_first_exceptions(
        dataset="datasets/aggregate/Game.msl",
        rows=(
            {
                "dataset": "datasets/aggregate/Game.msl",
                "record": 12,
                "player": 0,
                "field": "action_id",
                "subindex": -1,
                "seed": 21,
                "out": 91,
                "ref": 88,
                "seeded_break": False,
            },
        ),
        exceptions=exceptions,
    )

    assert len(accepted) == 1
    assert stale == ()

    accepted, stale = match_rollout_first_exceptions(
        dataset="datasets/aggregate/Game.msl",
        rows=(
            {
                "dataset": "datasets/aggregate/Game.msl",
                "record": 12,
                "player": 0,
                "field": "action_id",
                "subindex": -1,
                "seed": 21,
                "out": 90,
                "ref": 88,
                "seeded_break": False,
            },
        ),
        exceptions=exceptions,
    )

    assert accepted == ()
    assert len(stale) == 1


def test_rollout_exception_probe_limit_is_zero_for_unlisted_datasets(tmp_path: Path) -> None:
    path = tmp_path / "validation_exceptions.json"
    path.write_text(
        json.dumps(
            {
                "version": 1,
                "rollout_first_mismatch_exceptions": [
                    {
                        "dataset": "Game.msl",
                        "record": 12,
                        "player": 0,
                        "field": "action_id",
                        "subindex": -1,
                        "seed": 21,
                        "out": 91,
                        "ref": 88,
                        "seeded_break": False,
                        "category": "open_magnify_camera",
                        "reason": "reviewed display-only exception",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    exceptions = load_validation_exceptions(path)

    assert _exception_probe_limit(dataset="datasets/aggregate/Game.msl", exceptions=exceptions) == 13
    assert _exception_probe_limit(dataset="datasets/aggregate/Other.msl", exceptions=exceptions) == 0


def test_rollout_float_annotation_is_exact_to_dataset_record_player_field(tmp_path: Path) -> None:
    path = tmp_path / "validation_exceptions.json"
    path.write_text(
        json.dumps(
            {
                "version": 1,
                "rollout_float_annotations": [
                    {
                        "dataset": "Game.msl",
                        "record": 2463,
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
    exceptions = load_validation_exceptions(path)

    assert classify_float_row(
        dataset="datasets/aggregate/Game.msl",
        record=2463,
        player=0,
        field="percent",
        exceptions=exceptions,
    ) == ("open_magnify", "known open owner")
    assert classify_float_row(
        dataset="datasets/aggregate/Game.msl",
        record=2464,
        player=0,
        field="percent",
        exceptions=exceptions,
    ) == ("unclassified", "")


def test_rollout_float_annotation_does_not_block_float_clean_status(tmp_path: Path) -> None:
    path = tmp_path / "validation_exceptions.json"
    path.write_text(
        json.dumps(
            {
                "version": 1,
                "rollout_float_annotations": [
                    {
                        "dataset": "Game.msl",
                        "record": 2463,
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
    exceptions = load_validation_exceptions(path)

    summary = _float_summary(
        dataset="datasets/aggregate/Game.msl",
        rows_by_field={
            "percent": [
                {
                    "field": "percent",
                    "abs_err": 1.0,
                    "dataset": "datasets/aggregate/Game.msl",
                    "record": 2463,
                    "p": 0,
                    "seed": 89.0,
                    "out": 89.0,
                    "ref": 90.0,
                    "seed_action_id": 38,
                    "out_action_id": 38,
                    "ref_action_id": 38,
                    "discrete_state_matches": True,
                }
            ],
            "pos_x": [
                {
                    "field": "pos_x",
                    "abs_err": 0.00001,
                    "dataset": "datasets/aggregate/Game.msl",
                    "record": 2703,
                    "p": 0,
                    "seed": 132.0,
                    "out": 133.0,
                    "ref": 133.0,
                    "seed_action_id": 88,
                    "out_action_id": 88,
                    "ref_action_id": 88,
                    "discrete_state_matches": True,
                }
            ],
        },
        exceptions=exceptions,
        top=3,
    )

    assert summary["status"] == "float-clean"
    assert summary["accepted_float_exception_total"] == 1


def test_unclassified_rollout_float_still_blocks_float_clean_status() -> None:
    summary = _float_summary(
        dataset="datasets/aggregate/Game.msl",
        rows_by_field={
            "percent": [
                {
                    "field": "percent",
                    "abs_err": 1.0,
                    "dataset": "datasets/aggregate/Game.msl",
                    "record": 2464,
                    "p": 0,
                    "seed": 90.0,
                    "out": 89.0,
                    "ref": 90.0,
                    "seed_action_id": 38,
                    "out_action_id": 38,
                    "ref_action_id": 38,
                    "discrete_state_matches": True,
                }
            ],
        },
        exceptions=load_validation_exceptions(Path("/definitely/missing/validation_exceptions.json")),
        top=3,
    )

    assert summary["status"] == "not-clean"
    assert summary["accepted_float_exception_total"] == 0
