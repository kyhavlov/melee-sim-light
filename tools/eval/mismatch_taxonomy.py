from __future__ import annotations

import argparse
import csv
import importlib
import json
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.facing_residual_blocker_report import load_action_id_names
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


PLAYER_FIELDS: tuple[str, ...] = (
    "action_id",
    "action_frame",
    "on_ground",
    "facing",
    "stocks",
    "jumps_left",
    "is_dead",
    "hitlag",
    "hitstun",
    "l_cancel",
    "hurtbox_state",
    "ground_id",
    "animation_index",
    "instance_hit_by",
    "instance_id",
    "last_attack_landed",
    "combo_count",
    "last_hit_by",
    "state_flags",
)

ITEM_FIELD_TO_SUBFIELD: dict[str, str] = {
    "item_exists": "exists",
    "item_type": "type",
    "item_state": "state",
    "item_owner": "owner",
    "item_instance_id": "instance_id",
}

ACTION_CORE_FIELDS: frozenset[str] = frozenset(
    {
        "action_id",
        "action_frame",
        "animation_index",
        "facing",
        "hitlag",
        "hitstun",
        "instance_hit_by",
        "instance_id",
        "last_attack_landed",
        "last_hit_by",
        "combo_count",
        "hurtbox_state",
        "on_ground",
        "ground_id",
        "jumps_left",
    }
)

STATE_FLAG_FIELDS: frozenset[str] = frozenset(
    {"state_flags[0]", "state_flags[1]", "state_flags[2]", "state_flags[3]", "state_flags[4]"}
)


@dataclass(frozen=True)
class FamilyMeta:
    label: str
    owner_module: str
    fix_type: str
    risk: str
    confidence: str
    hypothesis: str
    refs: tuple[str, ...]


FAMILY_META: dict[str, FamilyMeta] = {
    "F01_guard_release_collision": FamilyMeta(
        label="Guard Release / Reflect Collision Ordering",
        owner_module="combat",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "GuardOn / Guard / GuardSetOff / GuardReflect callback ordering is still admitting or "
            "suppressing shield-hit followups a frame off, which cascades into action_id, hitlag, "
            "instance_id, and item ownership mismatches."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092450",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA",
        ),
    ),
    "F02_guard_timer_flags": FamilyMeta(
        label="Guard Timer / State Flag Parity",
        owner_module="timers",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "GuardSetOff / GuardReflect timer and state flag lanes are still ticking or clearing at "
            "the wrong point relative to the anim callback, but without broader combat divergence."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}",
        ),
    ),
    "F03_capturewait_bridge": FamilyMeta(
        label="CaptureWait / CapturePulled Ownership Bridge",
        owner_module="grab_flow",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "Catch / CapturePulled / CaptureWait callback ordering still leaves a one-tick victim "
            "anim-rate and action_frame ownership gap on the first steady CaptureWait frames."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DB6C8,fn_800DB790,fn_800DBAE4}",
        ),
    ),
    "F04_match_flow_rebirth": FamilyMeta(
        label="Death / Rebirth Match-Flow Identity Reset",
        owner_module="match_flow",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "Death -> Rebirth still has residual state_flags / identity-reset parity issues on the "
            "first Rebirth-visible frames."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebirth.c::ftCo_800D4FF4",
            "refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354",
        ),
    ),
    "F05_damage_state_flags": FamilyMeta(
        label="Damage State Flag Parity",
        owner_module="timers",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "DamageFly / DamageFall / Down* / Passive rows still mis-order state_flags updates "
            "relative to damage callbacks, but without a full action-state fork."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll",
        ),
    ),
    "F06_damageflyroll_rng_gate": FamilyMeta(
        label="DamageFlyRoll RNG / Pre-Gate Admission",
        owner_module="combat",
        fix_type="instrumentation first",
        risk="high",
        confidence="high",
        hypothesis=(
            "The remaining DamageFlyTop -> DamageFlyRoll admission blocker still depends on an "
            "unmodeled pre-gate Fighter_8006CDA4 RNG consumer family, so instrumentation/seed work "
            "should land before another runtime branch."
        ),
        refs=(
            "refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
            "refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf",
        ),
    ),
    "F07_knockdown_grounding": FamilyMeta(
        label="DamageFly / Passive / DownBound Grounding",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "DamageFly collision ownership is still choosing Passive / PassiveStand / DownBound / "
            "landing a frame off on grounded contact rows, which leaks into hurtbox_state, on_ground, "
            "ground_id, and jumps_left."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40",
        ),
    ),
    "F08_damage_resolution_combat": FamilyMeta(
        label="Damage Resolution / Combat Followup",
        owner_module="combat",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "These rows still diverge in the combat followup pipeline around ProcessHit / damage "
            "entry / hitlag ordering, producing the familiar action_id + hitlag + hitstun + "
            "instance ownership bundles."
        ),
        refs=(
            "refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724}",
        ),
    ),
    "F09_aerial_combat_resolution": FamilyMeta(
        label="Aerial / Jump Combat Resolution",
        owner_module="combat",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Jump / AttackAir followup rows still miss the correct damage admission or aerial "
            "continuation window, especially around AttackAir* -> DamageFly* and JumpF hit-confirm lanes."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_DamageFly_IASA}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}",
        ),
    ),
    "F10_grounded_transition_resolution": FamilyMeta(
        label="Grounded Transition / Motion Entry",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Grounded Dash / Walk / Turn / KneeBend / attack transition ordering still bumps motion "
            "state or instance_id at the wrong point, often without a full combat dependency."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C",
        ),
    ),
    "F11_locomotion_action_frame": FamilyMeta(
        label="Walk / Grounded Action-Frame Rate",
        owner_module="anim_timebase",
        fix_type="seed/schema + runtime",
        risk="low",
        confidence="high",
        hypothesis=(
            "The remaining action_frame-only walk lanes still point to callback-owned source-rate "
            "or timebase carry gaps rather than a broader state transition bug."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC",
            "refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFEC8",
        ),
    ),
    "F12_instance_id_transition_only": FamilyMeta(
        label="Instance ID Motion-Entry Residual",
        owner_module="anim_timebase",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "Pure instance_id mismatches are still concentrated in motion-entry rows where "
            "ft_800895E0 bump gating or x2073 compare-byte parity differs."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/ft/ftmotionstates.c",
        ),
    ),
    "F13_specialhi_landing": FamilyMeta(
        label="SpecialHi / FallSpecial Landing Continuations",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Firefox hold / launch / fall-special landing continuations still disagree on the "
            "transition boundary, especially around jump count and landing ownership."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHi_Anim,ftFx_SpecialAirHi_Anim}",
            "refs/melee/src/melee/ft/chara/ftFox/types.h::ftFox_DatAttrs",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll",
        ),
    ),
    "F14_throw_item_bookkeeping": FamilyMeta(
        label="ThrowHi / Item Bookkeeping",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "ThrowHi thrower-before-victim ordering still leaves a small attached-item bookkeeping "
            "residual in combo_count / item owner / item instance_id rows."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim",
            "data/moves/fox.json moves[\"ftCo_SM_ThrowHi\"]",
        ),
    ),
    "F15_guard_item_ownership": FamilyMeta(
        label="Guard / Reflect Item Ownership",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="med",
        hypothesis=(
            "Shield / GuardReflect rows still leave a small reflected-laser owner / instance_id "
            "residual after the fighter-side guard transition resolves."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
        ),
    ),
    "F16_item_identity_residual": FamilyMeta(
        label="Item Identity / Ownership Residual",
        owner_module="items",
        fix_type="runtime-only",
        risk="low",
        confidence="med",
        hypothesis=(
            "Small remaining item-only mismatches are concentrated in owner / instance_id handoff rows "
            "outside the shield- and throw-specific lanes."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0",
        ),
    ),
    "F99_misc_other": FamilyMeta(
        label="Residual Mixed Bucket",
        owner_module="triage",
        fix_type="instrumentation first",
        risk="high",
        confidence="low",
        hypothesis=(
            "This is the leftover mixed bucket after the dominant causal families are removed. The "
            "current heads are SpecialLw enter/loop, ThrownLw, and a small TurnRun slice, so this "
            "lane needs another triage split before any runtime patch is justified."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrownLw_Anim",
            "refs/melee/src/melee/ft/ftmotionstates.c",
        ),
    ),
}


