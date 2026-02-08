from __future__ import annotations

from pathlib import Path

from tools.eval.diff_locate import diff_locate_rows
from tools.eval.locate_discrete_mismatches import ITEM_FIELD_TO_SUBFIELD
from tools.eval.locate_tsv import parse_locate_tsv
from tools.eval.rollout_metrics import diff_rollout_summaries, summarize_rollout_payload, validate_rollout_payload
from tools.eval.top_triples import count_ref_out, count_triples


def _write_locate_tsv(path: Path, rows: list[tuple[object, ...]]) -> None:
    header = "dataset\trecord\tseed_frame\tref_frame\tp\tfield\tseed\tout\tref\n"
    lines = [header]
    for row in rows:
        lines.append("\t".join(str(x) for x in row) + "\n")
    path.write_text("".join(lines), encoding="utf-8")


def test_locate_tsv_seed_out_ref_order_is_parsed_and_counted(tmp_path: Path) -> None:
    tsv = tmp_path / "locate_order.tsv"
    _write_locate_tsv(
        tsv,
        [
            ("a.msl", 10, 100, 101, 0, "action_id", 236, 999, 43),
            ("a.msl", 11, 101, 102, 0, "action_id", 236, 999, 43),
        ],
    )
    rows = parse_locate_tsv(tsv)
    assert rows[0].seed == 236
    assert rows[0].out == 999
    assert rows[0].ref == 43

    counts, _examples = count_triples(rows)
    assert counts[(236, 43, 999)] == 2


def test_diff_locate_new_and_gone_rows_on_synthetic_fixture(tmp_path: Path) -> None:
    before = tmp_path / "locate_before.tsv"
    after = tmp_path / "locate_after.tsv"
    _write_locate_tsv(
        before,
        [
            ("a.msl", 10, 100, 101, 0, "action_id", 1, 1, 2),
            ("a.msl", 11, 101, 102, 0, "action_id", 2, 2, 3),
        ],
    )
    _write_locate_tsv(
        after,
        [
            ("a.msl", 11, 101, 102, 0, "action_id", 2, 2, 3),
            ("a.msl", 12, 102, 103, 0, "action_id", 4, 7, 5),
        ],
    )
    before_rows = parse_locate_tsv(before)
    after_rows = parse_locate_tsv(after)
    report = diff_locate_rows(
        before_rows=before_rows,
        after_rows=after_rows,
        key_fields=("dataset", "record", "p", "field"),
    )

    assert report.before_count == 2
    assert report.after_count == 2
    assert report.unchanged == 1
    assert len(report.new_rows) == 1
    assert len(report.gone_rows) == 1

    assert report.new_rows[0].record == 12
    assert report.gone_rows[0].record == 10


def test_top_triples_reports_seed_ref_out_and_ref_out_from_seed_out_ref_tsv(tmp_path: Path) -> None:
    tsv = tmp_path / "locate_seed_out_ref.tsv"
    _write_locate_tsv(
        tsv,
        [
            ("d.msl", 1, 10, 11, 0, "action_id", 100, 900, 200),
            ("d.msl", 2, 11, 12, 0, "action_id", 100, 900, 200),
        ],
    )
    rows = parse_locate_tsv(tsv)

    triples, _tri_examples = count_triples(rows)
    ref_out, _ro_examples = count_ref_out(rows)

    assert triples[(100, 200, 900)] == 2
    assert ref_out[(200, 900)] == 2


def test_locate_discrete_item_aliases_map_to_compare_item_subfields() -> None:
    assert ITEM_FIELD_TO_SUBFIELD == {
        "item_exists": "exists",
        "item_type": "type",
        "item_state": "state",
        "item_owner": "owner",
        "item_instance_id": "instance_id",
    }


def test_rollout_metrics_summary_and_diff_on_synthetic_payload() -> None:
    before = {
        "suite": "s",
        "fields": ["action_id", "hitlag"],
        "per_dataset": [
            {
                "dataset": "datasets/x/a.msl",
                "best_len": 6,
                "streak_histogram": {"1": 2, "2": 2, "6": 1},
                "first_mismatch_field_counts": {"action_id": 5, "hitlag": 2},
                "first_mismatch_field_counts_seeded": {"action_id": 3, "hitlag": 1},
            },
            {
                "dataset": "datasets/x/b.msl",
                "best_len": 5,
                "streak_histogram": {"1": 3, "3": 1, "5": 1},
                "first_mismatch_field_counts": {"action_id": 4},
                "first_mismatch_field_counts_seeded": {"action_id": 2},
            },
        ],
    }
    after = {
        "suite": "s",
        "fields": ["action_id", "hitlag"],
        "per_dataset": [
            {
                "dataset": "datasets/x/a.msl",
                "best_len": 8,
                "streak_histogram": {"1": 1, "2": 2, "4": 1, "8": 1},
                "first_mismatch_field_counts": {"action_id": 4, "hitlag": 1},
                "first_mismatch_field_counts_seeded": {"action_id": 2, "hitlag": 1},
            },
            {
                "dataset": "datasets/x/b.msl",
                "best_len": 7,
                "streak_histogram": {"1": 1, "3": 2, "7": 1},
                "first_mismatch_field_counts": {"action_id": 2},
                "first_mismatch_field_counts_seeded": {"action_id": 1},
            },
        ],
    }

    s_before = summarize_rollout_payload(before)
    s_after = summarize_rollout_payload(after)

    assert s_before["suite_summary"]["dataset_count"] == 2
    assert s_before["suite_summary"]["max_best_len"] == 6
    assert s_before["suite_summary"]["first_mismatch_total"] == 11
    assert s_after["suite_summary"]["max_best_len"] == 8
    assert s_after["suite_summary"]["first_mismatch_total"] == 7

    diff = diff_rollout_summaries(before, after)
    assert diff["suite_delta"]["max_best_len"] == 2
    assert diff["suite_delta"]["first_mismatch_total"] == -4
    assert diff["suite_first_mismatch_field_delta"]["action_id"] == -3
    assert diff["suite_first_mismatch_field_delta"]["hitlag"] == -1
    assert len(diff["per_dataset_delta"]) == 2


def test_rollout_metrics_handles_empty_histograms_stably() -> None:
    payload = {
        "suite": "s",
        "fields": ["action_id"],
        "per_dataset": [
            {
                "dataset": "datasets/x/empty.msl",
                "best_len": 0,
                "streak_histogram": {},
                "first_mismatch_field_counts": {},
                "first_mismatch_field_counts_seeded": {},
            }
        ],
    }

    summary = summarize_rollout_payload(payload)
    row = summary["dataset_summaries"][0]
    suite = summary["suite_summary"]

    assert row["total_streaks"] == 0
    assert row["median_streak_len"] == 0
    assert row["p90_streak_len"] == 0
    assert row["p95_streak_len"] == 0
    assert row["max_streak_len"] == 0
    assert suite["total_streaks"] == 0
    assert suite["median_streak_len"] == 0
    assert suite["p90_streak_len"] == 0
    assert suite["p95_streak_len"] == 0
    assert suite["max_streak_len"] == 0


def test_rollout_metrics_validate_payload_requires_expected_keys() -> None:
    bad = {"suite": "s"}
    try:
        validate_rollout_payload(bad, label="bad")
    except ValueError as e:
        assert "missing keys" in str(e)
        return
    raise AssertionError("expected ValueError for missing rollout payload keys")
