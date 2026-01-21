from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from melee_sim.hsd_archive import _u32_be, parse_hsd_archive

HURTCAPS_BIN_MAGIC = b"MSLHURT1"
HURTCAPS_BIN_VERSION = 1


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def _is_finite(x: float) -> bool:
    return x == x and abs(x) != float("inf")


def _fighter_prefix(character: str) -> str:
    return {
        "fox": "PlFx",
        "falco": "PlFc",
        "sheik": "PlSk",
        "peach": "PlPe",
        "marth": "PlMs",
        "puff": "PlPr",
        "falcon": "PlCa",
    }[character]


def _ftdata_symbol(character: str) -> str:
    return {
        "fox": "ftDataFox",
        "falco": "ftDataFalco",
        "sheik": "ftDataSeak",
        "peach": "ftDataPeach",
        "marth": "ftDataMars",
        "puff": "ftDataPurin",
        "falcon": "ftDataCaptain",
    }[character]


def _load_parts_num(character: str, iso_dir: Path) -> int:
    # Same logic as `extract_fighter_anims._load_parts_table`, but we only need array length.
    plco = parse_hsd_archive((iso_dir / "PlCo.dat").read_bytes())
    ft_load_common = plco.get_public_offset("ftLoadCommonData")
    if ft_load_common is None:
        raise RuntimeError("PlCo.dat missing ftLoadCommonData")
    p_data = [_u32_be(plco.buf, ft_load_common + i * 4) for i in range(23)]

    ftkind = {
        "fox": 0x01,
        "falco": 0x16,
        "sheik": 0x07,
        "peach": 0x09,
        "marth": 0x12,
        "puff": 0x0F,
        "falcon": 0x02,
    }[character]
    ft_parts_table_abs = plco.data_base + p_data[4]
    ft_parts_tbl_ptr = _u32_be(plco.buf, ft_parts_table_abs + ftkind * 4)
    ft_parts_tbl_abs = plco.data_base + ft_parts_tbl_ptr
    parts_num = _u32_be(plco.buf, ft_parts_tbl_abs + 8)
    return int(parts_num)


@dataclass(frozen=True)
class HurtCapsuleInit:
    # `ftHurtboxInit.bone_idx` is a Fighter_Part id (refs/melee/src/melee/ft/forward.h `enum Fighter_Part`).
    # Decomp usage: ftColl_HurtboxInit uses it as `fp->parts[hurt->capsule.bone_idx].joint`.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_HurtboxInit
    #
    # IMPORTANT: This is the same part_id domain used by our SSANIM pose sampler
    # (src/anim_pose.c: header joint_parts bytes -> part_id -> joint_index),
    # so the exported .bin also stores this value as `bone_part_id`.
    bone_idx: int
    height: int
    is_grabbable: int
    a_offset: tuple[float, float, float]
    b_offset: tuple[float, float, float]
    scale: float


def _try_parse_hurt_init(buf: bytes, abs_off: int, *, parts_num: int) -> HurtCapsuleInit | None:
    if abs_off < 0 or abs_off + 0x28 > len(buf):
        return None

    bone = _u32_be(buf, abs_off + 0x00)
    height = _u32_be(buf, abs_off + 0x04)
    is_grab = _u32_be(buf, abs_off + 0x08)

    # Heuristics: this is reverse-engineered from the shipped ISO data layout, but
    # the resulting values are still "decomp-first" in the sense that we're using
    # the same structs (`ftHurtboxInit`) and offsets the game consumes.
    if bone > max(parts_num, 1) + 10:
        return None
    if height > 3:
        return None
    if is_grab > 1:
        return None

    ax = _f32_be(buf, abs_off + 0x0C)
    ay = _f32_be(buf, abs_off + 0x10)
    az = _f32_be(buf, abs_off + 0x14)
    bx = _f32_be(buf, abs_off + 0x18)
    by = _f32_be(buf, abs_off + 0x1C)
    bz = _f32_be(buf, abs_off + 0x20)
    sc = _f32_be(buf, abs_off + 0x24)
    if not all(_is_finite(x) for x in (ax, ay, az, bx, by, bz, sc)):
        return None
    if not (0.01 <= sc <= 50.0):
        return None
    if any(abs(x) > 200.0 for x in (ax, ay, az, bx, by, bz)):
        return None

    return HurtCapsuleInit(
        bone_idx=int(bone),
        height=int(height),
        is_grabbable=int(is_grab),
        a_offset=(float(ax), float(ay), float(az)),
        b_offset=(float(bx), float(by), float(bz)),
        scale=float(sc),
    )


def _find_best_run(buf: bytes, start_abs: int, end_abs: int, *, parts_num: int) -> tuple[int, int] | None:
    best_run = 0
    best_off = -1
    for abs_off in range(start_abs, end_abs, 4):
        if _try_parse_hurt_init(buf, abs_off, parts_num=parts_num) is None:
            continue
        run = 0
        cur = abs_off
        while _try_parse_hurt_init(buf, cur, parts_num=parts_num) is not None:
            run += 1
            cur += 0x28
            if run > 20:
                break
        if run > best_run:
            best_run = run
            best_off = abs_off
    if best_run == 0:
        return None
    return best_run, best_off


