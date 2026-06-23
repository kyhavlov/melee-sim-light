from __future__ import annotations

"""Run held-out validation suites and write a normalized cross-suite summary."""

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from tools.eval import run_one_step_suite_eval, run_rollout_suite_eval
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
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--fields", default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags")
    ap.add_argument("--out-dir", default="reports/validation/heldout")
    ap.add_argument("--summary-out", default="reports/validation/heldout/summary.txt")
    ap.add_argument("--summary-only", action="store_true")
    args = ap.parse_args()

    out_dir = repo_root() / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    suites = load_heldout_index(repo_root() / args.index)
    if not args.summary_only:
        validate_heldout_corpus(suites)
    rows: list[HeldoutReport] = []
    for suite_path in suites:
        suite = load_suite(suite_path)
        suite_arg = suite_cli_arg(suite_path)
        one_step_out, rollout_out = _report_paths(out_dir, suite.name)
        if not args.summary_only:
            _run_main(
                "run_one_step_suite_eval",
                run_one_step_suite_eval.main,
                [
                    "--suite",
                    suite_arg,
                    "--datasets-dir",
                    args.datasets_dir,
                    "--chunk",
                    str(int(args.chunk)),
                    "--out",
                    str(one_step_out),
                    "--quiet",
                ],
            )
            _run_main(
                "run_rollout_suite_eval",
                run_rollout_suite_eval.main,
                [
                    "--suite",
                    suite_arg,
                    "--datasets-dir",
                    args.datasets_dir,
                    "--fields",
                    args.fields,
                    "--out",
                    str(rollout_out),
                    "--quiet",
                ],
            )
        rows.append(summarize_suite(suite_path, one_step_out, rollout_out))

    print(write_summary(rows, repo_root() / args.summary_out), end="")


if __name__ == "__main__":
    main()
