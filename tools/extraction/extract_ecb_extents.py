from __future__ import annotations

import argparse
import array
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


ANIM_MAGIC = b"SSANIM01"
ANIM_VERSION = 4

ECB_MAGIC = b"MSLECB01"
# Bump when the extents payload meaning/axis mapping changes. The loader rejects mismatches.
ECB_VERSION = 4

# SSANIM01 v4 matrix record layout: <12f (3x4) => (m00,m01,m02,tx, m10,m11,m12,ty, m20,m21,m22,tz)
# tools/extraction/extract_fighter_anims.py:_mtx_concat emits ty at float index 7 and tz at float index 11.
_MAT_BYTES = 12 * 4
_MAT_TY_BYTE_OFF = 7 * 4
_MAT_TZ_BYTE_OFF = 11 * 4

# ECB extents payload is float32[frame_count][4] in order:
#   (min_x, max_x, min_y, max_y)
#
# This corresponds to the joint-loop extrema in `mpColl_LoadECB_JObj` before decomp runtime expansion
# (left_x/right_x/bottom_y/top_y as min/max of dx/dy over the 6 ECB source joints).
# Source pointer: refs/melee/src/melee/mp/mpcoll.c:328 (mpColl_LoadECB_JObj joint loop).
_EXTENTS_STRIDE_BYTES = 16


def _u16_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "little", signed=False)


def _u32_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little", signed=False)


def _f32_le(buf: bytes, off: int) -> float:
    return struct.unpack_from("<f", buf, off)[0]


@dataclass(frozen=True)
class _AnimHeader:
    joint_parts: list[int]
    anim_count: int


def _read_anim_header(buf: bytes) -> _AnimHeader:
    if len(buf) < 16:
        raise ValueError("SSANIM01: file too small for header")
    if buf[:8] != ANIM_MAGIC:
        raise ValueError(f"SSANIM01: bad magic: {buf[:8]!r}")
    ver = _u32_le(buf, 8)
    if ver != ANIM_VERSION:
        raise ValueError(f"SSANIM01: unsupported version: {ver} (want {ANIM_VERSION})")
    joint_count = _u16_le(buf, 12)
    anim_count = _u16_le(buf, 14)
    off = 16
    if len(buf) < off + joint_count:
        raise ValueError("SSANIM01: truncated joint_parts")
    joint_parts = [int(x) for x in buf[off : off + joint_count]]
    return _AnimHeader(joint_parts=joint_parts, anim_count=anim_count)


def _load_ecb_joints(attrs_path: Path) -> list[int]:
    d = json.loads(attrs_path.read_text())
    joints = d.get("ecb_joints")
    if not isinstance(joints, list) or len(joints) != 6:
        raise ValueError(f"{attrs_path}: expected ecb_joints list[6]")
    out: list[int] = []
    for x in joints:
        if not isinstance(x, int):
            raise ValueError(f"{attrs_path}: ecb_joints entries must be ints")
        out.append(x)
    return out


@dataclass
class _EcbAnim:
    msid: int
    frame_count: int
    values: array.array  # 'f', length = frame_count * 4


