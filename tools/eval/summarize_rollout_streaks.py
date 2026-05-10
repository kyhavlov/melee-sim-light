from __future__ import annotations

import argparse
import json
from pathlib import Path

from tools.eval.rollout_metrics import summarize_rollout_payload, validate_rollout_payload


def _top_counts(d: dict[str, int], *, k: int) -> str:
    if not d:
        return "(none)"
    items = sorted(((str(kk), int(vv)) for kk, vv in d.items()), key=lambda kv: (-kv[1], kv[0]))[:k]
    return " ".join(f"{name}:{count}" for name, count in items)


def main() -> None:
    ap = argparse.ArgumentParser(description="Summarize rollout streak JSON into stable headline metrics.")
    ap.add_argument("--in", dest="inp", required=True, type=Path, help="Input rollout JSON path.")
    ap.add_argument("--out", type=Path, default=None, help="Optional JSON output path for computed summary.")
    ap.add_argument("--top", type=int, default=8, help="Top K mismatch fields to print.")
    args = ap.parse_args()

    payload = json.loads(args.inp.read_text(encoding="utf-8"))
    try:
        validate_rollout_payload(payload, label=str(args.inp))
    except ValueError as e:
        raise SystemExit(f"error: {e}") from e
    summary = summarize_rollout_payload(payload)
    suite = summary["suite_summary"]

    print(f"rollout: {args.inp}")
    print(
        "suite:",
        summary["suite"],
        "datasets:",
        suite["dataset_count"],
        "fields:",
        ",".join(str(x) for x in summary["fields"]),
    )
    print(
        "suite_streaks:",
        f"count={suite['total_streaks']}",
        f"median={suite['median_streak_len']}",
        f"p90={suite['p90_streak_len']}",
        f"p95={suite['p95_streak_len']}",
        f"max={suite['max_streak_len']}",
        f"best_max={suite['max_best_len']}",
    )
    print(
        "suite_first_mismatch_total:",
        f"raw={suite['first_mismatch_total']}",
        f"seeded={suite['first_mismatch_seeded_total']}",
        f"non_seeded={suite['first_mismatch_non_seeded_total']}",
    )
    print(
        "suite_first_mismatch_top:",
        _top_counts(dict(suite["first_mismatch_field_counts"]), k=max(1, int(args.top))),
    )
    print(
        "suite_seeded_mismatch_top:",
        _top_counts(dict(suite["first_mismatch_field_counts_seeded"]), k=max(1, int(args.top))),
    )

    for row in summary["dataset_summaries"]:
        ds_name = Path(str(row["dataset"])).name
        print(
            f"- {ds_name}:",
            f"best={row['best_len']}",
            f"median={row['median_streak_len']}",
            f"p90={row['p90_streak_len']}",
            f"p95={row['p95_streak_len']}",
            f"max={row['max_streak_len']}",
            f"first_mm={row['first_mismatch_total']}",
            f"seeded_mm={row['first_mismatch_seeded_total']}",
            f"non_seeded_mm={row['first_mismatch_non_seeded_total']}",
        )

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote: {args.out}")


if __name__ == "__main__":
    main()
