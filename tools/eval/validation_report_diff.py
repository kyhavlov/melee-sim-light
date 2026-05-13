from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class ReportSpec:
    label: str
    rel_path: str
    optional_before: bool = False


REPORT_FILES: tuple[ReportSpec, ...] = (
    ReportSpec("primary one-step", "one_step_suite_eval.txt"),
    ReportSpec("primary rollout", "rollout_suite_eval.txt"),
    ReportSpec("aggregate one-step", "aggregate_recent_one_step_suite_eval.txt"),
    ReportSpec("aggregate rollout", "aggregate_recent_rollout_suite_eval.txt"),
    ReportSpec("doubles one-step", "doubles_recent_one_step_suite_eval.txt", optional_before=True),
    ReportSpec("doubles rollout", "doubles_recent_rollout_suite_eval.txt", optional_before=True),
)

ONE_STEP_METRICS: dict[str, str] = {
    "overall.discrete_mismatch": "lower",
    "overall.strict_discrete_mismatch": "lower",
    "overall.float_norm_mae_p95": "lower",
}

ROLLOUT_METRICS: dict[str, str] = {
    "rollout.streak_count": "lower",
    "rollout.streak_len.median": "higher",
    "rollout.streak_len.p90": "higher",
    "rollout.streak_len.p95": "higher",
    "rollout.streak_len.max": "higher",
    "rollout.best_len": "higher",
    "rollout.first_mismatch_total": "lower",
    "rollout.first_mismatch_seeded_total": "lower",
    "rollout.first_mismatch_non_seeded_total": "lower",
    "overall.rollout.streak_count": "lower",
    "overall.rollout.streak_len.median": "higher",
    "overall.rollout.streak_len.p90": "higher",
    "overall.rollout.streak_len.p95": "higher",
    "overall.rollout.streak_len.max": "higher",
    "overall.rollout.best_len.max": "higher",
    "overall.rollout.first_mismatch_total": "lower",
    "overall.rollout.first_mismatch_seeded_total": "lower",
    "overall.rollout.first_mismatch_non_seeded_total": "lower",
}


@dataclass(frozen=True)
class MetricValue:
    raw: str
    value: float


@dataclass(frozen=True)
class MetricDelta:
    report: str
    section: str
    metric: str
    before: MetricValue
    after: MetricValue
    direction: str

    @property
    def delta(self) -> float:
        return self.after.value - self.before.value

    @property
    def is_regression(self) -> bool:
        if self.direction == "lower":
            return self.delta > 0
        if self.direction == "higher":
            return self.delta < 0
        raise ValueError(f"unknown metric direction: {self.direction}")


@dataclass(frozen=True)
class RedClassification:
    hard: tuple[MetricDelta, ...]
    distribution_only: tuple[MetricDelta, ...]
    derived_only: tuple[MetricDelta, ...]
    unclassified: tuple[MetricDelta, ...]


_HEADER_RE = re.compile(r"^== (?P<section>.+) ==$")
_METRIC_RE = re.compile(r"^(?P<key>[A-Za-z0-9_.]+): (?P<value>.+)$")
_NUMBER_RE = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _parse_metric_value(raw: str) -> MetricValue:
    match = _NUMBER_RE.search(raw)
    if match is None:
        raise ValueError(f"metric value has no number: {raw!r}")
    return MetricValue(raw=raw.strip(), value=float(match.group(0)))


