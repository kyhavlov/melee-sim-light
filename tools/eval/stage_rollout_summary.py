from __future__ import annotations

"""Stage-normalized rollout mismatch summary from existing validation reports."""

import argparse
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

from tools.slippi.suite_io import load_suite, repo_root


STAGE_NAMES = {
    2: "Fountain of Dreams",
    3: "Pokemon Stadium",
    8: "Yoshi's Story",
    28: "Dream Land N64",
    31: "Battlefield",
    32: "Final Destination",
}


@dataclass(frozen=True)
class ReplayRolloutMetrics:
    stem: str
    dataset: str
    first_total: int
    seeded_total: int
    status: str


@dataclass(frozen=True)
class StageRolloutSummary:
    stage_id: int
    stage: str
    replay_count: int
    records: int
    first_total: int
    seeded_total: int
    top_replay: str
    top_replay_first_total: int

    @property
    def first_per_1k(self) -> float:
        return (float(self.first_total) / float(self.records) * 1000.0) if self.records else 0.0


def _replay_stem(path: str | Path) -> str:
    name = Path(path).name
    for suffix in (".slpz", ".slp", ".slpz"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return Path(name).stem


def parse_rollout_report(path: Path) -> dict[str, ReplayRolloutMetrics]:
    out: dict[str, ReplayRolloutMetrics] = {}
    current: dict[str, object] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^== (.+\.(?:slpz|slp|msl)) ==$", line)
        if m:
            if current and "first_total" in current:
                metric = ReplayRolloutMetrics(
                    stem=str(current["stem"]),
                    dataset=str(current["dataset"]),
                    first_total=int(current["first_total"]),
                    seeded_total=int(current.get("seeded_total", 0)),
                    status=str(current.get("status", "")),
                )
                out[metric.stem] = metric
            dataset = m.group(1)
            current = {"dataset": dataset, "stem": _replay_stem(dataset)}
            continue
        if current is None:
            continue
        if line.startswith("rollout.status:"):
            current["status"] = line.split(":", 1)[1].strip()
        elif line.startswith("rollout.first_mismatch_total:"):
            current["first_total"] = int(line.split(":", 1)[1].strip())
        elif line.startswith("rollout.first_mismatch_seeded_total:"):
            current["seeded_total"] = int(line.split(":", 1)[1].strip())
    if current and "first_total" in current:
        metric = ReplayRolloutMetrics(
            stem=str(current["stem"]),
            dataset=str(current["dataset"]),
            first_total=int(current["first_total"]),
            seeded_total=int(current.get("seeded_total", 0)),
            status=str(current.get("status", "")),
        )
        out[metric.stem] = metric
    return out


def parse_record_counts(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    current: str | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^== (.+\.(?:slpz|slp|msl)) ==$", line)
        if m:
            current = _replay_stem(m.group(1))
            continue
        if current is not None and line.startswith("Records:"):
            out[current] = int(line.split()[1])
    return out


def summarize_by_stage(
    *,
    suite_path: Path,
    rollout_report: Path,
    one_step_report: Path,
    exclude_doubles: bool = False,
) -> list[StageRolloutSummary]:
    suite = load_suite(suite_path)
    rollout = parse_rollout_report(rollout_report)
    records = parse_record_counts(one_step_report)
    grouped: dict[int, dict[str, object]] = defaultdict(
        lambda: {"replays": 0, "records": 0, "first": 0, "seeded": 0, "top": ("", -1)}
    )

    for entry in suite.replays:
        stem = _replay_stem(entry.replay)
        metric = rollout.get(stem)
        if metric is None:
            continue
        replay_records = records.get(stem)
        if replay_records is None:
            continue
        if exclude_doubles and ("doubles" in metric.dataset.lower()):
            continue

        stage_id = int(entry.stage_id) if entry.stage_id is not None else -1
        group = grouped[stage_id]
        group["replays"] = int(group["replays"]) + 1
        group["records"] = int(group["records"]) + int(replay_records)
        group["first"] = int(group["first"]) + int(metric.first_total)
        group["seeded"] = int(group["seeded"]) + int(metric.seeded_total)
        top_name, top_first = group["top"]  # type: ignore[misc]
        if int(metric.first_total) > int(top_first):
            group["top"] = (stem, int(metric.first_total))

    summaries: list[StageRolloutSummary] = []
    for stage_id, group in grouped.items():
        top_name, top_first = group["top"]  # type: ignore[misc]
        summaries.append(
            StageRolloutSummary(
                stage_id=stage_id,
                stage=STAGE_NAMES.get(stage_id, f"stage {stage_id}"),
                replay_count=int(group["replays"]),
                records=int(group["records"]),
                first_total=int(group["first"]),
                seeded_total=int(group["seeded"]),
                top_replay=str(top_name),
                top_replay_first_total=int(top_first),
            )
        )
    return sorted(summaries, key=lambda row: (-row.first_per_1k, -row.first_total, row.stage))


def _print_table(rows: list[StageRolloutSummary]) -> None:
    print(
        "stage_id\tstage\treplay_count\trecords\tfirst_total\tseeded_total\t"
        "first_per_1k\ttop_replay\ttop_replay_first_total"
    )
    for row in rows:
        print(
            f"{row.stage_id}\t{row.stage}\t{row.replay_count}\t{row.records}\t"
            f"{row.first_total}\t{row.seeded_total}\t{row.first_per_1k:.3f}\t"
            f"{row.top_replay}\t{row.top_replay_first_total}"
        )


def main() -> None:
    root = repo_root()
    ap = argparse.ArgumentParser(
        description="Summarize rollout first mismatches normalized by stage and replay length."
    )
    ap.add_argument("--suite", default="replays/suites/aggregate_recent.json")
    ap.add_argument(
        "--rollout-report",
        default="reports/validation/aggregate_recent_rollout_suite_eval.txt",
        help="Generated rollout validation report to read.",
    )
    ap.add_argument(
        "--one-step-report",
        default="reports/validation/aggregate_recent_one_step_suite_eval.txt",
        help="Generated one-step report used for per-replay record counts.",
    )
    ap.add_argument(
        "--exclude-doubles",
        action="store_true",
        help="Skip datasets whose report path contains 'doubles'.",
    )
    args = ap.parse_args()

    rows = summarize_by_stage(
        suite_path=(root / args.suite).resolve(),
        rollout_report=(root / args.rollout_report).resolve(),
        one_step_report=(root / args.one_step_report).resolve(),
        exclude_doubles=bool(args.exclude_doubles),
    )
    _print_table(rows)


if __name__ == "__main__":
    main()
