from __future__ import annotations

import argparse
import json
import time
from collections import Counter
from pathlib import Path
from typing import Any

import numpy as np

from tools.eval.streaming_validation import default_players
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import SuiteReplay, load_suite, repo_root
from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp


SUPPORTED_CHARACTERS = ("Fox", "Falco", "Marth", "Captain Falcon", "Sheik", "Zelda")


def _selected_entries(entries: tuple[SuiteReplay, ...], per_character: int) -> list[SuiteReplay]:
    if per_character <= 0:
        return list(entries)
    counts: Counter[str] = Counter()
    selected: list[SuiteReplay] = []
    for entry in entries:
        characters = set((entry.characters or {}).values()) & set(SUPPORTED_CHARACTERS)
        if not any(counts[name] < per_character for name in characters):
            continue
        selected.append(entry)
        counts.update(characters)
        if all(counts[name] >= per_character for name in SUPPORTED_CHARACTERS):
            break
    missing = [name for name in SUPPORTED_CHARACTERS if counts[name] < per_character]
    if missing:
        raise ValueError(f"suite cannot provide {per_character} replay(s) for: {', '.join(missing)}")
    return selected


def _percentile(sorted_ns: np.ndarray, quantile: float) -> int:
    index = max(0, min(len(sorted_ns) - 1, int(np.ceil(quantile * len(sorted_ns))) - 1))
    return int(sorted_ns[index])


def _timing_summary(events: list[dict[str, Any]], *, top: int) -> dict[str, Any]:
    durations = np.asarray([int(event["step_ns"]) for event in events], dtype=np.uint64)
    sorted_ns = np.sort(durations)
    total_ns = int(durations.sum(dtype=np.uint64))
    average_ns = total_ns / len(events)
    ranked = sorted(events, key=lambda event: int(event["step_ns"]), reverse=True)
    return {
        "steps": len(events),
        "total_step_ns": total_ns,
        "single_core_fps": len(events) * 1_000_000_000.0 / total_ns,
        "avg_ns": average_ns,
        "p50_ns": _percentile(sorted_ns, 0.50),
        "p95_ns": _percentile(sorted_ns, 0.95),
        "p99_ns": _percentile(sorted_ns, 0.99),
        "p999_ns": _percentile(sorted_ns, 0.999),
        "max_ns": int(sorted_ns[-1]),
        "p99_over_avg": _percentile(sorted_ns, 0.99) / average_ns,
        "max_over_avg": int(sorted_ns[-1]) / average_ns,
        "top": ranked[: max(0, int(top))],
    }


