from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, replace
from pathlib import Path

from tools.extraction.char_registry import CHARS
from tools.extraction.dol import DolImage


ATTACK_FORMAT_MAGIC = b"MSLACID1"
ATTACK_FORMAT_VERSION = 3
OWNER_FORMAT_MAGIC = b"MSLMSO01"
OWNER_FORMAT_VERSION = 26
OWNER_HEADER_BYTES = 8 + 4 + 2 + 2 + 4 * 13
STALING_FORMAT_MAGIC = b"MSLSTID1"
STALING_FORMAT_VERSION = 1
U16_ABSENT = 0xFFFF

CLASS_ATTACK_AIR = 1 << 0
CLASS_ATTACK_S3 = 1 << 1
CLASS_ATTACK_S4 = 1 << 2
CLASS_DAMAGE_COMMON = 1 << 3
CLASS_DAMAGE_FLY = 1 << 4
CLASS_LANDING_AIR = 1 << 5
CLASS_COMMON_FALL = 1 << 6
CLASS_SPECIALHI = 1 << 7
CLASS_COMMON_AIR_PHYS = 1 << 8
CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL = 1 << 16
CLASS_GROUNDED_ATTACK = 1 << 17
CLASS_GUARDON_FRAME_START_X672_IASA = 1 << 18
CLASS_FT80081D0C_AIR_COLL = 1 << 19
CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL = 1 << 20
CLASS_FT80083F88_GROUND_TO_AIR_COLL = 1 << 21
CLASS_FT80083090_PLATFORM_PASS_COLL = 1 << 22
CLASS_FT800827A0_EDGE_SNAP_COLL = 1 << 23
CLASS_DAMAGE_AIR = 1 << 24
CLASS_DAMAGE_GROUND = 1 << 25
CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS = 1 << 26
CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION = 1 << 27
CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD = 1 << 28
CLASS_FX_SPECIALS_GROUND_B108_COLL = 1 << 30
CLASS_FT80082B1C_BASIC_LANDING_COLL = 1 << 31

CLASS2_COMMON_AIRBORNE_COLL = 1 << 2
CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA = 1 << 5
CLASS2_WALK_ACTION = 1 << 6
CLASS2_FALL_LIKE_ACTION = 1 << 7
CLASS2_GUARD_STATE = 1 << 8
CLASS2_CATCH_START_FLOOR_LOSS = 1 << 9
CLASS2_GROUND_FLOOR_LOSS_TO_FALL = 1 << 10
CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE = 1 << 11
CLASS2_LANDING_ROOT_FLOOR_SNAP = 1 << 12
CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST = 1 << 13
CLASS2_CLIFF_HOLD_PHYS_SNAP = 1 << 14
CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL = 1 << 15
CLASS2_FALCON_DIVE_OWNER_CONDITIONAL_COLL = 1 << 16
CLASS2_CAPTURE_CONSTRAINT_CONDITIONAL_COLL = 1 << 17

CLASS3_ORDINARY_WALLJUMP_COLL = 1 << 5
CLASS3_WALLTECH_COLL = 1 << 6
CLASS3_CATCH_TARGET_MASK_1 = 1 << 7
CLASS3_CATCH_TARGET_MASK_511 = 1 << 8
CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED = 1 << 9
CLASS3_CATCH_KIND_1 = 1 << 10
CLASS3_CATCH_KIND_2 = 1 << 11
CLASS3_JUMP_COLL = 1 << 12
CLASS3_FALL_COLL = 1 << 13
CLASS3_FALCON_SPECIALHI_THROW0_COLL = 1 << 14

COLL_HANDLER_LEGACY = 0
COLL_HANDLER_GROUND_B108_FALL = 1
COLL_HANDLER_GROUND_B2DC_FALL = 2
COLL_HANDLER_GROUND_B4B0_TEETER = 3
COLL_HANDLER_GROUND_RUN = 4
COLL_HANDLER_GROUND_GUARD = 5
COLL_HANDLER_GROUND_GUARD_SETOFF = 6
COLL_HANDLER_GROUND_OTTOTTO = 7
COLL_HANDLER_AIR_COMMON = 8
COLL_HANDLER_AIR_ATTACK = 9
COLL_HANDLER_AIR_ESCAPE = 10
COLL_HANDLER_DAMAGE_COMMON = 11
COLL_HANDLER_DAMAGE_FLY = 12
COLL_HANDLER_DAMAGE_FALL = 13
COLL_HANDLER_DOWN_BOUND = 14
COLL_HANDLER_DOWN_B108 = 15
COLL_HANDLER_DOWN_B2DC = 16
COLL_HANDLER_PASSIVE_B108 = 17
COLL_HANDLER_PASSIVE_B2DC = 18
COLL_HANDLER_PASSIVE_WALL = 19
COLL_HANDLER_PASSIVE_CEIL = 20
COLL_HANDLER_AIR_FALL_SPECIAL = 21
COLL_HANDLER_GROUND_LANDING = 22
COLL_HANDLER_GROUND_LANDING_AIR = 23
COLL_HANDLER_DOWN_REFLECT = 24
COLL_HANDLER_DOWN_DAMAGE = 25

