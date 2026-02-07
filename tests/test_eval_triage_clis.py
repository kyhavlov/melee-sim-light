from __future__ import annotations

from pathlib import Path

from tools.eval.diff_locate import diff_locate_rows
from tools.eval.locate_tsv import parse_locate_tsv
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
