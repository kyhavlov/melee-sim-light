from __future__ import annotations

"""Run one-step and rollout reports for a suite while building each replay once."""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval
from tools.eval.one_step_report import Reporter
from tools.eval.streaming_validation import (
    default_players,
    evaluate_validation_buffers,
    scan_validation_buffers_with_native_float_rows,
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


def _combined_dataset_task(task: dict, collect_stats: bool = False) -> dict:
    from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp

    if collect_stats:
        import time

        task_start = time.perf_counter()
        build_start = time.perf_counter()
    root = Path(str(task["root"]))
    dataset_label = Path(str(task["dataset_label"]))
    buffers = build_validation_buffers_from_slp(
        slp_path=str(task["slp_path"]),
        ports=[int(p) for p in task["ports"]],
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
    )
    if collect_stats:
        build_wall_s = time.perf_counter() - build_start

    records = int(buffers.num_records)
    num_players = int(buffers.num_players)
    one_capture = _CaptureReporter()
    if collect_stats:
        one_step_start = time.perf_counter()
    one_summary = evaluate_validation_buffers(
        dataset_path=dataset_label,
        buffers=buffers,
        chunk=int(task["chunk"]),
        profile=str(task["profile"]),
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
        reporter=one_capture,  # type: ignore[arg-type]
        print_profile=False,
    )
    if collect_stats:
        one_step_wall_s = time.perf_counter() - one_step_start

    players = default_players(buffers, task["players_csv"])
    float_top = int(task.get("float_top_scan", 0))
    rollout_dataset_label = _display_path(dataset_label, root=root)
    if collect_stats:
        rollout_start = time.perf_counter()
    exception_probe_limit = int(task.get("exception_probe_limit", 0))
    if exception_probe_limit > 0 and int(task["max_records"]) > 0:
        exception_probe_limit = min(exception_probe_limit, int(task["max_records"]))
    streaks, float_rows, first_rows = scan_validation_buffers_with_native_float_rows(
        dataset_path=dataset_label,
        buffers=buffers,
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
        first_mismatch_probe_limit=exception_probe_limit,
    )
    result = {
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
    if collect_stats:
        rollout_wall_s = time.perf_counter() - rollout_start
        total_wall_s = time.perf_counter() - task_start
        result["_stats"] = {
            "dataset": rollout_dataset_label,
            "records": records,
            "build_wall_s": build_wall_s,
            "one_step_wall_s": one_step_wall_s,
            "rollout_wall_s": rollout_wall_s,
            "total_wall_s": total_wall_s,
        }
    return result


def _task_schedule_weight(task: dict) -> int:
    try:
        return max(1, Path(str(task["slp_path"])).stat().st_size)
    except OSError:
        return 1


def _partition_indexed_tasks(tasks: list[dict], workers: int) -> list[list[tuple[int, dict]]]:
    worker_count = max(1, int(workers))
    # At high worker counts a strict one-packet-per-worker split leaves cores idle behind the
    # slowest packet. Use a small number of deterministic coarse packets so process setup remains
    # amortized but the executor can rebalance the tail.
    packet_count = worker_count * 4 if worker_count >= 4 else worker_count
    shard_count = max(1, min(packet_count, len(tasks) if tasks else 1))
    shards: list[list[tuple[int, dict]]] = [[] for _ in range(shard_count)]
    shard_weights = [0] * shard_count
    indexed_tasks = list(enumerate(tasks))
    indexed_tasks.sort(key=lambda item: (-_task_schedule_weight(item[1]), item[0]))
    for idx, task in indexed_tasks:
        shard_idx = min(range(shard_count), key=lambda i: (shard_weights[i], i))
        shards[shard_idx].append((idx, task))
        shard_weights[shard_idx] += _task_schedule_weight(task)
    for shard in shards:
        shard.sort(key=lambda item: item[0])
    return [shard for shard in shards if shard]


def _combined_dataset_shard(shard: list[tuple[int, dict]], collect_stats: bool = False) -> dict:
    if collect_stats:
        import pickle
        import time

        init_start = time.perf_counter()
    import msl_binding  # type: ignore
    from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp  # noqa: F401

    # Touch the native module once per worker so init/import cost is charged to the shard, not a
    # particular replay.
    msl_binding.sizes()
    if collect_stats:
        init_wall_s = time.perf_counter() - init_start
        shard_start = time.perf_counter()
    indexed_results: list[tuple[int, dict]] = []
    build_wall_s = 0.0
    one_step_wall_s = 0.0
    rollout_wall_s = 0.0
    total_task_wall_s = 0.0
    records = 0
    schedule_weight = 0
    for idx, task in shard:
        result = _combined_dataset_task(task, collect_stats=collect_stats)
        if collect_stats:
            schedule_weight += _task_schedule_weight(task)
            stats = result.get("_stats", {})
            build_wall_s += float(stats.get("build_wall_s", 0.0))
            one_step_wall_s += float(stats.get("one_step_wall_s", 0.0))
            rollout_wall_s += float(stats.get("rollout_wall_s", 0.0))
            total_task_wall_s += float(stats.get("total_wall_s", 0.0))
            records += int(stats.get("records", 0))
        indexed_results.append((idx, result))
    payload = {"results": indexed_results}
    if collect_stats:
        shard_wall_s = time.perf_counter() - shard_start
        result_bytes = len(pickle.dumps(indexed_results, protocol=pickle.HIGHEST_PROTOCOL))
        payload["stats"] = {
            "pid": os.getpid(),
            "tasks": len(shard),
            "records": records,
            "schedule_weight": schedule_weight,
            "init_wall_s": init_wall_s,
            "build_wall_s": build_wall_s,
            "one_step_wall_s": one_step_wall_s,
            "rollout_wall_s": rollout_wall_s,
            "task_wall_s": total_task_wall_s,
            "shard_wall_s": shard_wall_s,
            "result_bytes": result_bytes,
        }
    return payload


def run_combined_dataset_tasks(
    tasks: list[dict], *, workers: int, collect_stats: bool = False
) -> tuple[list[dict], list[dict]]:
    if not tasks:
        return [], []
    from tools.slippi.validation_buffer_builder import warm_validation_generated_data_cache

    root = Path(str(tasks[0].get("root", repo_root())))
    stage_ids = tuple(sorted({int(t.get("stage_id", 0)) for t in tasks if int(t.get("stage_id", 0)) > 0}))
    warm_validation_generated_data_cache(data_root=root / "data", stage_ids=stage_ids)
    import msl_binding  # type: ignore

    for num_players in sorted({int(t.get("num_players", len(t.get("ports", ()))) or 2) for t in tasks}):
        handle = msl_binding.init(batch_size=1, num_players=num_players)
        msl_binding.destroy(handle)
    shards = _partition_indexed_tasks(tasks, workers)
    if int(workers) == 1 or len(shards) <= 1:
        shard_payloads = [_combined_dataset_shard(shards[0], collect_stats)]
    else:
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(int(workers), len(shards))) as executor:
            shard_payloads = list(executor.map(_combined_dataset_shard, shards, [collect_stats] * len(shards)))
    results: list[dict | None] = [None] * len(tasks)
    stats: list[dict] = []
    for shard_index, payload in enumerate(shard_payloads):
        if collect_stats:
            shard_stats = dict(payload["stats"])
            shard_stats["shard_index"] = shard_index
            stats.append(shard_stats)
        for idx, result in payload["results"]:
            results[int(idx)] = result
    return [r for r in results if r is not None], stats


def write_shard_stats(path: Path, *, workers: int, task_count: int, stats: list[dict]) -> None:
    worker_stats: dict[int, dict[str, float | int]] = {}
    for shard in stats:
        pid = int(shard.get("pid", -1))
        worker = worker_stats.setdefault(
            pid,
            {
                "pid": pid,
                "packets": 0,
                "tasks": 0,
                "records": 0,
                "init_wall_s": 0.0,
                "build_wall_s": 0.0,
                "one_step_wall_s": 0.0,
                "rollout_wall_s": 0.0,
                "task_wall_s": 0.0,
                "worker_wall_s": 0.0,
                "result_bytes": 0,
            },
        )
        worker["packets"] = int(worker["packets"]) + 1
        worker["tasks"] = int(worker["tasks"]) + int(shard.get("tasks", 0))
        worker["records"] = int(worker["records"]) + int(shard.get("records", 0))
        worker["init_wall_s"] = float(worker["init_wall_s"]) + float(shard.get("init_wall_s", 0.0))
        worker["build_wall_s"] = float(worker["build_wall_s"]) + float(shard.get("build_wall_s", 0.0))
        worker["one_step_wall_s"] = float(worker["one_step_wall_s"]) + float(
            shard.get("one_step_wall_s", 0.0)
        )
        worker["rollout_wall_s"] = float(worker["rollout_wall_s"]) + float(
            shard.get("rollout_wall_s", 0.0)
        )
        worker["task_wall_s"] = float(worker["task_wall_s"]) + float(shard.get("task_wall_s", 0.0))
        worker["worker_wall_s"] = float(worker["worker_wall_s"]) + float(shard.get("shard_wall_s", 0.0))
        worker["result_bytes"] = int(worker["result_bytes"]) + int(shard.get("result_bytes", 0))
    workers_observed = sorted(worker_stats.values(), key=lambda s: (-float(s["worker_wall_s"]), int(s["pid"])))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "workers": int(workers),
                "task_count": int(task_count),
                "critical_path_worker_wall_s": max(
                    (float(s.get("worker_wall_s", 0.0)) for s in workers_observed), default=0.0
                ),
                "critical_path_shard_wall_s": max(
                    (float(s.get("init_wall_s", 0.0)) + float(s.get("shard_wall_s", 0.0)) for s in stats),
                    default=0.0,
                ),
                "worker_stats": workers_observed,
                "shards": stats,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--chunk", type=int, default=64)
    ap.add_argument("--players", default=None)
    ap.add_argument("--max-records", type=int, default=0)
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--one-step-out", type=Path, required=True)
    ap.add_argument("--rollout-out", type=Path, required=True)
    ap.add_argument("--workers", type=int, default=0)
    ap.add_argument("--exceptions", type=Path, default=Path("replays/validation_exceptions.json"))
    ap.add_argument("--float-top", type=int, default=3)
    ap.add_argument("--stats-out", type=Path, default=None)
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    fields = run_rollout_suite_eval.standard_rollout_fields()
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
            "stage_id": int(entry.stage_id) if entry.stage_id is not None else 0,
            "chunk": int(args.chunk),
            "profile": validation_profile.name,
            "ucf_enabled": bool(suite.ucf_enabled),
            "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
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
    task_results, shard_stats = run_combined_dataset_tasks(
        tasks, workers=workers, collect_stats=args.stats_out is not None
    )
    if args.stats_out is not None:
        write_shard_stats(Path(args.stats_out), workers=workers, task_count=len(tasks), stats=shard_stats)
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
