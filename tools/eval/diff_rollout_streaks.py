from __future__ import annotations

import argparse
import json
from pathlib import Path

from tools.eval.rollout_metrics import diff_rollout_summaries, validate_rollout_payload


def _fmt_delta(v: int) -> str:
    if v > 0:
        return f"+{v}"
    return str(v)


def _print_suite_delta(rep: dict) -> None:
    d = rep["suite_delta"]
    print(
        "suite_delta:",
        f"total_streaks={_fmt_delta(int(d['total_streaks']))}",
        f"median={_fmt_delta(int(d['median_streak_len']))}",
        f"p90={_fmt_delta(int(d['p90_streak_len']))}",
        f"p95={_fmt_delta(int(d['p95_streak_len']))}",
        f"max={_fmt_delta(int(d['max_streak_len']))}",
        f"best_max={_fmt_delta(int(d['max_best_len']))}",
    )
    print(
        "suite_first_mismatch_delta:",
        f"raw={_fmt_delta(int(d['first_mismatch_total']))}",
        f"seeded={_fmt_delta(int(d['first_mismatch_seeded_total']))}",
    )


def _print_field_delta(name: str, d: dict[str, int], top: int) -> None:
    if not d:
        print(f"{name}: (none)")
        return
    items = sorted(((k, int(v)) for k, v in d.items() if int(v) != 0), key=lambda kv: (-abs(kv[1]), kv[0]))[
        : max(1, top)
    ]
    if not items:
        print(f"{name}: (all zero)")
        return
    text = " ".join(f"{k}:{_fmt_delta(v)}" for k, v in items)
    print(f"{name}: {text}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Diff two rollout streak JSON files.")
    ap.add_argument("--before", required=True, type=Path, help="Baseline rollout JSON.")
    ap.add_argument("--after", required=True, type=Path, help="Current rollout JSON.")
    ap.add_argument("--out", type=Path, default=None, help="Optional JSON diff output path.")
    ap.add_argument("--top", type=int, default=8, help="Top K field and dataset rows to print.")
    args = ap.parse_args()

    before = json.loads(args.before.read_text(encoding="utf-8"))
    after = json.loads(args.after.read_text(encoding="utf-8"))
    try:
        validate_rollout_payload(before, label=f"before({args.before})")
        validate_rollout_payload(after, label=f"after({args.after})")
    except ValueError as e:
        raise SystemExit(f"error: {e}") from e
    rep = diff_rollout_summaries(before, after)

    print(f"before: {args.before}")
    print(f"after:  {args.after}")
    print(f"suite:  {rep['before_suite']} -> {rep['after_suite']}")
    _print_suite_delta(rep)
    _print_field_delta(
        "first_mismatch_field_delta",
        dict(rep["suite_first_mismatch_field_delta"]),
        top=max(1, int(args.top)),
    )
    _print_field_delta(
        "seeded_mismatch_field_delta",
        dict(rep["suite_first_mismatch_field_seeded_delta"]),
        top=max(1, int(args.top)),
    )

    if rep["dataset_new"]:
        print("dataset_new:", ", ".join(str(x) for x in rep["dataset_new"]))
    if rep["dataset_gone"]:
        print("dataset_gone:", ", ".join(str(x) for x in rep["dataset_gone"]))

    rows = list(rep["per_dataset_delta"])[: max(1, int(args.top))]
    if rows:
        print("per_dataset_delta_top:")
        for row in rows:
            ds_name = Path(str(row["dataset"])).name
            print(
                f"- {ds_name}:",
                f"best={_fmt_delta(int(row['best_len']))}",
                f"median={_fmt_delta(int(row['median_streak_len']))}",
                f"p90={_fmt_delta(int(row['p90_streak_len']))}",
                f"p95={_fmt_delta(int(row['p95_streak_len']))}",
                f"first_mm={_fmt_delta(int(row['first_mismatch_total']))}",
                f"seeded_mm={_fmt_delta(int(row['first_mismatch_seeded_total']))}",
            )

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(rep, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote: {args.out}")


if __name__ == "__main__":
    main()
