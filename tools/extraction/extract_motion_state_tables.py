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
OWNER_FORMAT_VERSION = 28
OWNER_HEADER_BYTES = 8 + 4 + 2 + 2 + 4 * 15
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
CLASS_GROUNDED_ATTACK = 1 << 17
CLASS_GUARDON_FRAME_START_X672_IASA = 1 << 18
CLASS_DAMAGE_AIR = 1 << 24
CLASS_DAMAGE_GROUND = 1 << 25
CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS = 1 << 26
CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION = 1 << 27
CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD = 1 << 28
CLASS2_FRESH_GUARDON_ITEM_SHIELDDESC_IASA = 1 << 5
CLASS2_WALK_ACTION = 1 << 6
CLASS2_FALL_LIKE_ACTION = 1 << 7
CLASS2_GUARD_STATE = 1 << 8
CLASS2_CLIFF_LEDGE_FLOOR_PRESERVE = 1 << 11
CLASS2_LANDING_ROOT_FLOOR_SNAP = 1 << 12
CLASS2_GROUNDED_ATTACK_WAIT_IASA_INTERRUPT_DEST = 1 << 13
CLASS2_CLIFF_HOLD_PHYS_SNAP = 1 << 14
CLASS3_CATCH_TARGET_MASK_1 = 1 << 7
CLASS3_CATCH_TARGET_MASK_511 = 1 << 8
CLASS3_CATCH_TARGET_MASK_511_WHILE_ATTACHED = 1 << 9
CLASS3_CATCH_KIND_1 = 1 << 10
CLASS3_CATCH_KIND_2 = 1 << 11
CLASS3_JUMP_FLOOR_SKIP = 1 << 12
CLASS3_FALL_FLOOR_SKIP = 1 << 13

COLL_HANDLER_NONE = 0
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

# Exact source map-callback geometry family. Unlike coll_source_plan, this is an exclusive callback
# owner; callbacks with live procedural choices retain one selector family and resolve the concrete
# mpColl wrapper at execution time.
COLL_SELECTOR_NONE = 0
COLL_SELECTOR_GROUND_B108 = 1
COLL_SELECTOR_GROUND_B2DC = 2
COLL_SELECTOR_GROUND_B4B0_NUDGE = 3
COLL_SELECTOR_GROUND_STOPWALL_B5C4 = 4
COLL_SELECTOR_AIR_471F8 = 5
COLL_SELECTOR_AIR_LEDGE_FACING = 6
COLL_SELECTOR_AIR_LEDGE_BOTH = 7
COLL_SELECTOR_AIR_DAMAGE = 8
COLL_SELECTOR_AIR_CALLBACK_LEDGE = 9
COLL_SELECTOR_AIR_PASSIVEWALL_TIMER = 10
COLL_SELECTOR_AIR_STOPCEIL = 11
COLL_SELECTOR_AIR_FLYREFLECT = 12
COLL_SELECTOR_AIR_48160 = 13
COLL_SELECTOR_AIR_477E0_CONSTRAINED = 14
COLL_SELECTOR_GROUND_B108_CONSTRAINED = 15
COLL_SELECTOR_GA_DAMAGE = 16
COLL_SELECTOR_GUARD_SETOFF_ALLOW_SDI = 17
COLL_SELECTOR_GA_CAPTURECUT = 18
COLL_SELECTOR_GA_CATCHCUT = 19
COLL_SELECTOR_GA_CLIFF_ACTION = 20
COLL_SELECTOR_GA_THROW = 21
COLL_SELECTOR_GA_ENTRY_CUSTOM = 22
COLL_SELECTOR_MATCH_REBIRTH = 23
COLL_SELECTOR_MATCH_REBIRTH_WAIT = 24
COLL_SELECTOR_GA_B108_AIR471 = 25
COLL_SELECTOR_GA_B2DC_AIR471 = 26
COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH = 27
COLL_SELECTOR_FALCON_LW_END = 28
COLL_SELECTOR_FALCON_S_START = 29
COLL_SELECTOR_MARS_HI = 30
COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL = 31

