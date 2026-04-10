from __future__ import annotations

"""Diff two rollout locate TSV outputs at cluster level."""

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from tools.eval.rollout_locate_tsv import (
    RolloutClusterSummary,
    RolloutLocateRow,
    parse_rollout_locate_tsv,
    summarize_clusters,
)


@dataclass(frozen=True)
class RolloutClusterDelta:
    cluster_key: str
    before_frequency: int
    after_frequency: int
    before_impact: int
    after_impact: int
    before_seeded_breaks: int
    after_seeded_breaks: int
    example: RolloutLocateRow

    @property
    def frequency_delta(self) -> int:
        return self.after_frequency - self.before_frequency

    @property
    def impact_delta(self) -> int:
        return self.after_impact - self.before_impact

    @property
    def seeded_breaks_delta(self) -> int:
        return self.after_seeded_breaks - self.before_seeded_breaks


@dataclass(frozen=True)
class RolloutLocateDiff:
    before_rows: int
    after_rows: int
    before_clusters: int
    after_clusters: int
    new_clusters: tuple[RolloutClusterSummary, ...]
    gone_clusters: tuple[RolloutClusterSummary, ...]
    changed_clusters: tuple[RolloutClusterDelta, ...]


def _index_clusters(rows: list[RolloutLocateRow]) -> dict[str, RolloutClusterSummary]:
    return {row.cluster_key: row for row in summarize_clusters(rows)}


def diff_rollout_locate_rows(
    *,
    before_rows: list[RolloutLocateRow],
    after_rows: list[RolloutLocateRow],
) -> RolloutLocateDiff:
    before = _index_clusters(before_rows)
    after = _index_clusters(after_rows)

    before_keys = set(before.keys())
    after_keys = set(after.keys())

    new_clusters = tuple(after[key] for key in sorted(after_keys - before_keys))
    gone_clusters = tuple(before[key] for key in sorted(before_keys - after_keys))

    changed: list[RolloutClusterDelta] = []
    for key in sorted(before_keys & after_keys):
        b = before[key]
        a = after[key]
        if (
            b.frequency != a.frequency
            or b.impact != a.impact
            or b.seeded_breaks != a.seeded_breaks
        ):
            changed.append(
                RolloutClusterDelta(
                    cluster_key=key,
                    before_frequency=int(b.frequency),
                    after_frequency=int(a.frequency),
                    before_impact=int(b.impact),
                    after_impact=int(a.impact),
                    before_seeded_breaks=int(b.seeded_breaks),
                    after_seeded_breaks=int(a.seeded_breaks),
                    example=a.example,
                )
            )

    return RolloutLocateDiff(
        before_rows=len(before_rows),
        after_rows=len(after_rows),
        before_clusters=len(before),
        after_clusters=len(after),
        new_clusters=tuple(
            sorted(new_clusters, key=lambda row: (-row.impact, -row.frequency, row.cluster_key))
        ),
        gone_clusters=tuple(
            sorted(gone_clusters, key=lambda row: (-row.impact, -row.frequency, row.cluster_key))
        ),
        changed_clusters=tuple(
            sorted(changed, key=lambda row: (-abs(row.impact_delta), -abs(row.frequency_delta), row.cluster_key))
        ),
    )


def _fmt_delta(value: int) -> str:
    if value > 0:
        return f"+{value}"
    return str(value)


def _print_cluster_summary(prefix: str, row: RolloutClusterSummary) -> None:
    ex = row.example
    print(
        f"  {prefix} impact={row.impact:<5d} freq={row.frequency:<5d} seeded={row.seeded_breaks:<5d} "
        f"{row.field}[{row.subindex}] seed/out/ref={row.seed}/{row.out}/{row.ref} "
        f"example={Path(ex.dataset).name}:rec={ex.record}:p={ex.player}"
    )


def _print_changed(row: RolloutClusterDelta) -> None:
    ex = row.example
    print(
        f"  impact={_fmt_delta(row.impact_delta):<6s} freq={_fmt_delta(row.frequency_delta):<6s} "
        f"seeded={_fmt_delta(row.seeded_breaks_delta):<6s} "
        f"now={row.after_impact}/{row.after_frequency}/{row.after_seeded_breaks} "
        f"example={Path(ex.dataset).name}:rec={ex.record}:p={ex.player} key={row.cluster_key}"
    )


