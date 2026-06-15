from __future__ import annotations

import argparse

from tools.extraction.char_registry import CHARS
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from tools.extraction.extract_attack_id_move_id import (
    _eval_motion_flags_expr,
    _parse_ft_move_id_enum,
    _parse_motion_flags_constants,
    _u16_le,
    _u32_le,
)


FORMAT_MAGIC = b"MSLMSO01"
FORMAT_VERSION = 18
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
CLASS_COMMON_AIR_COLL = 1 << 9
CLASS_COMMON_AIR_WALLJUMP_COLL = 1 << 10
CLASS_LANDING_COLL = 1 << 11
CLASS_LANDING_AIR_COLL = 1 << 12
CLASS_DAMAGE_COMMON_COLL = 1 << 13
CLASS_DAMAGE_FLY_COLL = 1 << 14
CLASS_DAMAGE_FALL_COLL = 1 << 15
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
CLASS_ESCAPE_AIR_COLL = 1 << 29
CLASS_FX_SPECIALS_GROUND_B108_COLL = 1 << 30
CLASS_FT80082B1C_BASIC_LANDING_COLL = 1 << 31

CLASS2_COMMON_GROUNDED_COLL = 1 << 0
CLASS2_COMMON_GROUNDED_B108_COLL = 1 << 1
CLASS2_COMMON_AIRBORNE_COLL = 1 << 2
CLASS2_COMMON_GROUNDED_B2DC_COLL = 1 << 3
CLASS2_COMMON_GROUNDED_B4B0_COLL = 1 << 4

CLASS3_PHASE4_ATTACK_AIR_COLL = 1 << 0
CLASS3_PHASE4_ESCAPE_AIR_COLL = 1 << 1
CLASS3_PHASE4_DAMAGE_COMMON_COLL = 1 << 2
CLASS3_PHASE4_DAMAGE_FLY_COLL = 1 << 3
CLASS3_PHASE4_DAMAGE_FALL_COLL = 1 << 4

# fx_special_kind (v18): per-(char, action) identity of the Fox/Falco bespoke special
# MotionState rows, generated 1:1 from each row's ANIM callback symbol (unique per row).
# This is THE migration target for runtime `msl_char_id_is_spacie(char) && action_id ==
# MSL_ACT_FX_*` predicate gates: a new character's same-numbered actions carry its own
# callbacks and stay kind 0 (NONE). Values are the 1-based index of this ordered table;
# the C enum (motion_state_owners.h MslMsFxSpecialKind) and the callback_symbols.json
# "fx_special_kinds" manifest are parity-locked to it by tests.
FX_SPECIAL_KIND_BY_ANIM_CB: tuple[tuple[str, str], ...] = (
    ("ftFx_SpecialNStart_Anim", "SPECIAL_N_START"),
    ("ftFx_SpecialNLoop_Anim", "SPECIAL_N_LOOP"),
    ("ftFx_SpecialNEnd_Anim", "SPECIAL_N_END"),
    ("ftFx_SpecialAirNStart_Anim", "SPECIAL_AIR_N_START"),
    ("ftFx_SpecialAirNLoop_Anim", "SPECIAL_AIR_N_LOOP"),
    ("ftFx_SpecialAirNEnd_Anim", "SPECIAL_AIR_N_END"),
    ("ftFx_SpecialSStart_Anim", "SPECIAL_S_START"),
    ("ftFx_SpecialS_Anim", "SPECIAL_S"),
    ("ftFx_SpecialSEnd_Anim", "SPECIAL_S_END"),
    ("ftFx_SpecialAirSStart_Anim", "SPECIAL_AIR_S_START"),
    ("ftFx_SpecialAirS_Anim", "SPECIAL_AIR_S"),
    ("ftFx_SpecialAirSEnd_Anim", "SPECIAL_AIR_S_END"),
    ("ftFx_SpecialHiHold_Anim", "SPECIAL_HI_HOLD"),
    ("ftFx_SpecialHiHoldAir_Anim", "SPECIAL_HI_HOLD_AIR"),
    ("ftFx_SpecialHi_Anim", "SPECIAL_HI"),
    ("ftFx_SpecialAirHi_Anim", "SPECIAL_AIR_HI"),
    ("ftFx_SpecialHiLanding_Anim", "SPECIAL_HI_LANDING"),
    ("ftFx_SpecialHiFall_Anim", "SPECIAL_HI_FALL"),
    ("ftFx_SpecialHiBound_Anim", "SPECIAL_HI_BOUND"),
    ("ftFx_SpecialLwStart_Anim", "SPECIAL_LW_START"),
    ("ftFx_SpecialLwLoop_Anim", "SPECIAL_LW_LOOP"),
    ("ftFx_SpecialLwHit_Anim", "SPECIAL_LW_HIT"),
    ("ftFx_SpecialLwEnd_Anim", "SPECIAL_LW_END"),
    ("ftFx_SpecialLwTurn_Anim", "SPECIAL_LW_TURN"),
    ("ftFx_SpecialAirLwStart_Anim", "SPECIAL_AIR_LW_START"),
    ("ftFx_SpecialAirLwLoop_Anim", "SPECIAL_AIR_LW_LOOP"),
    ("ftFx_SpecialAirLwHit_Anim", "SPECIAL_AIR_LW_HIT"),
    ("ftFx_SpecialAirLwEnd_Anim", "SPECIAL_AIR_LW_END"),
    ("ftFx_SpecialAirLwTurn_Anim", "SPECIAL_AIR_LW_TURN"),
)
FX_SPECIAL_KIND_BY_SYMBOL: dict[str, int] = {
    sym: i + 1 for i, (sym, _name) in enumerate(FX_SPECIAL_KIND_BY_ANIM_CB)
}
FX_SPECIAL_KIND_VALUES: dict[str, int] = {
    name: i + 1 for i, (_sym, name) in enumerate(FX_SPECIAL_KIND_BY_ANIM_CB)
}


