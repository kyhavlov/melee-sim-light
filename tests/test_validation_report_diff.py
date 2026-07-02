from __future__ import annotations

from pathlib import Path

import pytest

import tools.eval.validation_report_diff as validation_report_diff
from tools.eval.validation_report_diff import (
    classify_reds,
    diff_report_sets,
    discover_report_specs,
    main,
    read_report_set,
)


def _write_reports(root: Path, *, one_step: str, rollout: str, include_doubles: bool = True) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
    (root / "aggregate_recent_one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
    (root / "rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")
    (root / "aggregate_recent_rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")
    if include_doubles:
        (root / "doubles_recent_one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
        (root / "doubles_recent_rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")


def _one_step(
    *,
    total: int,
    strict: int,
    p95: str,
    replay_total: int | None = None,
    ignored: int | None = None,
) -> str:
    replay = total if replay_total is None else replay_total
    ignored_line = (
        f"overall.ignored_discrete_mismatch: {ignored}\n" if ignored is not None else ""
    )
    return f"""
suite: synthetic

== replays/suite/Foo.slpz ==
overall.discrete_mismatch: {replay} / 100
overall.strict_discrete_mismatch: {strict} / 100
{ignored_line}overall.float_norm_mae_p95: {p95}

== suite summary ==
overall.discrete_mismatch: {total} / 100
overall.strict_discrete_mismatch: {strict} / 100
{ignored_line}overall.float_norm_mae_p95: {p95}
"""


def _rollout(
    *,
    streak_count: int,
    first: int,
    seeded: int,
    median: int,
    p90: int,
    replay_streak_count: int | None = None,
    best_len: int = 1000,
    approved_exception_total: int = 0,
) -> str:
    replay_streak = streak_count if replay_streak_count is None else replay_streak_count
    approved = (
        f"rollout.approved_exception_total: {approved_exception_total}\n"
        if approved_exception_total
        else ""
    )
    return f"""
suite: synthetic

== replays/suite/Foo.slpz ==
rollout.best_len: {best_len}
rollout.streak_count: {replay_streak}
rollout.streak_len.median: {median}
rollout.streak_len.p90: {p90}
rollout.streak_len.p95: {p90}
rollout.streak_len.max: {best_len}
rollout.first_mismatch_total: {first}
rollout.first_mismatch_seeded_total: {seeded}
{approved}

== suite summary ==
overall.rollout.streak_count: {streak_count}
overall.rollout.streak_len.median: {median}
overall.rollout.streak_len.p90: {p90}
overall.rollout.streak_len.p95: {p90}
overall.rollout.streak_len.max: {best_len}
overall.rollout.best_len.max: {best_len}
overall.rollout.first_mismatch_total: {first}
overall.rollout.first_mismatch_seeded_total: {seeded}
"""


def test_validation_report_diff_detects_replay_regression_despite_suite_improvement(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=8, strict=10, p95="0.10"),
        rollout=_rollout(
            streak_count=25,
            first=25,
            seeded=7,
            median=120,
            p90=220,
            replay_streak_count=31,
        ),
    )

    deltas = diff_report_sets(read_report_set(str(before), before=True), read_report_set(str(after)))

    assert any(
        d.report == "primary rollout"
        and d.section.endswith("Foo.slpz")
        and d.metric == "rollout.streak_count"
        and d.is_regression
        for d in deltas
    )
    assert not any(
        d.section == "suite" and d.metric == "overall.rollout.streak_count" and d.is_regression
        for d in deltas
    )


def test_validation_report_diff_classifies_hard_reds(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=11, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert any(d.metric == "overall.discrete_mismatch" for d in classification.hard)
    assert not classification.distribution_only


def test_validation_report_diff_ignored_lane_only_strict_movement_is_not_hard(
    tmp_path: Path,
) -> None:
    # A strict regression fully explained by the profile-ignored diagnostic lane
    # (state_flags[4]&0x80 camera visibility) with scored lanes non-regressing must be
    # classified diagnostic-only, not a hard replay-level regression (TBK/PPA class
    # from re-widening the camera gates to the source's char-blind shape).
    before = tmp_path / "before"
    after = tmp_path / "after"
    rollout = _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200)
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=29, p95="0.20", ignored=19),
        rollout=rollout,
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=30, p95="0.20", ignored=20),
        rollout=rollout,
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert not classification.hard
    assert not classification.unclassified
    moved = {d.metric for d in classification.ignored_lane_only}
    assert moved == {"overall.strict_discrete_mismatch"}

    # --fail-on-regression must not fire for diagnostic-only movement.
    main(["--before", str(before), "--after", str(after), "--fail-on-regression"])


