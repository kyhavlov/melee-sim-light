from __future__ import annotations

import argparse
import cProfile
import json
import pstats
import time
from dataclasses import asdict
from pathlib import Path
from typing import Any

import numpy as np

from tools.eval.one_step_report import Reporter
from tools.eval.run_heldout_validation import load_heldout_index
from tools.eval.run_rollout_suite_eval import _float_compare_fields, standard_rollout_fields
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.eval.streaming_validation import (
    default_players,
    evaluate_validation_buffers,
    scan_validation_buffers_with_native_float_rows,
)
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import SuiteReplay, load_suite, repo_root
from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp


DEFAULT_CASES = (
    ("normal_fd", "replays/suites/fox_falco_fd_ucf084_recent.json", "AttachedGoodNaturedGuanaco"),
    ("sheik", "replays/suites/sheik.json", "StiffLustrousZebra"),
    ("dream_land", "replays/suites/dream_land_recent.json", "FlippantEnchantedHorse"),
)


class CaptureReporter(Reporter):
    def __init__(self) -> None:
        super().__init__(None, echo=False)
        self.lines: list[str] = []

    def print(self, *args) -> None:
        self.lines.append(" ".join(str(a) for a in args))


def _case_from_entry(suite_path: Path, entry: SuiteReplay) -> dict[str, Any]:
    root = repo_root()
    suite = load_suite(suite_path)
    replay = Path(entry.replay)
    replay_path = replay if replay.is_absolute() else root / replay
    return {
        "suite_path": suite_path.relative_to(root).as_posix()
        if suite_path.is_absolute() and suite_path.is_relative_to(root)
        else suite_path.as_posix(),
        "suite_name": suite.name,
        "replay": str(resolve_replay_path(replay_path.resolve())),
        "replay_label": replay.as_posix(),
        "ports": [int(p) for p in entry.ports],
        "stage_id": entry.stage_id,
        "characters": entry.characters or {},
        "ucf_enabled": bool(suite.ucf_enabled),
        "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
    }


def _cases_from_suite(suite_path: Path) -> list[dict[str, Any]]:
    suite = load_suite(suite_path)
    return [_case_from_entry(suite_path, entry) for entry in suite.replays]


def _default_cases() -> list[dict[str, Any]]:
    root = repo_root()
    out: list[dict[str, Any]] = []
    for label, suite_rel, replay_key in DEFAULT_CASES:
        suite_path = root / suite_rel
        suite = load_suite(suite_path)
        for entry in suite.replays:
            if replay_key in Path(entry.replay).stem:
                case = _case_from_entry(suite_path, entry)
                case["case"] = label
                out.append(case)
                break
        else:
            raise SystemExit(f"case replay {replay_key!r} not found in {suite_rel}")
    return out


def _collect_cases(args: argparse.Namespace) -> list[dict[str, Any]]:
    root = repo_root()
    cases: list[dict[str, Any]] = []
    for suite_arg in args.suite or ():
        suite_path = Path(suite_arg)
        if not suite_path.is_absolute():
            suite_path = root / suite_path
        cases.extend(_cases_from_suite(suite_path))
    for index_arg in args.heldout_index or ():
        index_path = Path(index_arg)
        if not index_path.is_absolute():
            index_path = root / index_path
        for suite_path in load_heldout_index(index_path):
            cases.extend(_cases_from_suite(suite_path))
    if not cases:
        cases = _default_cases()
    if args.limit is not None:
        cases = cases[: max(0, int(args.limit))]
    return cases


def _probe_one_step_native_phases(
    buffers, *, chunk: int, ucf_enabled: bool, ucf_cardinals_1_0_enabled: bool
) -> dict[str, float]:
    import msl_binding  # type: ignore

    records = int(buffers.num_records)
    capacity = max(1, min(int(chunk), max(1, records)))
    timings = {
        "one_step_native_init_wall_s": 0.0,
        "one_step_native_reseed_wall_s": 0.0,
        "one_step_native_step_wall_s": 0.0,
        "one_step_native_write_wall_s": 0.0,
    }
    start = time.perf_counter()
    handle = msl_binding.init(
        batch_size=capacity,
        num_players=int(buffers.num_players),
        ucf_enabled=int(bool(ucf_enabled)),
        ucf_cardinals_1_0_enabled=int(bool(ucf_cardinals_1_0_enabled)),
    )
    timings["one_step_native_init_wall_s"] = time.perf_counter() - start
    seed_u8 = buffers.seed_u8()
    prev_u8 = buffers.prev_input_u8()
    input_u8 = buffers.input_u8()
    out = np.zeros(capacity, dtype=COMPARE_DTYPE).view(np.uint8).reshape(capacity, -1)
    seed_tail = np.empty((capacity, seed_u8.shape[1]), dtype=np.uint8)
    prev_tail = np.empty((capacity, prev_u8.shape[1]), dtype=np.uint8)
    input_tail = np.empty((capacity, input_u8.shape[1]), dtype=np.uint8)
    try:
        for offset in range(0, records, capacity):
            end = min(records, offset + capacity)
            n = end - offset
            if n == capacity:
                seed_chunk = seed_u8[offset:end]
                prev_chunk = prev_u8[offset:end]
                input_chunk = input_u8[offset:end]
            else:
                seed_tail[:n] = seed_u8[offset:end]
                prev_tail[:n] = prev_u8[offset:end]
                input_tail[:n] = input_u8[offset:end]
                seed_tail[n:] = seed_tail[0]
                prev_tail[n:] = prev_tail[0]
                input_tail[n:] = input_tail[0]
                seed_chunk = seed_tail
                prev_chunk = prev_tail
                input_chunk = input_tail
            start = time.perf_counter()
            msl_binding.reseed_seed(handle, seed_chunk)
            timings["one_step_native_reseed_wall_s"] += time.perf_counter() - start
            start = time.perf_counter()
            msl_binding.step_input(handle, prev_chunk, input_chunk)
            timings["one_step_native_step_wall_s"] += time.perf_counter() - start
            start = time.perf_counter()
            msl_binding.write_compare(handle, out)
            timings["one_step_native_write_wall_s"] += time.perf_counter() - start
    finally:
        msl_binding.destroy(handle)
    return timings


