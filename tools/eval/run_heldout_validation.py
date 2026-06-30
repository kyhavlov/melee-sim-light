from __future__ import annotations

"""Run held-out validation suites and write a normalized cross-suite summary."""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from tools.eval import run_combined_suite_eval
from tools.slippi.suite_io import load_suite, repo_root


STAGE_NAMES = {
    2: "fountain_of_dreams",
    3: "frozen_pokemon_stadium",
    8: "yoshis_story",
    28: "dream_land",
    31: "battlefield",
    32: "final_destination",
}

@dataclass(frozen=True)
class HeldoutReport:
    suite: str
    replay_count: int
    records: int
    scored_lanes: int
    one_step_discrete: int
    strict_discrete: int
    rollout_first: int
    rollout_seeded: int
    stage_coverage: str


def _run_main(name: str, main, args: list[str]) -> None:
    old_argv = sys.argv
    try:
        sys.argv = [name, *args]
        main()
    finally:
        sys.argv = old_argv


def _resolve_suite_worker_count(requested: int, task_count: int) -> int:
    if int(requested) > 0:
        return max(1, int(requested))
    return max(1, min(3, int(os.cpu_count() or 1), int(task_count) if task_count else 1))


def _run_subprocess(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "tools.eval.run_combined_suite_eval", *args],
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


def load_heldout_index(path: Path) -> list[Path]:
    data = json.loads(path.read_text())
    suites = data.get("suites") if isinstance(data, dict) else data
    if not isinstance(suites, list):
        raise ValueError(f"{path}: expected a list or object with a suites list")
    root = repo_root()
    out: list[Path] = []
    for entry in suites:
        suite = root / str(entry)
        if suite.name == "aggregate_recent.json":
            raise ValueError(f"{path}: held-out index must not reference aggregate_recent.json")
        out.append(suite)
    return out


def _last_pair(pattern: str, text: str, path: Path) -> tuple[int, int]:
    matches = re.findall(pattern, text, flags=re.MULTILINE)
    if not matches:
        raise ValueError(f"{path}: missing metric {pattern}")
    a, b = matches[-1]
    return int(a), int(b)


def _last_int(pattern: str, text: str, path: Path) -> int:
    matches = re.findall(pattern, text, flags=re.MULTILINE)
    if not matches:
        raise ValueError(f"{path}: missing metric {pattern}")
    return int(matches[-1])


def _records_from_one_step(text: str) -> int:
    return sum(int(value) for value in re.findall(r"^Records: (\d+)", text, flags=re.MULTILINE))


def _stage_coverage(suite_path: Path) -> str:
    suite = load_suite(suite_path)
    counts: dict[int, int] = {}
    for replay in suite.replays:
        stage_id = int(replay.stage_id or -1)
        counts[stage_id] = counts.get(stage_id, 0) + 1
    return ",".join(
        f"{STAGE_NAMES.get(stage_id, str(stage_id))}:{counts[stage_id]}"
        for stage_id in sorted(counts, key=lambda value: STAGE_NAMES.get(value, str(value)))
    )


def validate_heldout_corpus(suite_paths: list[Path]) -> None:
    root = repo_root()
    for suite_path in suite_paths:
        suite = load_suite(suite_path)
        for replay in suite.replays:
            replay_path = Path(replay.replay)
            if not replay_path.is_absolute():
                replay_path = root / replay_path
            if replay_path.exists():
                continue
            raise SystemExit(
                f"Missing held-out replay corpus file: {replay.replay}\n"
                "Held-out replay blobs are local ignored data. Restore/symlink/copy the local "
                "held-out corpus under replays/heldout/ before running make validate-heldout."
            )


def summarize_suite(suite_path: Path, one_step_path: Path, rollout_path: Path) -> HeldoutReport:
    suite = load_suite(suite_path)
    one_step = one_step_path.read_text()
    rollout = rollout_path.read_text()
    discrete, lanes = _last_pair(r"^overall\.discrete_mismatch: (\d+) / (\d+)", one_step, one_step_path)
    strict, _strict_lanes = _last_pair(
        r"^overall\.strict_discrete_mismatch: (\d+) / (\d+)", one_step, one_step_path
    )
    return HeldoutReport(
        suite=suite.name,
        replay_count=len(suite.replays),
        records=_records_from_one_step(one_step),
        scored_lanes=lanes,
        one_step_discrete=discrete,
        strict_discrete=strict,
        rollout_first=_last_int(r"^overall\.rollout\.first_mismatch_total: (\d+)", rollout, rollout_path),
        rollout_seeded=_last_int(
            r"^overall\.rollout\.first_mismatch_seeded_total: (\d+)", rollout, rollout_path
        ),
        stage_coverage=_stage_coverage(suite_path),
    )


