from __future__ import annotations

import argparse
from pathlib import Path

from tools.eval.run_one_step_eval import EvalSummary, Reporter, _discrete_mismatch_total, evaluate_dataset
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--suite", required=True, help="Path to suite JSON (e.g. replays/suites/...)")
    ap.add_argument(
        "--datasets-dir",
        default="datasets",
        help="Directory under repo root that stores preprocessed datasets",
    )
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--out", type=Path, default=None, help="optional output path to write the report")
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)

    missing = []
    dataset_paths: list[Path] = []
    for entry in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=args.datasets_dir,
        )
        if not ds_path.exists():
            missing.append(str(ds_path.relative_to(root)))
        else:
            dataset_paths.append(ds_path)

    if missing:
        print(f"Missing {len(missing)} preprocessed dataset files for suite {suite.name}:")
        for p in missing:
            print(f"  {p}")
        print("Run preprocessing first:")
        print(f"  uv run python -m tools.slippi.preprocess_suite --suite {args.suite} --datasets-dir {args.datasets_dir}")
        raise SystemExit(2)

    reporter = Reporter(args.out)
    try:
        reporter.print(f"suite: {suite.name}  datasets: {len(dataset_paths)}")
        suite_mismatches: dict[str, int] | None = None
        suite_totals = {
            "total_records": 0,
            "total_player_frames": 0,
            "total_state_flags": 0,
            "total_item_slots": 0,
            "float_norm_sum": 0.0,
            "float_norm_count": 0,
        }

        for ds in dataset_paths:
            reporter.print()
            reporter.print(f"== {ds.relative_to(root)} ==")
            summary = evaluate_dataset(dataset_path=ds, chunk=args.chunk, reporter=reporter)

            if suite_mismatches is None:
                suite_mismatches = {k: 0 for k in summary.mismatches.keys()}
            for k, v in summary.mismatches.items():
                suite_mismatches[k] += v
            suite_totals["total_records"] += summary.total_records
            suite_totals["total_player_frames"] += summary.total_player_frames
            suite_totals["total_state_flags"] += summary.total_state_flags
            suite_totals["total_item_slots"] += summary.total_item_slots
            suite_totals["float_norm_sum"] += summary.float_norm_sum
            suite_totals["float_norm_count"] += summary.float_norm_count

        if suite_mismatches is None:
            return

        reporter.print()
        reporter.print("== suite summary ==")
        suite_summary = EvalSummary(
            total_records=suite_totals["total_records"],
            total_player_frames=suite_totals["total_player_frames"],
            total_state_flags=suite_totals["total_state_flags"],
            total_item_slots=suite_totals["total_item_slots"],
            mismatches=suite_mismatches,
            float_norm_sum=float(suite_totals["float_norm_sum"]),
            float_norm_count=int(suite_totals["float_norm_count"]),
        )
        mismatches_total, checks_total = _discrete_mismatch_total(suite_summary)
        reporter.print(f"overall.discrete_mismatch: {mismatches_total} / {checks_total}")
        overall_float = (
            suite_summary.float_norm_sum / suite_summary.float_norm_count
            if suite_summary.float_norm_count > 0
            else 0.0
        )
        reporter.print(f"overall.float_norm_mae_p95: {overall_float:.8f}")
    finally:
        reporter.close()


if __name__ == "__main__":
    main()