FX_SPECIAL_KIND_NAMES = (
    "SPECIAL_N_START",
    "SPECIAL_N_LOOP",
    "SPECIAL_N_END",
    "SPECIAL_AIR_N_START",
    "SPECIAL_AIR_N_LOOP",
    "SPECIAL_AIR_N_END",
    "SPECIAL_S_START",
    "SPECIAL_S",
    "SPECIAL_S_END",
    "SPECIAL_AIR_S_START",
    "SPECIAL_AIR_S",
    "SPECIAL_AIR_S_END",
    "SPECIAL_HI_HOLD",
    "SPECIAL_HI_HOLD_AIR",
    "SPECIAL_HI",
    "SPECIAL_AIR_HI",
    "SPECIAL_HI_LANDING",
    "SPECIAL_HI_FALL",
    "SPECIAL_HI_BOUND",
    "SPECIAL_LW_START",
    "SPECIAL_LW_LOOP",
    "SPECIAL_LW_HIT",
    "SPECIAL_LW_END",
    "SPECIAL_LW_TURN",
    "SPECIAL_AIR_LW_START",
    "SPECIAL_AIR_LW_LOOP",
    "SPECIAL_AIR_LW_HIT",
    "SPECIAL_AIR_LW_END",
    "SPECIAL_AIR_LW_TURN",
)
FX_SPECIAL_KIND_VALUES = {name: i + 1 for i, name in enumerate(FX_SPECIAL_KIND_NAMES)}


@dataclass(frozen=True)
class MotionStateRow:
    action_id: int
    submotion_id: int
    x4_flags: int
    motion_state_word: int
    anim: int
    iasa: int
    phys: int
    coll: int
    cam: int
    class_bits: int = 0
    class2_bits: int = 0
    class3_bits: int = 0
    fx_special_kind: int = 0
    coll_handler_kind: int = 0


# GALE01's retail executable owns the source tables. Callback families below are expressed as
# pointer-equivalence sets anchored by representative MotionState rows; no decomp symbol table is
# required. The row anchors are audited against the same source families named by the output bits.
# refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
# refs/melee/src/melee/ft/ftdata.c::ftData_CharacterStateTables
COMMON_TABLE_ADDRESS = 0x803C2800
CHARACTER_TABLE_POINTERS_ADDRESS = 0x803C12E0
COMMON_ACTION_COUNT = 341
# The 14-row `ftData_UnkMotionStates0` table immediately follows the common action table in
# GALE01. Its 0..13 submotion domain supplies four Wait variants not referenced by normal action
# rows but retained by MSLSTID1's historical staling map.
# refs/melee/src/melee/ft/ftmotionstates.c::ftData_UnkMotionStates0
COMMON_AUX_STALING_ROW_COUNT = 14

Anchor = tuple[str, int]
PointerRule = tuple[str, tuple[Anchor, ...]]