# Source-level map-collision calls made by the installed MotionState Coll callback. These values
# are a compact recipe, not a semantic action class: each row inherits the recipe from exact retail
# callback pointer identity. The conditional bit represents ft_CheckGroundAndLedge's runtime branch
# between mpColl_800471F8 and mpColl_800473CC.
# refs/melee/src/melee/ft/ft_081B.c
# refs/melee/src/melee/mp/mpcoll.c
COLL_RECIPE_AIR_471F8 = 1 << 0
COLL_RECIPE_AIR_473CC = 1 << 1
COLL_RECIPE_AIR_477E0 = 1 << 2
COLL_RECIPE_GROUND_B108 = 1 << 3
COLL_RECIPE_GROUNDED_4ACE4 = 1 << 4
COLL_RECIPE_AIR_48160 = 1 << 5
COLL_RECIPE_EDGE_SNAP = 1 << 6
COLL_RECIPE_PLATFORM_PASS = 1 << 7
COLL_RECIPE_CHECK_GROUND_LEDGE = 1 << 8
COLL_RECIPE_CLIFF_CATCH = 1 << 9
COLL_RECIPE_CLIFF_CATCH_CMD1 = 1 << 10
COLL_RECIPE_CLIFF_CATCH_DESCENDING_CMD1 = 1 << 11
COLL_RECIPE_WALLJUMP = 1 << 12
COLL_RECIPE_CAPTURE_CONSTRAINT = 1 << 13
COLL_RECIPE_FALCON_DIVE_CONDITIONAL = 1 << 14
COLL_RECIPE_WALLTECH = 1 << 15
COLL_RECIPE_COMMON_AIRBORNE = 1 << 16
COLL_RECIPE_STAGE_OBJECT_CARRY = 1 << 17
COLL_RECIPE_GROUND_TO_AIR_B108 = 1 << 18
COLL_RECIPE_EDGE_SNAP_WRAPPER = 1 << 19
COLL_RECIPE_FX_GROUND_B108 = 1 << 20
COLL_RECIPE_BASIC_LANDING = 1 << 21
COLL_RECIPE_CLIFF_CATCH_BOTH = 1 << 22
COLL_RECIPE_JUMP_FLOOR_SKIP = 1 << 23
COLL_RECIPE_FALL_FLOOR_SKIP = 1 << 24
COLL_RECIPE_FALCON_SPECIALHI_THROW0 = 1 << 25
COLL_RECIPE_FLOOR_LOSS_TO_FALL = 1 << 26
COLL_RECIPE_CATCH_START_FLOOR_LOSS = 1 << 27
COLL_RECIPE_JOBJ_NARROW_X = 1 << 28

# Runtime plan contains only callback policy after the selector has resolved geometry.
COLL_SOURCE_CLIFF_CATCH = 1 << 0
COLL_SOURCE_CLIFF_CATCH_CMD1 = 1 << 1
COLL_SOURCE_WALLJUMP = 1 << 2
COLL_SOURCE_WALLTECH = 1 << 3
COLL_SOURCE_BASIC_LANDING = 1 << 4
COLL_SOURCE_FLOOR_CALLBACK_PLATFORM_PASS = 1 << 5
COLL_SOURCE_FALCON_SPECIALHI_THROW0 = 1 << 6
COLL_SOURCE_FLOOR_LOSS_TO_FALL = 1 << 7
COLL_SOURCE_CATCH_START_FLOOR_LOSS = 1 << 8
COLL_SOURCE_GROUND_TO_AIR = 1 << 9
COLL_SOURCE_FX_GROUND_TO_AIR_PAIR = 1 << 10

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
    coll_wrapper_selector_kind: int = 0
    coll_source_plan: int = 0


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
}

CLASS2_POINTER_RULES: dict[int, PointerRule] = {
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
}

CLASS3_POINTER_RULES: dict[int, PointerRule] = {
}