@dataclass(frozen=True)
class PlayerRow:
    dataset: str
    record: int
    p: int
    seed_frame: int
    ref_frame: int
    seed_action_id: int
    ref_action_id: int
    out_action_id: int
    prev_action_id: int
    seed_action_frame: int
    ref_action_frame: int
    out_action_frame: int
    on_ground: int
    hitlag: int
    hitstun: int
    fields: tuple[str, ...]
    family_id: str


@dataclass(frozen=True)
class ItemSlotRow:
    dataset: str
    record: int
    slot: int
    seed_frame: int
    ref_frame: int
    player_actions: tuple[int, ...]
    ref_actions: tuple[int, ...]
    out_actions: tuple[int, ...]
    fields: tuple[str, ...]
    family_id: str


@dataclass(frozen=True)
class MismatchEvent:
    family_id: str
    dataset: str
    record: int
    subject: str
    seed_frame: int
    ref_frame: int
    field: str
    seed: int
    ref: int
    out: int
    seed_action_id: int | None
    ref_action_id: int | None
    out_action_id: int | None
    prev_action_id: int | None
    seed_action_frame: int | None
    ref_action_frame: int | None
    out_action_frame: int | None
    on_ground: int | None
    hitlag: int | None
    hitstun: int | None


@dataclass(frozen=True)
class AuditSample:
    family_id: str
    dataset: str
    record: int
    subject: str
    matched: bool
    audit_reason: str
    field_bundle: tuple[str, ...]
    seed_action_id: int | None
    ref_action_id: int | None
    out_action_id: int | None
    seed_action_frame_bucket: str
    ref_action_frame_bucket: str
    out_action_frame_bucket: str


def _load_binding():
    return importlib.import_module("msl_binding")


def _action_name(names: dict[int, str], action_id: int | None) -> str:
    if action_id is None:
        return "NONE"
    return names.get(action_id, f"UNKNOWN_0x{int(action_id) & 0xFFFF:04X}")


def _bucket_action_frame(v: int) -> str:
    if v < 0:
        return "<0"
    if v == 0:
        return "0"
    if v == 1:
        return "1"
    if v == 2:
        return "2"
    if v == 3:
        return "3"
    if v <= 5:
        return "4-5"
    if v <= 10:
        return "6-10"
    if v <= 20:
        return "11-20"
    return "21+"


def _base_field(field: str) -> str:
    return field.split("@", 1)[0]


def _looks_like_guard(name: str) -> bool:
    return "GUARD" in name


def _looks_like_capture(name: str) -> bool:
    return name.startswith("CAPTURE_") or name.startswith("CATCH")


def _looks_like_match_flow(name: str) -> bool:
    return name.startswith("REBIRTH") or name.startswith("DEAD_") or name.startswith("ENTRY")


def _looks_like_damage(name: str) -> bool:
    return name.startswith("DAMAGE_") or name.startswith("DOWN_") or name.startswith("PASSIVE")


def _looks_like_special_hi(name: str) -> bool:
    return "SPECIAL_HI" in name or "FALL_SPECIAL" in name


def _looks_like_aerial(name: str) -> bool:
    return name.startswith("ATTACK_AIR") or name.startswith("JUMP_")