@dataclass(frozen=True)
class MotionStateRow:
    action_id: int
    symbol: str
    submotion_id: int
    x4_flags: int
    motion_state_word: int
    anim_cb: str
    iasa_cb: str
    phys_cb: str
    coll_cb: str
    cam_cb: str
    class_bits: int
    class2_bits: int
    class3_bits: int


@dataclass(frozen=True)
class TableSpec:
    src: Path
    rel_name: str


def _strip_comments(line: str) -> str:
    return line.split("//", 1)[0].strip()


def _parse_enum(header: Path, *, enum_typedef: str, prefixes: tuple[str, ...], symbols: dict[str, int]) -> dict[str, int]:
    txt = header.read_text(encoding="utf-8", errors="replace").splitlines()
    in_enum = False
    value: int | None = None
    out: dict[str, int] = {}

    for line in txt:
        if enum_typedef in line:
            in_enum = True
            continue
        if not in_enum:
            continue
        if "}" in line and ";" in line:
            break

        s = _strip_comments(line).rstrip(",")
        if not s or not s.startswith(prefixes):
            continue
        if "=" in s:
            name, rhs = [x.strip() for x in s.split("=", 1)]
            if not re.fullmatch(r"[A-Za-z0-9_()+\- \t]+", rhs):
                raise RuntimeError(f"unsupported enum expression {rhs!r} in {header}")
            env = dict(symbols)
            env.update(out)
            value = int(eval(rhs, {"__builtins__": {}}, env))  # noqa: S307 - trusted local decomp
            out[name] = value
        else:
            if value is None:
                value = 0
            else:
                value += 1
            out[s] = value

    if not out:
        raise RuntimeError(f"failed to parse enum {enum_typedef!r} from {header}")
    return out


def _parse_submotion_ids(melee_decomp_root: Path) -> dict[str, int]:
    common_forward = melee_decomp_root / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h"

    out: dict[str, int] = {}
    out.update(_parse_enum(common_forward, enum_typedef="typedef enum ftCo_Submotion", prefixes=("ftCo_SM_",), symbols=out))
    # Per-character submotion enums (clones reuse the donor's enum: Falco uses ftFox's
    # ftFx_SM_*, so submotion_dir rows de-duplicate).
    seen: set[str] = set()
    for info in CHARS.values():
        if info.submotion_dir in seen:
            continue
        seen.add(info.submotion_dir)
        fwd = (
            melee_decomp_root / "src" / "melee" / "ft" / "chara" / info.submotion_dir
            / "forward.h"
        )
        enum_name = info.submotion_prefix.removesuffix("SM_") + "Submotion"
        out.update(
            _parse_enum(
                fwd,
                enum_typedef=f"typedef enum {enum_name}",
                prefixes=(info.submotion_prefix,),
                symbols=out,
            )
        )
    return out