def _run_case(
    case: dict[str, Any], *, chunk: int, float_top: int, max_records: int, native_phase_probe: bool
) -> dict[str, Any]:
    timings: dict[str, float] = {}

    start = time.perf_counter()
    buffers = build_validation_buffers_from_slp(
        slp_path=str(case["replay"]),
        ports=[int(p) for p in case["ports"]],
        ucf_enabled=bool(case["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
    )
    timings["build_wall_s"] = time.perf_counter() - start
    if native_phase_probe:
        timings.update(
            _probe_one_step_native_phases(
                buffers,
                chunk=chunk,
                ucf_enabled=bool(case["ucf_enabled"]),
                ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
            )
        )

    reporter = CaptureReporter()
    start = time.perf_counter()
    one_summary = evaluate_validation_buffers(
        dataset_path=Path(str(case["replay"])),
        buffers=buffers,
        chunk=chunk,
        profile="rl1_gameplay",
        ucf_enabled=bool(case["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
        reporter=reporter,
        print_profile=False,
    )
    timings["one_step_wall_s"] = time.perf_counter() - start

    start = time.perf_counter()
    players = default_players(buffers, None)
    rollout, float_errors, float_downstream = scan_validation_buffers_with_native_float_rows(
        dataset_path=Path(str(case["replay"])),
        buffers=buffers,
        fields=standard_rollout_fields(),
        players=players,
        max_records=max_records,
        profile="rl1_gameplay",
        ucf_enabled=bool(case["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
        float_fields=_float_compare_fields() if int(float_top) > 0 else (),
        float_top=float_top,
        float_threshold=0.0,
        float_dataset_label=str(case["replay_label"]),
    )
    timings["rollout_wall_s"] = time.perf_counter() - start

    records = int(buffers.num_records)
    build = float(timings["build_wall_s"])
    one_step = float(timings["one_step_wall_s"])
    rollout_wall = float(timings["rollout_wall_s"])
    total = build + one_step + rollout_wall
    return {
        "suite": case["suite_name"],
        "suite_path": case["suite_path"],
        "replay": case["replay_label"],
        "ports": ",".join(str(p) for p in case["ports"]),
        "stage_id": case["stage_id"],
        "characters": json.dumps(case["characters"], sort_keys=True, separators=(",", ":")),
        "records": records,
        "build_wall_s": build,
        "one_step_native_init_wall_s": float(timings.get("one_step_native_init_wall_s", 0.0)),
        "one_step_native_reseed_wall_s": float(timings.get("one_step_native_reseed_wall_s", 0.0)),
        "one_step_native_step_wall_s": float(timings.get("one_step_native_step_wall_s", 0.0)),
        "one_step_native_write_wall_s": float(timings.get("one_step_native_write_wall_s", 0.0)),
        "one_step_wall_s": one_step,
        "rollout_wall_s": rollout_wall,
        "total_wall_s": total,
        "build_records_per_s": records / build if build > 0.0 else 0.0,
        "one_step_records_per_s": records / one_step if one_step > 0.0 else 0.0,
        "rollout_records_per_s": records / rollout_wall if rollout_wall > 0.0 else 0.0,
        "total_records_per_s": records / total if total > 0.0 else 0.0,
        "one_step_discrete_mismatch": int(sum(one_summary.mismatches.values())),
        "rollout_first_mismatch_total": int(sum(rollout.first_mismatch_field_counts.values())),
        "float_top_errors": sum(len(v) for v in float_errors.values()),
        "float_top_downstream": len(float_downstream),
    }


def _tsv_value(value: Any) -> str:
    if isinstance(value, float):
        return f"{value:.9f}"
    return str(value)


def _write_tsv(path: Path, rows: list[dict[str, Any]], columns: tuple[str, ...]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["\t".join(columns)]
    for row in rows:
        lines.append("\t".join(_tsv_value(row.get(col, "")) for col in columns))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_rankings(out_dir: Path, rows: list[dict[str, Any]], *, top: int) -> None:
    columns = (
        "suite",
        "replay",
        "records",
        "stage_id",
        "characters",
        "total_wall_s",
        "build_wall_s",
        "one_step_native_init_wall_s",
        "one_step_native_reseed_wall_s",
        "one_step_native_step_wall_s",
        "one_step_native_write_wall_s",
        "one_step_wall_s",
        "rollout_wall_s",
        "total_records_per_s",
        "build_records_per_s",
        "one_step_records_per_s",
        "rollout_records_per_s",
    )
    ranked = {
        "outliers_total_wall.tsv": sorted(rows, key=lambda r: float(r["total_wall_s"]), reverse=True),
        "outliers_total_rps.tsv": sorted(rows, key=lambda r: float(r["total_records_per_s"])),
        "outliers_build_rps.tsv": sorted(rows, key=lambda r: float(r["build_records_per_s"])),
        "outliers_one_step_rps.tsv": sorted(rows, key=lambda r: float(r["one_step_records_per_s"])),
        "outliers_rollout_rps.tsv": sorted(rows, key=lambda r: float(r["rollout_records_per_s"])),
    }
    for name, ranked_rows in ranked.items():
        _write_tsv(out_dir / name, ranked_rows[:top], columns)


def _run(args: argparse.Namespace) -> dict[str, Any]:
    cases = _collect_cases(args)
    rows = [
        _run_case(
            case,
            chunk=int(args.chunk),
            float_top=int(args.float_top),
            max_records=int(args.max_records),
            native_phase_probe=not bool(args.no_native_phase_probe),
        )
        for case in cases
    ]
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    columns = (
        "suite",
        "suite_path",
        "replay",
        "ports",
        "stage_id",
        "characters",
        "records",
        "build_wall_s",
        "one_step_native_init_wall_s",
        "one_step_native_reseed_wall_s",
        "one_step_native_step_wall_s",
        "one_step_native_write_wall_s",
        "one_step_wall_s",
        "rollout_wall_s",
        "total_wall_s",
        "build_records_per_s",
        "one_step_records_per_s",
        "rollout_records_per_s",
        "total_records_per_s",
        "one_step_discrete_mismatch",
        "rollout_first_mismatch_total",
        "float_top_errors",
        "float_top_downstream",
    )
    _write_tsv(out_dir / "summary.tsv", rows, columns)
    _write_rankings(out_dir, rows, top=int(args.top))
    totals = {
        "replays": len(rows),
        "records": sum(int(r["records"]) for r in rows),
        "build_wall_s": sum(float(r["build_wall_s"]) for r in rows),
        "one_step_native_init_wall_s": sum(float(r.get("one_step_native_init_wall_s", 0.0)) for r in rows),
        "one_step_native_reseed_wall_s": sum(float(r.get("one_step_native_reseed_wall_s", 0.0)) for r in rows),
        "one_step_native_step_wall_s": sum(float(r.get("one_step_native_step_wall_s", 0.0)) for r in rows),
        "one_step_native_write_wall_s": sum(float(r.get("one_step_native_write_wall_s", 0.0)) for r in rows),
        "one_step_wall_s": sum(float(r["one_step_wall_s"]) for r in rows),
        "rollout_wall_s": sum(float(r["rollout_wall_s"]) for r in rows),
        "total_wall_s": sum(float(r["total_wall_s"]) for r in rows),
    }
    payload = {"totals": totals, "rows": rows}
    (out_dir / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return payload


def main() -> None:
    ap = argparse.ArgumentParser(description="Benchmark replay-buffer validation by replay.")
    ap.add_argument("--suite", action="append", help="Suite JSON to benchmark; may be repeated.")
    ap.add_argument("--heldout-index", action="append", help="Heldout index JSON; may be repeated.")
    ap.add_argument("--out-dir", type=Path, default=Path("reports/triage/validation_perf/benchmark_validation_replay"))
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--float-top", type=int, default=3)
    ap.add_argument("--max-records", type=int, default=0)
    ap.add_argument("--limit", type=int, help="Benchmark only the first N selected replay entries.")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument(
        "--no-native-phase-probe",
        action="store_true",
        help="Skip extra benchmark-only one-step reseed/step/write phase probing.",
    )
    ap.add_argument("--profile", action="store_true", help="Write cProfile output for the benchmark process.")
    args = ap.parse_args()

    if args.profile:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        profile_path = args.out_dir / "profile.prof"
        text_path = args.out_dir / "profile.txt"
        profiler = cProfile.Profile()
        profiler.enable()
        payload = _run(args)
        profiler.disable()
        profiler.dump_stats(str(profile_path))
        with text_path.open("w", encoding="utf-8") as fh:
            stats = pstats.Stats(profiler, stream=fh).sort_stats("cumulative")
            stats.print_stats(80)
    else:
        payload = _run(args)
    print(json.dumps({"totals": payload["totals"], "out_dir": str(args.out_dir)}, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
