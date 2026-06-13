from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np


ENGINE_DUMP_MAGIC = b"MSIMDMP\0"
ENGINE_DUMP_VERSION = 12
ENGINE_DUMP_SUPPORTED_VERSIONS = (6, 7, 8, 9, 10, 11, 12)

HEADER_DTYPE = np.dtype(
    [
        ("magic", "S8"),
        ("version", "<u4"),
        ("endian_tag", "<u4"),
        ("frame_count", "<u4"),
        ("port_count", "u1"),
        ("stage_id", "<u2"),
        ("is_teams", "u1"),
        # v12+: dense dump slots map to these 1-based controller ports.
        # Older dumps leave this zeroed and readers fall back to 1..port_count.
        ("port_ids", "u1", (4,)),
        ("_pad0", "V13"),
        ("frames_offset", "<u4"),
        ("inputs_offset", "<u4"),
        ("fighters_offset", "<u4"),
        ("items_offset", "<u4"),
        ("hitboxes_offset", "<u4"),
        ("hurtboxes_offset", "<u4"),
        ("total_items", "<u4"),
        # v7+: offset of fighter hitlist-provenance section (v6 keeps this 0 in padding).
        ("hitlists_offset", "<u4"),
        # v10+: offset of item hitbox hitlist/callback section (older dumps keep this 0 in padding).
        ("item_hitlists_offset", "<u4"),
        ("_pad1", "V3"),
    ],
    align=False,
)

FRAME_DTYPE = np.dtype(
    [
        ("frame_index", "<i4"),
        ("rng_state", "<u4"),
        ("rng_seed", "<u4"),
        ("rng_steps", "<u4"),
        ("item_count", "<u2"),
        ("item_offset", "<u4"),
        ("flags", "<u2"),
        ("game_timer", "<u4"),
        ("randall_exists", "u1"),
        ("randall_x_bits", "<u4"),
        ("randall_y_bits", "<u4"),
        ("fountain0_exists", "u1"),
        ("fountain0_y_bits", "<u4"),
        ("fountain1_exists", "u1"),
        ("fountain1_y_bits", "<u4"),
        ("_pad0", "u1"),
    ],
    align=False,
)

INPUT_DTYPE = np.dtype(
    [
        ("buttons", "<u4"),
        ("stick_x_bits", "<u4"),
        ("stick_y_bits", "<u4"),
        ("cstick_x_bits", "<u4"),
        ("cstick_y_bits", "<u4"),
        ("l_shoulder_bits", "<u4"),
        ("r_shoulder_bits", "<u4"),
        ("raw_stick_x_u8", "u1"),
        ("raw_stick_y_u8", "u1"),
        ("raw_cstick_x_u8", "u1"),
        ("raw_cstick_y_u8", "u1"),
    ],
    align=False,
)

FIGHTER_DTYPE_V7 = np.dtype(
    [
        ("flags", "<u4"),
        ("pos_x_bits", "<u4"),
        ("pos_y_bits", "<u4"),
        ("pos_z_bits", "<u4"),
        ("self_vel_x_bits", "<u4"),
        ("self_vel_y_bits", "<u4"),
        ("gr_vel_bits", "<u4"),
        ("action_state", "<u2"),
        ("anim_id", "<u2"),
        ("anim_frame_bits", "<u4"),
        ("action_frame_bits", "<u4"),
        ("state_flags_2218", "u1"),
        ("state_flags_221a", "u1"),
        ("state_flags_221b", "u1"),
        ("state_flags_221c", "u1"),
        ("state_flags_221f", "u1"),
        ("invulnerable", "u1"),
        ("ground_or_air", "u1"),
        ("stocks", "u1"),
        ("team", "u1"),
        ("costume_id", "u1"),
        ("facing_bits", "<u4"),
        ("percent_bits", "<u4"),
        ("hitlag_left_bits", "<u4"),
        ("misc_as_bits", "<u4"),
        ("shield_health_bits", "<u4"),
        ("ecb_top_x_bits", "<u4"),
        ("ecb_top_y_bits", "<u4"),
        ("ecb_bottom_x_bits", "<u4"),
        ("ecb_bottom_y_bits", "<u4"),
        ("ecb_left_x_bits", "<u4"),
        ("ecb_left_y_bits", "<u4"),
        ("ecb_right_x_bits", "<u4"),
        ("ecb_right_y_bits", "<u4"),
        ("floor_normal_x_bits", "<u4"),
        ("floor_normal_y_bits", "<u4"),
        ("ground_accel_1_bits", "<u4"),
        ("ground_accel_2_bits", "<u4"),
        ("anim_vel_x_bits", "<u4"),
        ("anim_vel_y_bits", "<u4"),
        ("_pad0", "V2"),
    ],
    align=False,
)

FIGHTER_DTYPE_V8 = np.dtype(
    FIGHTER_DTYPE_V7.descr[:-1]
    + [
        ("ecb_lock_timer", "u1"),
        ("_pad0", "V3"),
        ("coll_x130_flags", "<u4"),
        ("shield_damage_taken", "<u4"),
        ("shield_int_damage", "<u4"),
        ("shield_attacker_gobj", "<u4"),
        ("specialn_facing_dir_bits", "<u4"),
        ("shield_hit_element", "<u4"),
    ],
    align=False,
)