def _class_bits_for_callbacks(callbacks: tuple[str, str, str, str, str]) -> int:
    bits = 0
    iasa_cb = callbacks[1]
    phys_cb = callbacks[2]
    coll_cb = callbacks[3]
    if any(cb.startswith("ftCo_AttackAir") for cb in callbacks):
        bits |= CLASS_ATTACK_AIR
    if any(cb.startswith("ftCo_Attack") and not cb.startswith("ftCo_AttackAir") for cb in callbacks):
        bits |= CLASS_GROUNDED_ATTACK
    if iasa_cb in {
        "ftCo_AttackS3_IASA",
        "ftCo_AttackHi3_IASA",
        "ftCo_AttackS4_IASA",
        "ftCo_AttackHi4_IASA",
        "ftCo_AttackLw4_IASA",
    }:
        bits |= CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS
    if iasa_cb in {
        "ftCo_Attack11_IASA",
        "ftCo_Attack12_IASA",
        "ftCo_Attack13_IASA",
        "ftCo_AttackDash_IASA",
        "ftCo_AttackS3_IASA",
        "ftCo_AttackHi3_IASA",
        "ftCo_AttackLw3_IASA",
        "ftCo_AttackS4_IASA",
        "ftCo_AttackHi4_IASA",
        "ftCo_AttackLw4_IASA",
    }:
        bits |= CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION
    if iasa_cb in {
        "ftCo_Attack13_IASA",
        "ftCo_AttackS3_IASA",
        "ftCo_AttackHi4_IASA",
        "ftCo_AttackLw4_IASA",
    }:
        bits |= CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD
    if iasa_cb in {
        "ftCo_Wait_IASA",
        "ftCo_Walk_IASA",
        "ftCo_Turn_IASA",
        "ftCo_Dash_IASA",
        "ftCo_Run_IASA",
        "ftCo_RunDirect_IASA",
        "ftCo_Squat_IASA",
        "ftCo_SquatWait_IASA",
        "ftCo_SquatRv_IASA",
    }:
        # These grounded locomotion IASA callbacks reach ftCo_80091A4C on the source path that can
        # expose a fresh GuardOn row before a delayed digital L/R edge is consumed by GuardOn_IASA.
        bits |= CLASS_GUARDON_FRAME_START_X672_IASA
    if any(cb.startswith("ftCo_AttackS3") for cb in callbacks):
        bits |= CLASS_ATTACK_S3
    if any(cb.startswith("ftCo_AttackS4") for cb in callbacks):
        bits |= CLASS_ATTACK_S4
    if any(cb.startswith("ftCo_Damage_") or cb.startswith("ftCo_DownDamage_") for cb in callbacks):
        bits |= CLASS_DAMAGE_COMMON
    if any(cb.startswith("ftCo_DamageFly") or cb.startswith("ftCo_FlyReflect_") for cb in callbacks):
        bits |= CLASS_DAMAGE_FLY
    if any(cb.startswith("ftCo_LandingAir") for cb in callbacks):
        bits |= CLASS_LANDING_AIR
    if any(cb.startswith("ftCo_Fall") for cb in callbacks):
        bits |= CLASS_COMMON_FALL
    if any(cb.startswith("ftFx_SpecialHi") or cb.startswith("ftFx_SpecialAirHi") for cb in callbacks):
        bits |= CLASS_SPECIALHI
    if phys_cb in {
        "ftCo_Jump_Phys",
        "ftCo_JumpAerial_Phys",
        "ftCo_Fall_Phys",
        "ftCo_FallAerial_Phys",
        "ftCo_FallSpecial_Phys",
    }:
        bits |= CLASS_COMMON_AIR_PHYS
    if coll_cb in {
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_Fall_Coll",
        "ftCo_FallAerial_Coll",
        "ftCo_FallSpecial_Coll",
    }:
        bits |= CLASS_COMMON_AIR_COLL
    if coll_cb in {
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_Fall_Coll",
        "ftCo_PassiveWall_Coll",
    }:
        # These callbacks route through `ft_800831CC` or `ft_80083318`, whose source path runs the
        # airborne wall envelope and then the common walljump/ledge consumers when no floor
        # callback fires. PassiveWall{Jump} shares that collision helper even though its Phys
        # callback is not the generic common-air Phys family.
        bits |= CLASS_COMMON_AIR_WALLJUMP_COLL
    if coll_cb in {
        "ftCo_AttackAir_Coll",
        "ftCo_AirCatch_Coll",
        "ftCo_EscapeAir_Coll",
        "ftCo_ItemThrowAir_Coll",
        "ftCo_ShieldBreakFall_Coll",
        "ftCo_ShieldBreakFly_Coll",
        "ftCo_HammerFall_Coll",
        "ftCo_CargoFall_Coll",
        "ftCo_CargoThrow_Coll",
        "ftCo_YoshiEgg_Coll",
        "ftCo_KinokoSmallStart_Coll",
        "ftCo_KinokoSmallEnd_Coll",
        "ftCo_KinokoGiantStart_Coll",
        "ftCo_KinokoGiantEnd_Coll",
        "ftSk_SpecialAirSStart_Coll",
        "ftSk_SpecialAirS_Coll",
        "ftSk_SpecialAirSEnd_Coll",
    }:
        # These common Fox/Falco collision callbacks delegate through `ft_80082C74`, whose
        # `ft_80081D0C` helper loads the normal airborne ECB and runs `mpColl_800471F8`. That
        # source owner uses the full airborne wall/floor/ceiling collision callback without the
        # common-air walljump post-consumers.
        # Sheik Chain aerial Start/Active/End call `ft_80081D0C` directly before their source
        # state handoff.
        # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
        #   ftSk_SpecialAirSStart_Coll,ftSk_SpecialAirS_Coll,ftSk_SpecialAirSEnd_Coll}
        bits |= CLASS_FT80081D0C_AIR_COLL
    if coll_cb in {
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_Fall_Coll",
        "ftCo_FallAerial_Coll",
        "ftCo_FallSpecial_Coll",
        "ftCo_CliffJump2_Coll",
        "ftCo_PassiveWall_Coll",
        "ftCo_PassiveCeil_Coll",
    }:
        # These source callbacks route their floor check through ft_80083090/ft_800831CC/
        # ft_800835B0 with ftCo_80096CC8 as the platform callback. The callback accepts hard
        # floors and rejects passable platforms when held down, so gameplay should ask this
        # generated owner instead of maintaining local common-air action lists.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_800831CC,ft_800835B0}
        bits |= CLASS_FT80083090_PLATFORM_PASS_COLL
    if coll_cb in {
        "ftFx_SpecialAirSStart_Coll",
        "ftFx_SpecialAirS_Coll",
        "ftFx_SpecialAirSEnd_Coll",
        "ftFx_SpecialHiHoldAir_Coll",
        "ftFx_SpecialAirHi_Coll",
        "ftFx_SpecialHiFall_Coll",
        "ftSk_SpecialAirHiStart_0_Coll",
        "ftSk_SpecialAirHiStart_1_Coll",
        "ftSk_SpecialAirHi_Coll",
        "ftCo_MissFoot_Coll",
        "ftCo_Pass_Coll",
    }:
        # Fox/Falco aerial Side-B/SpecialHi{HoldAir,AirHi,Fall}, Sheik Vanish aerial
        # Start0/Start1/End, and common MissFoot/Pass collision callbacks call
        # ft_CheckGroundAndLedge directly (MissFoot/Pass through ft_80082F28), which snapshots
        # CollData and runs the airborne mpColl floor/wall/ceiling owner without the held-down
        # common-air platform rejection path.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        #   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
        # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
        #   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialAirHi_Coll}
        bits |= CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL
    if coll_cb == "ftCo_Landing_Coll":
        bits |= CLASS_LANDING_COLL
    if coll_cb == "ftCo_LandingAir_Coll":
        bits |= CLASS_LANDING_AIR_COLL
    if coll_cb in {"ftCo_Damage_Coll", "ftCo_DownDamage_Coll"}:
        bits |= CLASS_DAMAGE_COMMON_COLL
    if coll_cb in {"ftCo_DamageFly_Coll", "ftCo_DamageFlyRoll_Coll", "ftCo_FlyReflect_Coll"}:
        bits |= CLASS_DAMAGE_FLY_COLL
    if coll_cb == "ftCo_DamageFall_Coll":
        bits |= CLASS_DAMAGE_FALL_COLL
    if coll_cb in {
        "ftCo_Wait_Coll",
        "ftCo_Walk_Coll",
        "ftCo_Turn_Coll",
        "ftCo_TurnRun_Coll",
        "ftCo_Dash_Coll",
        "ftCo_Run_Coll",
        "ftCo_RunDirect_Coll",
        "ftCo_RunBrake_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_Landing_Coll",
        "ftCo_LandingAir_Coll",
        "ftCo_Attack11_Coll",
        "ftCo_Attack100Start_Coll",
        "ftCo_Attack100Loop_Coll",
        "ftCo_Attack100End_Coll",
        "ftCo_AttackDash_Coll",
        "ftCo_AttackS3_Coll",
        "ftCo_AttackHi3_Coll",
        "ftCo_AttackLw3_Coll",
        "ftCo_AttackS4_Coll",
        "ftCo_AttackHi4_Coll",
        "ftCo_AttackLw4_Coll",
        "ftCo_GuardOn_Coll",
        "ftCo_Guard_Coll",
        "ftCo_GuardOff_Coll",
        "ftCo_GuardSetOff_Coll",
        "ftCo_GuardReflect_Coll",
        "ftCo_Catch_Coll",
        "ftCo_CatchDash_Coll",
        "ftCo_CatchPull_Coll",
        "ftCo_CatchDashPull_Coll",
        "ftCo_CatchWait_Coll",
        "ftCo_CatchAttack_Coll",
        "ftCo_CatchCut_Coll",
        "ftCo_ThrowF_Coll",
        "ftCo_ThrowB_Coll",
        "ftCo_ThrowHi_Coll",
        "ftCo_ThrowLw_Coll",
        "ftCo_Down_Coll",
        "ftCo_DownAttack_Coll",
        "ftCo_PassiveStand_Coll",
    }:
        # These grounded callbacks keep an attached floor through the common map-collision owner
        # paths (`ft_80084280`, `ft_800844EC`, `ft_80084104`, `ft_800841B8`, or adjacent guarded
        # variants). They should inherit stage-object platform transform while already attached to a
        # moving floor.
        #
        # Deliberately excluded: DownBound/DownWait/DownStand/DownSpot/Passive callbacks that route
        # through `ft_80083F88` (or a custom downed/damage projection owner), and airborne collision
        # callbacks that may land this frame rather than persist an existing moving-floor attachment.
        bits |= CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL
    if coll_cb in {
        "ftCo_KneeBend_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_Turn_Coll",
        "ftCo_Rebound_Coll",
        "ftCo_DownBound_Coll",
        "ftCo_DownWait_Coll",
        "ftCo_DownStand_Coll",
        "ftCo_DownSpot_Coll",
        "ftCo_Passive_Coll",
        "ftCo_ShieldBreakDown_Coll",
        "ftCo_ShieldBreakStand_Coll",
        "ftCo_Furafura_Coll",
        "ftFx_SpecialNStart_Coll",
        "ftFx_SpecialNLoop_Coll",
        "ftFx_SpecialNEnd_Coll",
    }:
        # Collision callbacks whose decomp bodies call `ft_80083F88(gobj)`. That wrapper delegates
        # through `ft_80082708` / `mpColl_8004B108` and can leave ground when the frame's current
        # root position has crossed a floor endpoint.
        #
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
        # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
        bits |= CLASS_FT80083F88_GROUND_TO_AIR_COLL
    if coll_cb in {
        "ftCo_Attack11_Coll",
        "ftCo_Attack100Start_Coll",
        "ftCo_Attack100Loop_Coll",
        "ftCo_Attack100End_Coll",
        "ftCo_AttackDash_Coll",
        "ftCo_AttackS3_Coll",
        "ftCo_AttackHi3_Coll",
        "ftCo_AttackLw3_Coll",
        "ftCo_AttackS4_Coll",
        "ftCo_AttackHi4_Coll",
        "ftCo_AttackLw4_Coll",
        "ftCo_Escape_Coll",
        "ftCo_EscapeN_Coll",
        "ftCo_AppealS_Coll",
        "ftCo_Catch_Coll",
        "ftCo_CatchDash_Coll",
        "ftCo_CatchPull_Coll",
        "ftCo_CatchDashPull_Coll",
        "ftCo_CatchWait_Coll",
        "ftCo_CatchAttack_Coll",
        "ftCo_CatchCut_Coll",
        "ftCo_ThrowF_Coll",
        "ftCo_ThrowB_Coll",
        "ftCo_ThrowHi_Coll",
        "ftCo_ThrowLw_Coll",
        "ftCo_Down_Coll",
        "ftCo_DownAttack_Coll",
        "ftCo_PassiveStand_Coll",
        "ftFx_SpecialSEnd_Coll",
    }:
        # These grounded collision callbacks route to `ft_800827A0`, directly or through
        # `ft_80084104` / `ft_800841B8`, and therefore consume `mpColl_8004B2DC`'s
        # `mpColl_8004A45C_Floor` endpoint snap fallback. Keep this as generated callback
        # ownership so runtime edge-snap admission does not maintain a parallel action-id list.
        #
        # refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104,ft_800841B8}
        # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
        bits |= CLASS_FT800827A0_EDGE_SNAP_COLL
    if coll_cb == "ftCo_EscapeAir_Coll":
        bits |= CLASS_ESCAPE_AIR_COLL
    if coll_cb in {
        # Character-special collision callbacks that call ft_800827A0 (StopAtLedge) on the
        # grounded branch. The Marth Dancing Blade stage callbacks are shared by ground and air
        # variants and branch on ground_or_air internally; the grounded branch is 827A0. Sheik
        # grounded Vanish end calls the same owner before entering the air fall state.
        # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c::{
        #   ftMs_SpecialAirS1_Coll,ftMs_SpecialS3_Coll (and the S2/S4 pairs)}
        # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_Coll
        # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_Coll
        "ftMs_SpecialAirS1_Coll",
        "ftMs_SpecialS2_Coll",
        "ftMs_SpecialS3_Coll",
        "ftMs_SpecialS4_Coll",
        "ftMs_SpecialLw_Coll",
        "ftSk_SpecialHi_Coll",
        "ftSk_SpecialSStart_Coll",
        "ftSk_SpecialS_Coll",
        "ftSk_SpecialSEnd_Coll",
    }:
        # Sheik Chain grounded Start/Active/End all call `ft_800827A0` before their source handoff.
        # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
        #   ftSk_SpecialSStart_Coll,ftSk_SpecialS_Coll,ftSk_SpecialSEnd_Coll}
        bits |= CLASS_FT800827A0_EDGE_SNAP_COLL
    if coll_cb in {
        # Marth aerial special collision callbacks that call ft_80081D0C
        # (CheckGroundOnly, no ledge grab). The DB stage callbacks above also carry this for
        # their airborne branch; SpecialAirN*/AirLw* are air-only.
        # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::{
        #   ftMs_SpecialAirNStart_Coll,ftMs_SpecialAirNLoop_Coll,ftMs_SpecialAirNEnd_Coll}
        # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
        #   ftMs_SpecialAirLw_Coll,ftMs_SpecialAirLwHit_Coll}
        "ftMs_SpecialAirNStart_Coll",
        "ftMs_SpecialAirNLoop_Coll",
        "ftMs_SpecialAirNEnd_Coll",
        "ftMs_SpecialAirLw_Coll",
        "ftMs_SpecialAirLwHit_Coll",
        "ftMs_SpecialAirS1_Coll",
        "ftMs_SpecialS2_Coll",
        "ftMs_SpecialS3_Coll",
        "ftMs_SpecialS4_Coll",
    }:
        bits |= CLASS_FT80081D0C_AIR_COLL
    if coll_cb in {"ftFx_SpecialSStart_Coll", "ftFx_SpecialS_Coll"}:
        # Grounded Fox/Falco Side-B Start/Main collision callbacks call ft_80082708, which routes to
        # mpColl_8004B108. Aerial Side-B has separate ft_CheckGroundAndLedge ownership above.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
        #   ftFx_SpecialSStart_Coll,ftFx_SpecialS_Coll}
        # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
        bits |= CLASS_FX_SPECIALS_GROUND_B108_COLL
    if coll_cb in {
        "ftCo_Fall_Coll",
        "ftCo_FallAerial_Coll",
        "ftCo_FallSpecial_Coll",
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_CliffJump2_Coll",
        "ftCo_MissFoot_Coll",
        "ftCo_CaptureJump_Coll",
        "ftFx_SpecialAirNStart_Coll",
        "ftFx_SpecialAirNLoop_Coll",
        "ftFx_SpecialAirNEnd_Coll",
    }:
        # Collision callbacks that use the basic `ft_80082B1C` landing/Wait callback after floor
        # contact. Keep this generated from decomp callback symbols so runtime landing selection
        # does not carry a parallel action-id list.
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_800831CC,ft_800835B0}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureJump_Coll
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
        bits |= CLASS_FT80082B1C_BASIC_LANDING_COLL
    return bits


