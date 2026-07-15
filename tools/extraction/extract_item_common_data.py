from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive
from melee_sim.raw_data import raw_data_dir


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract ItemCommonData constants from ItCo.dat.")
    ap.add_argument(
        "--itco",
        type=Path,
        default=raw_data_dir() / "ItCo.dat",
        help="path to ItCo.dat",
    )
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
        # ftColl_8007A06C item-damage facing owner:
        #   if abs(item->x40_vel.x) < it_804D6D28->x78_float, use item position vs victim;
        #   otherwise use item velocity sign.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
        # refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
        "item_damage_facing_velocity_threshold": float(_f32_be(buf, item_common_abs + 0x78)),
        # it_80275158 sets item->xD48_halfLifeTimer = lifetime * it_804D6D28->x4C_float when an item's
        # lifeTimer is (re)seeded. it_2725_Logic109_Reflected then assigns xD44_lifeTimer =
        # xD48_halfLifeTimer, so a reflected Sheik Needle's remaining life becomes spawn_life * x4C.
        # refs/melee/src/melee/it/it_2725.c::{it_80275158,it_2725_Logic109_Reflected}
        # refs/melee/src/melee/it/types.h::ItemCommonData::x4C_float
        "reflect_half_life_fraction": float(_f32_be(buf, item_common_abs + 0x4C)),
        # Item_8026B424 computes item hitlag as:
        #   (s32)(damage * it_804D6D28->xB8 + it_804D6D28->xBC)
        # refs/melee/src/melee/it/it_26B1.c::it_8026B424
        # refs/melee/src/melee/it/types.h::ItemCommonData::{xB8,xBC}
        "item_hitlag_damage_mul": float(_f32_be(buf, item_common_abs + 0xB8)),
        "item_hitlag_base": float(_f32_be(buf, item_common_abs + 0xBC)),
        # Item_80269DC8 compares `item->xC54` against:
        #   deg_to_rad(90 + it_804D6D28->unk_degrees)
        # refs/melee/src/melee/it/item.c::Item_80269DC8
        # refs/melee/src/melee/it/types.h::ItemCommonData::unk_degrees at 0xE0
        "shield_bounce_extra_degrees": float(_f32_be(buf, item_common_abs + 0xE0)),
        # it_8027518C overwrites several spawned item lifetimes from ItemCommonData::xF8 after
        # item-local setup. Sheik's Vanish smoke calls this path immediately after seeding 60.0f,
        # so the replay-visible xD44_lifeTimer is this common value instead.
        # refs/melee/src/melee/it/it_2725.c::it_8027518C
        # refs/melee/src/melee/it/items/itseakvanish.c::it_802B1D40
        # refs/melee/src/melee/it/types.h::ItemCommonData::xF8
        "default_spawn_lifetime_frames": float(_f32_be(buf, item_common_abs + 0xF8)),
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