FIGHTER_DTYPE_V9 = np.dtype(
    FIGHTER_DTYPE_V7.descr[:-1]
    + [
        ("lightshield_amount_bits", "<u4"),
        ("ecb_lock_timer", "u1"),
        ("_pad0", "V3"),
        ("coll_x130_flags", "<u4"),
        ("shield_damage_taken", "<u4"),
        ("shield_int_damage", "<u4"),
        ("shield_attacker_gobj", "<u4"),
        ("specialn_facing_dir_bits", "<u4"),
        ("shield_hit_element", "<u4"),
    ],
    align=False,
)

# v11 keeps the v10 record (v7 fighter layout + item extensions) and appends the
# hidden-lane probe words after the trailing pad.
FIGHTER_DTYPE_V11 = np.dtype(
    FIGHTER_DTYPE_V7.descr
    + [
        ("x670_timers_bits", "<u4"),
        ("x674_timers_bits", "<u4"),
        ("x2344_bits", "<u4"),
        ("x2348_bits", "<u4"),
        ("x234c_bits", "<u4"),
        ("transn_x_bits", "<u4"),
        ("transn_y_bits", "<u4"),
        ("transn_z_bits", "<u4"),
    ],
    align=False,
)

# v12 appends the x1A50 grab-mash latch word.
FIGHTER_DTYPE_V12 = np.dtype(
    FIGHTER_DTYPE_V11.descr + [("x1a50_bits", "<u4")],
    align=False,
)

FIGHTER_DTYPE = FIGHTER_DTYPE_V12

ITEM_DTYPE = np.dtype(
    [
        ("item_id", "<u4"),
        ("kind", "<u2"),
        ("state", "<u2"),
        ("owner_port_i8", "i1"),
        ("flags", "u1"),
        ("pos_x_bits", "<u4"),
        ("pos_y_bits", "<u4"),
        ("pos_z_bits", "<u4"),
        ("vel_x_bits", "<u4"),
        ("vel_y_bits", "<u4"),
        ("vel_z_bits", "<u4"),
        ("facing_bits", "<u4"),
        ("anim_id", "<u2"),
        ("_pad0", "V2"),
        ("anim_frame_bits", "<u4"),
        ("lifetime_bits", "<u4"),
        ("damage", "<u4"),
        ("xC34_damage_dealt", "<i4"),
        ("xC48_clank_damage", "<i4"),
        ("xC4C_reflect_damage", "<i4"),
        ("xC50_shield_damage", "<i4"),
        ("xCA8_callback_damage", "<i4"),
        ("xCBC_hitlag_bits", "<u4"),
        ("xCC0_hitlag_min_bits", "<u4"),
        ("xDA8_short", "<u2"),
        ("xDC8_word", "<u4"),
        ("xDCE_flags", "u1"),
        ("xDD4_laser_scale_bits", "<u4"),
        ("xDD8_laser_angle_bits", "<u4"),
        ("xDDC_laser_speed_bits", "<u4"),
        ("xDE0_laser_pos_x_bits", "<u4"),
        ("xDE4_laser_pos_y_bits", "<u4"),
        ("xDE8_laser_pos_z_bits", "<u4"),
    ],
    align=False,
)

HITBOX_DTYPE = np.dtype(
    [
        ("state", "<u4"),
        ("group", "<u4"),
        ("damage", "<u4"),
        ("damage_stale_bits", "<u4"),
        ("offset_x_bits", "<u4"),
        ("offset_y_bits", "<u4"),
        ("offset_z_bits", "<u4"),
        ("size_bits", "<u4"),
        ("angle", "<u4"),
        ("kbg", "<u4"),
        ("wsk", "<u4"),
        ("bkb", "<u4"),
        ("element", "<u4"),
        ("shield_damage", "<u4"),
        ("sfx", "<u4"),
        ("sfx_kind", "<u4"),
        ("flags", ("u1", (8,))),
        ("bone_ptr", "<u4"),
        ("pos_x_bits", "<u4"),
        ("pos_y_bits", "<u4"),
        ("pos_z_bits", "<u4"),
    ],
    align=False,
)
HURTBOX_DTYPE = np.dtype(
    [
        ("state", "<u4"),
        ("a_offset_x_bits", "<u4"),
        ("a_offset_y_bits", "<u4"),
        ("a_offset_z_bits", "<u4"),
        ("b_offset_x_bits", "<u4"),
        ("b_offset_y_bits", "<u4"),
        ("b_offset_z_bits", "<u4"),
        ("scale_bits", "<u4"),
        ("a_pos_x_bits", "<u4"),
        ("a_pos_y_bits", "<u4"),
        ("a_pos_z_bits", "<u4"),
        ("b_pos_x_bits", "<u4"),
        ("b_pos_y_bits", "<u4"),
        ("b_pos_z_bits", "<u4"),
        ("bone_idx", "<i4"),
        ("height", "<u4"),
        ("is_grabbable", "u1"),
        ("flags", "u1"),
        ("_pad0", "V2"),
    ],
    align=False,
)
HITLIST_DTYPE = np.dtype(
    [
        ("group", "<u4"),
        ("victims1_cursor", "u1"),
        ("victims2_cursor", "u1"),
        ("_pad0", "V2"),
        ("owner_gobj", "<u4"),
        ("victims1_ptr", ("<u4", (12,))),
        ("victims1_cooldown", ("<u4", (12,))),
        ("victims2_ptr", ("<u4", (12,))),
        ("victims2_cooldown", ("<u4", (12,))),
    ],
    align=False,
)


