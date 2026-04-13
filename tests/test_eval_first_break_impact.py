from __future__ import annotations

from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.first_break_impact import (
    FirstBreakImpactRow,
    _score_compare_row,
    parse_first_break_impact_tsv,
    summarize_impact_clusters,
    write_first_break_impact_tsv,
)


def _blank_compare_row() -> np.void:
    rows = np.zeros(1, dtype=COMPARE_DTYPE)
    rows["num_players"][0] = 2
    return rows[0]


def _impact_row(
    *,
    dataset: str = "datasets/suite/a.msl",
    record: int = 10,
    field: str = "action_id",
    subindex: int = -1,
    seed: int = 1,
    out: int = 9,
    ref: int = 2,
    rollout_total_score: float = 500.0,
    reseed_total_score: float = 400.0,
    reseed_pos_l1_sum: float = 3.0,
) -> FirstBreakImpactRow:
    return FirstBreakImpactRow(
        dataset=dataset,
        record=record,
        seed_frame=100 + record,
        ref_frame=101 + record,
        player=0,
        field=field,
        subindex=subindex,
        seed=seed,
        out=out,
        ref=ref,
        streak_start_record=max(0, record - 3),
        streak_len=3,
        cluster_key=f"v1|field={field}|subindex={subindex}|seed={seed}|out={out}|ref={ref}",
        frames_scored=20,
        rollout_total_score=rollout_total_score,
        rollout_state_score=200.0,
        rollout_combat_score=150.0,
        rollout_position_score=100.0,
        rollout_item_score=50.0,
        rollout_action_mismatch_count=2,
        rollout_on_ground_mismatch_count=1,
        rollout_percent_mismatch_count=3,
        rollout_item_discrete_mismatch_count=1,
        rollout_major_state_frames=2,
        rollout_percent_abs_sum=9.0,
        rollout_shield_abs_sum=0.5,
        rollout_pos_l1_sum=6.0,
        rollout_max_pos_l1=1.25,
        rollout_item_pos_l1_sum=2.0,
        reseed_total_score=reseed_total_score,
        reseed_state_score=180.0,
        reseed_combat_score=120.0,
        reseed_position_score=80.0,
        reseed_item_score=20.0,
        reseed_action_mismatch_count=1,
        reseed_on_ground_mismatch_count=1,
        reseed_percent_mismatch_count=2,
        reseed_item_discrete_mismatch_count=0,
        reseed_major_state_frames=1,
        reseed_percent_abs_sum=5.0,
        reseed_shield_abs_sum=0.0,
        reseed_pos_l1_sum=reseed_pos_l1_sum,
        reseed_max_pos_l1=0.75,
        reseed_item_pos_l1_sum=0.5,
    )


def test_first_break_impact_scores_state_combat_position_and_items() -> None:
    out_row = _blank_compare_row()
    ref_row = _blank_compare_row()

    out_row["action_id"][0] = 90
    ref_row["action_id"][0] = 88
    out_row["on_ground"][0] = 1
    ref_row["on_ground"][0] = 0
    out_row["percent"][0] = 28.0
    ref_row["percent"][0] = 15.0
    out_row["pos_x"][0] = 4.0
    ref_row["pos_x"][0] = 1.5
    out_row["pos_y"][0] = 2.0
    ref_row["pos_y"][0] = 0.5
    out_row["items"][0]["exists"] = 1
    ref_row["items"][0]["exists"] = 0

    metrics = _score_compare_row(out_row=out_row, ref_row=ref_row, players=(0, 1))

    assert metrics.frames_scored == 1
    assert metrics.state_score > 0.0
    assert metrics.combat_score > 0.0
    assert metrics.position_score > 0.0
    assert metrics.item_score > 0.0
    assert metrics.total_score == (
        metrics.state_score + metrics.combat_score + metrics.position_score + metrics.item_score
    )
    assert metrics.action_mismatch_count == 1
    assert metrics.on_ground_mismatch_count == 1
    assert metrics.percent_mismatch_count == 1
    assert metrics.item_discrete_mismatch_count == 1
    assert metrics.major_state_frames == 1
    assert metrics.percent_abs_sum == 13.0
    assert metrics.max_pos_l1 == 4.0


def test_first_break_impact_tsv_roundtrip(tmp_path: Path) -> None:
    path = tmp_path / "impact.tsv"
    row = _impact_row()

    write_first_break_impact_tsv(path, [row])
    parsed = parse_first_break_impact_tsv(path)

    assert parsed == [row]


def test_first_break_impact_summary_ranks_by_reseed_sum_then_frequency() -> None:
    rows = [
        _impact_row(record=10, reseed_total_score=900.0, rollout_total_score=1100.0),
        _impact_row(record=11, reseed_total_score=800.0, rollout_total_score=1000.0),
        _impact_row(
            record=20,
            field="hitlag",
            seed=0,
            out=2,
            ref=0,
            rollout_total_score=1200.0,
            reseed_total_score=300.0,
        ),
    ]

    summaries = summarize_impact_clusters(rows)

    assert summaries[0].cluster_key == rows[0].cluster_key
    assert summaries[0].frequency == 2
    assert summaries[0].reseed_total_sum == 1700.0
    assert summaries[0].rollout_total_sum == 2100.0
    assert summaries[1].cluster_key == rows[2].cluster_key
