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
    scale = float(_f32_be(buf, gp_abs + 0x00)) if gp_abs is not None else 1.0

    verts_abs = arc.ptr32(coll_abs + 0x00)
    vert_count = int(_i32_be(buf, coll_abs + 0x04))
    lines_abs = arc.ptr32(coll_abs + 0x08)
    line_count = int(_i32_be(buf, coll_abs + 0x0C))

    verts: list[tuple[float, float]] = []
    for i in range(vert_count):
        x = float(_f32_be(buf, verts_abs + i * 8 + 0x00)) * scale
        y = float(_f32_be(buf, verts_abs + i * 8 + 0x04)) * scale
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
        local = Path("iso") / stage_dat_name
        _maybe_extract_from_iso(args.iso, stage_dat_name, local)
        args.dat = local

    extracted = _extract_segments(args.dat)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(extracted, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
