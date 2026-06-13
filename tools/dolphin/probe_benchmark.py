from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class BenchmarkCase:
    name: str
    row: str
    window_before: int = 1
    window_after: int = 1


DEFAULT_CASES = (
    BenchmarkCase(
        name="shallow_his",
        row="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl:500:0",
    ),
    BenchmarkCase(
        name="mid_his",
        row="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl:3000:0",
    ),
    BenchmarkCase(
        name="deep_his",
        row="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl:7485:0",
    ),
    BenchmarkCase(
        name="ipw_window_a",
        row="datasets/aggregate_recent/replays/validation/marth/InternalPowerlessWallaby.msl:1313:0",
        window_before=4,
        window_after=5,
    ),
    BenchmarkCase(
        name="ipw_window_b",
        row="datasets/aggregate_recent/replays/validation/marth/InternalPowerlessWallaby.msl:1383:0",
        window_before=4,
        window_after=5,
    ),
)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _count_event_lines(case_dir: Path) -> int:
    total = 0
    for path in case_dir.glob("*.jsonl"):
        with path.open("r", encoding="utf-8") as f:
            total += sum(1 for line in f if line.strip())
    return total


def _run_case(
    case: BenchmarkCase,
    *,
    dolphin: Path,
    iso: Path,
    out_dir: Path,
    timeout: float,
) -> dict[str, object]:
    case_dir = out_dir / case.name
    cmd = [
        sys.executable,
        "-m",
        "tools.dolphin.forensic_row_dump",
        "--row",
        case.row,
        "--dolphin",
        str(dolphin),
        "--iso",
        str(iso),
        "--window-before",
        str(case.window_before),
        "--window-after",
        str(case.window_after),
        "--timeout",
        str(timeout),
        "--out-dir",
        str(case_dir),
    ]
    proc = subprocess.run(cmd, cwd=_repo_root(), text=True, capture_output=True)
    summary_path = case_dir / "summary.json"
    rows_path = case_dir / "rows" / "engine_dump_rows.json"
    result: dict[str, object] = {
        "name": case.name,
        "row": case.row,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "summary_path": str(summary_path),
    }
    if summary_path.exists():
        with summary_path.open("r", encoding="utf-8") as f:
            summary = json.load(f)
        capture = summary.get("capture", {})
        result.update(
            {
                "elapsed_sec": capture.get("elapsed_sec"),
                "first_frame": capture.get("first_frame"),
                "last_frame": capture.get("last_frame"),
                "captured_frame_count": capture.get("frame_count"),
                "capture_error": capture.get("error"),
            }
        )
    if rows_path.exists():
        with rows_path.open("r", encoding="utf-8") as f:
            rows = json.load(f)
        result["row_count"] = rows.get("row_count")
        result["port_ids"] = rows.get("port_ids")
    result["event_count"] = _count_event_lines(case_dir)
    return result


def _print_table(results: list[dict[str, object]]) -> None:
    print("name\treturncode\telapsed_sec\tfirst_frame\tlast_frame\trow_count\tevent_count\tport_ids")
    for r in results:
        elapsed = r.get("elapsed_sec")
        elapsed_s = f"{float(elapsed):.3f}" if elapsed is not None else "NA"
        print(
            f"{r['name']}\t{r['returncode']}\t{elapsed_s}\t"
            f"{r.get('first_frame', 'NA')}\t{r.get('last_frame', 'NA')}\t"
            f"{r.get('row_count', 'NA')}\t{r.get('event_count', 0)}\t{r.get('port_ids', 'NA')}"
        )


def main() -> int:
    root = _repo_root()
    ap = argparse.ArgumentParser(
        description="Run fixed Dolphin probe smoke benchmarks and print capture latency."
    )
    ap.add_argument(
        "--dolphin",
        type=Path,
        default=root / "refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui",
    )
    ap.add_argument("--iso", type=Path, default=root / "SSBM.iso")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=root / "reports/triage" / f"{_timestamp()}_dolphin_probe_benchmark",
    )
    ap.add_argument("--timeout", type=float, default=90.0)
    args = ap.parse_args()

    results = [
        _run_case(case, dolphin=args.dolphin, iso=args.iso, out_dir=args.out_dir, timeout=args.timeout)
        for case in DEFAULT_CASES
    ]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.out_dir / "probe_benchmark_summary.json"
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump({"results": results}, f, indent=2, sort_keys=True)
        f.write("\n")
    _print_table(results)
    print(f"summary={summary_path}")
    return 0 if all(int(r["returncode"]) == 0 for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
