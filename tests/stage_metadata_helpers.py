from __future__ import annotations

from pathlib import Path

from tools.slippi.known_data_artifacts import read_mslstg01_v1

_STAGE_KIND_BY_ID = {
    0: "floor",
    1: "ceiling",
    2: "right_wall",
    3: "left_wall",
    4: "dynamic",
}


def fd_stage_segments() -> list[dict]:
    stage = read_mslstg01_v1(Path("data/stages/bin/grnla.bin"))
    return [
        {
            "i": int(seg.line_id),
            "kind": _STAGE_KIND_BY_ID.get(int(seg.kind_id), "dynamic"),
            "platform": bool(int(seg.flags) & 0x1),
            "ledge": bool(int(seg.flags) & 0x2),
            "hi_flags": int(seg.hi_flags),
            "lo_flags": int(seg.lo_flags),
            "x0": float(seg.x0),
            "y0": float(seg.y0),
            "x1": float(seg.x1),
            "y1": float(seg.y1),
        }
        for seg in stage.segments
    ]

