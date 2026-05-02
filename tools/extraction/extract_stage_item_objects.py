from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from melee_sim.hsd_archive import HsdArchive, parse_hsd_archive
from tools.extraction.extract_fighter_anims import _FObj
from tools.slippi.known_data_artifacts import (
    STAGE_ITEM_OBJECT_MAGIC,
    STAGE_ITEM_OBJECT_VERSION,
    STAGE_YOSHIS_STORY,
)


ITEM_KIND_HEIHO = 0xD2


def _f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _u16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=False)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def _ptr32(arc: HsdArchive, abs_off: int) -> int:
    ptr = _u32_be(arc.buf, abs_off)
    return arc.data_base + ptr if ptr != 0 else 0


def _extract_heiho_article(arc: HsdArchive) -> tuple[int, int, list[float], float, int]:
    """Return `(article_abs, state0_anim_abs, special_attrs, fall_accel, kind)`.

    Source shape:
    - `stage_info.itemdata` is an array of stage item entries.
    - `ground.c` registers each entry's `unk4` Article under `unk0` ItemKind.
    - `refs/melee/src/melee/gr/types.h::StageInfo.itemdata`
    - `refs/melee/src/melee/it/types.h::{Article,ItemAttr,ItemStateDesc}`
    """

    itemdata = arc.get_public_offset("itemdata")
    if itemdata is None:
        raise ValueError("GrSt.dat missing itemdata public symbol")

    p = itemdata
    while True:
        entry = _ptr32(arc, p)
        p += 4
        if entry == 0:
            break
        kind = _u32_be(arc.buf, entry + 0)
        article = _ptr32(arc, entry + 4)
        if kind != ITEM_KIND_HEIHO:
            continue

        attr = _ptr32(arc, article + 0x00)
        special = _ptr32(arc, article + 0x04)
        states = _ptr32(arc, article + 0x0C)
        if attr == 0 or special == 0 or states == 0:
            raise ValueError("GrSt.dat Heiho article has null required table")

        fall_accel = _f32_be(arc.buf, attr + 0x10)
        special_attrs = [_f32_be(arc.buf, special + i * 4) for i in range(7)]
        state0_anim = _ptr32(arc, states + 0x00)
        return article, state0_anim, special_attrs, fall_accel, kind

    raise ValueError("GrSt.dat itemdata missing It_Kind_Heiho article")


def _extract_child_jobj_tray_fobj_deltas(arc: HsdArchive, state0_anim: int) -> list[float]:
    """Extract the child-JObj Y translation delta stream used by `it_802D98C4`.

    The active Shy Guy item calls `it_802D98C4` on dynamic bone 1, which follows the child JObj's
    translation-Y AObj FObj. This interpreter mirrors sysdolphin/baselib FObj semantics through the
    existing extractor port.
    refs/melee/src/melee/it/items/itheiho.c::{it_802D8918,it_802D98C4}
    refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c,jobj.c}
    """

    child_anim = _ptr32(arc, state0_anim + 0x00)
    if child_anim == 0:
        raise ValueError("GrSt.dat Heiho state0 AnimJoint missing child")
    aobj = _ptr32(arc, child_anim + 0x08)
    if aobj == 0:
        raise ValueError("GrSt.dat Heiho child AnimJoint missing AObjDesc")
    fdesc = _ptr32(arc, aobj + 0x08)
    if fdesc == 0:
        raise ValueError("GrSt.dat Heiho child AObjDesc missing FObjDesc")

    length = _u32_be(arc.buf, fdesc + 0x04)
    startframe = _f32_be(arc.buf, fdesc + 0x08)
    obj_type = arc.buf[fdesc + 0x0C]
    frac_value = arc.buf[fdesc + 0x0D]
    frac_slope = arc.buf[fdesc + 0x0E]
    ad_abs = _ptr32(arc, fdesc + 0x10)
    if obj_type != 6:
        raise ValueError(f"unexpected Heiho child FObj type: {obj_type}")
    if length <= 0 or ad_abs == 0:
        raise ValueError("invalid Heiho child FObjDesc payload")

    fo = _FObj(
        ad=memoryview(arc.buf),
        ad_head=ad_abs,
        length=int(length),
        startframe=int(startframe),
        obj_type=int(obj_type),
        frac_value=int(frac_value),
        frac_slope=int(frac_slope),
    )
    fo.req_anim(0.0)
    values: list[float] = []
    for _ in range(128):
        out = fo.interpret(1.0)
        if not out:
            raise ValueError("Heiho child FObj produced no value")
        values.append(_f32(float(out[-1])))

    # Runtime wants the visible x40_vel.y delta sequence. The first active sample starts from the
    # reset x3C=0 baseline; subsequent samples are frame-to-frame deltas.
    deltas = [values[0]]
    deltas.extend(_f32(values[i] - values[i - 1]) for i in range(1, len(values)))
    return deltas


