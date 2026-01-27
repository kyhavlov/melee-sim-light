from __future__ import annotations

import argparse
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def main() -> None:
    # Decomp-first references (GALE01):
    # - `Fighter_804D6548 = pData[3];` where pData is the `ftLoadCommonData` pointer list loaded
    #   from PlCo.dat. refs/melee/src/melee/ft/fighter.c::Fighter_LoadCommonData
    # - Staling multiplier subtracts these weights for each occurrence in the last 9 stale entries:
    #   `var_f1 -= Fighter_804D6548[i];` for i=0..8.
    #   refs/melee/src/melee/ft/ft_0881.c::ft_80089118
    ap = argparse.ArgumentParser(
        description="Extract stale-move damage weights (Fighter_804D6548) from PlCo.dat (decomp-first)."
    )
    ap.add_argument("--plco", type=Path, default=Path("_iso/PlCo.dat"), help="path to PlCo.dat (HSD archive)")
    ap.add_argument("--out", type=Path, default=Path("data/staling/weights.bin"))
    args = ap.parse_args()

    buf = args.plco.read_bytes()
    arc = parse_hsd_archive(buf)

    ft = arc.get_public_offset("ftLoadCommonData")
    if ft is None:
        raise SystemExit("ftLoadCommonData not found in public symbols")

    # Fighter_LoadCommonData expects 23 pointers.
    n_ptr = 23
    raw = buf[ft : ft + n_ptr * 4]
    if len(raw) != n_ptr * 4:
        raise SystemExit("ftLoadCommonData out of bounds")

    ptrs = [_u32_be(raw, i * 4) for i in range(n_ptr)]
    weights_abs = arc.data_base + ptrs[3]

    # `Fighter_804D6548` is indexed by i=0..8 in ft_80089118.
    count = 9
    weights = [_f32_be(buf, weights_abs + 4 * i) for i in range(count)]

    # File format v1:
    #   u8  magic[8] = "MSLSTW01"
    #   u32 version = 1
    #   u16 count = 9
    #   u16 reserved = 0
    #   u32 file_bytes
    #   f32 weights[count] (little-endian)
    magic = b"MSLSTW01"
    version = 1
    hdr_bytes = 8 + 4 + 2 + 2 + 4
    file_bytes = hdr_bytes + 4 * count

    out = bytearray()
    out += magic
    out += struct.pack("<I", version)
    out += struct.pack("<H", count)
    out += struct.pack("<H", 0)
    out += struct.pack("<I", file_bytes)
    for w in weights:
        out += struct.pack("<f", float(w))

    if len(out) != file_bytes:
        raise RuntimeError(f"internal error: wrote {len(out)} bytes but header claims {file_bytes}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))


if __name__ == "__main__":
    main()
