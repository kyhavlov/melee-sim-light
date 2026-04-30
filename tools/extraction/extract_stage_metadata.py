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
    # Keep MSLSTG01 source-backed. The legacy JSON currently labels FD spawn/respawn/camera/blast
    # roles through coordinate heuristics because the DAT -> stage_info.x280 point mapping is not
    # extracted yet. Pack only raw stage-point positions here until that mapping is source-backed.
    spawn_points: list[dict] = []
    respawn_points: list[dict] = []
    cam: dict = {}
    blast: dict = {}

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
        _f32(cam.get("left", 0.0)),
        _f32(cam.get("right", 0.0)),
        _f32(cam.get("top", 0.0)),
        _f32(cam.get("bottom", 0.0)),
        _f32(blast.get("left", 0.0)),
        _f32(blast.get("right", 0.0)),
        _f32(blast.get("top", 0.0)),
        _f32(blast.get("bottom", 0.0)),
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
            _f32(seg["x0"]),
            _f32(seg["y0"]),
            _f32(seg["x1"]),
            _f32(seg["y1"]),
        )
    for pt in stage_points:
        buf += struct.pack("<fff", _f32(pt.get("x", 0.0)), _f32(pt.get("y", 0.0)), _f32(pt.get("z", 0.0)))
    for pt in spawn_points:
        buf += struct.pack("<ff", _f32(pt["x"]), _f32(pt["y"]))
    for pt in respawn_points:
        buf += struct.pack("<ff", _f32(pt["x"]), _f32(pt["y"]))
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
