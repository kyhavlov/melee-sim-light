from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from dataclasses import dataclass

from tools.extraction.known_data_artifacts import (
    ITEM_ARTICLE_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID,
    ITEM_ARTICLE_MAGIC,
    ITEM_ARTICLE_VALUE_F32,
    ITEM_ARTICLE_VALUE_U16,
    ITEM_ARTICLE_VALUE_U32,
    ITEM_ARTICLE_VERSION,
)


from tools.extraction.char_registry import CHARS

CHAR_IDS = {name: info.external_id for name, info in CHARS.items() if info.exports_item_article_constants}
ILLUSION_ITEM_KINDS = {
    # refs/melee/src/melee/it/forward.h::ItemKind
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_OnLoad
    "fox": 56,
    # refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_OnLoad
    "falco": 57,
}
SHEIK_SPECIAL_ARTICLE_CONSTANTS = {
    # refs/melee/src/melee/it/forward.h::ItemKind
    # refs/melee/src/melee/it/items/itseakchain.c::itSeakChain_Spawn
    "chain_itkind": 97,
    # Item_80268B18 seeds generic item lifetime from ItemCommonData; keep this article default
    # data-owned alongside the source article kind so runtime has no Sheik Chain literals.
    "chain_lifetime_frames": 1400,
    # refs/melee/src/melee/it/forward.h::ItemKind
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    "vanish_itkind": 85,
}
SHEIK_SPECIAL_ARTICLE_COMMON_LIFETIME_KEYS = {
    "vanish_lifetime_frames": "default_spawn_lifetime_frames",
}