CLASS_POINTER_RULES: dict[int, PointerRule] = {
    CLASS_ATTACK_AIR: ("anim", (("fox", 0x0041),)),
    CLASS_ATTACK_S3: ("anim", (("fox", 0x0033),)),
    CLASS_ATTACK_S4: ("anim", (("fox", 0x003A),)),
    CLASS_DAMAGE_COMMON: ("anim", (("fox", 0x004B), ("fox", 0x00B9))),
    CLASS_DAMAGE_FLY: ("anim", (("fox", 0x0057), ("fox", 0x005B), ("fox", 0x00F7))),
    CLASS_LANDING_AIR: ("anim", (("fox", 0x0046),)),
    CLASS_COMMON_FALL: ("anim", (("fox", 0x001D), ("fox", 0x0020), ("fox", 0x0023))),
    CLASS_SPECIALHI: (
        "anim",
        tuple(("fox", a) for a in range(0x0161, 0x0168))
        + (("marth", 0x016F), ("marth", 0x0170))
        + tuple(("falcon", a) for a in (0x0161, 0x0162, 0x0163, 0x0164, 0x016B)),
    ),
    CLASS_COMMON_AIR_PHYS: (
        "phys",
        tuple(("fox", a) for a in (0x0019, 0x001B, 0x001D, 0x0020, 0x0023)),
    ),
    CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL: (
        "coll",
        tuple(("fox", a) for a in range(0x002C, 0x0033))
        + tuple(("fox", a) for a in (0x0033, 0x0038, 0x0039, 0x003A, 0x003F, 0x0040))
        + tuple(("fox", a) for a in (0x00D4, 0x00D5, 0x00D6, 0x00D8, 0x00D9, 0x00DA))
        + tuple(("fox", a) for a in range(0x00DB, 0x00DF)),
    ),
    CLASS_GROUNDED_ATTACK: (
        "coll",
        tuple(("fox", a) for a in range(0x002C, 0x0033))
        + tuple(("fox", a) for a in (0x0033, 0x0038, 0x0039, 0x003A, 0x003F, 0x0040)),
    ),
    CLASS_GUARDON_FRAME_START_X672_IASA: (
        "iasa",
        tuple(
            ("fox", a)
            for a in (
                0x000E,
                0x000F,
                0x0012,
                0x0014,
                0x0015,
                0x0016,
                0x0027,
                0x0028,
                0x0029,
            )
        ),
    ),
    CLASS_FT80081D0C_AIR_COLL: (
        "coll",
        tuple(
            ("fox", a)
            for a in (0x00CD, 0x00CE, 0x0115, 0x0137, 0x013A, 0x013C, 0x013E, 0x0140)
        )
        + tuple(
            ("marth", a)
            for a in (
                0x0159,
                0x015A,
                0x015B,
                0x015D,
                0x015E,
                0x0160,
                0x0163,
                0x0173,
                0x0174,
            )
        )
        + tuple(
            ("falcon", a)
            for a in (
                0x015C,
                0x015F,
                0x0160,
                0x0164,
                0x0165,
                0x0166,
                0x0167,
                0x0169,
                0x016A,
            )
        )
        + tuple(("sheik", a) for a in (0x0159, 0x015A, 0x015B, 0x0160, 0x0161, 0x0162)),
    ),
    CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL: (
        "coll",
        tuple(
            ("fox", a)
            for a in (0x00F4, 0x00FB, 0x015E, 0x015F, 0x0160, 0x0162, 0x0164, 0x0166)
        )
        + (("falcon", 0x0161), ("falcon", 0x0162))
        + tuple(("sheik", a) for a in (0x0166, 0x0167, 0x0168))
        + tuple(("zelda", a) for a in (0x0160, 0x0161, 0x0162)),
    ),
    CLASS_FT80083F88_GROUND_TO_AIR_COLL: (
        "coll",
        tuple(
            ("fox", a) for a in (0x00CF, 0x00D1, 0x00D3, 0x00EE, 0x0155, 0x0156, 0x0157)
        ),
    ),
    CLASS_FT80083090_PLATFORM_PASS_COLL: ("coll", (("fox", 0x0105),)),
    CLASS_FT800827A0_EDGE_SNAP_COLL: (
        "coll",
        tuple(("fox", a) for a in range(0x002C, 0x0033))
        + tuple(("fox", a) for a in (0x0033, 0x0038, 0x0039, 0x003A, 0x003F, 0x0040))
        + tuple(("fox", a) for a in (0x00D4, 0x00D5, 0x00D6, 0x00D8, 0x00D9, 0x00DA))
        + tuple(("fox", a) for a in range(0x00DB, 0x00DF))
        + tuple(("fox", a) for a in (0x00E9, 0x00EB, 0x0108, 0x015D))
        + tuple(("marth", a) for a in (0x015D, 0x015E, 0x0160, 0x0163, 0x0171))
        + (("falcon", 0x015D), ("falcon", 0x0168))
        + tuple(("sheik", a) for a in (0x015D, 0x015E, 0x015F, 0x0165)),
    ),
    CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS: (
        "iasa",
        tuple(("fox", a) for a in (0x0033, 0x0038, 0x003A, 0x003F, 0x0040)),
    ),
    CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION: (
        "iasa",
        tuple(
            ("fox", a)
            for a in (
                0x002C,
                0x002D,
                0x002E,
                0x0032,
                0x0033,
                0x0038,
                0x0039,
                0x003A,
                0x003F,
                0x0040,
            )
        ),
    ),
    CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD: (
        "iasa",
        tuple(("fox", a) for a in (0x002E, 0x0033, 0x003F, 0x0040)),
    ),
    CLASS_FX_SPECIALS_GROUND_B108_COLL: (
        "coll",
        tuple(
            (c, a)
            for c, a in (
                ("fox", 0x015B),
                ("fox", 0x015C),
                ("falcon", 0x015D),
                ("falcon", 0x015E),
            )
        ),
    ),
    CLASS_FT80082B1C_BASIC_LANDING_COLL: (
        "coll",
        tuple(
            ("fox", a)
            for a in (
                0x0019,
                0x001B,
                0x001D,
                0x0020,
                0x0023,
                0x00E6,
                0x00FB,
                0x0105,
                0x0158,
                0x0159,
                0x015A,
            )
        ),
    ),
}