def _class2_bits_for_callbacks(callbacks: tuple[str, str, str, str, str]) -> int:
    bits = 0
    coll_cb = callbacks[3]
    if coll_cb in {
        "ftCo_Wait_Coll",
        "ftCo_Walk_Coll",
        "ftCo_Turn_Coll",
        "ftCo_TurnRun_Coll",
        "ftCo_Dash_Coll",
        "ftCo_Run_Coll",
        "ftCo_RunDirect_Coll",
        "ftCo_RunBrake_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_Landing_Coll",
        "ftCo_GuardOn_Coll",
        "ftCo_Guard_Coll",
        "ftCo_GuardOff_Coll",
        "ftCo_GuardSetOff_Coll",
        "ftCo_Ottotto_Coll",
        "ftCo_OttottoWait_Coll",
    }:
        # Phase 3 common grounded owner. This intentionally excludes grounded attacks,
        # catch/throw/capture, item/lift/hammer, damage/downed, and character-special callbacks.
        # refs/melee/src/melee/ft/ft_081B.c common grounded wrappers
        bits |= CLASS2_COMMON_GROUNDED_COLL
    if coll_cb in {
        "ftCo_KneeBend_Coll",
        "ftCo_Turn_Coll",
        "ftCo_Dash_Coll",
        "ftCo_Run_Coll",
        "ftCo_RunDirect_Coll",
        "ftCo_Squat_Coll",
        "ftCo_SquatWait_Coll",
        "ftCo_SquatRv_Coll",
        "ftCo_GuardOn_Coll",
        "ftCo_Guard_Coll",
        "ftCo_GuardOff_Coll",
        "ftCo_GuardSetOff_Coll",
    }:
        # Narrow Phase 3 ft_80082708/mpColl_8004B108 grounded floor-loss owner. The legacy
        # FT80083F88 bit covers only the direct wrapper and also includes downed/passive and
        # character-special callbacks; this word follows common source wrappers that reach B108.
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_800844EC,ft_800845B4,ft_80082708}
        bits |= CLASS2_COMMON_GROUNDED_B108_COLL
    if coll_cb in {
        "ftCo_TurnRun_Coll",
        "ftCo_Ottotto_Coll",
        "ftCo_OttottoWait_Coll",
    }:
        # Narrow Phase 3 ft_800827A0/mpColl_8004B2DC grounded endpoint owner.
        # refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104}
        bits |= CLASS2_COMMON_GROUNDED_B2DC_COLL
    if coll_cb in {
        "ftCo_Wait_Coll",
        "ftCo_Walk_Coll",
        "ftCo_RunBrake_Coll",
        "ftCo_Landing_Coll",
    }:
        # Narrow Phase 3 ft_80084280/mpColl_8004B4B0 grounded floor-release/teeter owner.
        # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
        bits |= CLASS2_COMMON_GROUNDED_B4B0_COLL
    if coll_cb in {
        "ftCo_Fall_Coll",
        "ftCo_FallAerial_Coll",
        "ftCo_FallSpecial_Coll",
        "ftCo_Jump_Coll",
        "ftCo_JumpAerial_Coll",
        "ftCo_CliffJump2_Coll",
        "ftCo_MissFoot_Coll",
        "ftCo_Pass_Coll",
    }:
        # Phase 3 common airborne owner. This intentionally excludes AttackAir, EscapeAir, Damage,
        # item/projectile, catch/throw/capture, and Fox/Falco bespoke special callbacks.
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80083090,ft_800831CC,ft_800835B0}
        bits |= CLASS2_COMMON_AIRBORNE_COLL
    return bits