def _profile_replay(
    *,
    suite_name: str,
    entry: SuiteReplay,
    start_record: int,
    max_records: int,
    repeat: int,
    top: int,
    ucf_enabled: bool,
    ucf_cardinals_enabled: bool,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    import msl_binding

    root = repo_root()
    replay = Path(entry.replay)
    replay_path = replay if replay.is_absolute() else root / replay
    replay_path = resolve_replay_path(replay_path.resolve())
    buffers = build_validation_buffers_from_slp(
        slp_path=str(replay_path),
        ports=[int(port) for port in entry.ports],
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_enabled,
    )
    records = int(buffers.num_records)
    start_record = min(start_record, records)
    stop_record = records if max_records <= 0 else min(records, start_record + max_records)
    if stop_record <= start_record:
        raise ValueError(f"{entry.replay}: selected validation record range is empty")

    seed_u8 = buffers.seed_u8()
    prev_u8 = buffers.prev_input_u8()
    input_u8 = buffers.input_u8()
    ref_u8 = buffers.ref_u8()
    players = np.asarray(default_players(buffers), dtype=np.uint8)
    sizes = msl_binding.sizes()
    seed = np.empty((1, int(sizes["seed"])), dtype=np.uint8)
    prev_input = np.empty((1, int(sizes["input"])), dtype=np.uint8)
    input_now = np.empty((1, int(sizes["input"])), dtype=np.uint8)
    out = np.empty((1, int(sizes["compare"])), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=1,
        num_players=int(buffers.num_players),
        ucf_enabled=int(ucf_enabled),
        ucf_cardinals_1_0_enabled=int(ucf_cardinals_enabled),
    )
    events: list[dict[str, Any]] = []

    def reseed_at(record: int) -> None:
        seed[0] = seed_u8[record]
        msl_binding.reseed_seed_rollout(handle, seed)

    def attempt(record: int, *, retry: bool, repeat_index: int, collect: bool) -> int:
        seed[0] = seed_u8[record]
        prev_input[0] = prev_u8[record]
        input_now[0] = input_u8[record]
        start_ns = time.perf_counter_ns()
        msl_binding.step_input_replay_frame_rng(handle, seed, prev_input, input_now)
        elapsed_ns = time.perf_counter_ns() - start_ns
        msl_binding.write_compare(handle, out)
        code = int(
            msl_binding.standard_rollout_compare(out, ref_u8[record : record + 1], players, 1)
        )
        if collect:
            events.append(
                {
                    "suite": suite_name,
                    "replay": entry.replay,
                    "record": record,
                    "seed_frame_id": int(buffers.seed_t["frame_id"][record]),
                    "char_id": [int(value) for value in buffers.seed_t["char_id"][record]],
                    "action_id": [int(value) for value in buffers.seed_t["action_id"][record]],
                    "retry": retry,
                    "repeat": repeat_index,
                    "step_ns": elapsed_ns,
                }
            )
        return code & 0xFF

    try:
        reseed_at(start_record)
        attempt(start_record, retry=False, repeat_index=-1, collect=False)
        for repeat_index in range(repeat):
            current_start = start_record
            needs_seed = True
            for record in range(start_record, stop_record):
                if needs_seed:
                    reseed_at(current_start)
                    needs_seed = False
                code = attempt(record, retry=False, repeat_index=repeat_index, collect=True)
                if code == 0:
                    continue
                current_start = record
                reseed_at(record)
                retry_code = attempt(record, retry=True, repeat_index=repeat_index, collect=True)
                if retry_code == 0:
                    continue
                current_start = record + 1
                needs_seed = True
    finally:
        msl_binding.destroy(handle)

    summary = _timing_summary(events, top=top)
    summary.update(
        {
            "suite": suite_name,
            "replay": entry.replay,
            "characters": entry.characters or {},
            "records_available": records,
            "start_record": start_record,
            "records_profiled": stop_record - start_record,
            "repeat": repeat,
        }
    )
    return summary, events


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Profile native sim-step latency during standard aggregate replay rollout."
    )
    parser.add_argument("--suite", default="replays/suites/aggregate_recent.json")
    parser.add_argument(
        "--replay",
        action="append",
        help="Profile only aggregate entries whose replay path contains this text; may be repeated.",
    )
    parser.add_argument(
        "--per-character",
        type=int,
        default=1,
        help="Greedily select N aggregate replays containing each supported character; 0 profiles all.",
    )
    parser.add_argument("--max-records", type=int, default=3000)
    parser.add_argument("--start-record", type=int, default=0)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("reports/triage/sim_performance/validation_rollout_frames.json"),
    )
    args = parser.parse_args()
    if args.max_records < 0 or args.start_record < 0 or args.repeat <= 0 or args.per_character < 0:
        parser.error("max-records/per-character must be non-negative and repeat must be positive")

    suite = load_suite(args.suite)
    if args.replay:
        entries = [
            entry
            for entry in suite.replays
            if any(text in entry.replay for text in args.replay)
        ]
        if not entries:
            parser.error("no suite replay matched --replay")
    else:
        entries = _selected_entries(suite.replays, int(args.per_character))
    case_summaries: list[dict[str, Any]] = []
    all_events: list[dict[str, Any]] = []
    for entry in entries:
        summary, events = _profile_replay(
            suite_name=suite.name,
            entry=entry,
            start_record=int(args.start_record),
            max_records=int(args.max_records),
            repeat=int(args.repeat),
            top=int(args.top),
            ucf_enabled=bool(suite.ucf_enabled),
            ucf_cardinals_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        )
        case_summaries.append(summary)
        all_events.extend(events)

    aggregate = _timing_summary(all_events, top=int(args.top))
    aggregate["replays"] = len(case_summaries)
    aggregate["records_profiled"] = sum(int(row["records_profiled"]) for row in case_summaries)
    payload = {"aggregate": aggregate, "replays": case_summaries}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"aggregate": aggregate, "out": str(args.out)}, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
