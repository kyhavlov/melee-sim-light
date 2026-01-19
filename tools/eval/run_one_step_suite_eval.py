from __future__ import annotations

import argparse
from pathlib import Path

from tools.eval.run_one_step_eval import main as run_one_step_eval_main
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

    print(f"suite: {suite.name}  datasets: {len(dataset_paths)}")
    # For now, run per-dataset eval serially (fast enough; performance work comes later).
    # We reuse the existing one-step evaluator module by setting sys.argv-style args.
    import sys

    for ds in dataset_paths:
        print()
        print(f"== {ds.relative_to(root)} ==")
        sys.argv = [
            "run_one_step_eval",
            "--dataset",
            str(ds),
            "--chunk",
            str(args.chunk),
        ]
        run_one_step_eval_main()


if __name__ == "__main__":
    main()
