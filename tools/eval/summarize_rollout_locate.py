from __future__ import annotations

"""Summarize rollout locate TSV clusters."""

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

from tools.eval.rollout_locate_tsv import RolloutClusterSummary, parse_rollout_locate_tsv, summarize_clusters


def _cluster_to_json(row: RolloutClusterSummary) -> dict[str, Any]:
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
        "datasets": list(row.datasets),
        "example": {
            "dataset": ex.dataset,
            "record": int(ex.record),
            "seed_frame": int(ex.seed_frame),
            "ref_frame": int(ex.ref_frame),
            "player": int(ex.player),
            "streak_start_record": int(ex.streak_start_record),
            "streak_len": int(ex.streak_len),
            "seeded_break": bool(ex.seeded_break),
        },
    }


def build_summary(path: Path, *, top: int) -> dict[str, Any]:
    rows = parse_rollout_locate_tsv(path)
    clusters = summarize_clusters(rows)
    per_dataset = Counter(row.dataset for row in rows)
    seeded_per_dataset = Counter(row.dataset for row in rows if row.seeded_break)
    return {
        "input": str(path),
        "row_count": len(rows),
        "cluster_count": len(clusters),
        "top": int(top),
        "top_clusters": [_cluster_to_json(row) for row in clusters[: max(1, int(top))]],
        "per_dataset": [
            {
                "dataset": dataset,
                "rows": int(count),
                "seeded_breaks": int(seeded_per_dataset.get(dataset, 0)),
            }
            for dataset, count in sorted(per_dataset.items(), key=lambda item: (-item[1], item[0]))
        ],
    }


def print_summary(summary: dict[str, Any]) -> None:
    print(f"rollout_locate: {summary['input']}")
    print(f"rows={summary['row_count']} clusters={summary['cluster_count']}")
    print("top_clusters:")
    if not summary["top_clusters"]:
        print("  (none)")
    for i, row in enumerate(summary["top_clusters"], start=1):
        ex = row["example"]
        ds_preview = ",".join(Path(ds).name for ds in row["datasets"][:3])
        if len(row["datasets"]) > 3:
            ds_preview += f",+{len(row['datasets']) - 3}"
        print(
            f"{i:>2}. impact={row['impact']:<5d} freq={row['frequency']:<5d} "
            f"seeded={row['seeded_breaks']:<5d} {row['field']}[{row['subindex']}] "
            f"seed/out/ref={row['seed']}/{row['out']}/{row['ref']} "
            f"example={Path(ex['dataset']).name}:rec={ex['record']}:p={ex['player']} datasets={ds_preview}"
        )

    print("per_dataset:")
    if not summary["per_dataset"]:
        print("  (none)")
    for row in summary["per_dataset"]:
        print(
            f"  {Path(row['dataset']).name}: rows={row['rows']} "
            f"seeded_breaks={row['seeded_breaks']}"
        )


def main() -> None:
    ap = argparse.ArgumentParser(description="Summarize rollout locate TSV clusters.")
    ap.add_argument("--in", dest="inp", type=Path, required=True, help="Input rollout locate TSV path.")
    ap.add_argument("--top", type=int, default=12, help="Top-N clusters to print.")
    ap.add_argument("--json-out", type=Path, default=None, help="Optional JSON summary output path.")
    args = ap.parse_args()

    summary = build_summary(args.inp, top=max(1, int(args.top)))
    print_summary(summary)

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote: {args.json_out}")


if __name__ == "__main__":
    main()

