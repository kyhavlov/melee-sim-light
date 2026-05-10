from __future__ import annotations

"""Run the standard validation report set."""

import argparse
import concurrent.futures
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval


def _run_main(name: str, main: Callable[[], None], args: list[str]) -> None:
    old_argv = sys.argv
    try:
        sys.argv = [name, *args]
        main()
    finally:
        sys.argv = old_argv


@dataclass(frozen=True)
class _ReportJob:
    name: str
    module: str
    args: list[str]
    main: Callable[[], None] | None = None


def _run_subprocess(job: _ReportJob) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", job.module, *job.args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def _replay_completed_process(result: subprocess.CompletedProcess[str]) -> None:
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description="Run all committed validation reports.")
    ap.add_argument("--suite", required=True)
    ap.add_argument("--agg-suite", required=True)
    ap.add_argument("--doubles-suite", default="")
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--one-step-out", default="reports/validation/one_step_suite_eval.txt")
    ap.add_argument("--rollout-out", default="reports/validation/rollout_suite_eval.txt")
    ap.add_argument("--agg-one-step-out", default="reports/validation/aggregate_recent_one_step_suite_eval.txt")
    ap.add_argument("--agg-rollout-out", default="reports/validation/aggregate_recent_rollout_suite_eval.txt")
    ap.add_argument("--doubles-one-step-out", default="reports/validation/doubles_recent_one_step_suite_eval.txt")
    ap.add_argument("--doubles-rollout-out", default="reports/validation/doubles_recent_rollout_suite_eval.txt")
    ap.add_argument("--workers", type=int, default=1, help="Parallel report worker subprocesses.")
    args = ap.parse_args()

    one_step_common = ["--datasets-dir", args.datasets_dir, "--chunk", str(int(args.chunk))]
    rollout_common = ["--datasets-dir", args.datasets_dir, "--fields", str(args.fields)]

    jobs = [
        _ReportJob(
            name="run_one_step_suite_eval",
            module="tools.eval.run_one_step_suite_eval",
            main=run_one_step_suite_eval.main,
            args=["--suite", args.suite, *one_step_common, "--out", args.one_step_out, "--quiet"],
        ),
        _ReportJob(
            name="run_rollout_suite_eval",
            module="tools.eval.run_rollout_suite_eval",
            main=run_rollout_suite_eval.main,
            args=["--suite", args.suite, *rollout_common, "--out", args.rollout_out, "--quiet"],
        ),
        _ReportJob(
            name="run_one_step_suite_eval",
            module="tools.eval.run_one_step_suite_eval",
            main=run_one_step_suite_eval.main,
            args=["--suite", args.agg_suite, *one_step_common, "--out", args.agg_one_step_out, "--quiet"],
        ),
        _ReportJob(
            name="run_rollout_suite_eval",
            module="tools.eval.run_rollout_suite_eval",
            main=run_rollout_suite_eval.main,
            args=["--suite", args.agg_suite, *rollout_common, "--out", args.agg_rollout_out, "--quiet"],
        ),
    ]
    if args.doubles_suite:
        jobs.extend(
            [
                _ReportJob(
                    name="run_one_step_suite_eval",
                    module="tools.eval.run_one_step_suite_eval",
                    main=run_one_step_suite_eval.main,
                    args=[
                        "--suite",
                        args.doubles_suite,
                        *one_step_common,
                        "--out",
                        args.doubles_one_step_out,
                        "--quiet",
                    ],
                ),
                _ReportJob(
                    name="run_rollout_suite_eval",
                    module="tools.eval.run_rollout_suite_eval",
                    main=run_rollout_suite_eval.main,
                    args=[
                        "--suite",
                        args.doubles_suite,
                        *rollout_common,
                        "--out",
                        args.doubles_rollout_out,
                        "--quiet",
                    ],
                ),
            ]
        )

    workers = max(1, int(args.workers))
    if workers == 1:
        for job in jobs:
            assert job.main is not None
            _run_main(job.name, job.main, job.args)
        return

    results: list[subprocess.CompletedProcess[str] | None] = [None for _ in jobs]
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(workers, len(jobs))) as executor:
        future_to_index = {executor.submit(_run_subprocess, job): i for i, job in enumerate(jobs)}
        for future in concurrent.futures.as_completed(future_to_index):
            results[future_to_index[future]] = future.result()

    for result in results:
        assert result is not None
        _replay_completed_process(result)


if __name__ == "__main__":
    main()
