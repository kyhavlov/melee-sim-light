from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from tools.extraction.extract_stage_collision import _extract_segments
from tools.extraction.known_data_artifacts import STAGE_MAGIC, STAGE_VERSION


KIND_ID = {
    "floor": 0,
    "ceiling": 1,
    "right_wall": 2,
    "left_wall": 3,
    "dynamic": 4,
}

PLATFORM_TRANSFORM_KIND_ID = {
    "height": 1,
    "static_y": 2,
    "randall": 3,
}

PLATFORM_MOTION_KIND_ID = {
    "fountain_platform": 1,
}

STAGE_OBJECT_SUPPORT_KIND_ID = {
    "yoshi_shyguy": 1,
}

# Source floor material friction multipliers used by mpLib_800569EC(MapLine.lo_flags & 0xFF).
# Keep this in the generator so runtime consumes a generated MSLSTG01 field instead of duplicating
# the mplib material table on gameplay paths.
# refs/melee/src/melee/mp/mplib.c::{
#   mpLib_800569EC,mpLib_803BD3D8,mpLib_803BD430,mpLib_803BD488,mpLib_803BD4E0,
#   mpLib_803BD538,mpLib_803BD590,mpLib_803BD5E8,mpLib_803BD640,mpLib_803BD698,
#   mpLib_803BD6F0,mpLib_803BD748,mpLib_803BD8A8,mpLib_803BD900,mpLib_803BD958,
#   mpLib_803BD9B0,mpLib_803BDA60,mpLib_803BDAB8,mpLib_803BDB10,mpLib_803BDB68,
#   mpLib_803BDBC0}
MPLIB_GROUND_FRICTION_MUL_BY_MATERIAL = (
    1.0,
    1.0,
    1.5,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    0.1,
    0.9,
    1.0,
    0.2,
    1.0,
    0.1,
    1.0,
    1.0,
)


def _f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


def _ground_friction_mul_from_lo_flags(lo_flags: int) -> float:
    material = int(lo_flags) & 0xFF
    if 0 <= material < len(MPLIB_GROUND_FRICTION_MUL_BY_MATERIAL):
        return float(MPLIB_GROUND_FRICTION_MUL_BY_MATERIAL[material])
    return 1.0


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
    platform_transforms = list(data.get("platform_transforms", []))
    platform_motion = dict(data.get("platform_motion", {}))
    platform_paths = list(data.get("platform_paths", []))
    platform_motion_records: list[tuple[str, dict]] = [
        (name, dict(params))
        for name, params in sorted(platform_motion.items())
        if name in PLATFORM_MOTION_KIND_ID and isinstance(params, dict)
    ]

    buf = bytearray()
    buf += STAGE_MAGIC
    buf += struct.pack(
        "<IHHHHHHHHHHffffffff",
        STAGE_VERSION,
        len(segments),
        len(stage_points),
        len(spawn_points),
        len(respawn_points),
        len(platform_transforms),
        20 if platform_transforms else 0,
        len(platform_motion_records),
        72 if platform_motion_records else 0,
            len(platform_paths),
            16 if platform_paths else 0,
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
        stage_object_support_kind = STAGE_OBJECT_SUPPORT_KIND_ID.get(
            str(seg.get("stage_object_support", "")), 0
        )
        flags = (
            (1 if seg.get("platform") else 0)
            | (2 if seg.get("ledge") else 0)
            | (4 if seg.get("fighter_solid", True) else 0)
            | ((stage_object_support_kind & 0x1F) << 3)
        )
        buf += struct.pack(
            "<HBBHHhhhhfffff",
            int(seg["i"]) & 0xFFFF,
            KIND_ID[str(seg["kind"])] & 0xFF,
            flags & 0xFF,
            int(seg.get("hi_flags", 0)) & 0xFFFF,
            int(seg.get("lo_flags", 0)) & 0xFFFF,
            int(seg.get("prev_id0", -1)),
            int(seg.get("next_id0", -1)),
            int(seg.get("prev_id1", -1)),
            int(seg.get("next_id1", -1)),
            _f32(float(seg["x0"]) * unit_scale),
            _f32(float(seg["y0"]) * unit_scale),
            _f32(float(seg["x1"]) * unit_scale),
            _f32(float(seg["y1"]) * unit_scale),
            _f32(_ground_friction_mul_from_lo_flags(int(seg.get("lo_flags", 0)))),
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
    for rec in platform_transforms:
        kind = PLATFORM_TRANSFORM_KIND_ID[str(rec["kind"])]
        # Platform transform records are already in world/viewer coordinates, matching the live
        # grIzumi JObj->mpLib_80055E9C output consumed by runtime collision.
        buf += struct.pack(
            "<HBBffff",
            int(rec["line_id"]) & 0xFFFF,
            kind & 0xFF,
            int(rec.get("platform_id", 255)) & 0xFF,
            _f32(float(rec["x0"])),
            _f32(float(rec["x1"])),
            _f32(float(rec.get("y_const", 0.0))),
            _f32(float(rec.get("height_coeff", 0.0))),
        )
    for name, params in platform_motion_records:
        # Runtime-owned platform scheduler parameters are DAT/decomp data, not audit JSON. Pack the
        # FoD grIzumi params directly into MSLSTG01 so gameplay code can consume the binary artifact
        # during init without sidecar parsing.
        # refs/melee/src/melee/gr/grizumi.c::{FountainParams,grIzumi_801CC358}
        buf += struct.pack(
            "<BBHfffffffffffffffff",
            PLATFORM_MOTION_KIND_ID[name] & 0xFF,
            int(params.get("platform_count", 2)) & 0xFF,
            0,
            _f32(float(params["home_height"])),
            _f32(float(params["hidden_target_height"])),
            _f32(float(params["max_height"])),
            _f32(float(params["min_visible_height"])),
            _f32(float(params["up_speed"])),
            _f32(float(params["down_speed"])),
            _f32(float(params["wait_min_frames"])),
            _f32(float(params["wait_max_frames"])),
            _f32(float(params["hidden_wait_min_frames"])),
            _f32(float(params["hidden_wait_max_frames"])),
            _f32(float(params["target_delta_min"])),
            _f32(float(params["target_delta_max"])),
            _f32(float(params["bias_below_home"])),
            _f32(float(params["bias_above_home"])),
            _f32(float(params["hidden_weight"])),
            _f32(float(params["stay_weight"])),
            _f32(float(params["move_weight"])),
        )
    for rec in platform_paths:
        # Runtime moving-collision paths are generated from source stage-object JObj animation data.
        # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
        # refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
        buf += struct.pack(
            "<HHfff",
            int(rec["line_id"]) & 0xFFFF,
            int(rec["frame"]) & 0xFFFF,
            _f32(float(rec["x0"])),
            _f32(float(rec["y"])),
            _f32(float(rec["x1"])),
        )
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
