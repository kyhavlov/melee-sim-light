from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from tools.dolphin.dolphin_engine_dump import capture_engine_dump
from tools.dolphin.extract_engine_dump_rows import extract_to_dir
from tools.eval.dataset import read_dataset
from tools.slippi.slpz import replay_path_for_peppi, resolve_replay_path
from tools.slippi.suite_io import repo_root


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _display_path(path: Path, root: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(root))
    except ValueError:
        return str(resolved)


@dataclass(frozen=True)
class RowSpec:
    dataset: str
    record: int
    p: int


def _parse_row(spec: str) -> RowSpec:
    parts = spec.rsplit(":", 2)
    if len(parts) != 3:
        raise ValueError(f"invalid --row spec (expected <dataset>:<record>:<p>): {spec!r}")
    return RowSpec(dataset=parts[0], record=int(parts[1]), p=int(parts[2]))


def _resolve_dataset(root: Path, dataset_token: str) -> Path:
    candidate = Path(dataset_token)
    if candidate.is_absolute():
        return candidate.resolve()
    return (root / candidate).resolve()


def _infer_replay_from_dataset(root: Path, dataset_path: Path) -> Path:
    rel = dataset_path.resolve().relative_to(root)
    parts = list(rel.parts)
    if "replays" not in parts:
        raise ValueError(f"cannot infer replay path from dataset (missing 'replays' segment): {dataset_path}")
    idx = parts.index("replays")
    replay_rel = Path(*parts[idx:]).with_suffix(".slp")
    replay_path = resolve_replay_path((root / replay_rel).resolve())
    if not replay_path.exists():
        raise FileNotFoundError(f"inferred replay not found: {replay_path}")
    return replay_path


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Capture playback engine dump around a dataset row and extract frame rows."
    )
    ap.add_argument("--row", required=True, help="<dataset_path>:<record>:<p>")
    ap.add_argument("--dolphin", required=True, type=Path, help="path to playback dolphin-emu(-nogui)")
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--window-before", type=int, default=3, help="frames before min(seed,ref)")
    ap.add_argument("--window-after", type=int, default=3, help="frames after max(seed,ref)")
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument(
        "--collision-probe",
        action="store_true",
        help="also write interpreter pre-collision primitive JSONL beside the dump",
    )
    ap.add_argument(
        "--damagefall-probe",
        action="store_true",
        help="also write interpreter DamageFall IASA/Fall_Enter JSONL beside the dump",
    )
    ap.add_argument(
        "--throw-release-probe",
        action="store_true",
        help="also write interpreter ftCo_800DDDE4 throw-release JSONL beside the dump",
    )
    ap.add_argument(
        "--throw-laser-event-probe",
        action="store_true",
        help="also write interpreter throw-laser item event JSONL beside the dump",
    )
    ap.add_argument(
        "--laser-shield-reflect-event-probe",
        action="store_true",
        help="also write interpreter laser shield/reflect event JSONL beside the dump",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_dolphin_forensic_row",
    )
    args = ap.parse_args()

    root = repo_root()
    spec = _parse_row(str(args.row))
    dataset_path = _resolve_dataset(root, spec.dataset)
    if not dataset_path.exists():
        raise SystemExit(f"missing dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    if spec.record < 0 or spec.record >= int(ds.samples.shape[0]):
        raise SystemExit(
            f"record out of range: dataset={dataset_path} record={spec.record} rows={int(ds.samples.shape[0])}"
        )
    if spec.p < 0 or spec.p >= int(ds.header["num_players"]):
        raise SystemExit(f"invalid p={spec.p} for num_players={int(ds.header['num_players'])}")

    row = ds.samples[spec.record]
    seed_frame = int(row["seed_t"]["frame_id"])
    ref_frame = int(row["ref_t1"]["frame_id"])
    start_frame = min(seed_frame, ref_frame) - int(args.window_before)
    end_frame = max(seed_frame, ref_frame) + int(args.window_after)

    replay_path = _infer_replay_from_dataset(root, dataset_path)
    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    stem = f"{dataset_path.stem}_rec{spec.record}_p{spec.p}_f{start_frame}_{end_frame}"
    dump_path = out_dir / f"{stem}.bin"
    collision_probe_path = out_dir / f"{stem}_collision_probe.jsonl" if args.collision_probe else None
    damagefall_probe_path = out_dir / f"{stem}_damagefall_probe.jsonl" if args.damagefall_probe else None
    throw_release_probe_path = (
        out_dir / f"{stem}_throw_release_probe.jsonl" if args.throw_release_probe else None
    )
    throw_laser_event_probe_path = (
        out_dir / f"{stem}_throw_laser_events.jsonl" if args.throw_laser_event_probe else None
    )
    laser_shield_reflect_event_probe_path = (
        out_dir / f"{stem}_laser_shield_reflect_events.jsonl"
        if args.laser_shield_reflect_event_probe
        else None
    )
    user_dir = out_dir / "dolphin_user"
    with replay_path_for_peppi(replay_path) as capture_replay_path:
        result = capture_engine_dump(
            replay=capture_replay_path,
            dolphin=args.dolphin,
            iso=args.iso,
            user_dir=user_dir,
            out_bin=dump_path,
            start_frame=start_frame,
            end_frame=end_frame,
            timeout=float(args.timeout),
            collision_probe_path=collision_probe_path,
            damagefall_probe_path=damagefall_probe_path,
            throw_release_probe_path=throw_release_probe_path,
            throw_laser_event_probe_path=throw_laser_event_probe_path,
            laser_shield_reflect_event_probe_path=laser_shield_reflect_event_probe_path,
        )
    rc = int(result.returncode)
    capture_summary = {
        "returncode": rc,
        "elapsed_sec": result.elapsed_sec,
        "frame_count": result.frame_count,
        "first_frame": result.first_frame,
        "last_frame": result.last_frame,
        "stdout_log": _display_path(result.stdout_log, root),
        "stderr_log": _display_path(result.stderr_log, root),
        "error": result.error,
    }
    if rc != 0:
        raise SystemExit(
            f"capture failed for replay={replay_path} frame_window={start_frame}..{end_frame} "
            f"details={capture_summary}"
        )

    rows_dir = out_dir / "rows"
    json_path, txt_path = extract_to_dir(
        dump_path=dump_path,
        start_frame=start_frame,
        end_frame=end_frame,
        ports=[spec.p + 1],
        out_dir=rows_dir,
    )

    summary = {
        "row": {"dataset": _display_path(dataset_path, root), "record": spec.record, "p": spec.p},
        "replay": _display_path(replay_path, root),
        "seed_frame": seed_frame,
        "ref_frame": ref_frame,
        "capture_window": {"start": start_frame, "end": end_frame},
        "capture": capture_summary,
        "dump_bin": _display_path(dump_path, root),
        "rows_json": _display_path(json_path, root),
        "rows_txt": _display_path(txt_path, root),
    }
    if collision_probe_path is not None:
        summary["collision_probe_jsonl"] = _display_path(collision_probe_path, root)
    if damagefall_probe_path is not None:
        summary["damagefall_probe_jsonl"] = _display_path(damagefall_probe_path, root)
    if throw_release_probe_path is not None:
        summary["throw_release_probe_jsonl"] = _display_path(throw_release_probe_path, root)
    if throw_laser_event_probe_path is not None:
        summary["throw_laser_event_probe_jsonl"] = _display_path(throw_laser_event_probe_path, root)
    if laser_shield_reflect_event_probe_path is not None:
        summary["laser_shield_reflect_event_probe_jsonl"] = _display_path(
            laser_shield_reflect_event_probe_path, root
        )
    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"wrote {dump_path}")
    print(f"wrote {json_path}")
    print(f"wrote {txt_path}")
    print(f"wrote {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