CLASS2_POINTER_RULES: dict[int, PointerRule] = {
    CLASS2_COMMON_AIRBORNE_COLL: (
        "coll",
        tuple(("fox", a) for a in (0x00F4, 0x00FB, 0x0105)),
    ),
    CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA: (
        "iasa",
        tuple(
            ("fox", a)
            for a in (
                0x000E,
                0x000F,
                0x0012,
                0x0014,
                0x0015,
                0x0016,
                0x0027,
                0x0028,
                0x0029,
                0x002A,
            )
        ),
    ),
    CLASS2_WALK_ACTION: ("anim", (("fox", 0x000F),)),
    CLASS2_FALL_LIKE_ACTION: ("iasa", (("fox", 0x001D), ("fox", 0x0020))),
    CLASS2_GUARD_STATE: ("coll", tuple(("fox", a) for a in range(0x00B2, 0x00B7))),
    CLASS2_CATCH_START_FLOOR_LOSS: ("coll", (("fox", 0x00D4), ("fox", 0x00D6))),
    CLASS2_GROUND_FLOOR_LOSS_TO_FALL: (
        "coll",
        tuple(
            ("fox", a)
            for a in (
                0x000E,
                0x000F,
                0x0012,
                0x0013,
                0x0014,
                0x0015,
                0x0016,
                0x0017,
                0x0018,
                0x0027,
                0x0028,
                0x0029,
                0x002A,
                0x002C,
                0x0032,
                0x0033,
                0x0038,
                0x0039,
                0x003A,
                0x003F,
                0x0040,
                0x0046,
                0x00B2,
                0x00B3,
                0x00B4,
                0x00B5,
                0x00B6,
                0x00EB,
                0x00F5,
                0x00F6,
            )
        ),
    ),
    CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE: (
        "iasa",
        tuple(("fox", a) for a in (0x0019, 0x001B, 0x001D, 0x00EC, 0x00FC, 0x00FD)),
    ),
    CLASS2_LANDING_ROOT_FLOOR_SNAP: (
        "anim",
        (("fox", 0x000E), ("fox", 0x002A), ("fox", 0x0046)),
    ),
    CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST: (
        "anim",
        tuple(
            ("fox", a)
            for a in (
                0x000F,
                0x0012,
                0x0014,
                0x0018,
                0x0027,
                0x00B2,
                0x00D4,
                0x00D6,
                0x00EB,
            )
        ),
    ),
    CLASS2_CLIFF_HOLD_PHYS_SNAP: (
        "phys",
        tuple(("fox", a) for a in (0x00FC, 0x00FD, 0x0104)),
    ),
    CLASS2_FT_CHECK_GROUND_LEDGE_BOTH_COLL: (
        "coll",
        (("fox", 0x0164), ("fox", 0x0166), ("falcon", 0x0161), ("falcon", 0x0162)),
    ),
    CLASS2_FALCON_DIVE_OWNER_CONDITIONAL_COLL: ("coll", (("falcon", 0x0163),)),
    CLASS2_CAPTURE_CONSTRAINT_CONDITIONAL_COLL: ("coll", (("fox", 0x0113),)),
}

CLASS3_POINTER_RULES: dict[int, PointerRule] = {
    CLASS3_ORDINARY_WALLJUMP_COLL: (
        "coll",
        tuple(
            ("fox", a)
            for a in (
                0x0019,
                0x001B,
                0x001D,
                0x0020,
                0x0026,
                0x0090,
                0x0091,
                0x0092,
                0x0093,
                0x009A,
                0x009B,
                0x00CA,
                0x00CC,
                0x00DA,
                0x00E5,
                0x00F4,
                0x00FA,
                0x00FB,
                0x0105,
                0x0146,
            )
        )
        + (("marth", 0x016F), ("marth", 0x0170)),
    ),
    CLASS3_WALLTECH_COLL: (
        "coll",
        tuple(("fox", a) for a in (0x0057, 0x005B, 0x00B9, 0x00F7)),
    ),
    CLASS3_JUMP_COLL: ("coll", (("fox", 0x0019),)),
    CLASS3_FALL_COLL: ("coll", (("fox", 0x001D),)),
    CLASS3_FALCON_SPECIALHI_THROW0_COLL: ("coll", (("falcon", 0x0164),)),
}

