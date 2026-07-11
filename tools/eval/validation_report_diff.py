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


def _label_for_report_path(rel_path: str) -> str:
    stem = rel_path.removesuffix(".txt")
    kind = "one-step" if "one_step" in rel_path else "rollout"
    return f"{stem.replace('/', ' / ')} {kind}"


def _is_validation_report_path(rel_path: str) -> bool:
    return rel_path.endswith(".txt") and ("one_step" in rel_path or "rollout" in rel_path)


def _report_dir_root(path: Path) -> Path:
    nested = path / "reports" / "validation"
    return nested if nested.is_dir() else path


def _report_paths_from_source(source: str) -> set[str]:
    path = Path(source)
    if path.is_dir():
        root = _report_dir_root(path)
        return {
            p.relative_to(root).as_posix()
            for p in root.rglob("*.txt")
            if _is_validation_report_path(p.relative_to(root).as_posix())
        }
    if path.is_file():
        return {path.name} if _is_validation_report_path(path.name) else set()
    try:
        out = subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", source, "--", "reports/validation"],
            cwd=_repo_root(),
            text=True,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError:
        return set()
    prefix = "reports/validation/"
    rel_paths: set[str] = set()
    for line in out.splitlines():
        if not line.startswith(prefix):
            continue
        rel_path = line.removeprefix(prefix)
        if _is_validation_report_path(rel_path):
            rel_paths.add(rel_path)
    return rel_paths


def discover_report_specs(*sources: str) -> tuple[ReportSpec, ...]:
    source_paths = [_report_paths_from_source(source) for source in sources]
    rel_paths: set[str] = set().union(*source_paths) if source_paths else set()
    if not rel_paths:
        return REPORT_FILES
    before_paths = source_paths[0] if len(source_paths) >= 2 else rel_paths
    return tuple(
        ReportSpec(
            _label_for_report_path(rel_path),
            rel_path,
            optional_before=rel_path not in before_paths,
        )
        for rel_path in sorted(rel_paths)
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
    "overall.rollout.streak_count": "lower",
    "overall.rollout.streak_len.median": "higher",
    "overall.rollout.streak_len.p90": "higher",
    "overall.rollout.streak_len.p95": "higher",
    "overall.rollout.streak_len.max": "higher",
    "overall.rollout.best_len.max": "higher",
    "overall.rollout.first_mismatch_total": "lower",
    "overall.rollout.first_mismatch_seeded_total": "lower",
}

ROLLOUT_METADATA_METRICS: frozenset[str] = frozenset(
    {
        "rollout.approved_exception_total",
        "rollout.approved_exception_seeded_total",
    }
)

# Carried for classification context only (no delta rows): the strict counter scores
# every compare lane INCLUDING the bits the validation profile explicitly ignores
# (state_flags[4]&0x80 camera visibility). This per-section counter lets the classifier
# attribute strict movement to that diagnostic lane.
ONE_STEP_METADATA_METRICS: frozenset[str] = frozenset({"overall.ignored_discrete_mismatch"})


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
    ignored_lane_only: tuple[MetricDelta, ...]
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


def _canonical_section(section: str) -> str:
    if section == "suite summary":
        return "suite"
    marker = "/replays/"
    if marker in section:
        section = "replays/" + section.split(marker, 1)[1]
    if section.endswith(".msl"):
        return section.removesuffix(".msl") + ".slpz"
    return section


def _read_report(source: str, rel_path: str, *, required: bool) -> str | None:
    path = Path(source)
    if path.is_dir():
        report_path = _report_dir_root(path) / rel_path
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
            section = _canonical_section(header.group("section"))
            continue
        metric = _METRIC_RE.match(line)
        if metric is None:
            continue
        key = metric.group("key")
        if (
            key not in wanted
            and key not in ROLLOUT_METADATA_METRICS
            and key not in ONE_STEP_METADATA_METRICS
        ):
            continue
        sections.setdefault(section, {})[key] = _parse_metric_value(metric.group("value"))
    return sections


def read_report_set(
    source: str,
    *,
    before: bool = False,
    report_specs: tuple[ReportSpec, ...] = REPORT_FILES,
) -> dict[str, dict[str, dict[str, MetricValue]]]:
    reports: dict[str, dict[str, dict[str, MetricValue]]] = {}
    for spec in report_specs:
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
                direction = directions.get(metric)
                if direction is None:
                    continue
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
                        direction=direction,
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