def _extract_ecb_extents_for_anim_file(*, anims: Path, attrs: Path) -> list[_EcbAnim]:
    buf = anims.read_bytes()
    hdr = _read_anim_header(buf)
    ecb_joints = _load_ecb_joints(attrs)

    joint_count = len(hdr.joint_parts)
    if joint_count == 0:
        raise ValueError("SSANIM01: joint_count=0")

    # Map FtPart id -> index in joint_parts stream (exactly like extract_ecb_bottom.py).
    part_to_joint_index = {int(p): i for i, p in enumerate(hdr.joint_parts)}
    ecb_joint_indices: list[int] = []
    for p in ecb_joints:
        if p < 0:
            continue
        ji = part_to_joint_index.get(int(p))
        if ji is None:
            raise ValueError(
                f"{anims}: missing ECB joint part {p} in joint_parts; "
                f"expected extract_fighter_anims to include data/characters/<char>.json ecb_joints"
            )
        ecb_joint_indices.append(int(ji))
    if not ecb_joint_indices:
        raise ValueError(f"{attrs}: ecb_joints contains no valid joints")

    # Walk SSANIM01 payload.
    off = 16 + joint_count
    out: list[_EcbAnim] = []
    for _ai in range(int(hdr.anim_count)):
        if off + 4 > len(buf):
            raise ValueError("SSANIM01: truncated anim table")
        msid = _u16_le(buf, off + 0)
        frame_count = _u16_le(buf, off + 2)
        off += 4

        mats_bytes = int(frame_count) * joint_count * _MAT_BYTES
        transn_bytes = int(frame_count) * 3 * 4  # v4 tail
        need = mats_bytes + transn_bytes
        if off + need > len(buf):
            raise ValueError(
                f"SSANIM01: truncated anim payload: msid={msid} frame_count={frame_count} off={off}"
            )

        vals = array.array("f")
        if frame_count:
            for fi in range(int(frame_count)):
                frame_base = off + fi * joint_count * _MAT_BYTES
                min_x = math.inf
                max_x = -math.inf
                min_y = math.inf
                max_y = -math.inf
                for ji in ecb_joint_indices:
                    base = frame_base + ji * _MAT_BYTES
                    # Coordinate mapping note:
                    # - In this project, stage/world horizontal (pos_x) is aligned to SSANIM01's Z
                    #   translation component, not X. This matches downstream consumers that treat
                    #   TransN's z component as the horizontal axis (e.g., ledge snapping).
                    # - Therefore, extract (min_x,max_x) from tz and (min_y,max_y) from ty.
                    tz = float(_f32_le(buf, base + _MAT_TZ_BYTE_OFF))
                    ty = float(_f32_le(buf, base + _MAT_TY_BYTE_OFF))
                    if not (math.isfinite(tz) and math.isfinite(ty)):
                        raise ValueError(
                            f"SSANIM01: non-finite ECB joint xy for msid={msid} frame={fi} joint_i={ji}"
                        )
                    if tz < min_x:
                        min_x = tz
                    if tz > max_x:
                        max_x = tz
                    if ty < min_y:
                        min_y = ty
                    if ty > max_y:
                        max_y = ty
                if not (math.isfinite(min_x) and math.isfinite(max_x) and math.isfinite(min_y) and math.isfinite(max_y)):
                    min_x = max_x = min_y = max_y = 0.0
                vals.extend((float(min_x), float(max_x), float(min_y), float(max_y)))

        out.append(_EcbAnim(msid=int(msid), frame_count=int(frame_count), values=vals))
        off += need

    if off != len(buf):
        raise ValueError(f"SSANIM01: unexpected trailing bytes: {len(buf) - off}")

    out.sort(key=lambda a: a.msid)
    return out


def _write_ecb_extents_table(path: Path, anims: list[_EcbAnim]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    header_size = 8 + 4 + 2 + 2 + 4 + 4
    toc_offset = header_size
    toc_entry_size = 2 + 2 + 4 + 4
    toc_bytes = len(anims) * toc_entry_size

    values_off = toc_offset + toc_bytes
    entries: list[tuple[int, int, int, int]] = []
    for a in anims:
        values_bytes = int(a.frame_count) * _EXTENTS_STRIDE_BYTES
        entries.append((int(a.msid), int(a.frame_count), int(values_off), int(values_bytes)))
        values_off += values_bytes

    file_size = values_off

    with path.open("wb") as f:
        f.write(ECB_MAGIC)
        # Header reserved field encodes the stride so loaders can self-check.
        f.write(struct.pack("<IHHII", ECB_VERSION, len(anims), _EXTENTS_STRIDE_BYTES, toc_offset, file_size))
        for msid, frame_count, off, values_bytes in entries:
            f.write(struct.pack("<HHII", msid & 0xFFFF, frame_count & 0xFFFF, off, values_bytes))
        for a in anims:
            if a.frame_count == 0:
                continue
            vals = a.values
            if vals.typecode != "f":
                raise RuntimeError("internal: values must be array('f')")
            if len(vals) != int(a.frame_count) * 4:
                raise RuntimeError("internal: extents values length mismatch")
            if sys.byteorder != "little":
                vals = array.array("f", vals)
                vals.byteswap()
            f.write(vals.tobytes())


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract per-msid ECB extents (min/max X/Y) tables from SSANIM01 v4 matrices."
    )
    ap.add_argument("--character", type=str, required=True, help="character key (fox,falco,...)")
    ap.add_argument("--anims", type=Path, default=None, help="path to data/anims/<char>.bin (SSANIM01 v4)")
    ap.add_argument(
        "--attrs",
        type=Path,
        default=None,
        help="path to data/characters/<char>.json (must contain ecb_joints)",
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="output path (default: data/ecb/<char>_extents.bin)",
    )
    args = ap.parse_args()

    ch = args.character.strip().lower()
    anims = args.anims or (Path("data/anims") / f"{ch}.bin")
    attrs = args.attrs or (Path("data/characters") / f"{ch}.json")
    out = args.out or (Path("data/ecb") / f"{ch}_extents.bin")

    if not anims.exists():
        raise SystemExit(f"missing input anim file: {anims}")
    if not attrs.exists():
        raise SystemExit(f"missing input attrs file: {attrs}")

    ecb = _extract_ecb_extents_for_anim_file(anims=anims, attrs=attrs)
    _write_ecb_extents_table(out, ecb)
    print(f"wrote {out} ({len(ecb)} animations)")


if __name__ == "__main__":
    main()