HANDLER_POINTER_RULES: dict[int, tuple[Anchor, ...]] = {
    COLL_HANDLER_GROUND_B108_FALL: tuple(
        ("fox", a) for a in (0x0012, 0x0018, 0x0027, 0x0028, 0x0029)
    ),
    COLL_HANDLER_GROUND_B2DC_FALL: (("fox", 0x0013),),
    COLL_HANDLER_GROUND_B4B0_TEETER: tuple(
        ("fox", a) for a in (0x000E, 0x000F, 0x0017)
    ),
    COLL_HANDLER_GROUND_RUN: tuple(("fox", a) for a in (0x0014, 0x0015, 0x0016)),
    COLL_HANDLER_GROUND_GUARD: tuple(
        ("fox", a) for a in (0x00B2, 0x00B3, 0x00B4, 0x00B6)
    ),
    COLL_HANDLER_GROUND_GUARD_SETOFF: (("fox", 0x00B5),),
    COLL_HANDLER_GROUND_OTTOTTO: (("fox", 0x00F5), ("fox", 0x00F6)),
    COLL_HANDLER_AIR_COMMON: tuple(
        ("fox", a) for a in (0x0019, 0x001B, 0x001D, 0x0020)
    ),
    COLL_HANDLER_AIR_ATTACK: (("fox", 0x0041),),
    COLL_HANDLER_AIR_ESCAPE: (("fox", 0x00EC),),
    COLL_HANDLER_DAMAGE_COMMON: (("fox", 0x004B),),
    COLL_HANDLER_DAMAGE_FLY: (("fox", 0x0057), ("fox", 0x005B), ("fox", 0x00F7)),
    COLL_HANDLER_DAMAGE_FALL: (("fox", 0x0026),),
    COLL_HANDLER_DOWN_BOUND: (("fox", 0x00B7),),
    COLL_HANDLER_DOWN_B108: (("fox", 0x00B8), ("fox", 0x00BA), ("fox", 0x00BE)),
    COLL_HANDLER_DOWN_B2DC: (("fox", 0x00BB), ("fox", 0x00BC)),
    COLL_HANDLER_PASSIVE_B108: (("fox", 0x00C7),),
    COLL_HANDLER_PASSIVE_B2DC: (("fox", 0x00C8),),
    COLL_HANDLER_PASSIVE_WALL: (("fox", 0x00CA),),
    COLL_HANDLER_PASSIVE_CEIL: (("fox", 0x00CC),),
    COLL_HANDLER_AIR_FALL_SPECIAL: (("fox", 0x0023),),
    COLL_HANDLER_GROUND_LANDING: (("fox", 0x002A),),
    COLL_HANDLER_GROUND_LANDING_AIR: (("fox", 0x0046),),
    COLL_HANDLER_DOWN_REFLECT: (("fox", 0x014F),),
    COLL_HANDLER_DOWN_DAMAGE: (("fox", 0x00B9),),
}

_COMMON_ENTRY_BITS = {
    CLASS3_CATCH_TARGET_MASK_1: frozenset((0x00B8, 0x00B9, 0x00C0, 0x00C1)),
    CLASS3_CATCH_TARGET_MASK_511: frozenset(
        (
            0x00B7,
            0x00BE,
            0x00BF,
            0x00C6,
            0x00D8,
            0x00D9,
            0x00DF,
            0x00E0,
            0x00E1,
            0x00E2,
            0x00E3,
            0x00E4,
            0x00EF,
            0x00F0,
            0x00F1,
            0x00F2,
            0x00F3,
            0x00FC,
            0x00FD,
            0x0113,
        )
    ),
    CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED: frozenset(range(0x00DB, 0x00DF)),
    CLASS3_CATCH_KIND_1: frozenset((0x00D4, 0x00D6)),
}


def _u16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def _u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFF_FFFF)