def test_validation_report_diff_ignored_lane_rows_print_as_diagnostic_everywhere(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # Output lock: diagnostic ignored-lane-only rows must never carry the REGRESSION
    # marker - not in the summary listings and not in the classification section
    # (named "movements", not "reds").
    before = tmp_path / "before"
    after = tmp_path / "after"
    rollout = _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200)
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=29, p95="0.20", ignored=19),
        rollout=rollout,
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=30, p95="0.20", ignored=20),
        rollout=rollout,
    )

    main(["--before", str(before), "--after", str(after)])
    out = capsys.readouterr().out

    assert "REGRESSION" not in out
    assert "ignored-lane-only movements (diagnostic, outside the scored profile):" in out
    strict_rows = [line for line in out.splitlines() if "strict_discrete_mismatch" in line and "->" in line]
    assert strict_rows and all(line.rstrip().endswith("diagnostic") for line in strict_rows)


def test_validation_report_diff_scored_strict_movement_stays_hard(tmp_path: Path) -> None:
    # Control: strict movement carried by SCORED lanes (ignored unchanged) stays hard.
    before = tmp_path / "before"
    after = tmp_path / "after"
    rollout = _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200)
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=29, p95="0.20", ignored=19),
        rollout=rollout,
    )
    _write_reports(
        after,
        one_step=_one_step(total=11, strict=30, p95="0.20", ignored=19),
        rollout=rollout,
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert any(d.metric == "overall.strict_discrete_mismatch" for d in classification.hard)
    assert not classification.ignored_lane_only


def test_validation_report_diff_undecomposed_strict_movement_stays_hard(tmp_path: Path) -> None:
    # Control: strict +2 with ignored +1 and scored flat does NOT decompose - the extra
    # row is a real (unattributed) strict regression and must stay hard.
    before = tmp_path / "before"
    after = tmp_path / "after"
    rollout = _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200)
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=29, p95="0.20", ignored=19),
        rollout=rollout,
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=31, p95="0.20", ignored=20),
        rollout=rollout,
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert any(d.metric == "overall.strict_discrete_mismatch" for d in classification.hard)
    assert not classification.ignored_lane_only


def test_validation_report_diff_treats_improved_first_counts_as_clean_streak_reshuffle(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=29, first=29, seeded=7, median=90, p90=220),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert not classification.hard
    assert not classification.distribution_only
    assert not classification.unclassified


def test_validation_report_diff_prints_clean_streak_reshuffle_as_non_regression(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=29, first=29, seeded=7, median=90, p90=220),
    )

    main(["--before", str(before), "--after", str(after), "--fail-on-regression"])
    out = capsys.readouterr().out
    assert "replay-level regressions:\n- none" in out
    assert "replay-level non-regression movements:" in out
    assert "distribution-only" in out


def test_validation_report_diff_treats_improved_first_counts_best_len_drop_as_clean_reshuffle(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=700),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(
            streak_count=28,
            first=28,
            seeded=8,
            median=120,
            p90=600,
            best_len=800,
            approved_exception_total=1,
        ),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert not classification.hard
    assert not classification.distribution_only
    assert not classification.unclassified


def test_validation_report_diff_keeps_best_len_red_hard_when_first_counts_regress(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=700),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=31, first=31, seeded=8, median=120, p90=600, best_len=800),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert any(d.metric == "rollout.best_len" for d in classification.hard)
    assert any(d.metric == "overall.rollout.best_len.max" for d in classification.hard)


def test_validation_report_diff_treats_suite_improved_first_counts_as_clean_streak_reshuffle(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=29, first=29, seeded=7, median=90, p90=220),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert not classification.hard
    assert not classification.distribution_only
    assert not classification.unclassified