def _print_delta(delta: MetricDelta, *, marker: str | None = None) -> None:
    if marker is None:
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
        return (
            "overall.rollout.best_len.max",
            "overall.rollout.streak_len.max",
            "overall.rollout.first_mismatch_total",
            "overall.rollout.first_mismatch_seeded_total",
            "overall.rollout.streak_count",
        )
    return (
        "rollout.best_len",
        "rollout.streak_len.max",
        "rollout.first_mismatch_total",
        "rollout.first_mismatch_seeded_total",
        "rollout.streak_count",
    )


def _is_rollout_streak_distribution_metric(metric: str) -> bool:
    return metric in {
        "rollout.best_len",
        "rollout.streak_len.median",
        "rollout.streak_len.p90",
        "rollout.streak_len.p95",
        "rollout.streak_len.max",
        "overall.rollout.best_len.max",
        "overall.rollout.streak_len.median",
        "overall.rollout.streak_len.p90",
        "overall.rollout.streak_len.p95",
        "overall.rollout.streak_len.max",
    }


def _rollout_first_count_metrics(delta: MetricDelta) -> tuple[str, str]:
    if delta.section == "suite":
        return ("overall.rollout.first_mismatch_total", "overall.rollout.streak_count")
    return ("rollout.first_mismatch_total", "rollout.streak_count")


def _rollout_first_counts_non_regressing(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
) -> bool:
    first_metric, streak_metric = _rollout_first_count_metrics(delta)
    first_ok = _metric_non_regressing(before, after, delta, first_metric)
    streak_ok = _metric_non_regressing(before, after, delta, streak_metric)
    return first_ok is True and streak_ok is True


def _is_rollout_distribution_reshuffle_ok(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
) -> bool:
    return (
        "rollout" in delta.report
        and _is_rollout_streak_distribution_metric(delta.metric)
        and _rollout_first_counts_non_regressing(before, after, delta)
    )


def _is_one_step_float_only_ok(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
) -> bool:
    return (
        "one-step" in delta.report
        and delta.metric == "overall.float_norm_mae_p95"
        and _metric_non_regressing(before, after, delta, "overall.discrete_mismatch") is True
        and _metric_non_regressing(before, after, delta, "overall.strict_discrete_mismatch") is True
    )


def _report_replay_sections(
    reports: dict[str, dict[str, dict[str, MetricValue]]], report: str
) -> set[str]:
    return {
        section
        for section in reports.get(report, {})
        if section not in {"suite", "preamble"}
    }


def _report_replay_set_changed(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    report: str,
) -> bool:
    before_sections = _report_replay_sections(before, report)
    after_sections = _report_replay_sections(after, report)
    return bool(before_sections and after_sections and before_sections != after_sections)


def _is_suite_membership_distribution_only(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
) -> bool:
    # When a validation suite gains/removes replay sections, the report is a new coverage
    # distribution. Suite aggregates and newly-added/removed replay sections are informational for
    # that transition, but common replay sections remain hard locks.
    if not _report_replay_set_changed(before, after, delta.report):
        return False
    if delta.section == "suite":
        return True
    before_sections = _report_replay_sections(before, delta.report)
    after_sections = _report_replay_sections(after, delta.report)
    return delta.section not in (before_sections & after_sections)


def is_ignored_lane_only_strict_movement(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    delta: MetricDelta,
) -> bool:
    """True when a strict regression is fully explained by the profile-ignored lane.

    `overall.strict_discrete_mismatch` = scored lanes + the explicitly-ignored
    diagnostic lane (`overall.ignored_discrete_mismatch`, currently the
    state_flags[4]&0x80 camera-visibility bit). When the scored counter is
    non-regressing and the strict movement decomposes exactly into
    scored_delta + ignored_delta, the red is diagnostic-only - outside the
    validation profile's scored target - not a gameplay regression.
    """
    if "one-step" not in delta.report or delta.metric != "overall.strict_discrete_mismatch":
        return False
    if _metric_non_regressing(before, after, delta, "overall.discrete_mismatch") is not True:
        return False
    scored_before = _metric_value(before, delta, "overall.discrete_mismatch")
    scored_after = _metric_value(after, delta, "overall.discrete_mismatch")
    ignored_before = _metric_value(before, delta, "overall.ignored_discrete_mismatch")
    ignored_after = _metric_value(after, delta, "overall.ignored_discrete_mismatch")
    if scored_before is None or scored_after is None or ignored_before is None or ignored_after is None:
        return False
    scored_delta = scored_after.value - scored_before.value
    ignored_delta = ignored_after.value - ignored_before.value
    return delta.delta == scored_delta + ignored_delta