def _read_row(dol: DolImage, address: int, action_id: int) -> MotionStateRow:
    values = struct.unpack(">8I", dol.read(address, 0x20))
    submotion = U16_ABSENT if values[0] == 0xFFFF_FFFF else values[0]
    if submotion > U16_ABSENT:
        raise ValueError(
            f"MotionState action {action_id} has invalid submotion id {submotion:#x}"
        )
    return MotionStateRow(action_id, submotion, values[1], values[2], *values[3:])


def _rows_from_dol(dol: DolImage) -> dict[str, list[MotionStateRow]]:
    common = [
        _read_row(dol, COMMON_TABLE_ADDRESS + action * 0x20, action)
        for action in range(COMMON_ACTION_COUNT)
    ]
    out: dict[str, list[MotionStateRow]] = {}
    for name, info in CHARS.items():
        table = dol.u32(CHARACTER_TABLE_POINTERS_ADDRESS + info.internal_id * 4)
        rows = list(common)
        for self_index in range(256):
            address = table + self_index * 0x20
            try:
                row = _read_row(dol, address, COMMON_ACTION_COUNT + self_index)
            except ValueError:
                break
            callbacks = (row.anim, row.iasa, row.phys, row.coll, row.cam)
            if not all(value == 0 or dol.is_text_address(value) for value in callbacks):
                break
            rows.append(row)
        if len(rows) == COMMON_ACTION_COUNT:
            raise ValueError(f"{name}: failed to find character MotionState rows")
        out[name] = rows
    return out


def _compile_pointer_rules(
    rows_by_char: dict[str, list[MotionStateRow]], rules: dict[int, PointerRule]
) -> dict[int, tuple[str, frozenset[int]]]:
    compiled: dict[int, tuple[str, frozenset[int]]] = {}
    for bit, (lane, anchors) in rules.items():
        values = frozenset(
            getattr(rows_by_char[ch][action], lane) for ch, action in anchors
        )
        if 0 in values:
            raise ValueError(f"owner bit {bit:#x} has a NULL {lane} callback anchor")
        compiled[bit] = (lane, values)
    return compiled


def _apply_semantics(
    rows_by_char: dict[str, list[MotionStateRow]],
) -> dict[str, list[MotionStateRow]]:
    class_rules = _compile_pointer_rules(rows_by_char, CLASS_POINTER_RULES)
    class2_rules = _compile_pointer_rules(rows_by_char, CLASS2_POINTER_RULES)
    class3_rules = _compile_pointer_rules(rows_by_char, CLASS3_POINTER_RULES)
    handlers: dict[int, int] = {}
    for kind, anchors in HANDLER_POINTER_RULES.items():
        for char, action in anchors:
            pointer = rows_by_char[char][action].coll
            if pointer == 0:
                raise ValueError(f"collision handler {kind} has a NULL callback anchor")
            previous = handlers.setdefault(pointer, kind)
            if previous != kind:
                raise ValueError(
                    f"callback pointer {pointer:#x} has conflicting handlers"
                )
    damage_air_submotions = {
        rows_by_char["fox"][a].submotion_id for a in range(0x0054, 0x0057)
    }
    damage_ground_submotions = {
        rows_by_char["fox"][a].submotion_id for a in range(0x004B, 0x0054)
    }
    fx_kind_by_anim = {
        rows_by_char["fox"][COMMON_ACTION_COUNT + i].anim: i + 1
        for i in range(len(FX_SPECIAL_KIND_NAMES))
    }
    if 0 in fx_kind_by_anim or len(fx_kind_by_anim) != len(FX_SPECIAL_KIND_NAMES):
        raise ValueError(
            "Fox/Falco special ANIM callbacks are not unique non-NULL owners"
        )

    out: dict[str, list[MotionStateRow]] = {}
    for char, rows in rows_by_char.items():
        decorated: list[MotionStateRow] = []
        for row in rows:
            class_bits = sum(
                bit
                for bit, (lane, values) in class_rules.items()
                if getattr(row, lane) in values
            )
            if row.submotion_id in damage_air_submotions:
                class_bits |= CLASS_DAMAGE_AIR
            if row.submotion_id in damage_ground_submotions:
                class_bits |= CLASS_DAMAGE_GROUND
            class2_bits = sum(
                bit
                for bit, (lane, values) in class2_rules.items()
                if getattr(row, lane) in values
            )
            class3_bits = sum(
                bit
                for bit, (lane, values) in class3_rules.items()
                if getattr(row, lane) in values
            )
            for bit, actions in _COMMON_ENTRY_BITS.items():
                if row.action_id in actions:
                    class3_bits |= bit
            if char == "falcon" and row.action_id in (0x0161, 0x0162):
                class3_bits |= CLASS3_CATCH_KIND_2
            if char == "falcon" and row.action_id == 0x0163:
                class3_bits |= CLASS3_CATCH_TARGET_MASK_511
            decorated.append(
                replace(
                    row,
                    class_bits=class_bits,
                    class2_bits=class2_bits,
                    class3_bits=class3_bits,
                    fx_special_kind=fx_kind_by_anim.get(row.anim, 0),
                    coll_handler_kind=handlers.get(row.coll, COLL_HANDLER_LEGACY),
                )
            )
        out[char] = decorated
    return out


