from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from dataclasses import dataclass

from tools.extraction.known_data_artifacts import (
    ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND,
    ITEM_ARTICLE_MAGIC,
    ITEM_ARTICLE_VALUE_F32,
    ITEM_ARTICLE_VALUE_U16,
    ITEM_ARTICLE_VALUE_U32,
    ITEM_ARTICLE_VERSION,
)


from tools.extraction.char_registry import CHARS

# Article params exist only for characters whose ftData registers items (registry
# has_articles); article-less characters (Marth, ...) are skipped by _records.
CHAR_IDS = {name: info.external_id for name, info in CHARS.items() if info.has_articles}
ILLUSION_ITEM_KINDS = {
    # refs/melee/src/melee/it/forward.h::ItemKind
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_OnLoad
    "fox": 56,
    # refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_OnLoad
    "falco": 57,
}

UNIT_ITEM_KIND = 1
UNIT_PART_ID = 2
UNIT_FRAMES = 3
UNIT_DAMAGE = 4
UNIT_SIZE = 5
UNIT_DEGREES = 6


@dataclass(frozen=True)
class FieldSpec:
    field_id: int
    value_type: int
    unit_id: int


FIELD_SPECS = {
    "blaster_shot_itkind": FieldSpec(1, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "blaster_gun_itkind": FieldSpec(2, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "laser_spawn_joint_part_id": FieldSpec(3, ITEM_ARTICLE_VALUE_U16, UNIT_PART_ID),
    "laser_lifetime_frames": FieldSpec(4, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "laser_damage": FieldSpec(5, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE),
    "laser_size": FieldSpec(6, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "illusion_item_lifetime_state01_frames": FieldSpec(7, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "illusion_item_lifetime_state2_frames": FieldSpec(8, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "illusion_item_state0_damage": FieldSpec(9, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE),
    "illusion_item_state1_damage": FieldSpec(10, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE),
    "shield_bounce_extra_degrees": FieldSpec(11, ITEM_ARTICLE_VALUE_F32, UNIT_DEGREES),
    "side_special_illusion_itkind": FieldSpec(12, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
}


def _f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


def _pack_record(char_id: int, spec: FieldSpec, value: float) -> bytes:
    if spec.value_type in (ITEM_ARTICLE_VALUE_U16, ITEM_ARTICLE_VALUE_U32):
        u32_value = int(value)
        f32_value = 0.0
    elif spec.value_type == ITEM_ARTICLE_VALUE_F32:
        u32_value = 0
        f32_value = _f32(value)
    else:
        raise ValueError(f"unsupported MSLITAR1 value type: {spec.value_type}")
    return struct.pack(
        "<HBBHHIfII",
        int(char_id) & 0xFFFF,
        ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND,
        int(spec.value_type) & 0xFF,
        int(spec.field_id) & 0xFFFF,
        int(spec.unit_id) & 0xFFFF,
        int(u32_value) & 0xFFFF_FFFF,
        _f32(f32_value),
        0,
        0,
    )


def _records(chars: list[str], attrs_dir: Path, item_common: Path) -> list[tuple[int, FieldSpec, float]]:
    out: list[tuple[int, FieldSpec, float]] = []
    for ch in chars:
        if ch not in CHAR_IDS:
            continue
        attrs = json.loads((attrs_dir / f"{ch}.json").read_text(encoding="utf-8"))
        char_id = CHAR_IDS[ch]
        for key, spec in FIELD_SPECS.items():
            if key == "shield_bounce_extra_degrees":
                continue
            if key == "side_special_illusion_itkind":
                out.append((char_id, spec, float(ILLUSION_ITEM_KINDS[ch])))
                continue
            if key in attrs:
                out.append((char_id, spec, float(attrs[key])))
    common = json.loads(item_common.read_text(encoding="utf-8"))
    if "shield_bounce_extra_degrees" in common:
        for ch in chars:
            if ch not in CHAR_IDS:
                continue
            out.append((CHAR_IDS[ch], FIELD_SPECS["shield_bounce_extra_degrees"], float(common["shield_bounce_extra_degrees"])))
    return sorted(out, key=lambda row: (row[0], row[1].field_id))


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack known Fox/Falco item/article owner constants as MSLITAR1.")
    ap.add_argument("--attrs-dir", type=Path, default=Path("data/characters"))
    ap.add_argument("--item-common", type=Path, default=Path("data/items/item_common.json"))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, default=None)
    ap.add_argument("--chars", type=str, default="fox,falco")
    args = ap.parse_args()

    chars = [c.strip() for c in args.chars.split(",") if c.strip()]
    records = _records(chars, args.attrs_dir, args.item_common)
    buf = bytearray()
    buf += ITEM_ARTICLE_MAGIC
    buf += struct.pack("<II", ITEM_ARTICLE_VERSION, len(records))
    for char_id, spec, value in records:
        buf += _pack_record(char_id, spec, value)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(buf))
    if args.manifest is not None:
        payload = {
            "magic": ITEM_ARTICLE_MAGIC.decode("ascii"),
            "version": ITEM_ARTICLE_VERSION,
            "char_domain": {
                "id": ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND,
                "name": "GALE01 internal FighterKind enum",
                "note": "This is not Slippi/sim external character id domain.",
            },
            "units": [
                {"id": UNIT_ITEM_KIND, "name": "item_kind"},
                {"id": UNIT_PART_ID, "name": "fighter_part_id"},
                {"id": UNIT_FRAMES, "name": "frames"},
                {"id": UNIT_DAMAGE, "name": "damage"},
                {"id": UNIT_SIZE, "name": "size"},
                {"id": UNIT_DEGREES, "name": "degrees"},
            ],
            "value_types": [
                {"id": ITEM_ARTICLE_VALUE_U16, "name": "u16"},
                {"id": ITEM_ARTICLE_VALUE_U32, "name": "u32"},
                {"id": ITEM_ARTICLE_VALUE_F32, "name": "f32"},
            ],
            "fields": [
                {"id": spec.field_id, "name": name, "value_type": spec.value_type, "unit_id": spec.unit_id}
                for name, spec in sorted(FIELD_SPECS.items(), key=lambda kv: kv[1].field_id)
            ],
        }
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
