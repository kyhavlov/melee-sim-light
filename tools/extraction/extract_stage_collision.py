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

PS_FROZEN_FIGHTER_SOLID_FLOORS = frozenset({34, 35, 36, 51, 52, 53, 54})


def _fighter_solid_for_current_legal_policy(stage_dat: Path, kind: str, line_id: int) -> bool:
    if kind != "floor":
        return True
    if stage_dat.name.lower() != "grps.dat":
        return True
    # Current legal Pokemon Stadium support is frozen/base geometry. The frozen policy suppresses
    # transformation ground objects but keeps base body/platform floors active for fighter collision.
    # Store that active mask in MSLSTG01 so runtime consumes data rather than a gameplay line-id
    # switch.
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # refs/melee/src/melee/gr/grpstadium.c::{grStadium_OnInit,grStadium_801D10F0}
    # data/stages/pokemon_stadium.json::segments
    return line_id in PS_FROZEN_FIGHTER_SOLID_FLOORS


def _stage_platform_transforms(stage_dat: Path) -> list[dict]:
    if stage_dat.name.lower() != "griz.dat":
        return []
    # FoD platform collision lines are source-local MapLine records whose world transform is owned
    # by GrIz ground-object JObjs. The two side platforms use Slippi/grIzumi platform ids
    # 0=right, 1=left; the top platform is static.
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    # tools/modelplay/viewer/src/components/viewer/Stage.tsx::FountainOfDreams
    return [
        {
            "line_id": 0,
            "kind": "height",
            "platform_id": 1,
            "x0": -49.5,
            "x1": -21.0,
            "y_const": 20.0,
            "height_coeff": 0.80625,
        },
        {
            "line_id": 1,
            "kind": "height",
            "platform_id": 0,
            "x0": 21.0,
            "x1": 49.5,
            "y_const": 27.44186047,
            "height_coeff": 0.80625,
        },
        {
            "line_id": 2,
            "kind": "static_y",
            "platform_id": 255,
            "x0": -14.25,
            "x1": 14.25,
            "y_const": 42.75,
            "height_coeff": 0.0,
        },
    ]


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


def _ptr32_raw(arc, abs_off: int) -> int:
    return arc.data_base + _u32_be(arc.buf, abs_off)


def _traverse_joint_points(buf: bytes, arc, root_joint_abs: int) -> list[tuple[float, float, float]]:
    # Depth-first traversal matching Ground_801C34AC's child/next walk over the loaded JObj tree.
    # refs/melee/build/GALE01/asm/melee/gr/ground.s::Ground_801C34AC
    out: list[tuple[float, float, float]] = []
    stack: list[int] = [root_joint_abs]
    max_nodes = 10000
    while stack:
        node_abs = stack.pop()
        if len(out) >= max_nodes:
            break
        out.append(
            (
                float(_f32_be(buf, node_abs + 0x2C)),
                float(_f32_be(buf, node_abs + 0x30)),
                float(_f32_be(buf, node_abs + 0x34)),
            )
        )
        next_abs = _ptr32_or_none(arc, node_abs + 0x0C)
        child_abs = _ptr32_or_none(arc, node_abs + 0x08)
        if next_abs is not None:
            stack.append(next_abs)
        if child_abs is not None:
            stack.append(child_abs)
    return out


