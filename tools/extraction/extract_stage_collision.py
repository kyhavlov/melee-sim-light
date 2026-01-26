from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive
from melee_sim.iso import extract_file, find_files, list_files

# mp/forward.h
LINE_FLAG_EMPTY = 1 << 7
LINE_FLAG_PLATFORM = 1 << 8
LINE_FLAG_LEDGE = 1 << 9


def _u16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=False)


def _s16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=True)


def _i32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=True)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]

def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _ptr32_or_none(arc, abs_off: int) -> int | None:
    v = _u32_be(arc.buf, abs_off)
    if v == 0:
        return None
    return arc.data_base + v


def _extract_stage_points(stage_dat: Path, arc) -> dict | None:
    # Decomp-first: stage camera/dead ranges are derived from stage-point JObjs loaded from
    # `map_head` (UnkStageDat) and used by:
    # - Camera bounds: refs/melee/src/melee/gr/ground.c::Ground_801C39C0 (points 0x94-0x96)
    # - Blast zone: refs/melee/src/melee/gr/ground.c::Ground_801C3BB4 (points 0x97-0x98)
    #
    # `map_head->unk8[map_id].unk0` is an HSD_Joint tree root; we traverse it and extract joint
    # positions (HSD_Joint::position at +0x2C).
    # refs/melee/src/sysdolphin/baselib/jobj.h (struct HSD_Joint layout)
    buf = arc.buf
    map_head_abs = arc.get_public_offset("map_head")
    if map_head_abs is None:
        return None

    # UnkStageDat:
    # - unk8: pointer to UnkStageDat_x8_t array (offset +0x08)
    # - unkC: count (offset +0x0C)
    # refs/melee/src/melee/gr/types.h (struct UnkStageDat)
    unk8_arr_abs = _ptr32_or_none(arc, map_head_abs + 0x08)
    if unk8_arr_abs is None:
        return None
    map_count = _i32_be(buf, map_head_abs + 0x0C)
    if map_count <= 0:
        return None

    entry0_abs = unk8_arr_abs  # map_id==0 for the stage root (single-map FD)
    root_joint_abs = _ptr32_or_none(arc, entry0_abs + 0x00)
    if root_joint_abs is None:
        return None

    # Depth-first traversal of the HSD_Joint tree, matching Ground_801C126C order.
    # refs/melee/src/melee/gr/ground.c::Ground_801C126C
    points: list[tuple[float, float, float]] = []
    stack: list[int] = [root_joint_abs]
    # Stage JObj trees are expected to be acyclic; keep a hard cap to avoid infinite loops on
    # malformed inputs.
    max_nodes = 10000
    while stack:
        node_abs = stack.pop()
        if len(points) >= max_nodes:
            break
        x = float(_f32_be(buf, node_abs + 0x2C))
        y = float(_f32_be(buf, node_abs + 0x30))
        z = float(_f32_be(buf, node_abs + 0x34))
        # IMPORTANT: keep exact traversal order, including (0,0,0) joints, because stage-point IDs
        # used by the game are indices into this list.
        points.append((x, y, z))

        # Pre-order: push next then child so child is processed first.
        next_abs = _ptr32_or_none(arc, node_abs + 0x0C)
        child_abs = _ptr32_or_none(arc, node_abs + 0x08)
        if next_abs is not None:
            stack.append(next_abs)
        if child_abs is not None:
            stack.append(child_abs)

    if not points:
        return None

    out: dict = {
        "stage_points_joint_positions": [{"x": x, "y": y, "z": z} for (x, y, z) in points],
    }

    # Stage point selection for match-flow (FD only).
    #
    # Decomp pointers for how these points are consumed:
    # - Camera bounds: refs/melee/src/melee/gr/ground.c::Ground_801C39C0 (Ground_801C2D24(0x94..0x96))
    # - Blast/dead range: refs/melee/src/melee/gr/ground.c::Ground_801C3BB4 (Ground_801C2D24(0x97..0x98))
    # - Spawn points: refs/melee/src/melee/gr/stage.c::Stage_80224E64 (Ground_801C2D24(arg0), arg0=0..3)
    # - Respawn platforms: refs/melee/src/melee/gr/stage.c::Stage_80224E38 (Ground_801C2D24(arg1+4), arg1=0..3)
    #
    # NOTE: Ground_801C2D24 reads from stage_info.x280[stage_point_id], which is populated during
    # stage init (Ground_801C34AC / related stage setup). The DAT->x280 mapping is not extracted yet,
    # so for now we use an explicit FD-only heuristic over the map_head joint positions.
    #
    # TODO(decomp): extract the stage_point_id -> JObj mapping used to populate stage_info.x280
    # and select points by ID, not by coordinate patterns.
    if stage_dat.name == "GrNLa.dat":
        eps = 1e-3

        def _near(a: float, b: float) -> bool:
            return abs(a - b) <= eps

        pts2 = [(x, y) for (x, y, z) in points if _near(z, 0.0)]

        cam_offset = next(((x, y) for (x, y) in pts2 if _near(x, 0.0) and _near(y, 12.0)), None)
        cam_range = [(x, y) for (x, y) in pts2 if _near(abs(x), 170.0)]
        dead_range = [(x, y) for (x, y) in pts2 if _near(abs(x), 246.0)]

        # Keep traversal order rather than sorting: stage point ids are ordered and per-port
        # semantics depend on that ordering.
        spawn_points = [(x, y) for (x, y) in pts2 if _near(y, 10.0)]

        # GrNLa has 6 y=45 joints; exclude the two x=±25 joints that are not used as per-port
        # respawn platforms in our suite.
        respawn_candidates = [(x, y) for (x, y) in pts2 if _near(y, 45.0)]
        respawn_points = [(x, y) for (x, y) in respawn_candidates if not _near(abs(x), 25.0)]

        if cam_offset is not None and len(cam_range) == 2 and len(dead_range) == 2:
            cam_l = min(cam_range[0][0], cam_range[1][0])
            cam_r = max(cam_range[0][0], cam_range[1][0])
            cam_b = min(cam_range[0][1], cam_range[1][1])
            cam_t = max(cam_range[0][1], cam_range[1][1])
            dead_l = min(dead_range[0][0], dead_range[1][0])
            dead_r = max(dead_range[0][0], dead_range[1][0])
            dead_b = min(dead_range[0][1], dead_range[1][1])
            dead_t = max(dead_range[0][1], dead_range[1][1])

            out.update(
                {
                    "cam_offset": {"x": cam_offset[0], "y": cam_offset[1]},
                    "cam_range_points": [
                        {"x": cam_range[0][0], "y": cam_range[0][1]},
                        {"x": cam_range[1][0], "y": cam_range[1][1]},
                    ],
                    "dead_range_points": [
                        {"x": dead_range[0][0], "y": dead_range[0][1]},
                        {"x": dead_range[1][0], "y": dead_range[1][1]},
                    ],
                    # World-space helpers used by match-flow (Stage_GetCamBounds* / Stage_GetBlastZone*).
                    # refs/melee/src/melee/gr/stage.c (Stage_GetCamBoundsTopOffset, Stage_GetBlastZoneTopOffset)
                    "cam_bounds_world": {"left": cam_l, "right": cam_r, "top": cam_t, "bottom": cam_b},
                    "blast_bounds_world": {"left": dead_l, "right": dead_r, "top": dead_t, "bottom": dead_b},
                }
            )

        if len(spawn_points) == 4:
            out["spawn_points"] = [{"x": x, "y": y} for (x, y) in spawn_points]
        if len(respawn_points) == 4:
            out["respawn_points"] = [{"x": x, "y": y} for (x, y) in respawn_points]

    return out