def _read_report(source: str, rel_path: str, *, required: bool) -> str | None:
    path = Path(source)
    if path.is_dir():
        report_path = path / rel_path
        if not report_path.exists():
            if required:
                raise FileNotFoundError(f"required validation report missing: {report_path}")
            return None
        return report_path.read_text(encoding="utf-8")
    if path.is_file():
        return path.read_text(encoding="utf-8")
    try:
        return subprocess.check_output(
            ["git", "show", f"{source}:reports/validation/{rel_path}"],
            cwd=_repo_root(),
            text=True,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        if required:
            detail = (exc.stderr or "").strip()
            raise FileNotFoundError(
                f"required validation report missing from {source}: reports/validation/{rel_path}"
                + (f" ({detail})" if detail else "")
            ) from exc
        return None


def _metrics_for_report(label: str) -> dict[str, str]:
    return ONE_STEP_METRICS if "one-step" in label else ROLLOUT_METRICS


def parse_report(text: str, *, label: str) -> dict[str, dict[str, MetricValue]]:
    wanted = _metrics_for_report(label)
    sections: dict[str, dict[str, MetricValue]] = {}
    section = "preamble"
    for line in text.splitlines():
        header = _HEADER_RE.match(line)
        if header is not None:
            section = header.group("section")
            if section == "suite summary":
                section = "suite"
            continue
        metric = _METRIC_RE.match(line)
        if metric is None:
            continue
        key = metric.group("key")
        if key not in wanted:
            continue
        sections.setdefault(section, {})[key] = _parse_metric_value(metric.group("value"))
    return sections


def read_report_set(source: str, *, before: bool = False) -> dict[str, dict[str, dict[str, MetricValue]]]:
    reports: dict[str, dict[str, dict[str, MetricValue]]] = {}
    for spec in REPORT_FILES:
        text = _read_report(source, spec.rel_path, required=not (before and spec.optional_before))
        if text is None:
            continue
        reports[spec.label] = parse_report(text, label=spec.label)
    return reports


def diff_report_sets(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
) -> list[MetricDelta]:
    deltas: list[MetricDelta] = []
    for report, before_sections in before.items():
        after_sections = after.get(report, {})
        directions = _metrics_for_report(report)
        for section, before_metrics in before_sections.items():
            after_metrics = after_sections.get(section, {})
            for metric, before_value in before_metrics.items():
                after_value = after_metrics.get(metric)
                if after_value is None:
                    continue
                if before_value.value == after_value.value:
                    continue
                deltas.append(
                    MetricDelta(
                        report=report,
                        section=section,
                        metric=metric,
                        before=before_value,
                        after=after_value,
                        direction=directions[metric],
                    )
                )
    return deltas


def _fmt_delta(delta: float) -> str:
    if abs(delta - round(delta)) < 1e-9:
        i = int(round(delta))
        return f"+{i}" if i > 0 else str(i)
    return f"{delta:+.8f}"


def _short_section(section: str) -> str:
    if section == "suite":
        return "suite"
    return Path(section).name


def _sort_key(delta: MetricDelta) -> tuple[int, float, str, str, str]:
    return (
        0 if delta.is_regression else 1,
        -abs(delta.delta),
        delta.report,
        _short_section(delta.section),
        delta.metric,
    )


def _print_delta(delta: MetricDelta) -> None:
    marker = "REGRESSION" if delta.is_regression else "ok"
    print(
        f"- {delta.report} / {_short_section(delta.section)} / {delta.metric}: "
        f"{delta.before.raw} -> {delta.after.raw} ({_fmt_delta(delta.delta)}) {marker}"
    )


def _metric_value(
    reports: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
    metric: str,
) -> MetricValue | None:
    return reports.get(delta.report, {}).get(delta.section, {}).get(metric)


def _metric_non_regressing(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
    metric: str,
) -> bool | None:
    before_value = _metric_value(before, delta, metric)
    after_value = _metric_value(after, delta, metric)
    if before_value is None or after_value is None:
        return None
    direction = _metrics_for_report(delta.report).get(metric)
    if direction == "lower":
        return after_value.value <= before_value.value
    if direction == "higher":
        return after_value.value >= before_value.value
    return None


def _is_hard_red(delta: MetricDelta) -> bool:
    if "one-step" in delta.report:
        return delta.metric in {
            "overall.discrete_mismatch",
            "overall.strict_discrete_mismatch",
            "overall.float_norm_mae_p95",
        }
    if "rollout" not in delta.report:
        return False
    return delta.metric in {
        "rollout.best_len",
        "rollout.streak_len.max",
        "rollout.first_mismatch_total",
        "rollout.first_mismatch_seeded_total",
        "rollout.streak_count",
        "overall.rollout.best_len.max",
        "overall.rollout.streak_len.max",
        "overall.rollout.first_mismatch_total",
        "overall.rollout.first_mismatch_seeded_total",
        "overall.rollout.streak_count",
    }


def _distribution_hard_metrics(delta: MetricDelta) -> tuple[str, ...]:
    if delta.section == "suite":
        return ()
    return (
        "rollout.best_len",
        "rollout.streak_len.max",
        "rollout.first_mismatch_total",
        "rollout.first_mismatch_seeded_total",
        "rollout.streak_count",
    )


def classify_reds(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    deltas: Iterable[MetricDelta],
) -> RedClassification:
    hard: list[MetricDelta] = []
    distribution_only: list[MetricDelta] = []
    derived_only: list[MetricDelta] = []
    unclassified: list[MetricDelta] = []

    for delta in deltas:
        if not delta.is_regression:
            continue
        if _is_hard_red(delta):
            hard.append(delta)
            continue
        if (
            "rollout" in delta.report
            and delta.metric
            in {"rollout.streak_len.median", "rollout.streak_len.p90", "rollout.streak_len.p95"}
            and delta.section != "suite"
        ):
            statuses = [
                _metric_non_regressing(before, after, delta, metric)
                for metric in _distribution_hard_metrics(delta)
            ]
            if statuses and all(status is True for status in statuses):
                distribution_only.append(delta)
            else:
                unclassified.append(delta)
            continue
        if (
            "rollout" in delta.report
            and delta.metric
            in {
                "rollout.first_mismatch_non_seeded_total",
                "overall.rollout.first_mismatch_non_seeded_total",
            }
        ):
            if delta.section == "suite":
                total_metric = "overall.rollout.first_mismatch_total"
                streak_metric = "overall.rollout.streak_count"
            else:
                total_metric = "rollout.first_mismatch_total"
                streak_metric = "rollout.streak_count"
            total_ok = _metric_non_regressing(before, after, delta, total_metric)
            streak_ok = _metric_non_regressing(before, after, delta, streak_metric)
            if total_ok is True and streak_ok is True:
                derived_only.append(delta)
            else:
                unclassified.append(delta)
            continue
        unclassified.append(delta)

    return RedClassification(
        hard=tuple(hard),
        distribution_only=tuple(distribution_only),
        derived_only=tuple(derived_only),
        unclassified=tuple(unclassified),
    )


def _print_classification_group(name: str, rows: tuple[MetricDelta, ...], *, top: int) -> None:
    print(f"{name}:")
    if not rows:
        print("- none")
        return
    shown = rows[: max(1, top)]
    for delta in shown:
        _print_delta(delta)
    extra = len(rows) - len(shown)
    if extra > 0:
        print(f"- ... {extra} more")


def print_red_classification(classification: RedClassification, *, top: int) -> None:
    print("red classification:")
    _print_classification_group("hard reds", classification.hard, top=top)
    _print_classification_group(
        "distribution-only reds", classification.distribution_only, top=top
    )
    _print_classification_group("derived-only reds", classification.derived_only, top=top)
    _print_classification_group(
        "unclassified regressions", classification.unclassified, top=top
    )


def print_summary(deltas: Iterable[MetricDelta], *, top: int) -> int:
    rows = sorted(deltas, key=_sort_key)
    regressions = [d for d in rows if d.is_regression]
    suite_rows = [d for d in rows if d.section == "suite"]

    print("suite totals:")
    if suite_rows:
        for delta in suite_rows:
            _print_delta(delta)
    else:
        print("- no suite total changes")

    print("replay-level regressions:")
    replay_regressions = [d for d in regressions if d.section != "suite"]
    if replay_regressions:
        shown = replay_regressions[: max(1, top)]
        for delta in shown:
            _print_delta(delta)
        extra = len(replay_regressions) - len(shown)
        if extra > 0:
            print(f"- ... {extra} more replay-level regressions")
    else:
        print("- none")

    print("largest replay-level movements:")
    replay_rows = [d for d in rows if d.section != "suite"]
    if replay_rows:
        for delta in replay_rows[: max(1, top)]:
            _print_delta(delta)
    else:
        print("- none")
    return len(regressions)


def print_report(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    *,
    top: int,
) -> int:
    deltas = diff_report_sets(before, after)
    regression_count = print_summary(deltas, top=top)
    rows = sorted(deltas, key=_sort_key)
    print_red_classification(classify_reds(before, after, rows), top=top)
    return regression_count


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="Diff generated validation text reports.")
    ap.add_argument(
        "--before",
        default="HEAD",
        help="Baseline report dir or git rev containing reports/validation (default: HEAD).",
    )
    ap.add_argument(
        "--after",
        default="reports/validation",
        help="Current report dir or git rev containing reports/validation (default: reports/validation).",
    )
    ap.add_argument("--top", type=int, default=12, help="Replay-level rows to show.")
    ap.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="Exit nonzero if any tracked suite or replay metric regresses.",
    )
    args = ap.parse_args(argv)

    before = read_report_set(str(args.before), before=True)
    after = read_report_set(str(args.after), before=False)
    print(f"before: {args.before}")
    print(f"after:  {args.after}")
    regression_count = print_report(before, after, top=max(1, int(args.top)))
    if args.fail_on_regression and regression_count:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