def test_validation_report_diff_keeps_suite_distribution_red_unclassified_when_hard_context_regresses(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=31, first=31, seeded=8, median=90, p90=220),
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after))
    classification = classify_reds(
        before_reports, after_reports, diff_report_sets(before_reports, after_reports)
    )

    assert any(
        d.section == "suite" and d.metric == "overall.rollout.streak_len.median"
        for d in classification.unclassified
    )
    assert not any(
        d.section == "suite" and d.metric == "overall.rollout.streak_len.median"
        for d in classification.distribution_only
    )
    assert any(d.metric == "overall.rollout.first_mismatch_total" for d in classification.hard)
    assert any(d.metric == "overall.rollout.streak_count" for d in classification.hard)


def test_validation_report_diff_fail_on_regression_exits_nonzero(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=11, strict=13, p95="0.25"),
        rollout=_rollout(streak_count=31, first=31, seeded=9, median=90, p90=180),
    )

    with pytest.raises(SystemExit) as exc:
        main(["--before", str(before), "--after", str(after), "--fail-on-regression"])
    assert exc.value.code == 1


def test_validation_report_diff_prints_no_replay_regressions_for_suite_only_regression(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step="""
suite: synthetic

== replays/suite/Foo.slpz ==
overall.discrete_mismatch: 10 / 100
overall.strict_discrete_mismatch: 12 / 100
overall.float_norm_mae_p95: 0.20

== suite summary ==
overall.discrete_mismatch: 11 / 100
overall.strict_discrete_mismatch: 13 / 100
overall.float_norm_mae_p95: 0.25
""",
        rollout="""
suite: synthetic

== replays/suite/Foo.slpz ==
rollout.best_len: 1000
rollout.streak_count: 30
rollout.streak_len.median: 100
rollout.streak_len.p90: 200
rollout.streak_len.p95: 200
rollout.streak_len.max: 1000
rollout.first_mismatch_total: 30
rollout.first_mismatch_seeded_total: 8

== suite summary ==
overall.rollout.streak_count: 31
overall.rollout.streak_len.median: 90
overall.rollout.streak_len.p90: 180
overall.rollout.streak_len.p95: 180
overall.rollout.streak_len.max: 1000
overall.rollout.best_len.max: 1000
overall.rollout.first_mismatch_total: 31
overall.rollout.first_mismatch_seeded_total: 9
""",
    )

    with pytest.raises(SystemExit) as exc:
        main(["--before", str(before), "--after", str(after), "--fail-on-regression"])
    assert exc.value.code == 1
    out = capsys.readouterr().out
    assert "replay-level regressions:\n- none" in out


def test_validation_report_diff_tolerates_missing_new_optional_reports(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
        include_doubles=False,
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )

    deltas = diff_report_sets(read_report_set(str(before), before=True), read_report_set(str(after)))
    assert deltas == []


