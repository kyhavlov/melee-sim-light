from __future__ import annotations

"""Run the standard validation report set in one Python process."""

import argparse
import sys
from collections.abc import Callable

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval


def _run_main(name: str, main: Callable[[], None], args: list[str]) -> None:
    old_argv = sys.argv
    try:
        sys.argv = [name, *args]
        main()
    finally:
        sys.argv = old_argv


def main() -> None:
    ap = argparse.ArgumentParser(description="Run all committed validation reports.")
    ap.add_argument("--suite", required=True)
    ap.add_argument("--agg-suite", required=True)
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--one-step-out", default="reports/validation/one_step_suite_eval.txt")
    ap.add_argument("--rollout-out", default="reports/validation/rollout_suite_eval.txt")
    ap.add_argument("--agg-one-step-out", default="reports/validation/aggregate_recent_one_step_suite_eval.txt")
    ap.add_argument("--agg-rollout-out", default="reports/validation/aggregate_recent_rollout_suite_eval.txt")
    args = ap.parse_args()

    one_step_common = ["--datasets-dir", args.datasets_dir, "--chunk", str(int(args.chunk))]
    rollout_common = ["--datasets-dir", args.datasets_dir, "--fields", str(args.fields)]

    _run_main(
        "run_one_step_suite_eval",
        run_one_step_suite_eval.main,
        ["--suite", args.suite, *one_step_common, "--out", args.one_step_out],
    )
    _run_main(
        "run_rollout_suite_eval",
        run_rollout_suite_eval.main,
        ["--suite", args.suite, *rollout_common, "--out", args.rollout_out],
    )
    _run_main(
        "run_one_step_suite_eval",
        run_one_step_suite_eval.main,
        ["--suite", args.agg_suite, *one_step_common, "--out", args.agg_one_step_out],
    )
    _run_main(
        "run_rollout_suite_eval",
        run_rollout_suite_eval.main,
        ["--suite", args.agg_suite, *rollout_common, "--out", args.agg_rollout_out],
    )


if __name__ == "__main__":
    main()