def classify_reds(
    before: dict[str, dict[str, dict[str, MetricValue]]],
    after: dict[str, dict[str, dict[str, MetricValue]]],
    deltas: Iterable[MetricDelta],
) -> RedClassification:
    hard: list[MetricDelta] = []
    distribution_only: list[MetricDelta] = []
    ignored_lane_only: list[MetricDelta] = []
    unclassified: list[MetricDelta] = []
    rows = tuple(deltas)
    for delta in rows:
        if not delta.is_regression:
            continue
        if _is_suite_membership_distribution_only(before, after, delta):
            distribution_only.append(delta)
            continue
        if _is_rollout_distribution_reshuffle_ok(before, after, delta):
            continue
        if _is_one_step_float_only_ok(before, after, delta):
            continue
        if is_ignored_lane_only_strict_movement(before, after, delta):
            ignored_lane_only.append(delta)
            continue
        if _is_hard_red(delta):
            hard.append(delta)
            continue
        if (
            "rollout" in delta.report
            and delta.metric
            in {
                "rollout.streak_len.median",
                "rollout.streak_len.p90",
                "rollout.streak_len.p95",
                "overall.rollout.streak_len.median",
                "overall.rollout.streak_len.p90",
                "overall.rollout.streak_len.p95",
            }
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
        unclassified.append(delta)

    return RedClassification(
        hard=tuple(hard),
        distribution_only=tuple(distribution_only),
        ignored_lane_only=tuple(ignored_lane_only),
        unclassified=tuple(unclassified),
    )


def _print_classification_group(
    name: str, rows: tuple[MetricDelta, ...], *, top: int, marker: str | None = None
) -> None:
    print(f"{name}:")
    if not rows:
        print("- none")
        return
    shown = rows[: max(1, top)]
    for delta in shown:
        _print_delta(delta, marker=marker)
    extra = len(rows) - len(shown)
    if extra > 0:
        print(f"- ... {extra} more")


def print_red_classification(classification: RedClassification, *, top: int) -> None:
    print("red classification:")
    _print_classification_group("hard reds", classification.hard, top=top)
    _print_classification_group(
        "distribution-only reds",
        classification.distribution_only,
        top=top,
        marker="distribution-only",
    )
    _print_classification_group(
        "ignored-lane-only movements (diagnostic, outside the scored profile)",
        classification.ignored_lane_only,
        top=top,
        marker="diagnostic",
    )
    _print_classification_group(
        "unclassified regressions", classification.unclassified, top=top
    )


def print_summary(
    deltas: Iterable[MetricDelta],
    *,
    top: int,
    non_regression_markers: dict[MetricDelta, str] | None = None,
) -> int:
    if non_regression_markers is None:
        non_regression_markers = {}
    rows = sorted(deltas, key=_sort_key)
    regressions = [d for d in rows if d.is_regression and d not in non_regression_markers]
    suite_rows = [d for d in rows if d.section == "suite"]

    print("suite totals:")
    if suite_rows:
        for delta in suite_rows:
            _print_delta(delta, marker=non_regression_markers.get(delta))
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

    replay_non_regressions = [
        d for d in rows if d.section != "suite" and d.is_regression and d in non_regression_markers
    ]
    if replay_non_regressions:
        print("replay-level non-regression movements:")
        for delta in replay_non_regressions[: max(1, top)]:
            _print_delta(delta, marker=non_regression_markers[delta])

    print("largest replay-level movements:")
    replay_rows = [d for d in rows if d.section != "suite"]
    if replay_rows:
        for delta in replay_rows[: max(1, top)]:
            _print_delta(delta, marker=non_regression_markers.get(delta))
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
    rows = sorted(deltas, key=_sort_key)
    classification = classify_reds(before, after, rows)
    non_regression_markers: dict[MetricDelta, str] = {}
    for delta in rows:
        if not delta.is_regression:
            continue
        if _is_suite_membership_distribution_only(before, after, delta):
            non_regression_markers[delta] = "suite-membership"
        elif is_ignored_lane_only_strict_movement(before, after, delta):
            non_regression_markers[delta] = "diagnostic"
        elif _is_rollout_distribution_reshuffle_ok(before, after, delta):
            non_regression_markers[delta] = "distribution-only"
        elif _is_one_step_float_only_ok(before, after, delta):
            non_regression_markers[delta] = "float-only"
    for delta in classification.distribution_only:
        non_regression_markers.setdefault(delta, "distribution-only")
    regression_count = print_summary(
        deltas, top=top, non_regression_markers=non_regression_markers
    )
    print_red_classification(classification, top=top)
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

    report_specs = discover_report_specs(str(args.before), str(args.after))
    before = read_report_set(str(args.before), before=True, report_specs=report_specs)
    after = read_report_set(str(args.after), before=False, report_specs=report_specs)
    print(f"before: {args.before}")
    print(f"after:  {args.after}")
    regression_count = print_report(before, after, top=max(1, int(args.top)))
    if args.fail_on_regression and regression_count:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