UNIT_ITEM_KIND = 1
UNIT_PART_ID = 2
UNIT_FRAMES = 3
UNIT_DAMAGE = 4
UNIT_SIZE = 5
UNIT_DEGREES = 6
UNIT_VELOCITY = 7
UNIT_COUNT = 8
UNIT_BONE_ID = 9
UNIT_FLAGS = 10


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
    "needle_throw_itkind": FieldSpec(13, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "needle_held_itkind": FieldSpec(14, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "needle_lifetime_frames": FieldSpec(15, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "needle_bounce_lifetime_frames": FieldSpec(16, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "needle_launch_speed": FieldSpec(17, ITEM_ARTICLE_VALUE_F32, UNIT_VELOCITY),
    "needle_hurtbox_count": FieldSpec(18, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "needle_hurtbox_bone_id": FieldSpec(19, ITEM_ARTICLE_VALUE_U16, UNIT_BONE_ID),
    "needle_hurtbox_a_offset_x": FieldSpec(20, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_a_offset_y": FieldSpec(21, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_a_offset_z": FieldSpec(22, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_b_offset_x": FieldSpec(23, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_b_offset_y": FieldSpec(24, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_b_offset_z": FieldSpec(25, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hurtbox_scale": FieldSpec(26, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "needle_hitbox_damage": FieldSpec(27, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE),
    "chain_itkind": FieldSpec(28, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "chain_lifetime_frames": FieldSpec(29, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "vanish_itkind": FieldSpec(30, ITEM_ARTICLE_VALUE_U16, UNIT_ITEM_KIND),
    "vanish_lifetime_frames": FieldSpec(31, ITEM_ARTICLE_VALUE_U32, UNIT_FRAMES),
    "needle_hitbox_count": FieldSpec(32, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    **{
        f"needle_hitbox_damage_by_id_{i}": FieldSpec(33 + i, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_size_{i}": FieldSpec(37 + i, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_x_offset_{i}": FieldSpec(41 + i, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_y_offset_{i}": FieldSpec(45 + i, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_z_offset_{i}": FieldSpec(49 + i, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_angle_{i}": FieldSpec(53 + i, ITEM_ARTICLE_VALUE_U16, UNIT_DEGREES)
        for i in range(4)
    },
    **{
        f"needle_hitbox_kbg_{i}": FieldSpec(57 + i, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT)
        for i in range(4)
    },
    **{
        f"needle_hitbox_wsk_{i}": FieldSpec(61 + i, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT)
        for i in range(4)
    },
    **{
        f"needle_hitbox_bkb_{i}": FieldSpec(65 + i, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT)
        for i in range(4)
    },
    **{
        f"needle_hitbox_element_{i}": FieldSpec(69 + i, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT)
        for i in range(4)
    },
    **{
        f"needle_hitbox_shield_damage_{i}": FieldSpec(73 + i, ITEM_ARTICLE_VALUE_U16, UNIT_DAMAGE)
        for i in range(4)
    },
    **{
        f"needle_hitbox_flags_{i}": FieldSpec(77 + i, ITEM_ARTICLE_VALUE_U32, UNIT_FLAGS)
        for i in range(4)
    },
    "vanish_hitbox_count": FieldSpec(81, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_damage": FieldSpec(82, ITEM_ARTICLE_VALUE_F32, UNIT_DAMAGE),
    "vanish_hitbox_size": FieldSpec(83, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_x_offset": FieldSpec(84, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_y_offset": FieldSpec(85, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_z_offset": FieldSpec(86, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_angle": FieldSpec(87, ITEM_ARTICLE_VALUE_U16, UNIT_DEGREES),
    "vanish_hitbox_kbg": FieldSpec(88, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_wsk": FieldSpec(89, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_bkb": FieldSpec(90, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_element": FieldSpec(91, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_shield_damage": FieldSpec(92, ITEM_ARTICLE_VALUE_U16, UNIT_DAMAGE),
    "vanish_hitbox_flags": FieldSpec(93, ITEM_ARTICLE_VALUE_U32, UNIT_FLAGS),
    "vanish_hitbox_size_keyframe_count": FieldSpec(94, ITEM_ARTICLE_VALUE_U16, UNIT_COUNT),
    "vanish_hitbox_size_keyframe_frame_0": FieldSpec(95, ITEM_ARTICLE_VALUE_U16, UNIT_FRAMES),
    "vanish_hitbox_size_keyframe_value_0": FieldSpec(96, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_size_keyframe_frame_1": FieldSpec(97, ITEM_ARTICLE_VALUE_U16, UNIT_FRAMES),
    "vanish_hitbox_size_keyframe_value_1": FieldSpec(98, ITEM_ARTICLE_VALUE_F32, UNIT_SIZE),
    "vanish_hitbox_remove_frame": FieldSpec(99, ITEM_ARTICLE_VALUE_U16, UNIT_FRAMES),
}

SHEIK_NEEDLE_FIELD_NAMES = tuple(
    name for name in FIELD_SPECS if name.startswith("needle_")
)
SHEIK_SPECIAL_ARTICLE_FIELD_NAMES = tuple(SHEIK_SPECIAL_ARTICLE_CONSTANTS)
SHEIK_SPECIAL_ARTICLE_FIELD_NAMES += tuple(SHEIK_SPECIAL_ARTICLE_COMMON_LIFETIME_KEYS)
SHEIK_VANISH_HITBOX_FIELD_NAMES = tuple(
    name for name in FIELD_SPECS if name.startswith("vanish_hitbox_")
)


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
        ITEM_ARTICLE_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID,
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
    common = json.loads(item_common.read_text(encoding="utf-8"))
    for ch in chars:
        if ch not in CHAR_IDS:
            continue
        attrs = json.loads((attrs_dir / f"{ch}.json").read_text(encoding="utf-8"))
        if ch == "sheik":
            missing = [
                key
                for key in SHEIK_NEEDLE_FIELD_NAMES
                if key not in attrs
                and not key.startswith("needle_hurtbox_a_offset_")
                and not key.startswith("needle_hurtbox_b_offset_")
                and not (key.startswith("needle_hitbox_") and key[-1].isdigit())
            ]
            for vec_key in ("needle_hurtbox_a_offset", "needle_hurtbox_b_offset"):
                vals = attrs.get(vec_key)
                if not isinstance(vals, list) or len(vals) < 3:
                    missing.append(vec_key)
            for vec_key in (
                "needle_hitbox_damage_by_id",
                "needle_hitbox_size",
                "needle_hitbox_x_offset",
                "needle_hitbox_y_offset",
                "needle_hitbox_z_offset",
                "needle_hitbox_angle",
                "needle_hitbox_kbg",
                "needle_hitbox_wsk",
                "needle_hitbox_bkb",
                "needle_hitbox_element",
                "needle_hitbox_shield_damage",
                "needle_hitbox_flags",
            ):
                vals = attrs.get(vec_key)
                if not isinstance(vals, list) or len(vals) < 4:
                    missing.append(vec_key)
            for key in SHEIK_VANISH_HITBOX_FIELD_NAMES:
                if key.startswith("vanish_hitbox_size_keyframe_frame_"):
                    vals = attrs.get("vanish_hitbox_size_keyframe_frame")
                    if not isinstance(vals, list) or len(vals) < 2:
                        missing.append("vanish_hitbox_size_keyframe_frame")
                    continue
                if key.startswith("vanish_hitbox_size_keyframe_value_"):
                    vals = attrs.get("vanish_hitbox_size_keyframe_value")
                    if not isinstance(vals, list) or len(vals) < 2:
                        missing.append("vanish_hitbox_size_keyframe_value")
                    continue
                if key not in attrs:
                    missing.append(key)
            if missing:
                raise ValueError(
                    "Sheik exports MSLITAR1 special article fields but "
                    f"{attrs_dir / f'{ch}.json'} is missing: {', '.join(sorted(missing))}"
                )
        char_id = CHAR_IDS[ch]
        for key, spec in FIELD_SPECS.items():
            if key == "shield_bounce_extra_degrees":
                continue
            if key == "side_special_illusion_itkind":
                if ch not in ILLUSION_ITEM_KINDS:
                    continue
                out.append((char_id, spec, float(ILLUSION_ITEM_KINDS[ch])))
                continue
            if key in SHEIK_SPECIAL_ARTICLE_CONSTANTS:
                if ch == "sheik":
                    out.append((char_id, spec, float(SHEIK_SPECIAL_ARTICLE_CONSTANTS[key])))
                continue
            if key in SHEIK_SPECIAL_ARTICLE_COMMON_LIFETIME_KEYS:
                if ch == "sheik":
                    common_key = SHEIK_SPECIAL_ARTICLE_COMMON_LIFETIME_KEYS[key]
                    if common_key not in common:
                        raise ValueError(
                            f"{item_common} is missing required Sheik article lifetime key {common_key}"
                        )
                    out.append((char_id, spec, float(common[common_key])))
                continue
            if key.startswith("needle_hurtbox_a_offset_"):
                vals = attrs.get("needle_hurtbox_a_offset")
                comp = "xyz".index(key[-1])
                if isinstance(vals, list) and len(vals) > comp:
                    out.append((char_id, spec, float(vals[comp])))
                continue
            if key.startswith("needle_hurtbox_b_offset_"):
                vals = attrs.get("needle_hurtbox_b_offset")
                comp = "xyz".index(key[-1])
                if isinstance(vals, list) and len(vals) > comp:
                    out.append((char_id, spec, float(vals[comp])))
                continue
            if key.startswith("needle_hitbox_") and key[-1].isdigit():
                idx = int(key[-1])
                base_key = key[:-2]
                vals = attrs.get(base_key)
                if isinstance(vals, list) and len(vals) > idx:
                    out.append((char_id, spec, float(vals[idx])))
                continue
            if key.startswith("vanish_hitbox_size_keyframe_") and key[-1].isdigit():
                idx = int(key[-1])
                base_key = key[:-2]
                vals = attrs.get(base_key)
                if isinstance(vals, list) and len(vals) > idx:
                    out.append((char_id, spec, float(vals[idx])))
                continue
            if key in attrs:
                out.append((char_id, spec, float(attrs[key])))
    if "shield_bounce_extra_degrees" in common:
        for ch in chars:
            if ch not in CHAR_IDS:
                continue
            out.append((CHAR_IDS[ch], FIELD_SPECS["shield_bounce_extra_degrees"], float(common["shield_bounce_extra_degrees"])))
    return sorted(out, key=lambda row: (row[0], row[1].field_id))


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack known fighter item/article owner constants as MSLITAR1.")
    ap.add_argument("--attrs-dir", type=Path, default=Path("data/characters"))
    ap.add_argument("--item-common", type=Path, default=Path("data/items/item_common.json"))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, default=None)
    ap.add_argument("--chars", type=str, default="fox,falco,sheik")
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
                "id": ITEM_ARTICLE_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID,
                "name": "Slippi/CSS external character id",
                "note": "This is not the runtime MslSeed internal character id domain.",
            },
            "units": [
                {"id": UNIT_ITEM_KIND, "name": "item_kind"},
                {"id": UNIT_PART_ID, "name": "fighter_part_id"},
                {"id": UNIT_FRAMES, "name": "frames"},
                {"id": UNIT_DAMAGE, "name": "damage"},
                {"id": UNIT_SIZE, "name": "size"},
                {"id": UNIT_DEGREES, "name": "degrees"},
                {"id": UNIT_VELOCITY, "name": "velocity"},
                {"id": UNIT_COUNT, "name": "count"},
                {"id": UNIT_BONE_ID, "name": "bone_id"},
                {"id": UNIT_FLAGS, "name": "flags"},
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
