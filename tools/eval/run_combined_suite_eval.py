from __future__ import annotations

"""Run one-step and rollout reports for a suite while building each replay once."""

import argparse
import concurrent.futures
import os
from dataclasses import asdict
from pathlib import Path

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval
from tools.eval.run_longest_rollout_streaks import (
    _parse_csv,
    _parse_players,
    _scan_dataset_streaks_with_native_float_rows,
    _validate_discrete_fields,
)
from tools.eval.run_one_step_eval import (
    Reporter,
    create_one_step_eval_runtime,
    evaluate_dataset,
)
from tools.eval.validation_exceptions import load_validation_exceptions
from tools.eval.validation_profile import get_validation_profile, validation_profile_names
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import load_suite, repo_root


class _CaptureReporter:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def print(self, *args) -> None:
        self.lines.append(" ".join(str(a) for a in args))

    def close(self) -> None:
        return None


def _resolve_worker_count(requested: int, task_count: int) -> int:
    if int(requested) > 0:
        return max(1, int(requested))
    return max(1, min(8, int(os.cpu_count() or 1), int(task_count) if task_count else 1))


def _display_path(path: Path, *, root: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(root))
    except ValueError:
        return str(resolved)


def _combined_dataset_task(task: dict) -> dict:
    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    root = Path(str(task["root"]))
    dataset_label = Path(str(task["dataset_label"]))
    ds = build_dataset_from_slp(
        slp_path=str(task["slp_path"]),
        ports=[int(p) for p in task["ports"]],
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
    )

    records = int(ds.header["num_records"])
    num_players = int(ds.header["num_players"])
    runtime = create_one_step_eval_runtime(
        batch_size=max(1, min(int(task["chunk"]), max(1, records))),
        num_players=num_players,
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
    )
    one_capture = _CaptureReporter()
    try:
        one_summary = evaluate_dataset(
            dataset_path=dataset_label,
            dataset=ds,
            chunk=int(task["chunk"]),
            runtime=runtime,
            profile=str(task["profile"]),
            ucf_enabled=bool(task["ucf_enabled"]),
            ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
            reporter=one_capture,  # type: ignore[arg-type]
            print_profile=False,
            debug_mismatch=tuple(task["debug_mismatch"]),
            debug_limit=int(task["debug_limit"]),
            debug_float=tuple(task["debug_float"]),
            debug_float_limit=int(task["debug_float_limit"]),
        )
    finally:
        runtime.close()

    players = _parse_players(task["players_csv"], num_players=num_players)
    float_top = int(task.get("float_top_scan", 0))
    rollout_dataset_label = _display_path(dataset_label, root=root)
    streaks, float_rows = _scan_dataset_streaks_with_native_float_rows(
        dataset_path=dataset_label,
        ds=ds,
        fields=tuple(task["fields"]),
        players=players,
        max_records=int(task["max_records"]),
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
        profile=str(task["profile"]),
        float_fields=tuple(task["float_fields"]) if float_top > 0 else (),
        float_top=float_top,
        float_threshold=0.0,
        float_dataset_label=rollout_dataset_label,
    )
    first_rows: list[dict] = []
    exception_probe_limit = int(task.get("exception_probe_limit", 0))
    if exception_probe_limit > 0:
        max_records = int(task["max_records"])
        if max_records > 0:
            exception_probe_limit = min(exception_probe_limit, max_records)
        first_rows = [
            asdict(row)
            for row in run_rollout_suite_eval._locate_dataset_rollout_desyncs(
                dataset_path=dataset_label,
                dataset_label=rollout_dataset_label,
                ds=ds,
                fields=tuple(task["fields"]),
                players=players,
                max_records=exception_probe_limit,
                row_limit=None,
                ucf_enabled=bool(task["ucf_enabled"]),
                ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
                profile=str(task["profile"]),
            )
        ]
    return {
        "one_step_lines": one_capture.lines,
        "one_step_summary": one_summary,
        "rollout_payload": {
            "dataset": rollout_dataset_label,
            "num_records": streaks.num_records,
            "max_records_used": streaks.max_records_used,
            "players": list(streaks.players),
            "best_len": streaks.best_len,
            "best_start_record": streaks.best_start_record,
            "best_end_record_excl": streaks.best_end_record_excl,
            "best_start_seed_frame_id": streaks.best_start_seed_frame_id,
            "best_end_ref_frame_id_inclusive": streaks.best_end_ref_frame_id_inclusive,
            "streak_histogram": streaks.streak_histogram,
            "first_mismatch_field_counts": streaks.first_mismatch_field_counts,
            "first_mismatch_field_counts_seeded": streaks.first_mismatch_field_counts_seeded,
            "ignored_first_mismatch_field_counts": streaks.ignored_first_mismatch_field_counts,
            "ignored_first_mismatch_field_counts_seeded": streaks.ignored_first_mismatch_field_counts_seeded,
            "first_mismatch_rows": first_rows,
            "float_rows": float_rows,
        },
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--players", default=None)
    ap.add_argument("--max-records", type=int, default=0)
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--one-step-out", type=Path, required=True)
    ap.add_argument("--rollout-out", type=Path, required=True)
    ap.add_argument("--workers", type=int, default=0)
    ap.add_argument("--exceptions", type=Path, default=Path("replays/validation_exceptions.json"))
    ap.add_argument("--float-top", type=int, default=3)
    ap.add_argument("--debug-mismatch", default="")
    ap.add_argument("--debug-limit", type=int, default=10)
    ap.add_argument("--debug-float", default="")
    ap.add_argument("--debug-float-limit", type=int, default=10)
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    fields = _validate_discrete_fields(_parse_csv(str(args.fields)))
    validation_profile = get_validation_profile(args.profile)
    exceptions_path = (root / args.exceptions).resolve()
    exceptions = load_validation_exceptions(exceptions_path)
    float_fields = run_rollout_suite_eval._float_compare_fields()
    float_top_scan = max(0, int(args.float_top)) * 8
    replay_paths = [resolve_replay_path((root / entry.replay).resolve()) for entry in suite.replays]
    tasks = [
        {
            "root": str(root),
            "dataset_label": str(replay_path),
            "slp_path": str(replay_path),
            "ports": [int(p) for p in entry.ports],
            "chunk": int(args.chunk),
            "profile": validation_profile.name,
            "ucf_enabled": bool(suite.ucf_enabled),
            "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
            "debug_mismatch": tuple(s.strip() for s in args.debug_mismatch.split(",") if s.strip()),
            "debug_limit": int(args.debug_limit),
            "debug_float": tuple(s.strip() for s in args.debug_float.split(",") if s.strip()),
            "debug_float_limit": int(args.debug_float_limit),
            "fields": list(fields),
            "players_csv": args.players,
            "max_records": int(args.max_records),
            "float_fields": list(float_fields),
            "float_top_scan": int(float_top_scan),
            "exception_probe_limit": run_rollout_suite_eval._exception_probe_limit(
                dataset=_display_path(replay_path, root=root), exceptions=exceptions
            ),
        }
        for replay_path, entry in zip(replay_paths, suite.replays, strict=True)
    ]
    workers = _resolve_worker_count(int(args.workers), len(tasks))
    if workers == 1 or len(tasks) <= 1:
        task_results = [_combined_dataset_task(task) for task in tasks]
    else:
        with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as executor:
            task_results = list(executor.map(_combined_dataset_task, tasks))
    for result, task in zip(task_results, tasks, strict=True):
        result["ucf_enabled"] = task["ucf_enabled"]
        result["ucf_cardinals_1_0_enabled"] = task["ucf_cardinals_1_0_enabled"]

    one_reporter = Reporter(args.one_step_out, echo=False)
    try:
        run_one_step_suite_eval.emit_one_step_suite_report(
            reporter=one_reporter,
            root=root,
            suite_arg=args.suite,
            suite_name=suite.name,
            replay_paths=replay_paths,
            task_results=task_results,
            validation_profile=validation_profile,
            ucf_enabled=suite.ucf_enabled,
            ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            include_header=True,
        )
    finally:
        one_reporter.close()

    rollout_reporter = Reporter(args.rollout_out, echo=False)
    try:
        run_rollout_suite_eval.emit_rollout_suite_report(
            reporter=rollout_reporter,
            root=root,
            suite_arg=args.suite,
            suite_name=suite.name,
            replay_paths=replay_paths,
            fields=fields,
            validation_profile=validation_profile,
            exceptions_path=exceptions_path,
            exceptions=exceptions,
            per_dataset_payload=[dict(result["rollout_payload"]) for result in task_results],
            players_csv=None if args.players is None else str(args.players),
            max_records=int(args.max_records),
            float_top=int(args.float_top),
            ucf_enabled=suite.ucf_enabled,
            ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            include_header=True,
        )
    finally:
        rollout_reporter.close()


if __name__ == "__main__":
    main()