def load_motion_state_rows(dol_path: Path) -> dict[str, list[MotionStateRow]]:
    return _apply_semantics(_rows_from_dol(DolImage(dol_path)))


def common_submotion_ids(dol_path: Path) -> dict[str, int]:
    rows = _rows_from_dol(DolImage(dol_path))["fox"]
    anchors = {
        "ftCo_SM_Dash": 0x0014,
        "ftCo_SM_RunBrake": 0x0017,
        "ftCo_SM_TurnRun": 0x0013,
        "ftCo_SM_JumpF": 0x0019,
        "ftCo_SM_JumpB": 0x001A,
        "ftCo_SM_EscapeN": 0x00EB,
        "ftCo_SM_EscapeAir": 0x00EC,
        "ftCo_SM_CliffAttackSlow": 0x0100,
        "ftCo_SM_CliffAttackQuick": 0x0101,
        "ftCo_SM_Guard": 0x00B3,
        "ftCo_SM_EscapeF": 0x00E9,
        "ftCo_SM_EscapeB": 0x00EA,
        "ftCo_SM_Attack11": 0x002C,
        "ftCo_SM_Attack12": 0x002D,
        "ftCo_SM_Attack13": 0x002E,
        "ftCo_SM_Attack100Start": 0x002F,
        "ftCo_SM_Attack100Loop": 0x0030,
        "ftCo_SM_Attack100End": 0x0031,
        "ftCo_SM_AttackDash": 0x0032,
        "ftCo_SM_AttackS3": 0x0035,
        "ftCo_SM_AttackHi3": 0x0038,
        "ftCo_SM_AttackLw3": 0x0039,
        "ftCo_SM_AttackS4": 0x003C,
        "ftCo_SM_AttackHi4": 0x003F,
        "ftCo_SM_AttackLw4": 0x0040,
        "ftCo_SM_AttackAirN": 0x0041,
        "ftCo_SM_AttackAirF": 0x0042,
        "ftCo_SM_AttackAirB": 0x0043,
        "ftCo_SM_AttackAirHi": 0x0044,
        "ftCo_SM_AttackAirLw": 0x0045,
        "ftCo_SM_DownAttackU": 0x00BB,
        "ftCo_SM_DownAttackD": 0x00C3,
        "ftCo_SM_Catch": 0x00D4,
        "ftCo_SM_CatchDash": 0x00D6,
        "ftCo_SM_CatchWait": 0x00D8,
        "ftCo_SM_CatchAttack": 0x00D9,
        "ftCo_SM_ThrowF": 0x00DB,
        "ftCo_SM_ThrowB": 0x00DC,
        "ftCo_SM_ThrowHi": 0x00DD,
        "ftCo_SM_ThrowLw": 0x00DE,
        "ftCo_SM_ThrownF": 0x00EF,
        "ftCo_SM_ThrownB": 0x00F0,
        "ftCo_SM_ThrownHi": 0x00F1,
        "ftCo_SM_ThrownLw": 0x00F2,
    }
    out = {name: rows[action].submotion_id for name, action in anchors.items()}
    out["ftCo_SM_Count"] = (
        max(
            row.submotion_id
            for row in rows[:COMMON_ACTION_COUNT]
            if row.submotion_id != U16_ABSENT
        )
        + 1
    )
    return out


def _write_attack_table(path: Path, rows: list[MotionStateRow]) -> None:
    count = len(rows)
    header = 32
    move_off = header
    flags_off = move_off + count * 2
    motion_off = flags_off + count * 4
    file_bytes = motion_off + count * 4
    buf = bytearray(
        ATTACK_FORMAT_MAGIC + _u32(ATTACK_FORMAT_VERSION) + _u16(count) + _u16(0)
    )
    buf += _u32(move_off) + _u32(flags_off) + _u32(motion_off) + _u32(file_bytes)
    for row in rows:
        buf += _u16(row.motion_state_word >> 24)
    for row in rows:
        buf += _u32(row.x4_flags)
    for row in rows:
        buf += _u32(row.motion_state_word)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(buf)


