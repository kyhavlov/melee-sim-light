from __future__ import annotations

"""Run the standard validation report set."""

import argparse
import os
from pathlib import Path

from tools.eval import run_combined_suite_eval, run_one_step_suite_eval, run_rollout_suite_eval
from tools.eval.one_step_report import Reporter
from tools.eval.validation_exceptions import load_validation_exceptions
from tools.eval.validation_profile import get_validation_profile, validation_profile_names
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import load_suite, repo_root

_STANDARD_ROLLOUT_FIELDS = ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "state_flags")


def _parse_csv(s: str) -> tuple[str, ...]:
    return tuple(x.strip() for x in s.split(",") if x.strip() != "")


def _validate_discrete_fields(fields: tuple[str, ...]) -> tuple[str, ...]:
    if tuple(fields) != _STANDARD_ROLLOUT_FIELDS:
        raise SystemExit(
            "error: validate-all supports the standard rollout field set only: "
            + ",".join(_STANDARD_ROLLOUT_FIELDS)
        )
    return tuple(fields)


def _resolve_worker_count(requested: int, task_count: int) -> int:
    if int(requested) > 0:
        return max(1, int(requested))
    return max(1, min(16, int(os.cpu_count() or 1), int(task_count) if task_count else 1))


def _task_key(task: dict) -> tuple:
    return (
        str(task["slp_path"]),
        tuple(int(p) for p in task["ports"]),
        bool(task["ucf_enabled"]),
        bool(task["ucf_cardinals_1_0_enabled"]),
    )


