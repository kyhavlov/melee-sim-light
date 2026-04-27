from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def main() -> None:
    # Note: `data/items/item_common.json` is tracked. Re-run this extractor after any changes here
    # or when switching ISO so item-common gameplay constants stay source-backed.
    ap = argparse.ArgumentParser(description="Extract ItemCommonData constants from ItCo.dat.")
    ap.add_argument("--itco", type=Path, default=Path("_iso/ItCo.dat"), help="path to ItCo.dat")
    ap.add_argument("--out", type=Path, default=Path("data/items/item_common.json"))
    args = ap.parse_args()

    buf = args.itco.read_bytes()
    arc = parse_hsd_archive(buf)

    public_abs = arc.get_public_offset("itPublicData")
    if public_abs is None:
        raise SystemExit("itPublicData not found in public symbols")

    item_common_ptr = _u32_be(buf, public_abs)
    item_common_abs = arc.data_base + item_common_ptr

    out = {
        # Item_80269DC8 compares `item->xC54` against:
        #   deg_to_rad(90 + it_804D6D28->unk_degrees)
        # refs/melee/src/melee/it/item.c::Item_80269DC8
        # refs/melee/src/melee/it/types.h::ItemCommonData::unk_degrees at 0xE0
        "shield_bounce_extra_degrees": float(_f32_be(buf, item_common_abs + 0xE0)),
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
