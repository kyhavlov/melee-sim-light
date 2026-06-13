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
ECB_VERSION = 3

# SSANIM01 v4 matrix record layout: <12f (3x4) => (m00,m01,m02,tx, m10,m11,m12,ty, m20,m21,m22,tz)
# tools/extraction/extract_fighter_anims.py:_mtx_concat emits ty at float index 7.
_MAT_BYTES = 12 * 4
_MAT_TY_BYTE_OFF = 7 * 4
# Horizontal axis convention matches extract_ecb_extents.py: the skeleton's facing axis is model
# Z, so the ledge-grab `ecb.bottom.x` term is the min-ty joint's tz.
# refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044164,mpColl_800443C4} (cd->ecb.bottom.x)
_MAT_TZ_BYTE_OFF = 11 * 4


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
    values: array.array  # 'f'
    values_x: array.array  # 'f' (model-Z of the min-ty joint; ledge-grab ecb.bottom.x term)


def _extract_ecb_bottom_for_anim_file(*, anims: Path, attrs: Path) -> list[_EcbAnim]:
    buf = anims.read_bytes()
    hdr = _read_anim_header(buf)
    ecb_joints = _load_ecb_joints(attrs)

    joint_count = len(hdr.joint_parts)
    if joint_count == 0:
        raise ValueError("SSANIM01: joint_count=0")

    # Map FtPart id -> index in joint_parts stream.
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

        # Matrices: [frame][joint] of 3x4 matrices.
        mats_bytes = int(frame_count) * joint_count * _MAT_BYTES
        transn_bytes = int(frame_count) * 3 * 4  # v4 tail
        need = mats_bytes + transn_bytes
        if off + need > len(buf):
            raise ValueError(
                f"SSANIM01: truncated anim payload: msid={msid} frame_count={frame_count} off={off}"
            )

        vals = array.array("f")
        vals_x = array.array("f")
        if frame_count:
            for fi in range(int(frame_count)):
                frame_base = off + fi * joint_count * _MAT_BYTES
                min_y = math.inf
                min_tz = 0.0
                for ji in ecb_joint_indices:
                    base = frame_base + ji * _MAT_BYTES
                    ty = float(_f32_le(buf, base + _MAT_TY_BYTE_OFF))
                    if not math.isfinite(ty):
                        raise ValueError(f"SSANIM01: non-finite ty for msid={msid} frame={fi} part_i={ji}")
                    if ty < min_y:
                        min_y = ty
                        min_tz = float(_f32_le(buf, base + _MAT_TZ_BYTE_OFF))
                if not math.isfinite(min_y):
                    min_y = 0.0
                    min_tz = 0.0
                vals.append(float(min_y))
                vals_x.append(float(min_tz) if math.isfinite(min_tz) else 0.0)

        out.append(_EcbAnim(msid=int(msid), frame_count=int(frame_count), values=vals, values_x=vals_x))
        off += need

    # Extra bytes are allowed (future extension), but are unexpected today; fail loudly.
    if off != len(buf):
        raise ValueError(f"SSANIM01: unexpected trailing bytes: {len(buf) - off}")

    # Sort for deterministic lookup and binary search in C.
    out.sort(key=lambda a: a.msid)
    return out


def _write_ecb_bottom_table(path: Path, anims: list[_EcbAnim]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    header_size = 8 + 4 + 2 + 2 + 4 + 4
    toc_offset = header_size
    toc_entry_size = 2 + 2 + 4 + 4
    toc_bytes = len(anims) * toc_entry_size

    values_off = toc_offset + toc_bytes
    entries: list[tuple[int, int, int, int]] = []
    for a in anims:
        values_bytes = int(a.frame_count) * 4
        entries.append((int(a.msid), int(a.frame_count), int(values_off), int(values_bytes)))
        values_off += values_bytes

    file_size = values_off

    with path.open("wb") as f:
        f.write(ECB_MAGIC)
        f.write(struct.pack("<IHHII", ECB_VERSION, len(anims), 0, toc_offset, file_size))
        for msid, frame_count, off, values_bytes in entries:
            f.write(struct.pack("<HHII", msid & 0xFFFF, frame_count & 0xFFFF, off, values_bytes))
        for a in anims:
            if a.frame_count == 0:
                continue
            vals = a.values
            if vals.typecode != "f":
                raise RuntimeError("internal: values must be array('f')")
            if sys.byteorder != "little":
                vals = array.array("f", vals)
                vals.byteswap()
            f.write(vals.tobytes())


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract per-msid ECB bottom Y tables from SSANIM01 v4 matrices."
    )
    ap.add_argument("--character", type=str, required=True, help="character key (fox,falco,...)")
    ap.add_argument("--anims", type=Path, default=None, help="path to data/anims/<char>.bin (SSANIM01 v4)")
    ap.add_argument(
        "--attrs",
        type=Path,
        default=None,
        help="path to data/characters/<char>.json (must contain ecb_joints)",
    )
    ap.add_argument("--out", type=Path, default=None, help="output path (default: data/ecb/<char>_bottom.bin)")
    args = ap.parse_args()

    ch = args.character.strip().lower()
    anims = args.anims or (Path("data/anims") / f"{ch}.bin")
    attrs = args.attrs or (Path("data/characters") / f"{ch}.json")
    out = args.out or (Path("data/ecb") / f"{ch}_bottom.bin")

    if not anims.exists():
        raise SystemExit(f"missing input anim file: {anims}")
    if not attrs.exists():
        raise SystemExit(f"missing input attrs file: {attrs}")

    ecb = _extract_ecb_bottom_for_anim_file(anims=anims, attrs=attrs)
    _write_ecb_bottom_table(out, ecb)
    print(f"wrote {out} ({len(ecb)} animations)")

    # NOTE: a parallel *_bottom_x.bin (posed bottom-X) table was generated here historically
    # for the ledge-grab cd->ecb.bottom.x term. It was retired: mpCollInterpolateECB runs with
    # time=1.0 on the final substep, so cd->ecb snaps to desired_ecb whose bottom point is
    # CENTERED (bottom.x = 0 relative to cur_pos); the posed sample was over-fit and broke
    # flip-pose apex catches (see tests/test_ledge_grab_ecb_replay_real_locks.py).
    # refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB


if __name__ == "__main__":
    main()