def _kind_ranges(coll_abs: int, buf: bytes) -> list[tuple[str, int, int]]:
    # mp/types.h: MapCollData
    floor_start = _s16_be(buf, coll_abs + 0x10)
    floor_count = _s16_be(buf, coll_abs + 0x12)
    ceil_start = _s16_be(buf, coll_abs + 0x14)
    ceil_count = _s16_be(buf, coll_abs + 0x16)
    rwall_start = _s16_be(buf, coll_abs + 0x18)
    rwall_count = _s16_be(buf, coll_abs + 0x1A)
    lwall_start = _s16_be(buf, coll_abs + 0x1C)
    lwall_count = _s16_be(buf, coll_abs + 0x1E)
    dyn_start = _s16_be(buf, coll_abs + 0x20)
    dyn_count = _s16_be(buf, coll_abs + 0x22)
    return [
        ("floor", floor_start, floor_count),
        ("ceiling", ceil_start, ceil_count),
        ("right_wall", rwall_start, rwall_count),
        ("left_wall", lwall_start, lwall_count),
        ("dynamic", dyn_start, dyn_count),
    ]


def _extract_segments(stage_dat: Path) -> dict:
    buf = stage_dat.read_bytes()
    arc = parse_hsd_archive(buf)

    coll_abs = arc.get_public_offset("coll_data")
    if coll_abs is None:
        raise ValueError(f"{stage_dat.name}: missing public symbol 'coll_data'")

    gp_abs = arc.get_public_offset("grGroundParam")
    # Stage collision scale factor.
    #
    # Decomp:
    # - `Ground_801C0498()` returns `stage_info.param->x0` (loaded from public symbol `grGroundParam`).
    # - `mpLibLoad()` uses `f31 = Ground_801C0498()` and sets `groundCollVtx[i].pos = f31 * coll_data->verts[i]`.
    #   refs/melee/src/melee/gr/ground.c:270 (Ground_801C0498)
    #   refs/melee/src/melee/mp/mplib.c:174,252-263 (mpLibLoad)
    #
    # Convention for extracted `data/stages/*.json`:
    # - We store *unscaled* `coll_data->verts` coordinates (as they appear in the stage DAT).
    # - We also emit `unit_scale` so consumers can reproduce `mpLibLoad`'s scaled coordinates if needed.
    scale = float(_f32_be(buf, gp_abs + 0x00)) if gp_abs is not None else 1.0

    verts_abs = arc.ptr32(coll_abs + 0x00)
    vert_count = int(_i32_be(buf, coll_abs + 0x04))
    lines_abs = arc.ptr32(coll_abs + 0x08)
    line_count = int(_i32_be(buf, coll_abs + 0x0C))

    verts: list[tuple[float, float]] = []
    for i in range(vert_count):
        x = float(_f32_be(buf, verts_abs + i * 8 + 0x00))
        y = float(_f32_be(buf, verts_abs + i * 8 + 0x04))
        verts.append((x, y))

    kind_for_line: dict[int, str] = {}
    for kind, start, count in _kind_ranges(coll_abs, buf):
        if count <= 0:
            continue
        for i in range(start, start + count):
            kind_for_line[i] = kind

    segments: list[dict] = []
    for i in range(line_count):
        kind = kind_for_line.get(i)
        if kind is None:
            continue

        off = lines_abs + i * 0x10
        v0 = int(_u16_be(buf, off + 0x00))
        v1 = int(_u16_be(buf, off + 0x02))
        hi_flags = int(_u16_be(buf, off + 0x0C))
        lo_flags = int(_u16_be(buf, off + 0x0E))

        if hi_flags & LINE_FLAG_EMPTY:
            continue

        (x0, y0) = verts[v0]
        (x1, y1) = verts[v1]

        # Note: LINE_FLAG_PLATFORM / LINE_FLAG_LEDGE are stored in MapLine.lo_flags (u16).
        segments.append(
            {
                "i": i,
                "kind": kind,
                "x0": x0,
                "y0": y0,
                "x1": x1,
                "y1": y1,
                "platform": bool(lo_flags & LINE_FLAG_PLATFORM),
                "ledge": bool(lo_flags & LINE_FLAG_LEDGE),
                "hi_flags": hi_flags,
                "lo_flags": lo_flags,
            }
        )

    return {
        "stage_dat": stage_dat.name,
        "unit_scale": scale,
        "vert_count": vert_count,
        "line_count": line_count,
        "segments": segments,
        **(_extract_stage_points(stage_dat, arc) or {}),
    }