def main() -> None:
    ap = argparse.ArgumentParser(description="Run all committed validation reports.")
    ap.add_argument("--suite", required=True)
    ap.add_argument("--agg-suite", required=True)
    ap.add_argument("--doubles-suite", default="")
    ap.add_argument("--sheik-suite", default="")
    ap.add_argument("--chunk", type=int, default=64)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--exceptions", default="replays/validation_exceptions.json")
    ap.add_argument("--float-top", type=int, default=3)
    ap.add_argument("--one-step-out", default="reports/validation/one_step_suite_eval.txt")
    ap.add_argument("--rollout-out", default="reports/validation/rollout_suite_eval.txt")
    ap.add_argument("--agg-one-step-out", default="reports/validation/aggregate_recent_one_step_suite_eval.txt")
    ap.add_argument("--agg-rollout-out", default="reports/validation/aggregate_recent_rollout_suite_eval.txt")
    ap.add_argument("--doubles-one-step-out", default="reports/validation/doubles_recent_one_step_suite_eval.txt")
    ap.add_argument("--doubles-rollout-out", default="reports/validation/doubles_recent_rollout_suite_eval.txt")
    ap.add_argument("--sheik-one-step-out", default="reports/validation/sheik_one_step.txt")
    ap.add_argument("--sheik-rollout-out", default="reports/validation/sheik_rollout.txt")
    ap.add_argument("--workers", type=int, default=0, help="Parallel replay workers (0 = auto, capped at 16).")
    ap.add_argument("--stats-out", type=Path, default=None)
    args = ap.parse_args()

    root = repo_root()
    fields = _validate_discrete_fields(_parse_csv(str(args.fields)))
    validation_profile = get_validation_profile(args.profile)
    exceptions_path = (root / args.exceptions).resolve()
    exceptions = load_validation_exceptions(exceptions_path)
    float_fields = run_rollout_suite_eval._float_compare_fields()
    float_top_scan = max(0, int(args.float_top)) * 8

    suite_specs: list[tuple[str, str, str]] = [
        (args.suite, args.one_step_out, args.rollout_out),
        (args.agg_suite, args.agg_one_step_out, args.agg_rollout_out),
    ]
    if args.sheik_suite:
        suite_specs.append((args.sheik_suite, args.sheik_one_step_out, args.sheik_rollout_out))
    if args.doubles_suite:
        suite_specs.append((args.doubles_suite, args.doubles_one_step_out, args.doubles_rollout_out))

    suite_rows: list[dict] = []
    unique_tasks: list[dict] = []
    task_index: dict[tuple, int] = {}
    for suite_arg, one_step_out, rollout_out in suite_specs:
        suite_path = (root / suite_arg).resolve()
        suite = load_suite(suite_path)
        replay_paths = [resolve_replay_path((root / entry.replay).resolve()) for entry in suite.replays]
        indices: list[int] = []
        for replay_path, entry in zip(replay_paths, suite.replays, strict=True):
            display_path = run_combined_suite_eval._display_path(replay_path, root=root)
            task = {
                "root": str(root),
                "dataset_label": str(replay_path),
                "slp_path": str(replay_path),
                "ports": [int(p) for p in entry.ports],
                "stage_id": int(entry.stage_id) if entry.stage_id is not None else 0,
                "chunk": int(args.chunk),
                "profile": validation_profile.name,
                "ucf_enabled": bool(suite.ucf_enabled),
                "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
                "debug_mismatch": (),
                "debug_limit": 10,
                "debug_float": (),
                "debug_float_limit": 10,
                "fields": list(fields),
                "players_csv": None,
                "max_records": 0,
                "float_fields": list(float_fields),
                "float_top_scan": int(float_top_scan),
                "exception_probe_limit": run_rollout_suite_eval._exception_probe_limit(
                    dataset=display_path, exceptions=exceptions
                ),
            }
            key = _task_key(task)
            idx = task_index.get(key)
            if idx is None:
                idx = len(unique_tasks)
                task_index[key] = idx
                unique_tasks.append(task)
            indices.append(idx)
        suite_rows.append(
            {
                "suite_arg": suite_arg,
                "suite_name": suite.name,
                "one_step_out": one_step_out,
                "rollout_out": rollout_out,
                "replay_paths": replay_paths,
                "task_indices": indices,
                "ucf_enabled": bool(suite.ucf_enabled),
                "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
            }
        )

    workers = _resolve_worker_count(int(args.workers), len(unique_tasks))
    unique_results, shard_stats = run_combined_suite_eval.run_combined_dataset_tasks(
        unique_tasks, workers=workers, collect_stats=args.stats_out is not None
    )
    if args.stats_out is not None:
        run_combined_suite_eval.write_shard_stats(
            Path(args.stats_out), workers=workers, task_count=len(unique_tasks), stats=shard_stats
        )
    for result, task in zip(unique_results, unique_tasks, strict=True):
        result["ucf_enabled"] = task["ucf_enabled"]
        result["ucf_cardinals_1_0_enabled"] = task["ucf_cardinals_1_0_enabled"]

    for suite_row in suite_rows:
        task_results = [unique_results[i] for i in suite_row["task_indices"]]
        one_reporter = Reporter(Path(str(suite_row["one_step_out"])), echo=False)
        try:
            run_one_step_suite_eval.emit_one_step_suite_report(
                reporter=one_reporter,
                root=root,
                suite_arg=str(suite_row["suite_arg"]),
                suite_name=str(suite_row["suite_name"]),
                replay_paths=suite_row["replay_paths"],
                task_results=task_results,
                validation_profile=validation_profile,
                ucf_enabled=bool(suite_row["ucf_enabled"]),
                ucf_cardinals_1_0_enabled=bool(suite_row["ucf_cardinals_1_0_enabled"]),
                include_header=True,
            )
        finally:
            one_reporter.close()

        rollout_reporter = Reporter(Path(str(suite_row["rollout_out"])), echo=False)
        try:
            run_rollout_suite_eval.emit_rollout_suite_report(
                reporter=rollout_reporter,
                root=root,
                suite_arg=str(suite_row["suite_arg"]),
                suite_name=str(suite_row["suite_name"]),
                replay_paths=suite_row["replay_paths"],
                fields=fields,
                validation_profile=validation_profile,
                exceptions_path=exceptions_path,
                exceptions=exceptions,
                per_dataset_payload=[dict(result["rollout_payload"]) for result in task_results],
                players_csv=None,
                max_records=0,
                float_top=int(args.float_top),
                ucf_enabled=bool(suite_row["ucf_enabled"]),
                ucf_cardinals_1_0_enabled=bool(suite_row["ucf_cardinals_1_0_enabled"]),
                include_header=True,
            )
        finally:
            rollout_reporter.close()


if __name__ == "__main__":
    main()