def _looks_like_grounded(name: str) -> bool:
    return (
        name.startswith("WALK_")
        or name in {"DASH", "TURN", "RUN", "RUN_BRAKE", "KNEE_BEND", "FALL", "LANDING", "WAIT"}
        or name.startswith("ATTACK_")
    )


def _classify_player_row(row: PlayerRow, action_names: dict[int, str]) -> str:
    seed_name = _action_name(action_names, row.seed_action_id)
    ref_name = _action_name(action_names, row.ref_action_id)
    out_name = _action_name(action_names, row.out_action_id)
    names = (seed_name, ref_name, out_name)
    field_set = {_base_field(field) for field in row.fields}

    if (
        row.ref_action_id == 91
        or row.out_action_id == 91
        or row.seed_action_id == 91
        or (row.seed_action_id == 90 and row.ref_action_id == 91)
    ):
        return "F06_damageflyroll_rng_gate"
    if any(_looks_like_match_flow(name) for name in names):
        return "F04_match_flow_rebirth"
    if any(_looks_like_guard(name) for name in names):
        if field_set <= (STATE_FLAG_FIELDS | {"action_frame", "facing"}):
            return "F02_guard_timer_flags"
        return "F01_guard_release_collision"
    if any(_looks_like_capture(name) for name in names):
        return "F03_capturewait_bridge"
    if any(_looks_like_damage(name) for name in names):
        if field_set and field_set <= STATE_FLAG_FIELDS:
            return "F05_damage_state_flags"
        if field_set & {"on_ground", "ground_id", "jumps_left", "hurtbox_state"}:
            return "F07_knockdown_grounding"
        return "F08_damage_resolution_combat"
    if field_set == {"instance_id"}:
        return "F12_instance_id_transition_only"
    if field_set == {"action_frame"} and any(_looks_like_grounded(name) for name in names):
        return "F11_locomotion_action_frame"
    if any(_looks_like_special_hi(name) for name in names):
        return "F13_specialhi_landing"
    if any(_looks_like_aerial(name) for name in names):
        return "F09_aerial_combat_resolution"
    if any(_looks_like_grounded(name) for name in names):
        return "F10_grounded_transition_resolution"
    return "F99_misc_other"


def _classify_item_slot_row(row: ItemSlotRow, action_names: dict[int, str]) -> str:
    names = [
        _action_name(action_names, action_id)
        for triplet in (row.player_actions, row.ref_actions, row.out_actions)
        for action_id in triplet
    ]
    if any(name.startswith("THROW_") or name.startswith("THROWN_") or _looks_like_capture(name) for name in names):
        return "F14_throw_item_bookkeeping"
    if any(_looks_like_guard(name) for name in names):
        return "F15_guard_item_ownership"
    return "F16_item_identity_residual"


def _row_fields(row: PlayerRow | ItemSlotRow) -> frozenset[str]:
    return frozenset(_base_field(field) for field in row.fields)


def _player_names(row: PlayerRow, action_names: dict[int, str]) -> tuple[str, str, str]:
    return (
        _action_name(action_names, row.seed_action_id),
        _action_name(action_names, row.ref_action_id),
        _action_name(action_names, row.out_action_id),
    )


def _item_names(row: ItemSlotRow, action_names: dict[int, str]) -> tuple[str, ...]:
    return tuple(
        _action_name(action_names, action_id)
        for triplet in (row.player_actions, row.ref_actions, row.out_actions)
        for action_id in triplet
    )


def _audit_player_row(row: PlayerRow, action_names: dict[int, str]) -> tuple[bool, str]:
    names = _player_names(row, action_names)
    field_set = _row_fields(row)

    if row.family_id == "F01_guard_release_collision":
        ok = any(_looks_like_guard(name) for name in names) and bool(
            field_set & {"action_id", "animation_index", "instance_id", "item_exists", "item_owner", "item_instance_id"}
        )
        return ok, "guard family with collision/identity field bundle"
    if row.family_id == "F02_guard_timer_flags":
        ok = any(_looks_like_guard(name) for name in names) and field_set <= (STATE_FLAG_FIELDS | {"action_frame", "facing"})
        return ok, "guard family with timer/state-flag-only bundle"
    if row.family_id == "F03_capturewait_bridge":
        ok = any(_looks_like_capture(name) for name in names) and bool(
            field_set & {"action_frame", "instance_id", "jumps_left", "hurtbox_state", "action_id", "last_hit_by", "state_flags[1]"}
        )
        return ok, "capture family with first-steady ownership fields"
    if row.family_id == "F04_match_flow_rebirth":
        ok = any(_looks_like_match_flow(name) for name in names) and field_set <= {"state_flags[3]", "state_flags[4]", "instance_id"}
        return ok, "match-flow row with rebirth/death residual fields"
    if row.family_id == "F05_damage_state_flags":
        ok = any(_looks_like_damage(name) for name in names) and field_set <= STATE_FLAG_FIELDS
        return ok, "damage family with state-flag-only bundle"
    if row.family_id == "F06_damageflyroll_rng_gate":
        ok = (
            row.ref_action_id == 91
            or row.out_action_id == 91
            or row.seed_action_id == 91
            or (row.seed_action_id == 90 and row.ref_action_id == 91)
        )
        return ok, "DamageFlyRoll admission/gate context"
    if row.family_id == "F07_knockdown_grounding":
        ok = any(_looks_like_damage(name) for name in names) and bool(
            field_set & {"on_ground", "ground_id", "jumps_left", "hurtbox_state"}
        )
        return ok, "damage family with grounding/tech followup fields"
    if row.family_id == "F08_damage_resolution_combat":
        ok = any(_looks_like_damage(name) for name in names) and bool(
            field_set & {"action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by", "instance_id"}
        )
        return ok, "damage family with combat followup fields"
    if row.family_id == "F09_aerial_combat_resolution":
        ok = any(_looks_like_aerial(name) for name in names) and bool(
            field_set
            & {
                "hitlag",
                "hitstun",
                "last_attack_landed",
                "last_hit_by",
                "combo_count",
                "state_flags[0]",
                "state_flags[1]",
                "state_flags[4]",
                "instance_id",
                "jumps_left",
            }
        )
        return ok, "aerial/jump family with combat continuation fields"
    if row.family_id == "F10_grounded_transition_resolution":
        ok = (
            any(_looks_like_grounded(name) for name in names)
            and not any(_looks_like_capture(name) for name in names)
            and bool(
                field_set
                & {
                    "action_id",
                    "animation_index",
                    "instance_id",
                    "last_hit_by",
                    "last_attack_landed",
                    "ground_id",
                    "hurtbox_state",
                }
            )
        )
        return ok, "grounded transition family with motion-entry bundle"
    if row.family_id == "F11_locomotion_action_frame":
        ok = field_set == {"action_frame"} and any(_looks_like_grounded(name) for name in names)
        return ok, "grounded callback/timebase action_frame-only row"
    if row.family_id == "F12_instance_id_transition_only":
        ok = field_set == {"instance_id"}
        return ok, "instance_id-only motion entry residual"
    if row.family_id == "F13_specialhi_landing":
        ok = any(_looks_like_special_hi(name) for name in names) and bool(
            field_set
            & {"jumps_left", "on_ground", "ground_id", "action_id", "animation_index", "instance_id", "hurtbox_state", "state_flags[4]"}
        )
        return ok, "SpecialHi/FallSpecial landing continuation row"
    if row.family_id == "F99_misc_other":
        ok = any(
            ("SPECIAL_LW" in name) or (name == "THROWN_LW") or (name == "TURN_RUN")
            for name in names
        ) or bool(field_set & {"hurtbox_state", "state_flags[4]"})
        return ok, "mixed residual head is SpecialLw/ThrownLw/TurnRun or hurtbox-state residual"
    return False, "unhandled player family"


