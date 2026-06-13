from __future__ import annotations

import argparse
import json
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from tools.dolphin.dolphin_engine_dump import capture_engine_dump
from tools.dolphin.extract_engine_dump_rows import extract_to_dir
from tools.dolphin.patch_slp_preframe_window import (
    _apply_patches,
    _load_patch_spec,
    _load_ubjson_module,
)
from tools.slippi.slpz import decompress_slpz, resolve_replay_path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


_SAFE_WINDOW_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")


def _safe_window_name(value: Any, idx: int) -> str:
    name = str(value if value is not None else f"window_{idx}")
    if (
        name in {"", ".", ".."}
        or "/" in name
        or "\\" in name
        or Path(name).is_absolute()
        or _SAFE_WINDOW_NAME_RE.fullmatch(name) is None
    ):
        raise ValueError(
            f"window {idx} has unsafe name {name!r}; use a simple slug like 'after_patch'"
        )
    return name


def _read_scenario(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        spec = json.load(f)
    if not isinstance(spec, dict):
        raise ValueError("scenario spec must be a JSON object")
    if "source_replay" not in spec:
        raise ValueError("scenario spec must provide source_replay")
    patches = spec.get("patches", [])
    if not isinstance(patches, list) or not patches:
        raise ValueError("scenario spec must provide a non-empty patches list")
    windows = spec.get("windows", [])
    if not isinstance(windows, list) or not windows:
        raise ValueError("scenario spec must provide a non-empty windows list")
    for idx, window in enumerate(windows):
        if not isinstance(window, dict):
            raise ValueError(f"window {idx} must be an object")
        if "start_frame" not in window or "end_frame" not in window:
            raise ValueError(f"window {idx} must provide start_frame and end_frame")
        if int(window["end_frame"]) < int(window["start_frame"]):
            raise ValueError(f"window {idx} has end_frame < start_frame")
        _safe_window_name(window.get("name"), idx)
    return spec


def _materialize_slp(source: Path, out: Path) -> None:
    replay = resolve_replay_path(source)
    out.parent.mkdir(parents=True, exist_ok=True)
    if replay.suffix == ".slpz":
        out.write_bytes(decompress_slpz(replay.read_bytes()))
    elif replay.suffix == ".slp":
        shutil.copyfile(replay, out)
    else:
        raise ValueError(f"source replay must be .slp or .slpz, got {replay}")


def _write_patched_replay(source_slp: Path, patch_spec: Path, out_slp: Path) -> list[int]:
    ubjson = _load_ubjson_module()
    patches = _load_patch_spec(patch_spec)
    with source_slp.open("rb") as f:
        obj = ubjson.load(f)
    if not isinstance(obj, dict) or "raw" not in obj:
        raise ValueError("input does not look like a Slippi UBJSON object with a raw stream")
    raw_obj = obj["raw"]
    if not isinstance(raw_obj, (bytes, bytearray)):
        raise ValueError("Slippi UBJSON raw field is not bytes")
    raw = bytearray(raw_obj)
    matched = _apply_patches(raw, patches)
    empty = [idx for idx, count in enumerate(matched) if count == 0]
    if empty:
        raise ValueError(
            "patches matched zero pre-frame events: "
            + ", ".join(str(idx) for idx in empty)
            + ". Check raw Slippi frame numbers and zero-based player indices."
        )
    obj["raw"] = bytes(raw)
    with out_slp.open("wb") as f:
        ubjson.dump(obj, f)
    return matched


def _run_windows(
    *,
    replay: Path,
    windows: list[dict[str, Any]],
    out_dir: Path,
    dolphin: Path,
    iso: Path,
    timeout: float,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for idx, window in enumerate(windows):
        name = _safe_window_name(window.get("name"), idx)
        start_frame = int(window["start_frame"])
        end_frame = int(window["end_frame"])
        ports = [int(p) for p in window.get("ports", [1])]
        window_dir = out_dir / name
        dump_path = window_dir / "engine_dump.bin"
        result = capture_engine_dump(
            replay=replay,
            dolphin=dolphin,
            iso=iso,
            user_dir=window_dir / "dolphin_user",
            out_bin=dump_path,
            start_frame=start_frame,
            end_frame=end_frame,
            timeout=timeout,
        )
        row_count: int | None = None
        rows_json: str | None = None
        rows_txt: str | None = None
        if int(result.returncode) == 0:
            json_path, txt_path = extract_to_dir(
                dump_path=dump_path,
                start_frame=start_frame,
                end_frame=end_frame,
                ports=ports,
                out_dir=window_dir / "rows",
            )
            rows_json = str(json_path)
            rows_txt = str(txt_path)
            with json_path.open("r", encoding="utf-8") as f:
                payload = json.load(f)
            row_count = int(payload["row_count"])
        results.append(
            {
                "name": name,
                "start_frame": start_frame,
                "end_frame": end_frame,
                "ports": ports,
                "returncode": int(result.returncode),
                "elapsed_sec": result.elapsed_sec,
                "first_frame": result.first_frame,
                "last_frame": result.last_frame,
                "captured_frame_count": result.frame_count,
                "row_count": row_count,
                "rows_json": rows_json,
                "rows_txt": rows_txt,
                "stdout_log": str(result.stdout_log),
                "stderr_log": str(result.stderr_log),
                "error": result.error,
            }
        )
    return results


def main() -> int:
    root = _repo_root()
    ap = argparse.ArgumentParser(
        description=(
            "Patch Slippi pre-frame scenario fields, run playback Dolphin, and extract probe rows."
        )
    )
    ap.add_argument("--scenario", required=True, type=Path, help="scenario JSON spec")
    ap.add_argument(
        "--dolphin",
        type=Path,
        default=root / "refs/Ishiiruka/build_probe/Binaries/dolphin-emu-nogui",
    )
    ap.add_argument("--iso", type=Path, default=root / "SSBM.iso")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=root / "reports/triage" / f"{_timestamp()}_slp_scenario_probe",
    )
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument(
        "--baseline",
        action="store_true",
        help="also capture the unpatched source replay for the same windows",
    )
    args = ap.parse_args()

    spec = _read_scenario(args.scenario)
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    source_slp = out_dir / "source.slp"
    patched_slp = out_dir / "scenario.slp"
    _materialize_slp((root / spec["source_replay"]).resolve(), source_slp)

    scenario_input = out_dir / "scenario_input.json"
    shutil.copyfile(args.scenario, scenario_input)
    patch_spec = out_dir / "patch_spec.json"
    patch_spec.write_text(json.dumps({"patches": spec.get("patches", [])}, indent=2) + "\n")
    matched = _write_patched_replay(source_slp, patch_spec, patched_slp)

    windows = spec["windows"]
    summary: dict[str, Any] = {
        "scenario": str(args.scenario),
        "scenario_input": str(scenario_input),
        "source_replay": str(spec["source_replay"]),
        "patched_replay": str(patched_slp),
        "patch_match_counts": matched,
        "windows": {},
    }
    if args.baseline:
        summary["windows"]["baseline"] = _run_windows(
            replay=source_slp,
            windows=windows,
            out_dir=out_dir / "baseline",
            dolphin=args.dolphin,
            iso=args.iso,
            timeout=float(args.timeout),
        )
    summary["windows"]["scenario"] = _run_windows(
        replay=patched_slp,
        windows=windows,
        out_dir=out_dir / "scenario",
        dolphin=args.dolphin,
        iso=args.iso,
        timeout=float(args.timeout),
    )

    summary_path = out_dir / "scenario_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(f"wrote {patched_slp}")
    print(f"wrote {summary_path}")
    for label, results in summary["windows"].items():
        for result in results:
            print(
                f"{label}:{result['name']} rc={result['returncode']} elapsed={result['elapsed_sec']:.3f}s "
                f"frames={result['first_frame']}..{result['last_frame']} rows={result['row_count']}"
            )
    all_results = [result for results in summary["windows"].values() for result in results]
    return 0 if all(int(result["returncode"]) == 0 for result in all_results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