def _summary_json(row: RolloutClusterSummary) -> dict[str, Any]:
    ex = row.example
    return {
        "cluster_key": row.cluster_key,
        "field": row.field,
        "subindex": int(row.subindex),
        "seed": int(row.seed),
        "out": int(row.out),
        "ref": int(row.ref),
        "frequency": int(row.frequency),
        "impact": int(row.impact),
        "seeded_breaks": int(row.seeded_breaks),
        "example": {
            "dataset": ex.dataset,
            "record": int(ex.record),
            "player": int(ex.player),
            "seed_frame": int(ex.seed_frame),
            "ref_frame": int(ex.ref_frame),
        },
    }


def _delta_json(row: RolloutClusterDelta) -> dict[str, Any]:
    return {
        "cluster_key": row.cluster_key,
        "frequency_delta": int(row.frequency_delta),
        "impact_delta": int(row.impact_delta),
        "seeded_breaks_delta": int(row.seeded_breaks_delta),
        "before": {
            "frequency": int(row.before_frequency),
            "impact": int(row.before_impact),
            "seeded_breaks": int(row.before_seeded_breaks),
        },
        "after": {
            "frequency": int(row.after_frequency),
            "impact": int(row.after_impact),
            "seeded_breaks": int(row.after_seeded_breaks),
        },
    }


def main() -> None:
    ap = argparse.ArgumentParser(description="Diff rollout locate TSVs by stable cluster key.")
    ap.add_argument("--before", type=Path, required=True, help="Baseline rollout locate TSV.")
    ap.add_argument("--after", type=Path, required=True, help="Current rollout locate TSV.")
    ap.add_argument("--top", type=int, default=12, help="Top-N clusters per section.")
    ap.add_argument(
        "--show",
        default="new,gone,delta",
        help="Comma-separated sections to show: new,gone,delta",
    )
    ap.add_argument("--json-out", type=Path, default=None, help="Optional JSON diff output path.")
    args = ap.parse_args()

    before_rows = parse_rollout_locate_tsv(args.before)
    after_rows = parse_rollout_locate_tsv(args.after)
    report = diff_rollout_locate_rows(before_rows=before_rows, after_rows=after_rows)
    show = {part.strip() for part in str(args.show).split(",") if part.strip()}
    top = max(1, int(args.top))

    print(f"before: {args.before}")
    print(f"after:  {args.after}")
    print(
        f"rows(before)={report.before_rows} rows(after)={report.after_rows} "
        f"clusters(before)={report.before_clusters} clusters(after)={report.after_clusters}"
    )
    print(
        f"new={len(report.new_clusters)} gone={len(report.gone_clusters)} "
        f"changed={len(report.changed_clusters)}"
    )

    if "new" in show:
        print("== new clusters ==")
        if not report.new_clusters:
            print("  (none)")
        for row in report.new_clusters[:top]:
            _print_cluster_summary("new", row)

    if "gone" in show:
        print("== gone clusters ==")
        if not report.gone_clusters:
            print("  (none)")
        for row in report.gone_clusters[:top]:
            _print_cluster_summary("gone", row)

    if "delta" in show:
        print("== changed clusters ==")
        if not report.changed_clusters:
            print("  (none)")
        for row in report.changed_clusters[:top]:
            _print_changed(row)

    if args.json_out is not None:
        payload = {
            "before": str(args.before),
            "after": str(args.after),
            "counts": {
                "rows_before": report.before_rows,
                "rows_after": report.after_rows,
                "clusters_before": report.before_clusters,
                "clusters_after": report.after_clusters,
                "new": len(report.new_clusters),
                "gone": len(report.gone_clusters),
                "changed": len(report.changed_clusters),
            },
            "top": {
                "new": [_summary_json(row) for row in report.new_clusters[:top]],
                "gone": [_summary_json(row) for row in report.gone_clusters[:top]],
                "changed": [_delta_json(row) for row in report.changed_clusters[:top]],
            },
        }
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote: {args.json_out}")


if __name__ == "__main__":
    main()