def _audit_item_row(row: ItemSlotRow, action_names: dict[int, str]) -> tuple[bool, str]:
    names = _item_names(row, action_names)
    if row.family_id == "F14_throw_item_bookkeeping":
        ok = any(name.startswith("THROW_") or name.startswith("THROWN_") or _looks_like_capture(name) for name in names)
        return ok, "throw/capture context item row"
    if row.family_id == "F15_guard_item_ownership":
        ok = any(_looks_like_guard(name) for name in names)
        return ok, "guard/reflect context item row"
    if row.family_id == "F16_item_identity_residual":
        ok = not any(name.startswith("THROW_") or name.startswith("THROWN_") or _looks_like_capture(name) or _looks_like_guard(name) for name in names)
        return ok, "non-guard/non-throw item identity row"
    return False, "unhandled item family"


def _iter_family_rows(
    player_rows: dict[tuple[str, int, int], PlayerRow],
    item_rows: dict[tuple[str, int, int], ItemSlotRow],
) -> dict[str, list[PlayerRow | ItemSlotRow]]:
    family_rows: dict[str, list[PlayerRow | ItemSlotRow]] = defaultdict(list)
    for row in player_rows.values():
        if row.fields:
            family_rows[row.family_id].append(row)
    for row in item_rows.values():
        if row.fields:
            family_rows[row.family_id].append(row)
    for rows in family_rows.values():
        rows.sort(
            key=lambda row: (
                row.dataset,
                row.record,
                getattr(row, "p", getattr(row, "slot", -1)),
            )
        )
    return family_rows


def _sample_rows(rows: list[PlayerRow | ItemSlotRow], sample_n: int) -> list[PlayerRow | ItemSlotRow]:
    if len(rows) <= sample_n:
        return list(rows)
    if sample_n <= 1:
        return [rows[0]]
    max_idx = len(rows) - 1
    raw = [round(i * max_idx / (sample_n - 1)) for i in range(sample_n)]
    seen: set[int] = set()
    selected: list[PlayerRow | ItemSlotRow] = []
    for idx in raw:
        idx = int(idx)
        if idx in seen:
            continue
        seen.add(idx)
        selected.append(rows[idx])
    cursor = 0
    while len(selected) < sample_n and cursor < len(rows):
        if cursor not in seen:
            selected.append(rows[cursor])
            seen.add(cursor)
        cursor += 1
    return selected


def _row_to_audit_sample(row: PlayerRow | ItemSlotRow, action_names: dict[int, str]) -> AuditSample:
    if isinstance(row, PlayerRow):
        matched, reason = _audit_player_row(row, action_names)
        return AuditSample(
            family_id=row.family_id,
            dataset=row.dataset,
            record=int(row.record),
            subject=f"p{int(row.p)}",
            matched=bool(matched),
            audit_reason=f"{'matched' if matched else 'mismatch'}: {reason}",
            field_bundle=tuple(row.fields),
            seed_action_id=int(row.seed_action_id),
            ref_action_id=int(row.ref_action_id),
            out_action_id=int(row.out_action_id),
            seed_action_frame_bucket=_bucket_action_frame(int(row.seed_action_frame)),
            ref_action_frame_bucket=_bucket_action_frame(int(row.ref_action_frame)),
            out_action_frame_bucket=_bucket_action_frame(int(row.out_action_frame)),
        )
    matched, reason = _audit_item_row(row, action_names)
    return AuditSample(
        family_id=row.family_id,
        dataset=row.dataset,
        record=int(row.record),
        subject=f"item{int(row.slot)}",
        matched=bool(matched),
        audit_reason=f"{'matched' if matched else 'mismatch'}: {reason}",
        field_bundle=tuple(row.fields),
        seed_action_id=None,
        ref_action_id=None,
        out_action_id=None,
        seed_action_frame_bucket="",
        ref_action_frame_bucket="",
        out_action_frame_bucket="",
    )


