from __future__ import annotations

from pathlib import Path

import pytest

from tools.eval.validation_report_diff import diff_report_sets, main, read_report_set


def _write_reports(root: Path, *, one_step: str, rollout: str, include_doubles: bool = True) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
    (root / "aggregate_recent_one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
    (root / "rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")
    (root / "aggregate_recent_rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")
    if include_doubles:
        (root / "doubles_recent_one_step_suite_eval.txt").write_text(one_step, encoding="utf-8")
        (root / "doubles_recent_rollout_suite_eval.txt").write_text(rollout, encoding="utf-8")


def _one_step(*, total: int, strict: int, p95: str, replay_total: int | None = None) -> str:
    replay = total if replay_total is None else replay_total
    return f"""
suite: synthetic

== datasets/suite/Foo.msl ==
overall.discrete_mismatch: {replay} / 100
overall.strict_discrete_mismatch: {strict} / 100
overall.float_norm_mae_p95: {p95}

== suite summary ==
overall.discrete_mismatch: {total} / 100
overall.strict_discrete_mismatch: {strict} / 100
overall.float_norm_mae_p95: {p95}
"""


def _rollout(
    *,
    streak_count: int,
    first: int,
    seeded: int,
    median: int,
    p90: int,
    replay_streak_count: int | None = None,
) -> str:
    replay_streak = streak_count if replay_streak_count is None else replay_streak_count
    return f"""
suite: synthetic

== datasets/suite/Foo.msl ==
rollout.best_len: 1000
rollout.streak_count: {replay_streak}
rollout.streak_len.median: {median}
rollout.streak_len.p90: {p90}
rollout.streak_len.p95: {p90}
rollout.streak_len.max: 1000
rollout.first_mismatch_total: {first}
rollout.first_mismatch_seeded_total: {seeded}
rollout.first_mismatch_non_seeded_total: {first - seeded}

== suite summary ==
overall.rollout.streak_count: {streak_count}
overall.rollout.streak_len.median: {median}
overall.rollout.streak_len.p90: {p90}
overall.rollout.streak_len.p95: {p90}
overall.rollout.streak_len.max: 1000
overall.rollout.best_len.max: 1000
overall.rollout.first_mismatch_total: {first}
overall.rollout.first_mismatch_seeded_total: {seeded}
overall.rollout.first_mismatch_non_seeded_total: {first - seeded}
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
        and d.section.endswith("Foo.msl")
        and d.metric == "rollout.streak_count"
        and d.is_regression
        for d in deltas
    )
    assert not any(
        d.section == "suite" and d.metric == "overall.rollout.streak_count" and d.is_regression
        for d in deltas
    )


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

== datasets/suite/Foo.msl ==
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

== datasets/suite/Foo.msl ==
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