def _class3_bits_for_callbacks(callbacks: tuple[str, str, str, str, str]) -> int:
    bits = 0
    coll_cb = callbacks[3]
    if coll_cb == "ftCo_AttackAir_Coll":
        # Phase 4 later-owner airborne attack callback:
        # ftCo_AttackAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
        # This deliberately excludes AirCatch, ItemThrowAir, capture/cargo, item, and special
        # callbacks that share the older broad FT80081D0C wrapper class.
        bits |= CLASS3_PHASE4_ATTACK_AIR_COLL
    if coll_cb == "ftCo_EscapeAir_Coll":
        # Phase 4 EscapeAir callback:
        # ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
        bits |= CLASS3_PHASE4_ESCAPE_AIR_COLL
    if coll_cb in {"ftCo_Damage_Coll", "ftCo_DownDamage_Coll"}:
        # Phase 4 common Damage / DownDamage callback:
        # airborne path reaches ft_80081DD4; active-SDI rows select mpColl_800477E0.
        bits |= CLASS3_PHASE4_DAMAGE_COMMON_COLL
    if coll_cb in {"ftCo_DamageFly_Coll", "ftCo_DamageFlyRoll_Coll", "ftCo_FlyReflect_Coll"}:
        # Phase 4 DamageFly / FlyReflect callback family. Ordinary DamageFly selects
        # ft_80081DD4 -> mpColl_800473CC; FlyReflect uses adjacent damage wrappers, but remains a
        # source DamageFly collision callback owner and is intentionally separate from common air.
        bits |= CLASS3_PHASE4_DAMAGE_FLY_COLL
    if coll_cb == "ftCo_DamageFall_Coll":
        # Phase 4 DamageFall callback:
        # ftCo_DamageFall_Coll -> ft_8008370C -> mpColl_800473CC on ordinary airborne floor checks.
        bits |= CLASS3_PHASE4_DAMAGE_FALL_COLL
    return bits


