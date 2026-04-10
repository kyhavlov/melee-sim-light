from __future__ import annotations

from pathlib import Path

from tools.eval.diff_locate import diff_locate_rows
from tools.eval.diff_rollout_locate import diff_rollout_locate_rows
from tools.eval.facing_residual_blocker_report import FacingResidualRow, build_summary
from tools.eval.locate_discrete_mismatches import ITEM_FIELD_TO_SUBFIELD
from tools.eval.locate_rollout_desyncs import FirstMismatch, _scan_rollout_desync_rows
from tools.eval.locate_tsv import parse_locate_tsv
from tools.eval.rollout_locate_tsv import (
    ROLLOUT_LOCATE_COLUMNS,
    RolloutLocateRow,
    cluster_key_for,
    parse_rollout_locate_tsv,
    summarize_clusters,
    write_rollout_locate_tsv,
)
from tools.eval.rollout_metrics import diff_rollout_summaries, summarize_rollout_payload, validate_rollout_payload
from tools.eval.top_triples import count_ref_out, count_triples


def _write_locate_tsv(path: Path, rows: list[tuple[object, ...]]) -> None:
    header = "dataset\trecord\tseed_frame\tref_frame\tp\tfield\tseed\tout\tref\n"
    lines = [header]
    for row in rows:
        lines.append("\t".join(str(x) for x in row) + "\n")
    path.write_text("".join(lines), encoding="utf-8")


def _rollout_row(
    *,
    dataset: str = "datasets/s/a.msl",
    record: int,
    field: str,
    subindex: int = -1,
    seed: int,
    out: int,
    ref: int,
    streak_len: int,
    seeded_break: bool = False,
) -> RolloutLocateRow:
    return RolloutLocateRow(
        dataset=dataset,
        record=record,
        seed_frame=record + 100,
        ref_frame=record + 101,
        player=0,
        field=field,
        subindex=subindex,
        seed=seed,
        out=out,
        ref=ref,
        streak_start_record=max(0, record - streak_len),
        streak_len=streak_len,
        seeded_break=seeded_break,
        cluster_key=cluster_key_for(field=field, subindex=subindex, seed=seed, out=out, ref=ref),
    )


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


def test_rollout_locate_cluster_key_is_stable_and_ignores_row_identity() -> None:
    a = cluster_key_for(field="action_id", subindex=-1, seed=14, out=20, ref=15)
    b = _rollout_row(record=20, field="action_id", seed=14, out=20, ref=15, streak_len=4).cluster_key
    c = _rollout_row(
        dataset="datasets/s/b.msl",
        record=99,
        field="action_id",
        seed=14,
        out=20,
        ref=15,
        streak_len=1,
    ).cluster_key

    assert a == "v1|field=action_id|subindex=-1|seed=14|out=20|ref=15"
    assert a == b == c


def test_rollout_locate_tsv_schema_stability_and_roundtrip(tmp_path: Path) -> None:
    path = tmp_path / "rollout.tsv"
    row = _rollout_row(
        record=12,
        field="state_flags",
        subindex=2,
        seed=0,
        out=16,
        ref=0,
        streak_len=7,
        seeded_break=True,
    )
    write_rollout_locate_tsv(path, [row])

    header = path.read_text(encoding="utf-8").splitlines()[0].split("\t")
    assert tuple(header) == ROLLOUT_LOCATE_COLUMNS

    parsed = parse_rollout_locate_tsv(path)
    assert parsed == [row]


def test_rollout_locate_summary_groups_by_cluster_and_ranks_impact() -> None:
    rows = [
        _rollout_row(record=10, field="action_id", seed=1, out=9, ref=2, streak_len=3),
        _rollout_row(record=20, field="action_id", seed=1, out=9, ref=2, streak_len=5),
        _rollout_row(record=30, field="hitlag", seed=0, out=2, ref=0, streak_len=1, seeded_break=True),
    ]

    summaries = summarize_clusters(rows)

    assert summaries[0].cluster_key == rows[0].cluster_key
    assert summaries[0].frequency == 2
    assert summaries[0].impact == 8
    assert summaries[0].seeded_breaks == 0
    assert summaries[1].seeded_breaks == 1


