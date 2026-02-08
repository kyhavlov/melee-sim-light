from __future__ import annotations

import argparse
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _shield_part_from_pldat(pl_dat: Path, *, ftdata_symbol: str) -> int:
    """Return ftData.x8->x11 (shield bone index) for this character (decomp-first)."""
    buf = pl_dat.read_bytes()
    arc = parse_hsd_archive(buf)
    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise ValueError(f"{pl_dat.name}: missing public symbol {ftdata_symbol!r}")

    # struct ftData { ... struct ftData_x8* x8; } (ft/types.h +0x08)
    x8_ptr = _u32_be(buf, ftdata_abs + 0x08)
    # Pointers are offsets into the archive data section; 0 is valid (data_base).
    x8_abs = arc.data_base + x8_ptr
    if x8_abs + 0x12 > len(buf):
        raise ValueError(f"{pl_dat.name}: ftData.x8 out of bounds")

    # struct ftData_x8 { ... u8 x10; u8 x11; ... } (ft/types.h +0x10/+0x11)
    return int(buf[x8_abs + 0x11])


def _write_table(out_path: Path, *, neutral_frame: int, xyz_by_frame: list[tuple[float, float, float]]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    frame_count = len(xyz_by_frame)
    if frame_count <= 0 or neutral_frame < 0 or neutral_frame >= frame_count:
        raise ValueError("bad frame_count/neutral_frame")
    with out_path.open("wb") as f:
        f.write(b"MSLSHLD1")
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<HH", int(frame_count) & 0xFFFF, int(neutral_frame) & 0xFFFF))
        for (x, y, z) in xyz_by_frame:
            f.write(struct.pack("<3f", float(x), float(y), float(z)))


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract guard-tilt shield bubble center offsets (decomp-first, ISO-derived)."
    )
    ap.add_argument("--iso-dir", type=Path, default=Path("_iso"))
    ap.add_argument("--character", type=str, required=True, choices=["fox", "falco"])
    ap.add_argument("--out", type=Path, required=True, help="output path (e.g. data/shields/fox.bin)")
    args = ap.parse_args()

    # Reuse the decomp-first fobj + SRT evaluation code from extract_fighter_anims.py.
    # This keeps the table generation consistent with the SSANIM extractors.
    from tools.extraction import extract_fighter_anims as efa

    efa.ISO_DIR = args.iso_dir
    character = args.character

    prefix = {"fox": "PlFx", "falco": "PlFc"}[character]
    pl_dat = args.iso_dir / f"{prefix}.dat"
    aj_dat = args.iso_dir / f"{prefix}AJ.dat"
    if not pl_dat.exists() or not aj_dat.exists():
        raise SystemExit(f"missing required ISO artifacts: {pl_dat} / {aj_dat}")

    ftdata_symbol = {"fox": "ftDataFox", "falco": "ftDataFalco"}[character]
    shield_part = _shield_part_from_pldat(pl_dat, ftdata_symbol=ftdata_symbol)

    entry = efa._msid_anim_entry(character, 38)  # ftCo_SM_Guard (tilt timeline uses this)
    if entry is None:
        raise SystemExit(f"{character}: missing msid 38 entry")
    # extract_fighter_anims._msid_anim_entry returns
    # (public_symbol, aj_base_off, size_bytes, msid_flags_u8).
    sym, base_off, _size, _msid_flags_u8 = entry

    aj_buf = aj_dat.read_bytes()
    arc = parse_hsd_archive(aj_buf, base=base_off)
    fig_off = arc.get_public_offset(sym)
    if fig_off is None:
        raise SystemExit(f"{character}: missing figatree public symbol {sym!r} in {aj_dat.name}")

    fig = efa._read_figatree(arc, fig_off)

    # Decomp: mv.co.guard.x8 starts at 10 (ftCo_800921DC) and is updated via ftCo_80091BC4.
    neutral_frame = 10
    end_frame = int(round(float(fig.frames)))
    frame_count = end_frame + 1

    part_rot, part_scl, part_pos, parent_part, part_flags = efa._read_rest_srt_and_parents(character)
    model_scaling, inv_scale_part = efa._read_model_scale_and_inv_part(character)
    inv_model_scale = 1.0 / model_scaling if abs(model_scaling) > 1.0e-6 else 1.0
    part_to_joint, skip_parts, _joint_to_part = efa._load_parts_table(character)
    parts_num = len(part_to_joint)

    node_to_part, part_to_node, track_base_by_node = efa._node_mapping_for_parts(parts_num, skip_parts, fig)
    if shield_part < 0 or shield_part >= parts_num:
        raise SystemExit(f"{character}: shield_part out of bounds: {shield_part}")
    shield_node = part_to_node[shield_part]
    if shield_node < 0 or shield_node >= len(fig.nodes):
        raise SystemExit(f"{character}: shield_part {shield_part} not present in fig node mapping")

    # Build closure: shield part + ancestors (parents before children).
    closure_set: set[int] = set()
    p = shield_part
    while 0 <= p < parts_num and p not in closure_set:
        closure_set.add(int(p))
        p = int(parent_part[p])
    closure_parts = sorted(closure_set)

    # Stable parent-before-child order (matches extract_fighter_anims.py).
    depth_cache: dict[int, int] = {}

    def depth(part: int) -> int:
        d = depth_cache.get(part)
        if d is not None:
            return d
        pp = int(parent_part[part])
        if pp < 0:
            depth_cache[part] = 0
            return 0
        dd = 1 + depth(pp)
        depth_cache[part] = dd
        return dd

    order = sorted(closure_parts, key=lambda part: (depth(part), part))

    # Build fobj lists for closure parts.
    fobjs_by_part: dict[int, list[efa._FObj]] = {}
    for part in closure_parts:
        ni = part_to_node[part]
        if ni < 0 or ni >= len(fig.nodes):
            continue
        ntracks = int(fig.nodes[ni])
        base = track_base_by_node[ni]
        tracks = fig.tracks[base : base + ntracks]
        fobjs: list[efa._FObj] = []
        for t in tracks:
            if t.obj_type < 1 or t.obj_type > 10:
                continue
            ad_start = int(t.ad_abs)
            ad = memoryview(arc.buf)[ad_start : ad_start + int(t.length)]
            fo = efa._FObj(
                ad=ad,
                ad_head=0,
                length=int(t.length),
                startframe=int(t.startframe),
                obj_type=int(t.obj_type),
                frac_value=int(t.frac_value),
                frac_slope=int(t.frac_slope),
            )
            fo.req_anim(0.0)
            fobjs.append(fo)
        if fobjs:
            fobjs_by_part[int(part)] = fobjs

    cur_rot = part_rot[:]
    cur_scl = part_scl[:]
    cur_pos = part_pos[:]
    if 0 <= inv_scale_part < len(cur_scl):
        cur_scl[inv_scale_part] = (inv_model_scale, inv_model_scale, inv_model_scale)

    world_mtx: list[tuple[float, ...]] = [
        (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    ] * parts_num
    world_scl: list[tuple[float, float, float] | None] = [None] * parts_num

    out_xyz: list[tuple[float, float, float]] = []
    for frame in range(frame_count):
        rate = 0.0 if frame == 0 else 1.0
        for part, fobjs in fobjs_by_part.items():
            for fo in fobjs:
                vals = fo.interpret(rate)
                if not vals:
                    continue
                v = float(vals[-1])
                if fo.obj_type == 1:
                    rx, ry, rz = cur_rot[part]
                    cur_rot[part] = (v, ry, rz)
                elif fo.obj_type == 2:
                    rx, ry, rz = cur_rot[part]
                    cur_rot[part] = (rx, v, rz)
                elif fo.obj_type == 3:
                    rx, ry, rz = cur_rot[part]
                    cur_rot[part] = (rx, ry, v)
                elif fo.obj_type == 5:
                    px, py, pz = cur_pos[part]
                    cur_pos[part] = (v, py, pz)
                elif fo.obj_type == 6:
                    px, py, pz = cur_pos[part]
                    cur_pos[part] = (px, v, pz)
                elif fo.obj_type == 7:
                    px, py, pz = cur_pos[part]
                    cur_pos[part] = (px, py, v)
                elif fo.obj_type == 8:
                    sx, sy, sz = cur_scl[part]
                    cur_scl[part] = (v, sy, sz)
                elif fo.obj_type == 9:
                    sx, sy, sz = cur_scl[part]
                    cur_scl[part] = (sx, v, sz)
                elif fo.obj_type == 10:
                    sx, sy, sz = cur_scl[part]
                    cur_scl[part] = (sx, sy, v)

        for part in order:
            pp = int(parent_part[part])
            parent_m = (
                world_mtx[pp]
                if pp >= 0
                else (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
            )
            parent_s = world_scl[pp] if pp >= 0 else None
            local = efa._mtx_srt(cur_scl[part], cur_rot[part], cur_pos[part], parent_s)
            world = efa._mtx_concat(parent_m, local)
            world_mtx[part] = world
            if (int(part_flags[part]) & 8) != 0:
                world_scl[part] = parent_s if pp >= 0 and parent_s is not None else None
            else:
                if pp >= 0 and parent_s is not None:
                    psx, psy, psz = parent_s
                    sx, sy, sz = cur_scl[part]
                    world_scl[part] = (efa._f32_mul(sx, psx), efa._f32_mul(sy, psy), efa._f32_mul(sz, psz))
                else:
                    world_scl[part] = cur_scl[part]

        m = world_mtx[shield_part]
        out_xyz.append((float(m[3]), float(m[7]), float(m[11])))

    _write_table(args.out, neutral_frame=neutral_frame, xyz_by_frame=out_xyz)


if __name__ == "__main__":
    main()