@dataclass(frozen=True)
class EngineDump:
    path: Path
    header: np.void
    frames: np.ndarray
    inputs: np.ndarray
    fighters: np.ndarray
    items: np.ndarray
    hitboxes: np.ndarray
    hurtboxes: np.ndarray
    hitlists: np.ndarray
    item_hitlists: np.ndarray


def _f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", int(bits) & 0xFFFFFFFF))[0]


def f32_from_bits(bits: int) -> float:
    return float(_f32_from_bits(bits))


def i8_from_u8(value: int) -> int:
    v = int(value) & 0xFF
    if v >= 0x80:
        return v - 0x100
    return v


def read_engine_dump(path: str | Path) -> EngineDump:
    dump_path = Path(path).resolve()
    blob = dump_path.read_bytes()
    if len(blob) < HEADER_DTYPE.itemsize:
        raise ValueError(f"engine dump too small for header: {dump_path}")

    raw_magic = blob[0:8]
    if raw_magic != ENGINE_DUMP_MAGIC:
        raise ValueError(f"bad engine dump magic: {raw_magic!r}")

    header = np.frombuffer(blob, dtype=HEADER_DTYPE, count=1, offset=0)[0]
    version = int(header["version"])
    if version not in ENGINE_DUMP_SUPPORTED_VERSIONS:
        raise ValueError(
            f"unsupported engine dump version: {version} "
            f"(supported={ENGINE_DUMP_SUPPORTED_VERSIONS})"
        )

    frame_count = int(header["frame_count"])
    port_count = int(header["port_count"])
    if port_count <= 0:
        raise ValueError(f"invalid port_count={port_count}")
    total_items = int(header["total_items"])

    def _read(dtype: np.dtype, count: int, offset: int) -> np.ndarray:
        span = int(dtype.itemsize) * int(count)
        if offset < 0 or span < 0 or (offset + span) > len(blob):
            raise ValueError(
                f"out-of-range section read: offset={offset} span={span} file_size={len(blob)} dtype={dtype}"
            )
        return np.frombuffer(blob, dtype=dtype, count=count, offset=offset)

    frames = _read(FRAME_DTYPE, frame_count, int(header["frames_offset"]))
    inputs = _read(INPUT_DTYPE, frame_count * port_count, int(header["inputs_offset"]))
    # v10 is a targeted item-hitlist/callback extension built on the v7 fighter record layout.
    # v11 appends the hidden-lane probe words to the v10 fighter record.
    fighter_dtype = (
        FIGHTER_DTYPE_V12
        if version >= 12
        else FIGHTER_DTYPE_V11
        if version >= 11
        else FIGHTER_DTYPE_V7
        if version == 10
        else FIGHTER_DTYPE_V9
        if version >= 9
        else FIGHTER_DTYPE_V8
        if version >= 8
        else FIGHTER_DTYPE_V7
    )
    fighters = _read(fighter_dtype, frame_count * port_count, int(header["fighters_offset"]))
    if version >= 10:
        item_dtype = ITEM_DTYPE
    else:
        item_dtype = np.dtype(ITEM_DTYPE.descr[:17], align=False)
    items = _read(item_dtype, total_items, int(header["items_offset"]))
    hitboxes = _read(HITBOX_DTYPE, frame_count * port_count * 4, int(header["hitboxes_offset"]))
    hurtboxes = _read(HURTBOX_DTYPE, frame_count * port_count * 15, int(header["hurtboxes_offset"]))
    if version >= 7 and int(header["hitlists_offset"]) > 0:
        hitlists = _read(HITLIST_DTYPE, frame_count * port_count * 4, int(header["hitlists_offset"]))
    else:
        hitlists = np.empty((0,), dtype=HITLIST_DTYPE)
    if version >= 10 and int(header["item_hitlists_offset"]) > 0:
        item_hitlists = _read(HITLIST_DTYPE, total_items * 4, int(header["item_hitlists_offset"]))
    else:
        item_hitlists = np.empty((0,), dtype=HITLIST_DTYPE)

    return EngineDump(
        path=dump_path,
        header=header,
        frames=frames,
        inputs=inputs,
        fighters=fighters,
        items=items,
        hitboxes=hitboxes,
        hurtboxes=hurtboxes,
        hitlists=hitlists,
        item_hitlists=item_hitlists,
    )