def test_diff_rollout_locate_new_gone_and_delta_on_synthetic_fixture() -> None:
    shared_before = _rollout_row(record=10, field="action_id", seed=1, out=9, ref=2, streak_len=3)
    shared_after_a = _rollout_row(record=20, field="action_id", seed=1, out=9, ref=2, streak_len=5)
    shared_after_b = _rollout_row(record=21, field="action_id", seed=1, out=9, ref=2, streak_len=1)
    gone = _rollout_row(record=30, field="hitlag", seed=0, out=2, ref=0, streak_len=4)
    new = _rollout_row(record=40, field="state_flags", subindex=3, seed=0, out=8, ref=0, streak_len=2)

    report = diff_rollout_locate_rows(
        before_rows=[shared_before, gone],
        after_rows=[shared_after_a, shared_after_b, new],
    )

    assert report.before_rows == 2
    assert report.after_rows == 3
    assert len(report.new_clusters) == 1
    assert report.new_clusters[0].cluster_key == new.cluster_key
    assert len(report.gone_clusters) == 1
    assert report.gone_clusters[0].cluster_key == gone.cluster_key
    assert len(report.changed_clusters) == 1
    assert report.changed_clusters[0].cluster_key == shared_before.cluster_key
    assert report.changed_clusters[0].frequency_delta == 1
    assert report.changed_clusters[0].impact_delta == 3


def test_rollout_locate_scan_fixture_emits_unseeded_and_seeded_break_rows() -> None:
    attempts: list[tuple[str, int, int | None]] = []

    def attempt_from_current(j: int, *, seed_record: int | None) -> FirstMismatch | None:
        attempts.append(("cur", j, seed_record))
        if j == 1:
            return FirstMismatch(player=0, field="action_id", subindex=-1, seed=10, out=20, ref=11)
        return None

    def attempt_seeded_at_record(j: int) -> FirstMismatch | None:
        attempts.append(("seeded", j, j))
        return FirstMismatch(player=0, field="action_id", subindex=-1, seed=10, out=30, ref=11)

    def row_from_mismatch(
        *,
        record: int,
        mismatch: FirstMismatch,
        streak_start_record: int,
        streak_len: int,
        seeded_break: bool,
    ) -> RolloutLocateRow:
        return _rollout_row(
            record=record,
            field=mismatch.field,
            subindex=mismatch.subindex,
            seed=mismatch.seed,
            out=mismatch.out,
            ref=mismatch.ref,
            streak_len=streak_len,
            seeded_break=seeded_break,
        )

    rows = _scan_rollout_desync_rows(
        n=3,
        attempt_from_current=attempt_from_current,
        attempt_seeded_at_record=attempt_seeded_at_record,
        row_from_mismatch=row_from_mismatch,
        limit=None,
    )

    assert attempts == [("cur", 0, 0), ("cur", 1, None), ("seeded", 1, 1), ("cur", 2, 2)]
    assert len(rows) == 2
    assert rows[0].record == 1
    assert rows[0].streak_len == 1
    assert not rows[0].seeded_break
    assert rows[1].record == 1
    assert rows[1].streak_len == 0
    assert rows[1].seeded_break


def test_facing_residual_blocker_report_groups_seed_visible_context() -> None:
    rows = [
        FacingResidualRow(
            dataset="d.msl",
            record=10,
            seed_frame=100,
            ref_frame=101,
            p=0,
            seed_action_id=0x005B,
            ref_action_id=0x0058,
            out_action_id=0x005B,
            prev_action_id=0x0019,
            on_ground=0,
            hitlag=0,
            hitstun=0,
            seed_facing=1,
            ref_facing=1,
            out_facing=0,
            x2228_b7=0,
            last_hit_by=0,
            combo_count=1,
        ),
        FacingResidualRow(
            dataset="d.msl",
            record=11,
            seed_frame=101,
            ref_frame=102,
            p=0,
            seed_action_id=0x005B,
            ref_action_id=0x0058,
            out_action_id=0x005B,
            prev_action_id=0x0019,
            on_ground=0,
            hitlag=0,
            hitstun=0,
            seed_facing=1,
            ref_facing=1,
            out_facing=0,
            x2228_b7=0,
            last_hit_by=0,
            combo_count=1,
        ),
    ]

    summary = build_summary(
        rows,
        action_names={0x005B: "DAMAGE_FLY_ROLL", 0x0058: "DAMAGE_FLY_N", 0x0019: "JUMP_F"},
        top_n=5,
    )

    assert summary["row_count"] == 2
    assert summary["top_ref_out"] == [{"ref_out": "1->0", "count": 2}]
    assert summary["top_clusters"][0]["seed_action_name"] == "DAMAGE_FLY_ROLL"
    assert summary["top_clusters"][0]["prev_action_name"] == "JUMP_F"