def _class_bits_for_submotion(submotion_sym: str) -> int:
    if submotion_sym in {
        "ftCo_SM_DamageAir1",
        "ftCo_SM_DamageAir2",
        "ftCo_SM_DamageAir3",
    }:
        return CLASS_DAMAGE_AIR
    if submotion_sym in {
        "ftCo_SM_DamageHi1",
        "ftCo_SM_DamageHi2",
        "ftCo_SM_DamageHi3",
        "ftCo_SM_DamageN1",
        "ftCo_SM_DamageN2",
        "ftCo_SM_DamageN3",
        "ftCo_SM_DamageLw1",
        "ftCo_SM_DamageLw2",
        "ftCo_SM_DamageLw3",
    }:
        return CLASS_DAMAGE_GROUND
    return 0


def _parse_motion_state_rows(
    src: Path,
    *,
    submotion_ids: dict[str, int],
    ft_move_id: dict[str, int],
    motion_flags_expr_by_name: dict[str, str],
) -> dict[int, MotionStateRow]:
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()
    act_pat = re.compile(r"^\s*//\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<id>[0-9]+)\s*$")
    symbols: dict[str, str] = dict(motion_flags_expr_by_name)
    symbols.update({name: str(value) for name, value in ft_move_id.items()})

    out: dict[int, MotionStateRow] = {}
    pending_action_id: int | None = None
    pending_symbol = ""
    fields: list[str] = []

    def finish() -> None:
        nonlocal pending_action_id, pending_symbol, fields
        if pending_action_id is None:
            return
        if len(fields) < 8:
            raise RuntimeError(f"incomplete MotionState row {pending_symbol} in {src}: fields={fields!r}")
        submotion_sym = fields[0]
        submotion_id = submotion_ids.get(submotion_sym, U16_ABSENT)
        if submotion_id == U16_ABSENT and submotion_sym not in {"0", "NULL"}:
            raise RuntimeError(f"unresolved submotion symbol {submotion_sym!r} in {src} ({pending_symbol})")
        x4_flags = int(_eval_motion_flags_expr(fields[1], motion_flags_expr_by_name))
        motion_word = int(_eval_motion_flags_expr(fields[2], symbols))
        callbacks = tuple("NULL" if f == "NULL" else f for f in fields[3:8])
        row = MotionStateRow(
            action_id=pending_action_id,
            symbol=pending_symbol,
            submotion_id=int(submotion_id) & 0xFFFF,
            x4_flags=x4_flags & 0xFFFF_FFFF,
            motion_state_word=motion_word & 0xFFFF_FFFF,
            anim_cb=callbacks[0],
            iasa_cb=callbacks[1],
            phys_cb=callbacks[2],
            coll_cb=callbacks[3],
            cam_cb=callbacks[4],
            class_bits=_class_bits_for_callbacks(callbacks) | _class_bits_for_submotion(submotion_sym),
            class2_bits=_class2_bits_for_callbacks(callbacks),
            class3_bits=_class3_bits_for_callbacks(callbacks),
        )
        if row.action_id in out:
            raise RuntimeError(f"duplicate action id {row.action_id} in {src}")
        out[row.action_id] = row
        pending_action_id = None
        pending_symbol = ""
        fields = []

    for line in lines:
        m = act_pat.match(line)
        if m is not None:
            finish()
            pending_symbol = m.group("sym")
            pending_action_id = int(m.group("id"), 10)
            fields = []
            continue

        if pending_action_id is None:
            continue
        code = _strip_comments(line)
        if not code or code.startswith("{") or code.startswith("}"):
            continue
        tok = code.split(",", 1)[0].strip()
        if tok:
            fields.append(tok)
            if len(fields) == 8:
                finish()

    finish()
    if not out:
        raise RuntimeError(f"failed to parse MotionState rows from {src}")
    return out