def _report_paths(out_dir: Path, suite_name: str) -> tuple[Path, Path]:
    return out_dir / f"{suite_name}_one_step.txt", out_dir / f"{suite_name}_rollout.txt"


def suite_cli_arg(suite_path: Path) -> str:
    root = repo_root()
    try:
        return suite_path.relative_to(root).as_posix()
    except ValueError:
        return suite_path.as_posix()


def write_summary(rows: list[HeldoutReport], out: Path) -> str:
    header = (
        "suite\treplays\trecords\tscored_lanes\tone_step_discrete\tone_step_discrete_per_1k_lanes"
        "\tstrict_discrete\tstrict_discrete_per_1k_lanes\trollout_first\trollout_first_per_1k_records"
        "\trollout_seeded\trollout_seeded_per_1k_records\tstage_coverage"
    )
    lines = [
        "# Generated by run_heldout_validation. Held-out suites are generalization scorecards, not lock targets.",
        header,
    ]
    for row in rows:
        lane_scale = 1000.0 / row.scored_lanes if row.scored_lanes else 0.0
        record_scale = 1000.0 / row.records if row.records else 0.0
        lines.append(
            "\t".join(
                [
                    row.suite,
                    str(row.replay_count),
                    str(row.records),
                    str(row.scored_lanes),
                    str(row.one_step_discrete),
                    f"{row.one_step_discrete * lane_scale:.3f}",
                    str(row.strict_discrete),
                    f"{row.strict_discrete * lane_scale:.3f}",
                    str(row.rollout_first),
                    f"{row.rollout_first * record_scale:.3f}",
                    str(row.rollout_seeded),
                    f"{row.rollout_seeded * record_scale:.3f}",
                    row.stage_coverage,
                ]
            )
        )
    text = "\n".join(lines) + "\n"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    return text


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--index", default="replays/suites/heldout.json")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--out-dir", default="reports/validation/heldout")
    ap.add_argument("--summary-out", default="reports/validation/heldout/summary.txt")
    ap.add_argument("--summary-only", action="store_true")
    ap.add_argument("--workers", type=int, default=0)
    ap.add_argument(
        "--suite-workers",
        type=int,
        default=0,
        help="Parallel held-out suite workers (0 = auto, capped at 3).",
    )
    args = ap.parse_args()

    out_dir = repo_root() / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    suites = load_heldout_index(repo_root() / args.index)
    if not args.summary_only:
        validate_heldout_corpus(suites)
        suite_tasks: list[list[str]] = []
        for suite_path in suites:
            suite = load_suite(suite_path)
            one_step_out, rollout_out = _report_paths(out_dir, suite.name)
            suite_tasks.append(
                [
                    "--suite",
                    suite_cli_arg(suite_path),
                    "--chunk",
                    str(int(args.chunk)),
                    "--fields",
                    args.fields,
                    "--one-step-out",
                    str(one_step_out),
                    "--rollout-out",
                    str(rollout_out),
                    "--workers",
                    str(int(args.workers)),
                ]
            )
        suite_workers = _resolve_suite_worker_count(int(args.suite_workers), len(suite_tasks))
        if suite_workers == 1 or len(suite_tasks) <= 1:
            for task_args in suite_tasks:
                _run_main("run_combined_suite_eval", run_combined_suite_eval.main, task_args)
        else:
            results: list[subprocess.CompletedProcess[str] | None] = [None for _ in suite_tasks]
            with concurrent.futures.ThreadPoolExecutor(max_workers=suite_workers) as executor:
                future_to_index = {
                    executor.submit(_run_subprocess, task_args): i for i, task_args in enumerate(suite_tasks)
                }
                for future in concurrent.futures.as_completed(future_to_index):
                    results[future_to_index[future]] = future.result()
            for result in results:
                assert result is not None
                _replay_completed_process(result)
    rows: list[HeldoutReport] = []
    for suite_path in suites:
        suite = load_suite(suite_path)
        one_step_out, rollout_out = _report_paths(out_dir, suite.name)
        rows.append(summarize_suite(suite_path, one_step_out, rollout_out))

    print(write_summary(rows, repo_root() / args.summary_out), end="")


if __name__ == "__main__":
    main()