HANDLER_POINTER_RULES: dict[int, tuple[Anchor, ...]] = {
    COLL_HANDLER_GROUND_B108_FALL: tuple(
        ("fox", a) for a in (0x0012, 0x0018, 0x0027, 0x0028, 0x0029)
    ),
    COLL_HANDLER_GROUND_B2DC_FALL: (("fox", 0x0013),),
    COLL_HANDLER_GROUND_B4B0_TEETER: tuple(
        ("fox", a) for a in (0x000E, 0x000F, 0x0017, 0x00F9)
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

# Exclusive map-callback geometry families. Anchors name exact retail Coll callback pointers; all
# rows sharing a pointer inherit the same family. Live selectors (GA, SDI, cooldown, command vars)
# are evaluated by that family at execution time rather than encoded as intersecting recipe bits.
# refs/melee/src/melee/ft/ft_081B.c
# refs/melee/src/melee/ft/chara/ftCommon/*.c
COLL_SELECTOR_POINTER_RULES: dict[int, tuple[Anchor, ...]] = {
    COLL_SELECTOR_GROUND_B108: tuple(
        ("fox", a) for a in (0x0012, 0x0014, 0x0015, 0x0016, 0x0018, 0x0027, 0x0028,
                              0x0029, 0x00B2, 0x00B3, 0x00B4, 0x00B6, 0x00B7, 0x00B8,
                              0x00BA, 0x00BE, 0x00BF, 0x00C0, 0x00C2, 0x00C7,
                              0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3)
    ),
    COLL_SELECTOR_GROUND_B2DC: tuple(
        ("fox", a) for a in tuple(range(0x002C, 0x0041)) +
        (0x0013, 0x00BB, 0x00BC, 0x00BD, 0x00C3, 0x00C4, 0x00C5,
                              0x00C8, 0x00C9, 0x00D4, 0x00D5,
                              0x00D6, 0x00D8, 0x00D9, 0x00E9, 0x00EA, 0x00EB,
                              0x00F5, 0x00F6)
    ),
    COLL_SELECTOR_GROUND_B4B0_NUDGE: tuple(
        ("fox", a) for a in (0x000E, 0x000F, 0x0017, 0x002A, 0x0046, 0x0047,
                              0x0048, 0x0049, 0x004A)
    ),
    COLL_SELECTOR_GROUND_STOPWALL_B5C4: (("fox", 0x00F9),),
    COLL_SELECTOR_AIR_471F8: tuple(
        ("fox", a) for a in (0x0041, 0x00EC, 0x00E6, 0x00CD, 0x00CE)
    ) + (("falcon", 0x016B),),
    COLL_SELECTOR_AIR_LEDGE_FACING: tuple(("fox", a) for a in (0x0026, 0x00F4, 0x00FB)),
    COLL_SELECTOR_AIR_DAMAGE: (("fox", 0x014F),),
    COLL_SELECTOR_AIR_CALLBACK_LEDGE: tuple(
        ("fox", a) for a in (0x0019, 0x001B, 0x001D, 0x0020, 0x0023, 0x00CC,
                              0x0105, 0x0107)
    ),
    COLL_SELECTOR_AIR_PASSIVEWALL_TIMER: (("fox", 0x00CA), ("fox", 0x00CB)),
    COLL_SELECTOR_AIR_STOPCEIL: (("fox", 0x00FA),),
    COLL_SELECTOR_AIR_FLYREFLECT: (("fox", 0x00F7), ("fox", 0x00F8)),
    COLL_SELECTOR_AIR_48160: (("fox", 0x00FC), ("fox", 0x00FD)),
    COLL_SELECTOR_AIR_477E0_CONSTRAINED: tuple(
        ("fox", a) for a in (0x00DF, 0x00E0, 0x00E1, 0x0113)
    ),
    COLL_SELECTOR_GROUND_B108_CONSTRAINED: tuple(
        ("fox", a) for a in (0x00E2, 0x00E3, 0x00E4)
    ),
    COLL_SELECTOR_GA_DAMAGE: tuple(
        ("fox", a) for a in (0x004B, 0x0057, 0x005B, 0x00B9)
    ),
    COLL_SELECTOR_GUARD_SETOFF_ALLOW_SDI: (("fox", 0x00B5),),
    COLL_SELECTOR_GA_CAPTURECUT: (("fox", 0x00E5),),
    COLL_SELECTOR_GA_CATCHCUT: (("fox", 0x00DA),),
    COLL_SELECTOR_GA_CLIFF_ACTION: tuple(
        ("fox", a) for a in (0x00FE, 0x00FF, 0x0100, 0x0101, 0x0102, 0x0103,
                              0x0104, 0x0106)
    ),
    COLL_SELECTOR_GA_THROW: tuple(("fox", a) for a in range(0x00DB, 0x00DF)),
    COLL_SELECTOR_GA_ENTRY_CUSTOM: (("fox", 0x0143), ("fox", 0x0144)),
    COLL_SELECTOR_MATCH_REBIRTH: (("fox", 0x000C),),
    COLL_SELECTOR_MATCH_REBIRTH_WAIT: (("fox", 0x000D),),
    COLL_SELECTOR_GA_B108_AIR471: (("falcon", 0x0165), ("falcon", 0x0167)),
    COLL_SELECTOR_GA_B2DC_AIR471: tuple(
        ("marth", a) for a in range(0x015D, 0x016F)
    ),
    COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH: (("falcon", 0x0161), ("falcon", 0x0162)),
    COLL_SELECTOR_FALCON_LW_END: (("falcon", 0x0166), ("falcon", 0x0168)),
    COLL_SELECTOR_FALCON_S_START: (("falcon", 0x015D),),
    COLL_SELECTOR_MARS_HI: (("marth", 0x016F), ("marth", 0x0170)),
    COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL: (("falcon", 0x0163),),
}

# Post-mpColl callback calls that are part of the same installed Coll owner. These pointer sets
# capture the source wrapper call graph (cliff admission, wall jump, and conditional map-callback
# suppression) without asking gameplay code to rebuild it from character/action lists.
COLL_RECIPE_POINTER_RULES: dict[int, tuple[Anchor, ...]] = {
    COLL_RECIPE_AIR_471F8: tuple(
        ("fox", action)
        for action in (0x00CD, 0x00CE, 0x0115, 0x0137, 0x013A, 0x013C, 0x013E, 0x0140)
    )
    + tuple(
        ("marth", action)
        for action in (0x0159, 0x015A, 0x015B, 0x015D, 0x015E, 0x0160, 0x0163, 0x0173, 0x0174)
    )
    + tuple(
        ("falcon", action)
        for action in (0x015C, 0x015F, 0x0160, 0x0164, 0x0165, 0x0166, 0x0167, 0x0169, 0x016A)
    )
    + tuple(
        ("sheik", action)
        for action in (
            0x0159,
            0x015A,
            0x015B,
            0x015C,
            0x0160,
            0x0161,
            0x0162,
            0x016B,
            0x016C,
        )
    )
    + tuple(
        ("zelda", action)
        for action in (0x0156, 0x015A, 0x015B, 0x015C, 0x0165, 0x0166)
    )
    # Every aerial Reflector callback delegates to ft_80081D0C -> 471F8. These pointers are
    # distinct from the grounded Reflector callbacks and must not rely on the runtime fx-kind
    # fallback to enter the air kernel.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::*SpecialAirLw*_Coll
    + tuple(("fox", action) for action in range(0x016D, 0x0172)),
    # Dolphin Slash's early/rising branches call ft_80083B68 -> ft_80082578 -> 477E0. Its later
    # descending branch is separately identified by CLIFF_CATCH_DESCENDING_CMD1 below.
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::{ftMs_SpecialHi_Coll,
    # ftMs_SpecialAirHi_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082578,ft_80083B68,ft_800831CC}
    COLL_RECIPE_AIR_477E0: (
        ("fox", 0x00DF),
        ("fox", 0x00E0),
        ("fox", 0x00E1),
        ("marth", 0x016F),
        ("marth", 0x0170),
    ),
    # CliffClimb/Attack/Escape/Jump1 callbacks use ft_800821DC -> mpColl_80048160 while
    # airborne and ft_80084104 -> mpColl_8004B2DC after their Phys callback grounds them.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Cliff{Climb,Attack,Escape,Jump}.c
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800821DC,ft_80084104}
    COLL_RECIPE_AIR_48160 | COLL_RECIPE_EDGE_SNAP: tuple(
        ("fox", action) for action in (0x00FE, 0x0100, 0x0102, 0x0104, 0x0106)
    ),
    # CliffCatch/CliffWait use 48160 and then the common basic-landing continuation.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_Coll
    COLL_RECIPE_AIR_48160 | COLL_RECIPE_BASIC_LANDING: (
        ("fox", 0x00FC),
        ("fox", 0x00FD),
    ),
    # Falcon Dive's released victim callback is ftCo_AirCatchHit_Coll: 471F8 followed by the
    # ordinary basic landing selector.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow1_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
    COLL_RECIPE_AIR_471F8 | COLL_RECIPE_BASIC_LANDING: (("falcon", 0x016B),),
    COLL_RECIPE_CHECK_GROUND_LEDGE: tuple(
        ("fox", action)
        for action in (0x00F4, 0x00FB, 0x015E, 0x015F, 0x0160, 0x0162, 0x0164, 0x0166)
    )
    + (("falcon", 0x0161), ("falcon", 0x0162))
    + tuple(("sheik", action) for action in (0x0166, 0x0167, 0x0168))
    + tuple(("zelda", action) for action in (0x0160, 0x0161, 0x0162)),
    COLL_RECIPE_PLATFORM_PASS: (("fox", 0x0105),),
    COLL_RECIPE_COMMON_AIRBORNE: tuple(
        ("fox", action) for action in (0x00F4, 0x00FB, 0x0105)
    ),
    COLL_RECIPE_GROUNDED_4ACE4 | COLL_RECIPE_STAGE_OBJECT_CARRY: tuple(
        ("fox", action) for action in range(0x002C, 0x0033)
    )
    + tuple(("fox", action) for action in (0x0033, 0x0038, 0x0039, 0x003A, 0x003F, 0x0040))
    + tuple(("fox", action) for action in (0x00D4, 0x00D5, 0x00D6, 0x00D8, 0x00D9, 0x00DA))
    + tuple(("fox", action) for action in range(0x00DB, 0x00DF)),
    # Falcon Kick's shared Coll callbacks select B108 while grounded and 471F8 while airborne;
    # the End callback selects B2DC instead when cmd_vars[1] is live.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    #   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll}
    COLL_RECIPE_GROUND_B108 | COLL_RECIPE_GROUND_TO_AIR_B108: tuple(
        ("fox", action) for action in (0x00CF, 0x00D1, 0x00D3, 0x00EE, 0x0155, 0x0156, 0x0157)
    )
    + (("falcon", 0x0165), ("falcon", 0x0166))
    + tuple(("marth", action) for action in range(0x0155, 0x0159))
    + (("marth", 0x0172),)
    + tuple(("fox", action) for action in (0x0161, 0x0163, 0x0165))
    + tuple(("sheik", action) for action in range(0x0155, 0x0159))
    + tuple(("sheik", action) for action in (0x0163, 0x0164, 0x0169, 0x016A))
    + tuple(("zelda", action) for action in (0x0155, 0x0157, 0x0158, 0x0159, 0x015D, 0x015E,
                                                0x0163, 0x0164)),
    COLL_RECIPE_EDGE_SNAP | COLL_RECIPE_EDGE_SNAP_WRAPPER: tuple(
        ("fox", action) for action in range(0x002C, 0x0033)
    )
    + tuple(("fox", action) for action in (0x0033, 0x0038, 0x0039, 0x003A, 0x003F, 0x0040))
    + tuple(("fox", action) for action in (0x00D4, 0x00D5, 0x00D6, 0x00D8, 0x00D9, 0x00DA))
    + tuple(("fox", action) for action in range(0x00DB, 0x00DF))
    + tuple(("fox", action) for action in (0x00E9, 0x00EB, 0x0108, 0x015D))
    + tuple(("marth", action) for action in (0x015D, 0x015E, 0x0160, 0x0163, 0x0171))
    + (("falcon", 0x015B), ("falcon", 0x015D), ("falcon", 0x0166), ("falcon", 0x0168))
    + tuple(("sheik", action) for action in (0x015D, 0x015E, 0x015F, 0x0165))
    + (("zelda", 0x015F),),
    # Grounded Firefox/Firebird and Shine callbacks use the 4B108 floor owner; each Shine row
    # dispatches its generated GroundToAir partner after that wrapper reports floor loss.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_Coll,ftFx_SpecialLwLoop_Coll,ftFx_SpecialLwHit_Coll,
    #   ftFx_SpecialLwEnd_Coll,ftFx_SpecialLwTurn_Coll}
    COLL_RECIPE_GROUND_B108 | COLL_RECIPE_FX_GROUND_B108: (
        ("fox", 0x015B),
        ("fox", 0x015C),
        ("falcon", 0x015D),
        ("falcon", 0x015E),
    ),
    COLL_RECIPE_BASIC_LANDING: tuple(
        ("fox", action)
        for action in (
            0x0019,
            0x001B,
            0x001D,
            0x0020,
            0x0023,
            0x00E6,
            0x00F4,
            0x00FB,
            0x0105,
            0x0158,
            0x0159,
            0x015A,
        )
    ),
    COLL_RECIPE_CLIFF_CATCH_BOTH: (
        ("fox", 0x0164),
        ("fox", 0x0166),
        ("falcon", 0x0161),
        ("falcon", 0x0162),
    ),
    COLL_RECIPE_JUMP_FLOOR_SKIP: (("fox", 0x0019),),
    COLL_RECIPE_FALL_FLOOR_SKIP: (("fox", 0x001D),),
    COLL_RECIPE_FALCON_SPECIALHI_THROW0: (("falcon", 0x0164),),
    COLL_RECIPE_FLOOR_LOSS_TO_FALL: tuple(
        ("fox", action)
        for action in (
            0x000E, 0x000F, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
            0x0018, 0x0027, 0x0028, 0x0029, 0x002A, 0x002C, 0x0032, 0x0033,
            0x0038, 0x0039, 0x003A, 0x003F, 0x0040, 0x0046, 0x00B2, 0x00B3,
            0x00B4, 0x00B5, 0x00B6, 0x00EB, 0x00F5, 0x00F6,
        )
    ),
    COLL_RECIPE_CATCH_START_FLOOR_LOSS: (("fox", 0x00D4), ("fox", 0x00D6)),
    COLL_RECIPE_CLIFF_CATCH: (
        ("fox", 0x0019),
        ("fox", 0x001B),
        ("fox", 0x001D),
        ("fox", 0x0020),
        ("fox", 0x0023),
        ("fox", 0x0026),
        ("fox", 0x00F4),
        ("fox", 0x00FB),
        ("fox", 0x0105),
        ("fox", 0x015E),
        ("fox", 0x015F),
        ("fox", 0x0160),
        ("fox", 0x0162),
        ("fox", 0x0164),
        ("fox", 0x0166),
        ("fox", 0x0167),
        ("sheik", 0x0166),
        ("sheik", 0x0167),
        ("sheik", 0x0168),
        ("zelda", 0x0160),
        ("zelda", 0x0161),
        ("zelda", 0x0162),
    ),
    COLL_RECIPE_CLIFF_CATCH_CMD1: (("falcon", 0x0161), ("falcon", 0x0162)),
    COLL_RECIPE_CLIFF_CATCH_DESCENDING_CMD1: (("marth", 0x016F), ("marth", 0x0170)),
    COLL_RECIPE_WALLJUMP: tuple(
        ("fox", action)
        for action in (
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
    COLL_RECIPE_WALLTECH: tuple(
        ("fox", action) for action in (0x0057, 0x005B, 0x00B9, 0x00F7)
    ),
    # CapturePulled/Wait/Damage suppress their installed map callback while x2226_b2 is set. The
    # Hi callbacks wrap 477E0; the Lw callbacks below wrap B108.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledHi_Coll,ftCo_CaptureWaitHi_Coll,ftCo_CaptureDamageHi_Coll}
    COLL_RECIPE_CAPTURE_CONSTRAINT: (
        ("fox", 0x00DF),
        ("fox", 0x00E0),
        ("fox", 0x00E1),
        ("fox", 0x0113),
    ),
    # CapturePulledLw/WaitLw/DamageLw conditionally call ft_8008403C -> 4B108 when x2226_b2 is
    # clear.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledLw_Coll,ftCo_CaptureWaitLw_Coll,ftCo_CaptureDamageLw_Coll}
    COLL_RECIPE_CAPTURE_CONSTRAINT | COLL_RECIPE_GROUND_B108: (
        ("fox", 0x00E2),
        ("fox", 0x00E3),
        ("fox", 0x00E4),
    ),
    COLL_RECIPE_FALCON_DIVE_CONDITIONAL: (("falcon", 0x0163),),
    # StopWall's 4B5C4 wrapper is the 4B4B0 grounded/teeter path with
    # mpColl_LoadECB_JObj(flags=9), which narrows its horizontal ECB to +/-1.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopWall.c::ftCo_StopWall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800843FC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B5C4,mpColl_LoadECB_JObj}
    COLL_RECIPE_JOBJ_NARROW_X: (("fox", 0x00F9),),
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
    selectors: dict[int, int] = {}
    for kind, anchors in COLL_SELECTOR_POINTER_RULES.items():
        for char, action in anchors:
            pointer = rows_by_char[char][action].coll
            if pointer == 0:
                raise ValueError(f"collision selector {kind} has a NULL callback anchor")
            previous = selectors.setdefault(pointer, kind)
            if previous != kind:
                raise ValueError(
                    f"callback pointer {pointer:#x} has conflicting selectors {previous}/{kind}"
                )
    source_effects: dict[int, int] = {}
    for effect, anchors in COLL_RECIPE_POINTER_RULES.items():
        for char, action in anchors:
            pointer = rows_by_char[char][action].coll
            if pointer == 0:
                raise ValueError(f"collision source effect {effect:#x} has a NULL callback anchor")
            source_effects[pointer] = source_effects.get(pointer, 0) | effect
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
            handler_kind = handlers.get(row.coll, COLL_HANDLER_NONE)
            coll_wrapper_selector_kind = selectors.get(row.coll, COLL_SELECTOR_NONE)
            handler_source_plan = {
                COLL_HANDLER_AIR_COMMON: COLL_RECIPE_PLATFORM_PASS,
                COLL_HANDLER_AIR_FALL_SPECIAL: COLL_RECIPE_PLATFORM_PASS,
                COLL_HANDLER_AIR_ATTACK: COLL_RECIPE_AIR_471F8,
                COLL_HANDLER_AIR_ESCAPE: COLL_RECIPE_AIR_471F8,
                COLL_HANDLER_GROUND_LANDING: COLL_RECIPE_GROUNDED_4ACE4,
                COLL_HANDLER_GROUND_LANDING_AIR: 0,
                # ftCo_Damage_Coll selects the grounded 4B108 wrapper or the airborne 477E0
                # wrapper from ground_or_air inside the same installed callback.
                # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
                # refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708,ft_80081DD4}
                COLL_HANDLER_DAMAGE_COMMON: COLL_RECIPE_GROUND_B108 | COLL_RECIPE_AIR_477E0,
                COLL_HANDLER_DOWN_DAMAGE: COLL_RECIPE_AIR_477E0,
                COLL_HANDLER_DOWN_REFLECT: COLL_RECIPE_AIR_477E0,
                COLL_HANDLER_DAMAGE_FLY: COLL_RECIPE_AIR_473CC,
                COLL_HANDLER_DAMAGE_FALL: COLL_RECIPE_AIR_473CC,
                COLL_HANDLER_DOWN_BOUND: COLL_RECIPE_GROUND_B108,
                COLL_HANDLER_DOWN_B108: COLL_RECIPE_GROUND_B108,
                COLL_HANDLER_PASSIVE_B108: COLL_RECIPE_GROUND_B108,
                COLL_HANDLER_DOWN_B2DC: COLL_RECIPE_EDGE_SNAP,
                COLL_HANDLER_PASSIVE_B2DC: COLL_RECIPE_EDGE_SNAP,
                COLL_HANDLER_PASSIVE_WALL: COLL_RECIPE_PLATFORM_PASS,
                COLL_HANDLER_PASSIVE_CEIL: COLL_RECIPE_PLATFORM_PASS,
            }.get(handler_kind)
            if handler_source_plan is None:
                coll_source_plan = 0
            else:
                coll_source_plan = handler_source_plan
            coll_source_plan |= source_effects.get(row.coll, 0)
            fx_special_kind = fx_kind_by_anim.get(row.anim, 0)
            if (FX_SPECIAL_KIND_VALUES["SPECIAL_LW_START"] <= fx_special_kind <=
                    FX_SPECIAL_KIND_VALUES["SPECIAL_LW_TURN"]):
                # Shine action ids differ between Fox and Falco because their character tables do
                # not have identical preceding special-state counts. The extracted ANIM callback
                # identity is the stable owner of the five grounded B108 callbacks.
                # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::*_Coll
                coll_source_plan |= COLL_RECIPE_GROUND_B108 | COLL_RECIPE_FX_GROUND_B108
            if coll_wrapper_selector_kind == COLL_SELECTOR_NONE and row.coll != 0:
                if ((coll_source_plan & COLL_RECIPE_GROUND_B108) and
                        (coll_source_plan & COLL_RECIPE_AIR_471F8)):
                    coll_wrapper_selector_kind = COLL_SELECTOR_GA_B108_AIR471
                elif ((coll_source_plan & (COLL_RECIPE_EDGE_SNAP |
                                           COLL_RECIPE_GROUNDED_4ACE4)) and
                      (coll_source_plan & COLL_RECIPE_AIR_471F8)):
                    coll_wrapper_selector_kind = COLL_SELECTOR_GA_B2DC_AIR471
                elif coll_source_plan & COLL_RECIPE_CHECK_GROUND_LEDGE:
                    coll_wrapper_selector_kind = (
                        COLL_SELECTOR_AIR_LEDGE_BOTH
                        if coll_source_plan & COLL_RECIPE_CLIFF_CATCH_BOTH
                        else COLL_SELECTOR_AIR_LEDGE_FACING
                    )
                elif coll_source_plan & COLL_RECIPE_AIR_477E0:
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_477E0_CONSTRAINED
                elif coll_source_plan & COLL_RECIPE_AIR_48160:
                    coll_wrapper_selector_kind = (
                        COLL_SELECTOR_GA_CLIFF_ACTION
                        if coll_source_plan & COLL_RECIPE_EDGE_SNAP
                        else COLL_SELECTOR_AIR_48160
                    )
                elif coll_source_plan & COLL_RECIPE_AIR_473CC:
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_LEDGE_FACING
                elif coll_source_plan & COLL_RECIPE_AIR_471F8:
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_471F8
                elif coll_source_plan & COLL_RECIPE_GROUND_B108:
                    coll_wrapper_selector_kind = (
                        COLL_SELECTOR_GROUND_B108_CONSTRAINED
                        if coll_source_plan & COLL_RECIPE_CAPTURE_CONSTRAINT
                        else COLL_SELECTOR_GROUND_B108
                    )
                elif coll_source_plan & (COLL_RECIPE_EDGE_SNAP |
                                         COLL_RECIPE_GROUNDED_4ACE4):
                    coll_wrapper_selector_kind = COLL_SELECTOR_GROUND_B2DC
                elif coll_source_plan & (COLL_RECIPE_PLATFORM_PASS |
                                         COLL_RECIPE_COMMON_AIRBORNE):
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_CALLBACK_LEDGE
                elif coll_source_plan & COLL_RECIPE_BASIC_LANDING:
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_471F8
                elif coll_source_plan & (COLL_RECIPE_CLIFF_CATCH |
                                         COLL_RECIPE_CLIFF_CATCH_CMD1 |
                                         COLL_RECIPE_CLIFF_CATCH_DESCENDING_CMD1 |
                                         COLL_RECIPE_CLIFF_CATCH_BOTH):
                    coll_wrapper_selector_kind = COLL_SELECTOR_AIR_LEDGE_FACING
            recipe = coll_source_plan
            if recipe & COLL_RECIPE_JUMP_FLOOR_SKIP:
                class3_bits |= CLASS3_JUMP_FLOOR_SKIP
            if recipe & COLL_RECIPE_FALL_FLOOR_SKIP:
                class3_bits |= CLASS3_FALL_FLOOR_SKIP
            coll_source_plan = 0
            for recipe_bit, source_bit in (
                (COLL_RECIPE_CLIFF_CATCH, COLL_SOURCE_CLIFF_CATCH),
                (COLL_RECIPE_CLIFF_CATCH_CMD1, COLL_SOURCE_CLIFF_CATCH_CMD1),
                (COLL_RECIPE_WALLJUMP, COLL_SOURCE_WALLJUMP),
                (COLL_RECIPE_WALLTECH, COLL_SOURCE_WALLTECH),
                (COLL_RECIPE_BASIC_LANDING, COLL_SOURCE_BASIC_LANDING),
                (COLL_RECIPE_PLATFORM_PASS, COLL_SOURCE_FLOOR_CALLBACK_PLATFORM_PASS),
                (COLL_RECIPE_FALCON_SPECIALHI_THROW0,
                 COLL_SOURCE_FALCON_SPECIALHI_THROW0),
                (COLL_RECIPE_FLOOR_LOSS_TO_FALL, COLL_SOURCE_FLOOR_LOSS_TO_FALL),
                (COLL_RECIPE_CATCH_START_FLOOR_LOSS,
                 COLL_SOURCE_CATCH_START_FLOOR_LOSS),
                (COLL_RECIPE_GROUND_TO_AIR_B108, COLL_SOURCE_GROUND_TO_AIR),
                (COLL_RECIPE_FX_GROUND_B108, COLL_SOURCE_FX_GROUND_TO_AIR_PAIR),
            ):
                if recipe & recipe_bit:
                    coll_source_plan |= source_bit
            decorated.append(
                replace(
                    row,
                    class_bits=class_bits,
                    class2_bits=class2_bits,
                    class3_bits=class3_bits,
                    fx_special_kind=fx_special_kind,
                    coll_handler_kind=handler_kind,
                    coll_wrapper_selector_kind=coll_wrapper_selector_kind,
                    coll_source_plan=coll_source_plan,
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
        "ftCo_SM_Squat": 0x0027,
        "ftCo_SM_SquatWait": 0x0028,
        "ftCo_SM_SquatRv": 0x0029,
        "ftCo_SM_Landing": 0x002A,
        "ftCo_SM_LandingFallSpecial": 0x002B,
        "ftCo_SM_LandingAirN": 0x0046,
        "ftCo_SM_LandingAirF": 0x0047,
        "ftCo_SM_LandingAirB": 0x0048,
        "ftCo_SM_LandingAirHi": 0x0049,
        "ftCo_SM_LandingAirLw": 0x004A,
        "ftCo_SM_EscapeN": 0x00EB,
        "ftCo_SM_EscapeAir": 0x00EC,
        "ftCo_SM_CliffAttackSlow": 0x0100,
        "ftCo_SM_CliffAttackQuick": 0x0101,
        "ftCo_SM_CliffCatch": 0x00FC,
        "ftCo_SM_CliffWait": 0x00FD,
        "ftCo_SM_CliffClimbSlow": 0x00FE,
        "ftCo_SM_CliffClimbQuick": 0x00FF,
        "ftCo_SM_CliffEscapeSlow": 0x0102,
        "ftCo_SM_CliffEscapeQuick": 0x0103,
        "ftCo_SM_CliffJumpSlow1": 0x0104,
        "ftCo_SM_CliffJumpSlow2": 0x0105,
        "ftCo_SM_CliffJumpQuick1": 0x0106,
        "ftCo_SM_CliffJumpQuick2": 0x0107,
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
        # Angled side attacks are distinct MotionState/submotion rows. Some characters share the
        # same command stream across the family while others (notably Captain) author different
        # HitCapsules, so keep the concrete source row instead of aliasing at runtime.
        # refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* / AttackS4* rows)
        # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackS3.c,ftCo_AttackS4.c}
        "ftCo_SM_AttackS3Hi": 0x0033,
        "ftCo_SM_AttackS3HiS": 0x0034,
        "ftCo_SM_AttackS3": 0x0035,
        "ftCo_SM_AttackS3LwS": 0x0036,
        "ftCo_SM_AttackS3Lw": 0x0037,
        "ftCo_SM_AttackHi3": 0x0038,
        "ftCo_SM_AttackLw3": 0x0039,
        "ftCo_SM_AttackS4Hi": 0x003A,
        "ftCo_SM_AttackS4HiS": 0x003B,
        "ftCo_SM_AttackS4": 0x003C,
        "ftCo_SM_AttackS4LwS": 0x003D,
        "ftCo_SM_AttackS4Lw": 0x003E,
        "ftCo_SM_AttackHi4": 0x003F,
        "ftCo_SM_AttackLw4": 0x0040,
        "ftCo_SM_AttackAirN": 0x0041,
        "ftCo_SM_AttackAirF": 0x0042,
        "ftCo_SM_AttackAirB": 0x0043,
        "ftCo_SM_AttackAirHi": 0x0044,
        "ftCo_SM_AttackAirLw": 0x0045,
        "ftCo_SM_DownAttackU": 0x00BB,
        "ftCo_SM_DownAttackD": 0x00C3,
        "ftCo_SM_DownBoundU": 0x00B7,
        "ftCo_SM_DownWaitU": 0x00B8,
        "ftCo_SM_DownDamageU": 0x00B9,
        "ftCo_SM_DownStandU": 0x00BA,
        "ftCo_SM_DownFowardU": 0x00BC,
        "ftCo_SM_DownBackU": 0x00BD,
        "ftCo_SM_DownSpotU": 0x00BE,
        "ftCo_SM_DownBoundD": 0x00BF,
        "ftCo_SM_DownWaitD": 0x00C0,
        "ftCo_SM_DownDamageD": 0x00C1,
        "ftCo_SM_DownStandD": 0x00C2,
        "ftCo_SM_DownFowardD": 0x00C4,
        "ftCo_SM_DownBackD": 0x00C5,
        "ftCo_SM_DownSpotD": 0x00C6,
        "ftCo_SM_Passive": 0x00C7,
        "ftCo_SM_PassiveStandF": 0x00C8,
        "ftCo_SM_PassiveStandB": 0x00C9,
        "ftCo_SM_PassiveWall": 0x00CA,
        "ftCo_SM_PassiveWallJump": 0x00CB,
        "ftCo_SM_PassiveCeil": 0x00CC,
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
        # Common item-swing scripts are fighter HitCapsule producers. Derive their submotion ids
        # from the retail MotionState rows instead of walking arbitrary Pl*.dat S_TEMP4 indices;
        # absent common animations can otherwise make a valid-looking pointer outside the actual
        # subaction table. Action order is ftCo_MS_SwordSwing1..LipstickSwingDash.
        # refs/melee/src/melee/ft/chara/ftCommon/forward.h::{ftCo_MotionState,ftCo_Submotion}
        "ftCo_SM_SwordSwing1": 0x0078,
        "ftCo_SM_SwordSwing3": 0x0079,
        "ftCo_SM_SwordSwing4": 0x007A,
        "ftCo_SM_SwordSwingDash": 0x007B,
        "ftCo_SM_BatSwing1": 0x007C,
        "ftCo_SM_BatSwing3": 0x007D,
        "ftCo_SM_BatSwing4": 0x007E,
        "ftCo_SM_BatSwingDash": 0x007F,
        "ftCo_SM_ParasolSwing1": 0x0080,
        "ftCo_SM_ParasolSwing3": 0x0081,
        "ftCo_SM_ParasolSwing4": 0x0082,
        "ftCo_SM_ParasolSwingDash": 0x0083,
        "ftCo_SM_HarisenSwing1": 0x0084,
        "ftCo_SM_HarisenSwing3": 0x0085,
        "ftCo_SM_HarisenSwing4": 0x0086,
        "ftCo_SM_HarisenSwingDash": 0x0087,
        "ftCo_SM_StarRodSwing1": 0x0088,
        "ftCo_SM_StarRodSwing3": 0x0089,
        "ftCo_SM_StarRodSwing4": 0x008A,
        "ftCo_SM_StarRodSwingDash": 0x008B,
        "ftCo_SM_LipstickSwing1": 0x008C,
        "ftCo_SM_LipstickSwing3": 0x008D,
        "ftCo_SM_LipstickSwing4": 0x008E,
        "ftCo_SM_LipstickSwingDash": 0x008F,
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
    for width in (2, 4, 4, 2, 2, 2, 2, 2, 4, 4, 4, 1, 1, 1):
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
    buf += bytes(row.coll_wrapper_selector_kind for row in rows)
    for row in rows:
        buf += _u32(row.coll_source_plan)
    if len(buf) != offsets[-1] + count * 4:
        raise AssertionError((len(buf), offsets[-1] + count * 4))
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