def _merge_rows(common: dict[int, MotionStateRow], self_rows: dict[int, MotionStateRow]) -> dict[int, MotionStateRow]:
    out = dict(common)
    for action_id, row in self_rows.items():
        if action_id in out:
            raise RuntimeError(f"self MotionState row duplicates common action id {action_id}")
        out[action_id] = row
    return out


def _write_table(out_path: Path, rows: dict[int, MotionStateRow], callback_ids: dict[str, int]) -> None:
    max_action = max(rows.keys(), default=0)
    action_count = max_action + 1
    hdr_bytes = 8 + 4 + 2 + 2 + 4 * 12
    submotion_off = hdr_bytes
    x4_flags_off = submotion_off + action_count * 2
    motion_word_off = x4_flags_off + action_count * 4
    anim_cb_off = motion_word_off + action_count * 4
    iasa_cb_off = anim_cb_off + action_count * 2
    phys_cb_off = iasa_cb_off + action_count * 2
    coll_cb_off = phys_cb_off + action_count * 2
    cam_cb_off = coll_cb_off + action_count * 2
    class_bits_off = cam_cb_off + action_count * 2
    class2_bits_off = class_bits_off + action_count * 4
    class3_bits_off = class2_bits_off + action_count * 4
    fx_special_kind_off = class3_bits_off + action_count * 4
    file_bytes = fx_special_kind_off + action_count

    submotion = [U16_ABSENT] * action_count
    x4_flags = [0] * action_count
    motion_words = [0] * action_count
    anim_cb = [0] * action_count
    iasa_cb = [0] * action_count
    phys_cb = [0] * action_count
    coll_cb = [0] * action_count
    cam_cb = [0] * action_count
    class_bits = [0] * action_count
    class2_bits = [0] * action_count
    class3_bits = [0] * action_count
    fx_special_kind = [0] * action_count
    for action_id, row in rows.items():
        submotion[action_id] = row.submotion_id
        x4_flags[action_id] = row.x4_flags
        motion_words[action_id] = row.motion_state_word
        anim_cb[action_id] = callback_ids[row.anim_cb]
        iasa_cb[action_id] = callback_ids[row.iasa_cb]
        phys_cb[action_id] = callback_ids[row.phys_cb]
        coll_cb[action_id] = callback_ids[row.coll_cb]
        cam_cb[action_id] = callback_ids[row.cam_cb]
        class_bits[action_id] = row.class_bits
        class2_bits[action_id] = row.class2_bits
        class3_bits[action_id] = row.class3_bits
        fx_special_kind[action_id] = FX_SPECIAL_KIND_BY_SYMBOL.get(row.anim_cb, 0)

    buf = bytearray()
    buf += FORMAT_MAGIC
    buf += _u32_le(FORMAT_VERSION)
    buf += _u16_le(action_count)
    buf += _u16_le(0)
    for off in (
        submotion_off,
        x4_flags_off,
        motion_word_off,
        anim_cb_off,
        iasa_cb_off,
        phys_cb_off,
        coll_cb_off,
        cam_cb_off,
        class_bits_off,
        class2_bits_off,
        class3_bits_off,
        fx_special_kind_off,
    ):
        buf += _u32_le(off)
    for value in submotion:
        buf += _u16_le(value)
    for table in (x4_flags, motion_words):
        for value in table:
            buf += _u32_le(value)
    for table in (anim_cb, iasa_cb, phys_cb, coll_cb, cam_cb):
        for value in table:
            buf += _u16_le(value)
    for value in class_bits:
        buf += _u32_le(value)
    for value in class2_bits:
        buf += _u32_le(value)
    for value in class3_bits:
        buf += _u32_le(value)
    for value in fx_special_kind:
        buf += bytes((value & 0xFF,))
    if len(buf) != file_bytes:
        raise AssertionError((len(buf), file_bytes))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(buf))


