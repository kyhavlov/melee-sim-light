from __future__ import annotations

import argparse
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive
from melee_sim.raw_data import raw_data_dir
from tools.extraction.char_registry import CHARS


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _shield_part_from_pldat(pl_dat: Path, *, ftdata_symbol: str) -> int:
    buf = pl_dat.read_bytes()
    arc = parse_hsd_archive(buf)
    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise ValueError(f"{pl_dat.name}: missing public symbol {ftdata_symbol!r}")
    x8_abs = arc.data_base + _u32_be(buf, ftdata_abs + 0x08)
    if x8_abs + 0x12 > len(buf):
        raise ValueError(f"{pl_dat.name}: ftData.x8 out of bounds")
    return int(buf[x8_abs + 0x11])


def _guard_target_srt_from_pldat(
    pl_dat: Path,
    *,
    character: str,
    ftdata_symbol: str,
) -> list[tuple[int, tuple[float, ...]]]:
    """Extract the local HSD_Joint tree consumed by ftCo_80091E78.

    `ftAnim_80070010` and `ftAnim_80070108` map this tree from FtPart_TransN forward while
    skipping character parts reported by ftParts_8007506C. Keeping local SRTs lets runtime apply
    the actual two-stage JObj blend to every collision bone instead of baking one shield point.
    """
    from tools.extraction import extract_fighter_anims as efa

    buf = pl_dat.read_bytes()
    arc = parse_hsd_archive(buf)
    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise ValueError(f"{pl_dat.name}: missing public symbol {ftdata_symbol!r}")

    x20_abs = arc.data_base + _u32_be(buf, ftdata_abs + 0x20)
    if x20_abs + 4 > len(buf):
        raise ValueError(f"{pl_dat.name}: ftData.x20 out of bounds")
    root_obj_abs = arc.data_base + _u32_be(buf, x20_abs)
    if root_obj_abs + 0x0C > len(buf):
        raise ValueError(f"{pl_dat.name}: ftData.x20 root object out of bounds")
    root_abs = arc.data_base + _u32_be(buf, root_obj_abs + 0x08)

    nodes: list[tuple[float, ...]] = []
    stack = [root_abs]
    while stack:
        node_abs = stack.pop()
        if node_abs + 0x38 > len(buf):
            raise ValueError(f"{pl_dat.name}: ftData.x20 joint out of bounds")
        rot = tuple(efa._f32_be(buf, node_abs + off) for off in (0x14, 0x18, 0x1C))
        scl = tuple(efa._f32_be(buf, node_abs + off) for off in (0x20, 0x24, 0x28))
        pos = tuple(efa._f32_be(buf, node_abs + off) for off in (0x2C, 0x30, 0x34))
        nodes.append((*rot, *pos, *scl))
        next_ptr = _u32_be(buf, node_abs + 0x0C)
        child_ptr = _u32_be(buf, node_abs + 0x08)
        if next_ptr:
            stack.append(arc.data_base + next_ptr)
        if child_ptr:
            stack.append(arc.data_base + child_ptr)

    part_to_joint, skip_parts, _joint_to_part = efa._load_parts_table(character)
    is_skip = {int(part) for part in skip_parts}
    part = 1  # FtPart_TransN
    records: list[tuple[int, tuple[float, ...]]] = []
    for srt in nodes:
        while part < len(part_to_joint) and part in is_skip:
            part += 1
        if part >= len(part_to_joint):
            raise ValueError(f"{pl_dat.name}: ftData.x20 tree exceeds fighter part table")
        records.append((part, srt))
        part += 1
    return records


def _guard_frame_count(*, iso_dir: Path, character: str) -> int:
    from tools.extraction import extract_fighter_anims as efa

    config = CHARS[character]
    entry = efa._msid_anim_entry(character, 38)  # ftCo_SM_Guard
    if entry is None:
        raise ValueError(f"{character}: missing Guard msid 38")
    symbol, base_off, _size, _flags = entry
    aj_dat = iso_dir / config.aj_dat
    arc = parse_hsd_archive(aj_dat.read_bytes(), base=base_off)
    fig_off = arc.get_public_offset(symbol)
    if fig_off is None:
        raise ValueError(f"{aj_dat.name}: missing Guard symbol {symbol!r}")
    return int(round(float(efa._read_figatree(arc, fig_off).frames))) + 1


def _write_table(
    out_path: Path,
    *,
    neutral_frame: int,
    shield_part: int,
    guard_frame_count: int,
    target_srt: list[tuple[int, tuple[float, ...]]],
) -> None:
    if not target_srt or len(target_srt) > 0xFFFF or guard_frame_count <= neutral_frame:
        raise ValueError("bad Guard target part count")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(b"MSLSHLD1")
        f.write(
            struct.pack(
                "<IHHHH", 5, len(target_srt), neutral_frame, shield_part, guard_frame_count
            )
        )
        for part, srt in target_srt:
            f.write(struct.pack("<HH9f", part, 0, *srt))


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract the Guard live-JObj target tree.")
    ap.add_argument("--iso-dir", type=Path, default=raw_data_dir())
    ap.add_argument("--character", required=True, choices=sorted(CHARS))
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    from tools.extraction import extract_fighter_anims as efa

    efa.ISO_DIR = args.iso_dir
    config = CHARS[args.character]
    pl_dat = args.iso_dir / config.pl_dat
    if not pl_dat.exists():
        raise SystemExit(f"missing required ISO artifact: {pl_dat}")
    shield_part = _shield_part_from_pldat(pl_dat, ftdata_symbol=config.ftdata_symbol)
    target_srt = _guard_target_srt_from_pldat(
        pl_dat,
        character=args.character,
        ftdata_symbol=config.ftdata_symbol,
    )
    guard_frame_count = _guard_frame_count(iso_dir=args.iso_dir, character=args.character)
    _write_table(
        args.out,
        neutral_frame=10,  # ftCo_800921DC: mv.co.guard.x8 = 10
        shield_part=shield_part,
        guard_frame_count=guard_frame_count,
        target_srt=target_srt,
    )


if __name__ == "__main__":
    main()
