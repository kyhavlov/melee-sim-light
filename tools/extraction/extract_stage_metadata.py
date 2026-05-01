from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from tools.extraction.extract_stage_collision import _extract_segments
from tools.slippi.known_data_artifacts import STAGE_MAGIC, STAGE_VERSION


KIND_ID = {
    "floor": 0,
    "ceiling": 1,
    "right_wall": 2,
    "left_wall": 3,
    "dynamic": 4,
}


def _f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


def _write_stage_bin(out: Path, data: dict) -> None:
    segments = list(data.get("segments", []))
    stage_points = list(data.get("stage_points_joint_positions", []))
    unit_scale = _f32(data.get("unit_scale", 1.0))
    # Stage-point roles are source-backed by the same map_head table that Ground_801C34AC uses to
    # populate stage_info.x280. Pack role coordinates in world units, matching Ground_801C2D24
    # after the stage root scale is applied.
    spawn_points: list[dict] = list(data.get("spawn_points", []))
    respawn_points: list[dict] = list(data.get("respawn_points", []))
    cam: dict = dict(data.get("cam_bounds_world", {}))
    blast: dict = dict(data.get("blast_bounds_world", {}))

    buf = bytearray()
    buf += STAGE_MAGIC
    buf += struct.pack(
        "<IHHHHHHffffffff",
        STAGE_VERSION,
        len(segments),
        len(stage_points),
        len(spawn_points),
        len(respawn_points),
        0,
        0,
        _f32(float(cam.get("left", 0.0)) * unit_scale),
        _f32(float(cam.get("right", 0.0)) * unit_scale),
        _f32(float(cam.get("top", 0.0)) * unit_scale),
        _f32(float(cam.get("bottom", 0.0)) * unit_scale),
        _f32(float(blast.get("left", 0.0)) * unit_scale),
        _f32(float(blast.get("right", 0.0)) * unit_scale),
        _f32(float(blast.get("top", 0.0)) * unit_scale),
        _f32(float(blast.get("bottom", 0.0)) * unit_scale),
    )
    for seg in segments:
        flags = (1 if seg.get("platform") else 0) | (2 if seg.get("ledge") else 0)
        buf += struct.pack(
            "<HBBHHffff",
            int(seg["i"]) & 0xFFFF,
            KIND_ID[str(seg["kind"])] & 0xFF,
            flags & 0xFF,
            int(seg.get("hi_flags", 0)) & 0xFFFF,
            int(seg.get("lo_flags", 0)) & 0xFFFF,
            _f32(float(seg["x0"]) * unit_scale),
            _f32(float(seg["y0"]) * unit_scale),
            _f32(float(seg["x1"]) * unit_scale),
            _f32(float(seg["y1"]) * unit_scale),
        )
    for pt in stage_points:
        buf += struct.pack(
            "<fff",
            _f32(float(pt.get("x", 0.0)) * unit_scale),
            _f32(float(pt.get("y", 0.0)) * unit_scale),
            _f32(float(pt.get("z", 0.0)) * unit_scale),
        )
    for pt in spawn_points:
        buf += struct.pack("<ff", _f32(float(pt["x"]) * unit_scale), _f32(float(pt["y"]) * unit_scale))
    for pt in respawn_points:
        buf += struct.pack("<ff", _f32(float(pt["x"]) * unit_scale), _f32(float(pt["y"]) * unit_scale))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(buf))


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack known stage collision/metadata as MSLSTG01.")
    ap.add_argument("--dat", type=Path, required=True, help="stage Gr*.dat input")
    ap.add_argument("--out", type=Path, required=True, help="MSLSTG01 output path")
    ap.add_argument("--audit", type=Path, default=None, help="optional audit JSON output")
    args = ap.parse_args()

    data = _extract_segments(args.dat)
    _write_stage_bin(args.out, data)
    if args.audit is not None:
        args.audit.parent.mkdir(parents=True, exist_ok=True)
        args.audit.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