def _write_manifest(out_path: Path, callback_ids: dict[str, int]) -> None:
    classes = {
        "ATTACK_AIR": CLASS_ATTACK_AIR,
        "ATTACK_S3": CLASS_ATTACK_S3,
        "ATTACK_S4": CLASS_ATTACK_S4,
        "DAMAGE_COMMON": CLASS_DAMAGE_COMMON,
        "DAMAGE_FLY": CLASS_DAMAGE_FLY,
        "LANDING_AIR": CLASS_LANDING_AIR,
        "COMMON_FALL": CLASS_COMMON_FALL,
        "SPECIALHI": CLASS_SPECIALHI,
        "COMMON_AIR_PHYS": CLASS_COMMON_AIR_PHYS,
        "COMMON_AIR_COLL": CLASS_COMMON_AIR_COLL,
        "COMMON_AIR_WALLJUMP_COLL": CLASS_COMMON_AIR_WALLJUMP_COLL,
        "LANDING_COLL": CLASS_LANDING_COLL,
        "LANDING_AIR_COLL": CLASS_LANDING_AIR_COLL,
        "DAMAGE_COMMON_COLL": CLASS_DAMAGE_COMMON_COLL,
        "DAMAGE_FLY_COLL": CLASS_DAMAGE_FLY_COLL,
        "DAMAGE_FALL_COLL": CLASS_DAMAGE_FALL_COLL,
        "GROUNDED_STAGE_OBJECT_CARRY_COLL": CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL,
        "GROUNDED_ATTACK": CLASS_GROUNDED_ATTACK,
        "GUARDON_FRAME_START_X672_IASA": CLASS_GUARDON_FRAME_START_X672_IASA,
        "FT80081D0C_AIR_COLL": CLASS_FT80081D0C_AIR_COLL,
        "FT_CHECK_GROUND_LEDGE_AIR_COLL": CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL,
        "FT80083F88_GROUND_TO_AIR_COLL": CLASS_FT80083F88_GROUND_TO_AIR_COLL,
        "FT80083090_PLATFORM_PASS_COLL": CLASS_FT80083090_PLATFORM_PASS_COLL,
        "FT800827A0_EDGE_SNAP_COLL": CLASS_FT800827A0_EDGE_SNAP_COLL,
        "DAMAGE_AIR": CLASS_DAMAGE_AIR,
        "DAMAGE_GROUND": CLASS_DAMAGE_GROUND,
        "GROUNDED_ATTACK_WAIT_IASA_SPECIALS": CLASS_GROUNDED_ATTACK_WAIT_IASA_SPECIALS,
        "GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION": CLASS_GROUNDED_ATTACK_WAIT_IASA_LOCOMOTION,
        "GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD": CLASS_GROUNDED_ATTACK_WAIT_IASA_CATCH_GUARD,
        "ESCAPE_AIR_COLL": CLASS_ESCAPE_AIR_COLL,
        "FX_SPECIALS_GROUND_B108_COLL": CLASS_FX_SPECIALS_GROUND_B108_COLL,
        "FT80082B1C_BASIC_LANDING_COLL": CLASS_FT80082B1C_BASIC_LANDING_COLL,
    }
    classes2 = {
        "COMMON_AIRBORNE_COLL": CLASS2_COMMON_AIRBORNE_COLL,
        "COMMON_GROUNDED_B2DC_COLL": CLASS2_COMMON_GROUNDED_B2DC_COLL,
        "COMMON_GROUNDED_B4B0_COLL": CLASS2_COMMON_GROUNDED_B4B0_COLL,
        "COMMON_GROUNDED_B108_COLL": CLASS2_COMMON_GROUNDED_B108_COLL,
        "COMMON_GROUNDED_COLL": CLASS2_COMMON_GROUNDED_COLL,
    }
    classes3 = {
        "PHASE4_ATTACK_AIR_COLL": CLASS3_PHASE4_ATTACK_AIR_COLL,
        "PHASE4_DAMAGE_COMMON_COLL": CLASS3_PHASE4_DAMAGE_COMMON_COLL,
        "PHASE4_DAMAGE_FALL_COLL": CLASS3_PHASE4_DAMAGE_FALL_COLL,
        "PHASE4_DAMAGE_FLY_COLL": CLASS3_PHASE4_DAMAGE_FLY_COLL,
        "PHASE4_ESCAPE_AIR_COLL": CLASS3_PHASE4_ESCAPE_AIR_COLL,
    }
    fx_special_kinds = dict(FX_SPECIAL_KIND_VALUES)
    symbols = [{"id": int(i), "symbol": sym} for sym, i in sorted(callback_ids.items(), key=lambda kv: kv[1])]
    payload = {
        "magic": FORMAT_MAGIC.decode("ascii"),
        "version": FORMAT_VERSION,
        "id_policy": "0 is NULL; nonzero ids are sorted stable callback symbol names from decomp MotionState rows",
        "classes": classes,
        "classes2": classes2,
        "classes3": classes3,
        "fx_special_kinds": fx_special_kinds,
        "symbols": symbols,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract decomp MotionState owner/callback data.")
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/motion_state/owners"))
    ap.add_argument("--chars", type=str, default="fox,falco")
    args = ap.parse_args()

    chars = [c.strip() for c in args.chars.split(",") if c.strip()]
    supported = {
        name: TableSpec(
            src=args.melee_decomp / "src" / "melee" / "ft" / "chara" / info.decomp_dir
            / f"{info.decomp_prefix}Init.c",
            rel_name=name,
        )
        for name, info in CHARS.items()
    }

    common_src = args.melee_decomp / "src" / "melee" / "ft" / "ftmotionstates.c"
    common_headers = [
        args.melee_decomp / "src" / "melee" / "ft" / "forward.h",
        args.melee_decomp / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h",
        *sorted(
            {
                fh
                for info in CHARS.values()
                if (
                    fh := args.melee_decomp / "src" / "melee" / "ft" / "chara" / info.decomp_dir
                    / "forward.h"
                ).exists()
            }
        ),
    ]
    ft_move_id = _parse_ft_move_id_enum(args.melee_decomp)
    motion_flags = _parse_motion_flags_constants(common_headers)
    submotion_ids = _parse_submotion_ids(args.melee_decomp)
    common_rows = _parse_motion_state_rows(
        common_src,
        submotion_ids=submotion_ids,
        ft_move_id=ft_move_id,
        motion_flags_expr_by_name=motion_flags,
    )

    rows_by_char: dict[str, dict[int, MotionStateRow]] = {}
    all_callback_symbols = {"NULL"}
    for ch in chars:
        spec = supported.get(ch)
        if spec is None:
            raise SystemExit(f"unsupported character for now: {ch}")
        self_rows = _parse_motion_state_rows(
            spec.src,
            submotion_ids=submotion_ids,
            ft_move_id=ft_move_id,
            motion_flags_expr_by_name=motion_flags,
        )
        merged = _merge_rows(common_rows, self_rows)
        rows_by_char[spec.rel_name] = merged
        for row in merged.values():
            all_callback_symbols.update((row.anim_cb, row.iasa_cb, row.phys_cb, row.coll_cb, row.cam_cb))

    callback_ids = {"NULL": 0}
    for i, sym in enumerate(sorted(s for s in all_callback_symbols if s != "NULL"), start=1):
        callback_ids[sym] = i

    for rel_name, rows in rows_by_char.items():
        _write_table(args.out_dir / f"{rel_name}.bin", rows, callback_ids)
    _write_manifest(args.out_dir / "callback_symbols.json", callback_ids)


if __name__ == "__main__":
    main()