def build_audit_summary(
    player_rows: dict[tuple[str, int, int], PlayerRow],
    item_rows: dict[tuple[str, int, int], ItemSlotRow],
    *,
    action_names: dict[int, str],
    sample_n: int = 25,
) -> tuple[dict[str, Any], list[AuditSample]]:
    family_rows = _iter_family_rows(player_rows, item_rows)
    samples: list[AuditSample] = []
    family_summaries: list[dict[str, Any]] = []

    for family_id, rows in sorted(family_rows.items(), key=lambda item: (-len(item[1]), item[0])):
        selected = _sample_rows(rows, max(1, int(sample_n)))
        family_samples = [_row_to_audit_sample(row, action_names) for row in selected]
        matched_count = sum(1 for sample in family_samples if sample.matched)
        sample_count = len(family_samples)
        mismatches = [
            {
                "dataset": sample.dataset,
                "record": int(sample.record),
                "subject": sample.subject,
                "field_bundle": list(sample.field_bundle),
                "audit_reason": sample.audit_reason,
            }
            for sample in family_samples
            if not sample.matched
        ][:5]
        family_summaries.append(
            {
                "family_id": family_id,
                "label": FAMILY_META[family_id].label,
                "row_count": int(len(rows)),
                "sample_count": int(sample_count),
                "matched_count": int(matched_count),
                "precision_estimate": float(matched_count / sample_count) if sample_count else 0.0,
                "mismatch_count": int(sample_count - matched_count),
                "mismatch_examples": mismatches,
            }
        )
        samples.extend(family_samples)

    summary = {
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sample_n": int(sample_n),
        "families": family_summaries,
    }
    return summary, samples