def _extract_yoshi_shyguy(grst: Path) -> dict:
    arc = parse_hsd_archive(grst.read_bytes())
    yak = arc.get_public_offset("yakumono_param")
    if yak is None:
        raise ValueError("GrSt.dat missing yakumono_param public symbol")

    article_abs, state0_anim, special_attrs, fall_accel, item_kind = _extract_heiho_article(arc)
    dyn_y_vel = _extract_child_jobj_tray_fobj_deltas(arc, state0_anim)
    return {
        "stage_id": STAGE_YOSHIS_STORY,
        "item_kind": item_kind,
        "timer_min": int(_f32_be(arc.buf, yak + 0x00)),
        "timer_rand": int(_f32_be(arc.buf, yak + 0x04)),
        "timer_reset": 120,
        "spawnmany_rarity": int(_f32_be(arc.buf, yak + 0x08)),
        "spawn_delay_step": 25,
        "fall_accel": _f32(fall_accel),
        "spawn_left_x": _f32(-292.0),
        "spawn_right_x": _f32(304.0),
        "jitter_y_amp": _f32(3.0),
        "state4_speed_mul": _f32(1.5),
        "vpos": [_f32(_f32_be(arc.buf, yak + 0x0C + i * 4)) for i in range(6)],
        "speed": [_f32(v) for v in special_attrs[1:4]],
        "dyn_y_vel": dyn_y_vel,
        "source": {
            "stage_dat": str(grst),
            "yakumono_param_rel": yak - arc.data_base,
            "heiho_article_rel": article_abs - arc.data_base,
            "state0_anim_joint_rel": state0_anim - arc.data_base,
            "refs": [
                "refs/melee/src/melee/gr/grstory.c::{reset_shyguy_timer,grStory_801E3418}",
                "refs/melee/src/melee/it/items/itheiho.c::{it_802D8618,itHeiho_UnkMotion*_Phys,it_802D98C4}",
                "refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c,jobj.c}",
            ],
        },
    }


def _write_bin(path: Path, data: dict) -> None:
    vpos = [float(x) for x in data["vpos"]]
    speed = [float(x) for x in data["speed"]]
    dyn_y_vel = [float(x) for x in data["dyn_y_vel"]]
    if len(vpos) != 6 or len(speed) != 3 or len(dyn_y_vel) != 128:
        raise ValueError("unexpected Shy Guy table dimensions")

    buf = bytearray()
    buf += STAGE_ITEM_OBJECT_MAGIC
    buf += struct.pack(
        "<IHHHHHHHHHHfffff",
        STAGE_ITEM_OBJECT_VERSION,
        int(data["stage_id"]) & 0xFFFF,
        int(data["item_kind"]) & 0xFFFF,
        len(vpos),
        len(speed),
        len(dyn_y_vel),
        int(data["timer_min"]) & 0xFFFF,
        int(data["timer_rand"]) & 0xFFFF,
        int(data["timer_reset"]) & 0xFFFF,
        int(data["spawnmany_rarity"]) & 0xFFFF,
        int(data["spawn_delay_step"]) & 0xFFFF,
        _f32(data["fall_accel"]),
        _f32(data["spawn_left_x"]),
        _f32(data["spawn_right_x"]),
        _f32(data["state4_speed_mul"]),
        _f32(data["jitter_y_amp"]),
    )
    buf += struct.pack("<" + "f" * len(vpos), *[_f32(x) for x in vpos])
    buf += struct.pack("<" + "f" * len(speed), *[_f32(x) for x in speed])
    buf += struct.pack("<" + "f" * len(dyn_y_vel), *[_f32(x) for x in dyn_y_vel])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(buf))


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract stage-owned item object data.")
    ap.add_argument("--grst", type=Path, default=Path("_iso/GrSt.dat"), help="path to GrSt.dat")
    ap.add_argument("--out", type=Path, default=Path("data/stage_items/yoshi_shyguy.bin"))
    ap.add_argument("--audit", type=Path, default=Path("data/stage_items/yoshi_shyguy.json"))
    args = ap.parse_args()

    data = _extract_yoshi_shyguy(args.grst)
    _write_bin(args.out, data)
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    args.audit.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