def _maybe_extract_from_iso(iso: Path, stage_dat_name: str, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists():
        return
    files = list_files(iso)
    matches = find_files(files, f"*{stage_dat_name}")
    if not matches:
        raise SystemExit(f"no matches for {stage_dat_name!r} in ISO")
    extract_file(iso, matches[0], out_path)
    print(f"wrote {out_path} ({matches[0].size} bytes)")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract MapCollData line segments from a stage Gr*.dat (decomp-first) into JSON."
    )
    ap.add_argument("--dat", type=Path, default=None, help="path to stage Gr*.dat (HSD archive)")
    ap.add_argument("--iso", type=Path, default=None, help="optional path to SSBM.iso (to extract --stage into _iso)")
    ap.add_argument("--stage", type=str, default=None, help="stage file name like 'GrNBa.dat' (requires --iso)")
    ap.add_argument("--out", type=Path, required=True, help="output JSON path")
    args = ap.parse_args()

    if args.dat is None:
        if args.stage is None or args.iso is None:
            raise SystemExit("provide either --dat, or (--stage and --iso)")
        stage_dat_name = args.stage
        local = Path("_iso") / stage_dat_name
        _maybe_extract_from_iso(args.iso, stage_dat_name, local)
        args.dat = local

    extracted = _extract_segments(args.dat)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(extracted, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