def _iter_suite_events(
    suite_path: Path,
    datasets_dir: Path,
    *,
    action_names: dict[int, str],
) -> tuple[list[MismatchEvent], dict[tuple[str, int, int], PlayerRow], dict[tuple[str, int, int], ItemSlotRow]]:
    suite = load_suite(suite_path)
    binding = _load_binding()
    all_events: list[MismatchEvent] = []
    player_rows: dict[tuple[str, int, int], PlayerRow] = {}
    item_rows: dict[tuple[str, int, int], ItemSlotRow] = {}

    for replay in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=replay.replay,
            datasets_dir=datasets_dir,
        )
        ds = read_dataset(str(ds_path))
        samples = ds.samples
        n = int(samples.shape[0])
        num_players = int(ds.header["num_players"])

        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        compare_stride = int(sizes["compare"])

        init_kwargs = {"batch_size": n, "num_players": num_players}
        if suite.ucf_enabled is not None:
            init_kwargs["ucf_enabled"] = int(bool(suite.ucf_enabled))
        if suite.ucf_cardinals_1_0_enabled is not None:
            init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(suite.ucf_cardinals_1_0_enabled))

        handle = binding.init(**init_kwargs)
        try:
            seed_b = np.frombuffer(samples["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, seed_stride).copy()
            prev_b = np.frombuffer(samples["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, input_stride).copy()
            in_b = np.frombuffer(samples["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, input_stride).copy()
            out_b = np.empty((n, compare_stride), dtype=np.uint8)

            binding.reseed_seed(handle, seed_b)
            binding.step_input(handle, prev_b, in_b)
            binding.write_compare(handle, out_b)

            out = out_b.view(COMPARE_DTYPE).reshape(-1)
            seed = samples["seed_t"]
            ref = samples["ref_t1"]

            dataset_name = str(Path(ds_path).name)

            for i in range(n):
                for p in range(num_players):
                    row_fields: list[str] = []
                    player_key = (dataset_name, int(i), int(p))

                    for field in PLAYER_FIELDS:
                        if field == "state_flags":
                            for sub in np.argwhere(out[field][i, p] != ref[field][i, p]).flatten().tolist():
                                row_fields.append(f"state_flags[{int(sub)}]")
                            continue
                        if out[field][i, p] != ref[field][i, p]:
                            row_fields.append(field)

                    row = PlayerRow(
                        dataset=dataset_name,
                        record=int(i),
                        p=int(p),
                        seed_frame=int(seed["frame_id"][i]),
                        ref_frame=int(ref["frame_id"][i]),
                        seed_action_id=int(seed["action_id"][i, p]),
                        ref_action_id=int(ref["action_id"][i, p]),
                        out_action_id=int(out["action_id"][i, p]),
                        prev_action_id=int(seed["seed_prev_action_id"][i, p]),
                        seed_action_frame=int(np.int16(seed["action_frame"][i, p])),
                        ref_action_frame=int(np.int16(ref["action_frame"][i, p])),
                        out_action_frame=int(np.int16(out["action_frame"][i, p])),
                        on_ground=int(seed["on_ground"][i, p]),
                        hitlag=int(seed["hitlag"][i, p]),
                        hitstun=int(seed["hitstun"][i, p]),
                        fields=tuple(sorted(row_fields)),
                        family_id="F99_misc_other",
                    )
                    family_id = _classify_player_row(row, action_names)
                    row = PlayerRow(**{**asdict(row), "family_id": family_id})
                    player_rows[player_key] = row

                    for field in row.fields:
                        if field.startswith("state_flags["):
                            sub = int(field.removeprefix("state_flags[").removesuffix("]"))
                            seed_v = int(seed["state_flags"][i, p, sub])
                            ref_v = int(ref["state_flags"][i, p, sub])
                            out_v = int(out["state_flags"][i, p, sub])
                        else:
                            seed_v = int(seed[field][i, p])
                            ref_v = int(ref[field][i, p])
                            out_v = int(out[field][i, p])
                        all_events.append(
                            MismatchEvent(
                                family_id=family_id,
                                dataset=dataset_name,
                                record=int(i),
                                subject=f"p{int(p)}",
                                seed_frame=row.seed_frame,
                                ref_frame=row.ref_frame,
                                field=field,
                                seed=seed_v,
                                ref=ref_v,
                                out=out_v,
                                seed_action_id=row.seed_action_id,
                                ref_action_id=row.ref_action_id,
                                out_action_id=row.out_action_id,
                                prev_action_id=row.prev_action_id,
                                seed_action_frame=row.seed_action_frame,
                                ref_action_frame=row.ref_action_frame,
                                out_action_frame=row.out_action_frame,
                                on_ground=row.on_ground,
                                hitlag=row.hitlag,
                                hitstun=row.hitstun,
                            )
                        )

                for slot in range(out["items"].shape[1]):
                    slot_fields: list[str] = []
                    for field, subfield in ITEM_FIELD_TO_SUBFIELD.items():
                        if out["items"][subfield][i, slot] != ref["items"][subfield][i, slot]:
                            slot_fields.append(field)
                    slot_row = ItemSlotRow(
                        dataset=dataset_name,
                        record=int(i),
                        slot=int(slot),
                        seed_frame=int(seed["frame_id"][i]),
                        ref_frame=int(ref["frame_id"][i]),
                        player_actions=tuple(int(seed["action_id"][i, p]) for p in range(num_players)),
                        ref_actions=tuple(int(ref["action_id"][i, p]) for p in range(num_players)),
                        out_actions=tuple(int(out["action_id"][i, p]) for p in range(num_players)),
                        fields=tuple(sorted(slot_fields)),
                        family_id="F99_misc_other",
                    )
                    family_id = _classify_item_slot_row(slot_row, action_names)
                    slot_row = ItemSlotRow(**{**asdict(slot_row), "family_id": family_id})
                    item_rows[(dataset_name, int(i), int(slot))] = slot_row

                    for field in slot_row.fields:
                        subfield = ITEM_FIELD_TO_SUBFIELD[field]
                        all_events.append(
                            MismatchEvent(
                                family_id=family_id,
                                dataset=dataset_name,
                                record=int(i),
                                subject=f"item{int(slot)}",
                                seed_frame=slot_row.seed_frame,
                                ref_frame=slot_row.ref_frame,
                                field=field,
                                seed=int(seed["items"][subfield][i, slot]),
                                ref=int(ref["items"][subfield][i, slot]),
                                out=int(out["items"][subfield][i, slot]),
                                seed_action_id=None,
                                ref_action_id=None,
                                out_action_id=None,
                                prev_action_id=None,
                                seed_action_frame=None,
                                ref_action_frame=None,
                                out_action_frame=None,
                                on_ground=None,
                                hitlag=None,
                                hitstun=None,
                            )
                        )
        finally:
            binding.destroy(handle)

    return all_events, player_rows, item_rows


def _nearby_player_controls(row: PlayerRow, player_rows: dict[tuple[str, int, int], PlayerRow]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for delta in (-2, -1, 1, 2):
        candidate = player_rows.get((row.dataset, row.record + delta, row.p))
        if candidate is None:
            continue
        if candidate.fields:
            continue
        if candidate.seed_action_id != row.seed_action_id and candidate.ref_action_id != row.ref_action_id:
            continue
        out.append(
            {
                "dataset": candidate.dataset,
                "record": int(candidate.record),
                "subject": f"p{int(candidate.p)}",
                "seed_frame": int(candidate.seed_frame),
                "ref_frame": int(candidate.ref_frame),
                "seed_action_id": int(candidate.seed_action_id),
                "ref_action_id": int(candidate.ref_action_id),
            }
        )
        if len(out) >= 3:
            break
    return out


def _nearby_item_controls(row: ItemSlotRow, item_rows: dict[tuple[str, int, int], ItemSlotRow]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for delta in (-2, -1, 1, 2):
        candidate = item_rows.get((row.dataset, row.record + delta, row.slot))
        if candidate is None or candidate.fields:
            continue
        out.append(
            {
                "dataset": candidate.dataset,
                "record": int(candidate.record),
                "subject": f"item{int(candidate.slot)}",
                "seed_frame": int(candidate.seed_frame),
                "ref_frame": int(candidate.ref_frame),
            }
        )
        if len(out) >= 3:
            break
    return out


def _event_sort_key(event: MismatchEvent) -> tuple[Any, ...]:
    return (event.dataset, event.record, event.subject, event.field, event.seed, event.ref, event.out)


def build_summary(
    events: list[MismatchEvent],
    player_rows: dict[tuple[str, int, int], PlayerRow],
    item_rows: dict[tuple[str, int, int], ItemSlotRow],
    *,
    action_names: dict[int, str],
    top_n: int = 15,
) -> dict[str, Any]:
    family_events: dict[str, list[MismatchEvent]] = defaultdict(list)
    for event in events:
        family_events[event.family_id].append(event)

    families: list[dict[str, Any]] = []
    total = int(len(events))

    for family_id, family_list in sorted(
        family_events.items(),
        key=lambda item: (-len(item[1]), item[0]),
    ):
        meta = FAMILY_META[family_id]
        field_counts: Counter[str] = Counter(event.field for event in family_list)
        triple_counts: Counter[tuple[str, int, int, int]] = Counter(
            (event.field, event.seed, event.ref, event.out) for event in family_list
        )
        action_context_counts: Counter[tuple[Any, ...]] = Counter(
            (
                event.seed_action_id,
                event.ref_action_id,
                event.out_action_id,
                _bucket_action_frame(int(event.seed_action_frame or 0)),
                _bucket_action_frame(int(event.ref_action_frame or 0)),
                int(event.on_ground or 0),
                int(event.hitlag or 0),
                int(event.hitstun or 0),
            )
            for event in family_list
            if event.seed_action_id is not None
        )
        unique_rows = sorted({(event.dataset, event.record, event.subject) for event in family_list})
        representative_rows: list[dict[str, Any]] = []
        blocker_rows: list[dict[str, Any]] = []
        counterexample_rows: list[dict[str, Any]] = []
        seen_counterexamples: set[tuple[str, int, str]] = set()

        for dataset, record, subject in unique_rows[: min(5, len(unique_rows))]:
            representative_rows.append(
                {
                    "dataset": dataset,
                    "record": int(record),
                    "subject": subject,
                }
            )

        if family_id.startswith("F14") or family_id.startswith("F15") or family_id.startswith("F16"):
            for dataset, record, subject in unique_rows:
                slot = int(subject.removeprefix("item"))
                row = item_rows[(dataset, record, slot)]
                if len(counterexample_rows) < 3:
                    for ctrl in _nearby_item_controls(row, item_rows):
                        key = (str(ctrl["dataset"]), int(ctrl["record"]), str(ctrl["subject"]))
                        if key in seen_counterexamples:
                            continue
                        seen_counterexamples.add(key)
                        counterexample_rows.append(ctrl)
                break
        else:
            for dataset, record, subject in unique_rows:
                p = int(subject.removeprefix("p"))
                row = player_rows[(dataset, record, p)]
                if len(counterexample_rows) < 3:
                    for ctrl in _nearby_player_controls(row, player_rows):
                        key = (str(ctrl["dataset"]), int(ctrl["record"]), str(ctrl["subject"]))
                        if key in seen_counterexamples:
                            continue
                        seen_counterexamples.add(key)
                        counterexample_rows.append(ctrl)
                break

        for event in sorted(family_list, key=_event_sort_key):
            if event.seed != event.ref:
                blocker_rows.append(
                    {
                        "dataset": event.dataset,
                        "record": int(event.record),
                        "subject": event.subject,
                        "field": event.field,
                        "seed": int(event.seed),
                        "ref": int(event.ref),
                        "out": int(event.out),
                    }
                )
            if len(blocker_rows) >= 5:
                break

        families.append(
            {
                "family_id": family_id,
                "label": meta.label,
                "owner_module": meta.owner_module,
                "fix_type": meta.fix_type,
                "risk": meta.risk,
                "confidence": meta.confidence,
                "count": int(len(family_list)),
                "coverage": float(len(family_list) / total),
                "row_count": int(len(unique_rows)),
                "primary_fields": [
                    {"field": field, "count": int(count)} for field, count in field_counts.most_common(5)
                ],
                "top_triples": [
                    {
                        "field": field,
                        "seed": int(seed),
                        "ref": int(ref),
                        "out": int(out),
                        "count": int(count),
                    }
                    for (field, seed, ref, out), count in triple_counts.most_common(5)
                ],
                "top_action_contexts": [
                    {
                        "seed_action_id": int(seed_action_id),
                        "seed_action_name": _action_name(action_names, seed_action_id),
                        "ref_action_id": int(ref_action_id),
                        "ref_action_name": _action_name(action_names, ref_action_id),
                        "out_action_id": int(out_action_id),
                        "out_action_name": _action_name(action_names, out_action_id),
                        "seed_action_frame_bucket": seed_af,
                        "ref_action_frame_bucket": ref_af,
                        "on_ground": int(on_ground),
                        "hitlag": int(hitlag),
                        "hitstun": int(hitstun),
                        "count": int(count),
                    }
                    for (
                        seed_action_id,
                        ref_action_id,
                        out_action_id,
                        seed_af,
                        ref_af,
                        on_ground,
                        hitlag,
                        hitstun,
                    ), count in action_context_counts.most_common(5)
                ],
                "representative_rows": representative_rows,
                "blocker_rows": blocker_rows,
                "counterexample_rows": counterexample_rows,
                "hypothesis": meta.hypothesis,
                "refs": list(meta.refs),
            }
        )

    top_families = families[: max(1, int(top_n))]
    top_family_ids = {family["family_id"] for family in top_families}
    top_coverage = sum(family["count"] for family in top_families) / total if total else 0.0

    return {
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "total_mismatches": total,
        "family_count": int(len(families)),
        "top_families": top_families,
        "families": families,
        "top_coverage": float(top_coverage),
        "family_metadata": {
            family_id: {
                "label": meta.label,
                "owner_module": meta.owner_module,
                "fix_type": meta.fix_type,
                "risk": meta.risk,
                "confidence": meta.confidence,
                "hypothesis": meta.hypothesis,
                "refs": list(meta.refs),
            }
            for family_id, meta in FAMILY_META.items()
            if family_id in top_family_ids
        },
    }


def _write_family_tsv(path: Path, events: list[MismatchEvent], action_names: dict[int, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(
            (
                "family_id",
                "dataset",
                "record",
                "subject",
                "seed_frame",
                "ref_frame",
                "field",
                "seed",
                "ref",
                "out",
                "seed_action_id",
                "seed_action_name",
                "ref_action_id",
                "ref_action_name",
                "out_action_id",
                "out_action_name",
                "prev_action_id",
                "prev_action_name",
                "seed_action_frame",
                "seed_action_frame_bucket",
                "ref_action_frame",
                "ref_action_frame_bucket",
                "out_action_frame",
                "out_action_frame_bucket",
                "on_ground",
                "hitlag",
                "hitstun",
            )
        )
        for event in sorted(events, key=_event_sort_key):
            writer.writerow(
                (
                    event.family_id,
                    event.dataset,
                    int(event.record),
                    event.subject,
                    int(event.seed_frame),
                    int(event.ref_frame),
                    event.field,
                    int(event.seed),
                    int(event.ref),
                    int(event.out),
                    "" if event.seed_action_id is None else int(event.seed_action_id),
                    _action_name(action_names, event.seed_action_id),
                    "" if event.ref_action_id is None else int(event.ref_action_id),
                    _action_name(action_names, event.ref_action_id),
                    "" if event.out_action_id is None else int(event.out_action_id),
                    _action_name(action_names, event.out_action_id),
                    "" if event.prev_action_id is None else int(event.prev_action_id),
                    _action_name(action_names, event.prev_action_id),
                    "" if event.seed_action_frame is None else int(event.seed_action_frame),
                    "" if event.seed_action_frame is None else _bucket_action_frame(int(event.seed_action_frame)),
                    "" if event.ref_action_frame is None else int(event.ref_action_frame),
                    "" if event.ref_action_frame is None else _bucket_action_frame(int(event.ref_action_frame)),
                    "" if event.out_action_frame is None else int(event.out_action_frame),
                    "" if event.out_action_frame is None else _bucket_action_frame(int(event.out_action_frame)),
                    "" if event.on_ground is None else int(event.on_ground),
                    "" if event.hitlag is None else int(event.hitlag),
                    "" if event.hitstun is None else int(event.hitstun),
                )
            )


def _write_audit_samples_tsv(path: Path, samples: list[AuditSample], action_names: dict[int, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(
            (
                "family_id",
                "dataset",
                "record",
                "subject",
                "matched",
                "audit_reason",
                "field_bundle",
                "seed_action_id",
                "seed_action_name",
                "ref_action_id",
                "ref_action_name",
                "out_action_id",
                "out_action_name",
                "seed_action_frame_bucket",
                "ref_action_frame_bucket",
                "out_action_frame_bucket",
            )
        )
        for sample in samples:
            writer.writerow(
                (
                    sample.family_id,
                    sample.dataset,
                    int(sample.record),
                    sample.subject,
                    int(sample.matched),
                    sample.audit_reason,
                    ",".join(sample.field_bundle),
                    "" if sample.seed_action_id is None else int(sample.seed_action_id),
                    _action_name(action_names, sample.seed_action_id),
                    "" if sample.ref_action_id is None else int(sample.ref_action_id),
                    _action_name(action_names, sample.ref_action_id),
                    "" if sample.out_action_id is None else int(sample.out_action_id),
                    _action_name(action_names, sample.out_action_id),
                    sample.seed_action_frame_bucket,
                    sample.ref_action_frame_bucket,
                    sample.out_action_frame_bucket,
                )
            )


def _default_out_dir() -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return repo_root() / "reports" / "triage" / f"{stamp}_mismatch_taxonomy"


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Build a suite-wide one-step mismatch taxonomy with family inventories and controls."
    )
    ap.add_argument(
        "--suite",
        type=Path,
        default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"),
        help="Suite JSON path.",
    )
    ap.add_argument("--datasets-dir", type=Path, default=Path("datasets"), help="Datasets root.")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory under reports/triage (default: timestamped directory).",
    )
    ap.add_argument("--top", type=int, default=15, help="Top family count to print in the CLI summary.")
    ap.add_argument(
        "--audit-sample-n",
        type=int,
        default=25,
        help="Deterministic audit sample size per family (all rows if family is smaller).",
    )
    args = ap.parse_args()

    root = repo_root()
    suite_path = args.suite if args.suite.is_absolute() else root / args.suite
    datasets_dir = args.datasets_dir if args.datasets_dir.is_absolute() else root / args.datasets_dir
    out_dir = args.out_dir if args.out_dir is not None else _default_out_dir()
    if not out_dir.is_absolute():
        out_dir = root / out_dir

    action_names = load_action_id_names()
    events, player_rows, item_rows = _iter_suite_events(suite_path, datasets_dir, action_names=action_names)
    summary = build_summary(events, player_rows, item_rows, action_names=action_names, top_n=max(1, int(args.top)))

    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    audit_summary, audit_samples = build_audit_summary(
        player_rows,
        item_rows,
        action_names=action_names,
        sample_n=max(1, int(args.audit_sample_n)),
    )
    audit_summary_path = out_dir / "audit_summary.json"
    audit_samples_path = out_dir / "audit_samples.tsv"
    audit_summary_path.write_text(json.dumps(audit_summary, indent=2, sort_keys=True), encoding="utf-8")

    family_events: dict[str, list[MismatchEvent]] = defaultdict(list)
    for event in events:
        family_events[event.family_id].append(event)

    _write_family_tsv(out_dir / "all_events.tsv", events, action_names)
    for family_id, family_list in family_events.items():
        _write_family_tsv(out_dir / "families" / f"{family_id}.tsv", family_list, action_names)
    _write_audit_samples_tsv(audit_samples_path, audit_samples, action_names)

    print(f"suite: {suite_path}")
    print(f"out_dir: {out_dir}")
    print(f"total_mismatches: {summary['total_mismatches']}")
    print(f"family_count: {summary['family_count']}")
    print(f"top_coverage: {summary['top_coverage']:.3%}")
    print(f"audit_sample_n: {args.audit_sample_n}")
    print("top_families:")
    for family in summary["top_families"]:
        print(
            f"  {family['family_id']}: count={family['count']} owner={family['owner_module']} "
            f"fix_type={family['fix_type']} risk={family['risk']} coverage={family['coverage']:.3%}"
        )
    print(f"summary_json: {summary_path}")
    print(f"audit_summary_json: {audit_summary_path}")
    print(f"audit_samples_tsv: {audit_samples_path}")


if __name__ == "__main__":
    main()