def extract_character(character: str, *, iso_dir: Path) -> dict:
    prefix = _fighter_prefix(character)
    base = iso_dir / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")

    x18_ptr = _u32_be(arc.buf, ftdata_abs + 0x18)
    if x18_ptr == 0:
        raise RuntimeError(f"{base.name} ftData+0x18 (x18) is null")
    x18_base = arc.data_base + x18_ptr

    parts_num = _load_parts_num(character, iso_dir)

    scan_end = min(x18_base + 0x2000, arc.data_base + arc.header.data_size)
    found = _find_best_run(arc.buf, x18_base, scan_end, parts_num=parts_num)
    if found is None:
        raise RuntimeError(f"{base.name}: failed to locate hurt capsule init table near ftData.x18")
    run_len, run_abs = found
    if run_len < 5:
        raise RuntimeError(f"{base.name}: suspiciously short hurt capsule init run (len={run_len}) at {hex(run_abs)}")

    capsules: list[dict] = []
    for i in range(run_len):
        init = _try_parse_hurt_init(arc.buf, run_abs + i * 0x28, parts_num=parts_num)
        if init is None:
            break
        capsules.append(
            {
                "bone_idx": init.bone_idx,
                "height": init.height,
                "is_grabbable": bool(init.is_grabbable),
                "a_offset": list(init.a_offset),
                "b_offset": list(init.b_offset),
                "scale": init.scale,
            }
        )

    return {
        "character": character,
        "capsules": capsules,
        "meta": {
            "pl_dat": base.name,
            "ftdata_sym": _ftdata_symbol(character),
            "ftdata_x18_base_rel": int(x18_ptr),
            "found_rel": int(run_abs - arc.data_base),
            "found_delta_from_x18": int(run_abs - x18_base),
            "capsules_len": int(len(capsules)),
        },
    }

def _write_hurtcaps_bin(out_path: Path, *, capsules: list[HurtCapsuleInit]) -> None:
    # Binary format v1 (little-endian):
    # - magic: 8 bytes  "MSLHURT1"
    # - version: u32
    # - capsule_count: u16
    # - reserved: u16 (0)
    # - records[capsule_count]:
    #     - bone_part_id: u16 (Fighter_Part id; see HurtCapsuleInit.bone_idx)
    #     - height: u8 (HurtHeight; 0=low,1=mid,2=high in decomp: refs/melee/src/melee/lb/types.h)
    #     - is_grabbable: u8 (bool; decomp stores u32/bool; refs/melee/src/melee/ft/chara/ftCommon/types.h)
    #     - pad: u16 (0)
    #     - a_offset: 3*f32
    #     - b_offset: 3*f32
    #     - scale: f32
    if len(capsules) > 0xFFFF:
        raise ValueError(f"too many hurt capsules for v1: {len(capsules)}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(HURTCAPS_BIN_MAGIC)
        f.write(struct.pack("<IHH", int(HURTCAPS_BIN_VERSION), int(len(capsules)), 0))
        for cap in capsules:
            bone_part_id = int(cap.bone_idx)
            if not (0 <= bone_part_id <= 0xFFFF):
                raise ValueError(f"bone_idx out of range for u16: {bone_part_id}")
            height = int(cap.height)
            is_grab = int(cap.is_grabbable)
            if not (0 <= height <= 0xFF and 0 <= is_grab <= 0xFF):
                raise ValueError(f"bad height/is_grabbable: height={height} is_grabbable={is_grab}")
            f.write(struct.pack("<HBBH", bone_part_id, height, is_grab, 0))
            f.write(struct.pack("<fff", *cap.a_offset))
            f.write(struct.pack("<fff", *cap.b_offset))
            f.write(struct.pack("<f", float(cap.scale)))


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract fighter hurt capsule init data (ftHurtboxInit) from ISO-extracted Pl??.dat.")
    parser.add_argument("--iso_dir", type=Path, default=Path("_iso"))
    parser.add_argument("--out_dir", type=Path, default=Path("data/hurtcaps"))
    parser.add_argument(
        "--out_bin_dir",
        type=Path,
        default=None,
        help="optional output dir for compact .bin tables (data/hurtcaps/<character>.bin)",
    )
    parser.add_argument("--character", type=str, default=None)
    args = parser.parse_args()

    chars = ["fox", "falco", "sheik", "peach", "marth", "puff", "falcon"]
    if args.character is not None:
        if args.character not in chars:
            raise SystemExit(f"unknown character {args.character!r}")
        chars = [args.character]

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for ch in chars:
        out = extract_character(ch, iso_dir=args.iso_dir)
        (args.out_dir / f"{ch}.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {args.out_dir / (ch + '.json')} ({len(out['capsules'])} capsules)")
        if args.out_bin_dir is not None:
            # Re-parse into typed records so we can validate and write a stable binary table.
            caps: list[HurtCapsuleInit] = []
            for cap in out.get("capsules", []) or []:
                caps.append(
                    HurtCapsuleInit(
                        bone_idx=int(cap["bone_idx"]),
                        height=int(cap["height"]),
                        is_grabbable=1 if bool(cap["is_grabbable"]) else 0,
                        a_offset=(float(cap["a_offset"][0]), float(cap["a_offset"][1]), float(cap["a_offset"][2])),
                        b_offset=(float(cap["b_offset"][0]), float(cap["b_offset"][1]), float(cap["b_offset"][2])),
                        scale=float(cap["scale"]),
                    )
                )
            out_bin = args.out_bin_dir / f"{ch}.bin"
            _write_hurtcaps_bin(out_bin, capsules=caps)
            print(f"wrote {out_bin} ({len(caps)} capsules)")


if __name__ == "__main__":
    main()