def _extract_stage_point_id_map(arc) -> dict[int, tuple[float, float, float]]:
    # Source owner: Ground_801C34AC finds the map_head point-map entry for a source HSD_Joint,
    # walks the loaded JObj tree to each table index, then writes stage_info.x280[stage_point_id].
    # The per-entry pair table lives at offset 0 in GrNLa/GrNBa, so use raw archive offsets instead
    # of treating a zero pointer value as NULL for that field.
    # refs/melee/build/GALE01/asm/melee/gr/ground.s::Ground_801C34AC
    buf = arc.buf
    map_head_abs = arc.get_public_offset("map_head")
    if map_head_abs is None:
        return {}

    entries_abs = _ptr32_or_none(arc, map_head_abs + 0x00)
    if entries_abs is None:
        return {}
    entry_count = _i32_be(buf, map_head_abs + 0x04)
    if entry_count <= 0:
        return {}

    out: dict[int, tuple[float, float, float]] = {}
    for entry_i in range(entry_count):
        entry_abs = entries_abs + entry_i * 0x0C
        joint_abs = _ptr32_or_none(arc, entry_abs + 0x00)
        if joint_abs is None:
            continue
        pairs_abs = _ptr32_raw(arc, entry_abs + 0x04)
        pair_count = _i32_be(buf, entry_abs + 0x08)
        if pair_count <= 0:
            continue
        points = _traverse_joint_points(buf, arc, joint_abs)
        for pair_i in range(pair_count):
            pair_abs = pairs_abs + pair_i * 4
            walk_index = _s16_be(buf, pair_abs + 0x00)
            stage_point_id = _s16_be(buf, pair_abs + 0x02)
            if stage_point_id < 0 or walk_index < 0 or walk_index >= len(points):
                continue
            out[int(stage_point_id)] = points[int(walk_index)]
    return out


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

    entry0_abs = unk8_arr_abs  # map_id==0 for the base stage root used by current legal-stage bins.
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

    # Stage point roles through stage_info.x280.
    #
    # Decomp consumers:
    # - Spawn points: refs/melee/src/melee/gr/stage.c::Stage_80224E64 (Ground_801C2D24(arg0))
    # - Respawn platforms: refs/melee/src/melee/gr/stage.c::Stage_80224E38 (Ground_801C2D24(arg1+4))
    # - Camera bounds: refs/melee/src/melee/gr/ground.c::Ground_801C39C0 (0x94..0x96)
    # - Blast zone: refs/melee/src/melee/gr/ground.c::Ground_801C3BB4 (0x97..0x98)
    point_id_map = _extract_stage_point_id_map(arc)
    if point_id_map:
        out["stage_point_id_positions"] = {
            str(pid): {"x": x, "y": y, "z": z} for pid, (x, y, z) in sorted(point_id_map.items())
        }

        def _raw_xy(pid: int) -> tuple[float, float] | None:
            point = point_id_map.get(pid)
            if point is None:
                return None
            return (float(point[0]), float(point[1]))

        def _ground_xy(pid: int) -> tuple[float, float] | None:
            # Mirror Ground_801C2D24's source fallback rules for stage-point roles:
            # - missing spawn ids 1..3 fall back to id 0
            # - missing respawn ids 5..7 fall back to id 4
            # - id 8/9 are midpoint helpers for 4/5 and 6/7
            # refs/melee/src/melee/gr/ground.c::Ground_801C2D24
            point = _raw_xy(pid)
            if point is not None:
                return point
            if 1 <= pid <= 3:
                return _ground_xy(0)
            if 5 <= pid <= 7:
                return _ground_xy(4)
            if pid == 8:
                a = _ground_xy(4)
                b = _ground_xy(5)
                if a is not None and b is not None:
                    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
            if pid == 9:
                a = _ground_xy(6)
                b = _ground_xy(7)
                if a is not None and b is not None:
                    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
            if pid == 0x7F:
                a = _ground_xy(0x94)
                if a is not None:
                    return (a[0], a[1] + 50.0)
                return _ground_xy(0)
            return None

        spawn_points = [_ground_xy(pid) for pid in range(4)]
        respawn_points = [_ground_xy(pid) for pid in range(4, 8)]
        cam_offset = _ground_xy(0x94)
        cam_range = [_ground_xy(0x95), _ground_xy(0x96)]
        dead_range = [_ground_xy(0x97), _ground_xy(0x98)]

        if all(point is not None for point in spawn_points):
            out["spawn_points"] = [{"x": x, "y": y} for (x, y) in spawn_points if x is not None]
        if all(point is not None for point in respawn_points):
            out["respawn_points"] = [{"x": x, "y": y} for (x, y) in respawn_points if x is not None]
        if cam_offset is not None and cam_range[0] is not None and cam_range[1] is not None:
            p0 = cam_range[0]
            p1 = cam_range[1]
            cam_l = min(p0[0], p1[0])
            cam_r = max(p0[0], p1[0])
            cam_b = min(p0[1], p1[1])
            cam_t = max(p0[1], p1[1])
            out.update(
                {
                    "cam_offset": {"x": cam_offset[0], "y": cam_offset[1]},
                    "cam_range_points": [
                        {"x": p0[0], "y": p0[1]},
                        {"x": p1[0], "y": p1[1]},
                    ],
                    "cam_bounds_world": {"left": cam_l, "right": cam_r, "top": cam_t, "bottom": cam_b},
                }
            )
        elif stage_dat.name == "GrPs.dat":
            # Pokemon Stadium lacks the full 0x94..0x96 camera point role set in map_head. The
            # engine falls back to Ground_801C39C0's dummy camera range for internal stage 3.
            # refs/melee/src/melee/gr/ground.c::Ground_801C39C0
            out["cam_bounds_world"] = {"left": -200.0, "right": 200.0, "top": 150.0, "bottom": -160.0}
        if dead_range[0] is not None and dead_range[1] is not None:
            p0 = dead_range[0]
            p1 = dead_range[1]
            dead_l = min(p0[0], p1[0])
            dead_r = max(p0[0], p1[0])
            dead_b = min(p0[1], p1[1])
            dead_t = max(p0[1], p1[1])
            out.update(
                {
                    "dead_range_points": [
                        {"x": p0[0], "y": p0[1]},
                        {"x": p1[0], "y": p1[1]},
                    ],
                    "blast_bounds_world": {"left": dead_l, "right": dead_r, "top": dead_t, "bottom": dead_b},
                }
            )

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
        prev_id0 = int(_s16_be(buf, off + 0x04))
        next_id0 = int(_s16_be(buf, off + 0x06))
        prev_id1 = int(_s16_be(buf, off + 0x08))
        next_id1 = int(_s16_be(buf, off + 0x0A))
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
                "v0_idx": v0,
                "v1_idx": v1,
                "prev_id0": prev_id0,
                "next_id0": next_id0,
                "prev_id1": prev_id1,
                "next_id1": next_id1,
                "platform": bool(lo_flags & LINE_FLAG_PLATFORM),
                "ledge": bool(lo_flags & LINE_FLAG_LEDGE),
                "fighter_solid": _fighter_solid_for_current_legal_policy(stage_dat, kind, i),
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
        "platform_transforms": _stage_platform_transforms(stage_dat),
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