def test_validation_report_diff_rejects_missing_required_before_report(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    (before / "rollout_suite_eval.txt").unlink()

    with pytest.raises(FileNotFoundError, match="required validation report missing"):
        read_report_set(str(before), before=True)


def test_validation_report_diff_rejects_missing_after_doubles_report(tmp_path: Path) -> None:
    after = tmp_path / "after"
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    (after / "doubles_recent_rollout_suite_eval.txt").unlink()

    with pytest.raises(FileNotFoundError, match="required validation report missing"):
        read_report_set(str(after), before=False)


def test_validation_report_diff_discovers_character_and_heldout_reports(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    for root, total in ((before, 10), (after, 9)):
        (root / "heldout").mkdir(exist_ok=True)
        (root / "marth_one_step.txt").write_text(
            _one_step(total=total, strict=12, p95="0.20"),
            encoding="utf-8",
        )
        (root / "heldout" / "sheik_heldout_rollout.txt").write_text(
            _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
            encoding="utf-8",
        )

    specs = discover_report_specs(str(after))
    rel_paths = {spec.rel_path for spec in specs}
    assert "marth_one_step.txt" in rel_paths
    assert "heldout/sheik_heldout_rollout.txt" in rel_paths

    before_reports = read_report_set(str(before), before=True, report_specs=specs)
    after_reports = read_report_set(str(after), before=False, report_specs=specs)
    deltas = diff_report_sets(before_reports, after_reports)

    assert any(
        d.report == "marth_one_step one-step"
        and d.section == "suite"
        and d.metric == "overall.discrete_mismatch"
        for d in deltas
    )


def test_validation_report_diff_reads_new_after_reports_as_optional_before(
    tmp_path: Path,
) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
        include_doubles=False,
    )
    _write_reports(
        after,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
        include_doubles=False,
    )
    (after / "heldout").mkdir(exist_ok=True)
    (after / "marth_one_step.txt").write_text(
        _one_step(total=9, strict=12, p95="0.20"),
        encoding="utf-8",
    )
    (after / "heldout" / "sheik_heldout_rollout.txt").write_text(
        _rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
        encoding="utf-8",
    )

    specs = discover_report_specs(str(before), str(after))
    spec_by_path = {spec.rel_path: spec for spec in specs}
    assert spec_by_path["marth_one_step.txt"].optional_before
    assert spec_by_path["heldout/sheik_heldout_rollout.txt"].optional_before

    before_reports = read_report_set(str(before), before=True, report_specs=specs)
    after_reports = read_report_set(str(after), before=False, report_specs=specs)

    assert "marth_one_step one-step" not in before_reports
    assert "marth_one_step one-step" in after_reports
    assert "heldout / sheik_heldout_rollout rollout" in after_reports


def test_validation_report_diff_discovers_reports_from_git_refs(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def fake_check_output(cmd: list[str], **_: object) -> str:
        assert cmd[:4] == ["git", "ls-tree", "-r", "--name-only"]
        ref = cmd[4]
        if ref == "BASE":
            return "\n".join(
                (
                    "reports/validation/one_step_suite_eval.txt",
                    "reports/validation/marth_one_step.txt",
                    "reports/validation/heldout/sheik_heldout_rollout.txt",
                    "reports/validation/heldout/summary.txt",
                )
            )
        if ref == "HEAD":
            return "\n".join(
                (
                    "reports/validation/sheik_rollout.txt",
                    "reports/validation/heldout/marth_heldout_one_step.txt",
                )
            )
        raise AssertionError(f"unexpected ref {ref}")

    monkeypatch.setattr(validation_report_diff.subprocess, "check_output", fake_check_output)

    specs = discover_report_specs("BASE", "HEAD")
    rel_paths = {spec.rel_path for spec in specs}

    assert rel_paths == {
        "heldout/marth_heldout_one_step.txt",
        "heldout/sheik_heldout_rollout.txt",
        "marth_one_step.txt",
        "one_step_suite_eval.txt",
        "sheik_rollout.txt",
    }


def test_validation_report_diff_normalizes_legacy_msl_section_labels(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=8, strict=10, p95="0.10", replay_total=8),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    text = (before / "one_step_suite_eval.txt").read_text(encoding="utf-8")
    (before / "one_step_suite_eval.txt").write_text(
        text.replace("== replays/suite/Foo.slpz ==", "== datasets/foo/replays/suite/Foo.msl =="),
        encoding="utf-8",
    )

    before_reports = read_report_set(str(before), before=True)
    after_reports = read_report_set(str(after), before=False)
    deltas = diff_report_sets(before_reports, after_reports)

    assert any(
        d.report == "primary one-step"
        and d.section == "replays/suite/Foo.slpz"
        and d.metric == "overall.discrete_mismatch"
        and not d.is_regression
        for d in deltas
    )


def test_validation_report_diff_clean_improvement_does_not_exit(tmp_path: Path) -> None:
    before = tmp_path / "before"
    after = tmp_path / "after"
    _write_reports(
        before,
        one_step=_one_step(total=10, strict=12, p95="0.20"),
        rollout=_rollout(streak_count=30, first=30, seeded=8, median=100, p90=200),
    )
    _write_reports(
        after,
        one_step=_one_step(total=8, strict=10, p95="0.10"),
        rollout=_rollout(streak_count=25, first=25, seeded=7, median=120, p90=220),
    )

    main(["--before", str(before), "--after", str(after), "--fail-on-regression"])