def _write_staling_table(path: Path, rows: list[MotionStateRow]) -> None:
    by_submotion: dict[int, int] = {}
    for row in rows:
        if row.submotion_id == U16_ABSENT:
            continue
        move_id = row.motion_state_word >> 24
        previous = by_submotion.get(row.submotion_id)
        by_submotion[row.submotion_id] = (
            move_id if previous is None or previous == move_id else U16_ABSENT
        )
    entries = sorted(by_submotion.items())
    toc = 24
    file_bytes = toc + len(entries) * 4
    buf = bytearray(
        STALING_FORMAT_MAGIC
        + _u32(STALING_FORMAT_VERSION)
        + _u16(len(entries))
        + _u16(0)
    )
    buf += _u32(toc) + _u32(file_bytes)
    for submotion, move_id in entries:
        buf += _u16(submotion) + _u16(move_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(buf)


def _write_owner_table(
    path: Path, rows: list[MotionStateRow], callback_ids: dict[int, int]
) -> None:
    count = len(rows)
    offsets = [OWNER_HEADER_BYTES]
    for width in (2, 4, 4, 2, 2, 2, 2, 2, 4, 4, 4, 1):
        offsets.append(offsets[-1] + count * width)
    buf = bytearray(
        OWNER_FORMAT_MAGIC + _u32(OWNER_FORMAT_VERSION) + _u16(count) + _u16(0)
    )
    for offset in offsets:
        buf += _u32(offset)
    for row in rows:
        buf += _u16(row.submotion_id)
    for lane in ("x4_flags", "motion_state_word"):
        for row in rows:
            buf += _u32(getattr(row, lane))
    for lane in ("anim", "iasa", "phys", "coll", "cam"):
        for row in rows:
            buf += _u16(callback_ids[getattr(row, lane)])
    for lane in ("class_bits", "class2_bits", "class3_bits"):
        for row in rows:
            buf += _u32(getattr(row, lane))
    buf += bytes(row.fx_special_kind for row in rows)
    buf += bytes(row.coll_handler_kind for row in rows)
    if len(buf) != offsets[-1] + count:
        raise AssertionError((len(buf), offsets[-1] + count))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(buf)


def extract_all(dol_path: Path, out_root: Path, chars: list[str]) -> None:
    dol = DolImage(dol_path)
    rows_by_char = _apply_semantics(_rows_from_dol(dol))
    aux_staling_rows = [
        _read_row(
            dol,
            COMMON_TABLE_ADDRESS + (COMMON_ACTION_COUNT + index) * 0x20,
            COMMON_ACTION_COUNT + index,
        )
        for index in range(COMMON_AUX_STALING_ROW_COUNT)
    ]
    if [row.submotion_id for row in aux_staling_rows] != list(
        range(COMMON_AUX_STALING_ROW_COUNT)
    ) or any(
        row.x4_flags != 0 or row.motion_state_word >> 24 != 1
        for row in aux_staling_rows
    ):
        raise ValueError(
            "GALE01 auxiliary common MotionState table did not match its 0..13 domain"
        )
    callbacks = sorted(
        {
            value
            for rows in rows_by_char.values()
            for row in rows
            for value in (row.anim, row.iasa, row.phys, row.coll, row.cam)
        }
    )
    if not callbacks or callbacks[0] != 0:
        raise ValueError("MotionState callback namespace is missing NULL")
    callback_ids = {address: i for i, address in enumerate(callbacks)}
    if len(callback_ids) > U16_ABSENT:
        raise ValueError("MotionState callback namespace does not fit u16")
    for char in chars:
        rows = rows_by_char[char]
        _write_attack_table(out_root / "attack_id" / "move_id" / f"{char}.bin", rows)
        _write_staling_table(
            out_root / "staling" / "move_id" / f"{char}.bin", rows + aux_staling_rows
        )
        _write_owner_table(
            out_root / "motion_state" / "owners" / f"{char}.bin", rows, callback_ids
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract GALE01 MotionState tables from main.dol"
    )
    parser.add_argument("--dol", type=Path, required=True)
    parser.add_argument("--out-root", type=Path, default=Path("data"))
    parser.add_argument("--chars", default=",".join(CHARS))
    args = parser.parse_args()
    chars = [char.strip() for char in args.chars.split(",") if char.strip()]
    unknown = sorted(set(chars) - set(CHARS))
    if unknown:
        raise SystemExit(f"unsupported character key(s): {unknown!r}")
    extract_all(args.dol, args.out_root, chars)


if __name__ == "__main__":
    main()
