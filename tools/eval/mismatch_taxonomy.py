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
SPECIAL_ENTRY_DISPATCH_FIELDS: frozenset[str] = frozenset(
    {
        "action_id",
        "animation_index",
        "action_frame",
        "instance_id",
        "jumps_left",
        "on_ground",
        "ground_id",
        "facing",
        "state_flags[0]",
    }
)
SPECIAL_ENTRY_ACTION_FIELDS: frozenset[str] = frozenset({"action_id", "animation_index", "action_frame"})

_DEBUG_CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),  # 0=BODY, 1=SHIELD
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)

_DEBUG_SELECTED_BODY_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("hurtcap_id", "u1"),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
    ],
    align=False,
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
    "F25_camera_box_visibility_x221f": FamilyMeta(
        label="Camera-Box Visibility x221F",
        owner_module="state_flags",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "Rows reduced to Slippi state_flags[4] bit ownership are camera-subject visibility "
            "(`fp+0x221F`) rows, even when the visible action is a special. They are owned by "
            "ftLib_80086A8C / camera-box overlap, not by the per-special motion callbacks."
        ),
        refs=(
            "refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm",
            "refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C",
            "refs/melee/src/melee/cm/camera.c::Camera_80030CFC",
            "src/state_flags.c",
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
    "F08a_damage_identity_bookkeeping_residual": FamilyMeta(
        label="Damage Identity / Source Bookkeeping Residual",
        owner_module="anim_timebase",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Damage rows where the remaining visible diff is instance/source identity ordering, not "
            "BODY admission, hitlag, hitstun, or damage-state selection."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm",
        ),
    ),
    "F08b_body_contact_geometry_residual": FamilyMeta(
        label="Combat Geometry / HSD Pose Collision Owner",
        owner_module="hsd_pose_collision",
        fix_type="active split",
        risk="high",
        confidence="high",
        hypothesis=(
            "Rows where current-frame fighter BODY contact selection differs before "
            "post-admission combat followup. Live HSD JObj/AObj/dynamics pose feeding "
            "lb_8000B1CC can select different HitCapsule/HurtCapsule primitives than extracted "
            "SSANIM pose tables. The old broad family is split after the implemented SSDYNN01 "
            "dynamic-chain, HitCapsule-victim-lineage, GuardSetOff onset, and swept/same-group "
            "hitbox-vs-hitbox clank sub-owners, but the parent checklist item remains active while "
            "same-owner BODY candidate, exact lbColl narrowphase, damage-selection, and adjacent "
            "timebase/special-entry splits remain."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
            "refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}",
            "refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}",
            "refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006E7B8,ftAnim_8006EED4}",
            "refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC",
            "tools/extraction/extract_fighter_anims.py",
            "reports/triage/20260416_bhh1599_collision_probe11/BlondHardHippopotamus_rec1599_p1_f1473_1480_collision_probe.jsonl",
        ),
    ),
    "F08e_body_contact_no_candidate_adjacency": FamilyMeta(
        label="Action/Hitbox Timing No-Candidate Adjacency",
        owner_module="action_timebase",
        fix_type="split-first",
        risk="med",
        confidence="high",
        hypothesis=(
            "Rows initially shaped like current-frame BODY admission, but the debug pre-combat "
            "selector has no BODY candidate for the victim. These are not lb_8000B1CC primitive "
            "selection rows; they are action-entry, hitbox-enable timing, or adjacent damage "
            "transition rows until a primitive probe proves otherwise."
        ),
        refs=(
            "src/api.c::msl_batch_debug_step_input_pre_combat",
            "src/combat.c::combat_resolve",
            "refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
        ),
    ),
    "F08f_body_contact_candidate_filter_residual": FamilyMeta(
        label="Special-Move BODY Candidate Filter Residual",
        owner_module="specials",
        fix_type="instrumentation first",
        risk="high",
        confidence="high",
        hypothesis=(
            "Rows where a pre-combat BODY candidate exists but the selected BODY admission still "
            "differs on a special-move hitbox surface. The refreshed aggregate currently leaves "
            "this as the Firefox/SpecialHi hold candidate-filter owner, not the shared HSD pose "
            "collision parent."
        ),
        refs=(
            "src/combat.c::combat_resolve",
            "data/special_msids/{fox,falco}.json",
            "refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_8000805C,lbColl_80006E58}",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
        ),
    ),
    "F05b_damage_hurt_height_selection_residual": FamilyMeta(
        label="Damage Hurt-Height Selection Residual",
        owner_module="damage_selection",
        fix_type="instrumentation first",
        risk="high",
        confidence="high",
        hypothesis=(
            "Rows where both sim and vanilla admit BODY damage but choose different damage-state "
            "classes. The primitive overlap is not a hit/no-hit BODY admission disagreement; "
            "remaining work is the accepted-hit hurt-height/selected-HurtCapsule value consumed by "
            "ftCo_8008DCE0 after ftColl_80076ED8 has already accepted the hit."
        ),
        refs=(
            "src/combat.c::combat_apply_hit_to_player",
            "refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c",
        ),
    ),
    "F08h_body_selected_false_grounded_attack_pose": FamilyMeta(
        label="Grounded Selected False Positive / Exact lbColl Narrowphase",
        owner_module="lbcoll_narrowphase",
        fix_type="new precise owner",
        risk="high",
        confidence="high",
        hypothesis=(
            "Debug pre-combat selection picks a grounded-attack HitCapsule against a live "
            "HurtCapsule while vanilla does not enter damage, and the row is not explained by a "
            "non-damage action transition or ReboundStop expectation. This is now the exact "
            "lbColl_80006E58 narrowphase/scalar residual owner: the sim's reduced sphere/capsule "
            "proxy admits a selected primitive that the faithful lbColl path may reject or convert "
            "to phantom/contact bookkeeping. This remains inside the combat geometry checklist "
            "until the exact predicate/narrowphase owner is implemented."
        ),
        refs=(
            "src/combat.c::combat_resolve",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
            "refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}",
            "tools/triage/audit_f08b_residuals.py",
        ),
    ),
    "F08i_body_selected_false_aerial_attack_pose": FamilyMeta(
        label="Aerial Selected False Positive / Exact lbColl Narrowphase",
        owner_module="lbcoll_narrowphase",
        fix_type="new precise owner",
        risk="high",
        confidence="high",
        hypothesis=(
            "Debug pre-combat selection picks an AttackAir HitCapsule against a live HurtCapsule, "
            "but vanilla does not enter damage. Rows where the victim's post-timebase action is "
            "already the replay's non-damage destination are the aerial counterpart of the exact "
            "lbColl_80006E58 narrowphase/scalar residual. This remains inside the combat geometry "
            "checklist until the exact predicate/narrowphase owner is implemented."
        ),
        refs=(
            "src/combat.c::combat_resolve",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
            "refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}",
            "tools/triage/audit_f08b_residuals.py",
        ),
    ),
    "F08h1_body_selected_false_rebound_clank_residual": FamilyMeta(
        label="Selected False Positive / Rebound-Clank Residual",
        owner_module="clank_rebound",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Debug pre-combat BODY selection exists, but vanilla enters ReboundStop instead of "
            "damage. These rows are residual hitbox-vs-hitbox clank/rebound ordering after the "
            "swept lbColl_80007AFC sub-owner, not HSD hurtcap pose rows."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,ftColl_80078C70}",
            "refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007AFC,lbColl_80006094}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_ReboundStop_Anim}",
        ),
    ),
    "F08h2_body_selected_false_grounded_timebase_residual": FamilyMeta(
        label="Grounded Selected False Positive / Action-Timebase Residual",
        owner_module="action_timebase",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Debug pre-combat BODY selection exists, but the selected primitive is coupled to a "
            "same-frame action/timebase divergence between seed and vanilla non-damage destination. "
            "The owner is action entry/IASA/timebase ordering before BODY, not shared HSD pose."
        ),
        refs=(
            "src/api.c::msl_batch_debug_step_input_pre_combat",
            "refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4",
            "refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C",
        ),
    ),
    "F08i1_body_selected_false_aerial_timebase_residual": FamilyMeta(
        label="Aerial Selected False Positive / Action-Timebase Residual",
        owner_module="action_timebase",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Debug pre-combat BODY selection exists on an aerial HitCapsule, but the victim's "
            "same-frame action/timebase transition is already diverging into the replay's "
            "non-damage destination. The owner is aerial/landing/jump action entry timing, not "
            "shared HSD pose collision."
        ),
        refs=(
            "src/api.c::msl_batch_debug_step_input_pre_combat",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim",
            "refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4",
        ),
    ),
    "F08j_body_selected_false_special_entry_pose": FamilyMeta(
        label="Special Entry Selected False Positive",
        owner_module="specials",
        fix_type="split-first",
        risk="high",
        confidence="high",
        hypothesis=(
            "Debug pre-combat selection picks a special/action-entry HitCapsule (notably msid 313/308) "
            "against a live HurtCapsule, but vanilla does not enter damage. The next owner is special "
            "action-entry pose/hitbox enablement before treating these as shared grounded attack pose."
        ),
        refs=(
            "src/combat.c::combat_resolve",
            "data/special_msids/{fox,falco}.json",
            "refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}",
            "tools/triage/audit_f08b_residuals.py",
        ),
    ),
    "F10k_body_no_candidate_action_timing": FamilyMeta(
        label="BODY No-Candidate Action / Hitbox Timing",
        owner_module="action_timebase",
        fix_type="split-first",
        risk="med",
        confidence="high",
        hypothesis=(
            "Rows that were initially BODY-admission-shaped, but debug pre-combat has no BODY "
            "candidate and no selected BODY hit for the victim. The current collision primitive "
            "owner is not reached; remaining work belongs to action-entry, hitbox-enable timing, "
            "or adjacent damage-transition owners."
        ),
        refs=(
            "src/api.c::msl_batch_debug_step_input_pre_combat",
            "src/combat.c::combat_resolve",
            "refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C",
            "refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4",
        ),
    ),
    "F10l_body_selected_false_action_timebase": FamilyMeta(
        label="Selected False BODY / Action-Timebase",
        owner_module="action_timebase",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Debug pre-combat BODY selection exists, but the victim's pre-combat or replay "
            "destination already differs from the seed action. These are action-entry/timebase "
            "rows whose stale collision primitive is a symptom, not the shared HSD pose owner."
        ),
        refs=(
            "src/api.c::msl_batch_debug_step_input_pre_combat",
            "refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4",
            "refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C",
        ),
    ),
    "F08c_damage_state_transition_adjacency": FamilyMeta(
        label="Damage State Transition Adjacency",
        owner_module="locomotion",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Damage-family rows whose remaining action/action_frame disagreement is a landing, "
            "turn, Down*, Passive*, or floor/wall continuation owner rather than shared combat "
            "damage admission."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Anim,ftCo_DamageFly_Coll}",
            "refs/melee/src/melee/mp/mpcoll.c",
        ),
    ),
    "F08d_damage_timer_scalar_residual": FamilyMeta(
        label="Damage Timer / Scalar Residual",
        owner_module="timers",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Rows where accepted damage followup is now present but scalar timers or small damage "
            "bookkeeping values still differ."
        ),
        refs=(
            "refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744",
        ),
    ),
    "F09a_aerial_stateflag_hurtbox_adjacency": FamilyMeta(
        label="Aerial State-Flag / Hurtbox Adjacency",
        owner_module="state_flags",
        fix_type="runtime-only",
        risk="low",
        confidence="med",
        hypothesis=(
            "Aerial or jump rows with no fighter contact where the remaining diff is hurtbox or "
            "state-flag ownership, not combat continuation."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftaction.c::ftAction_80071A14",
            "refs/melee/src/melee/ft/fighter.c::Fighter_8006A360",
        ),
    ),
    "F09b_aerial_bookkeeping_adjacency": FamilyMeta(
        label="Aerial Combo / Source Bookkeeping Adjacency",
        owner_module="combat",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Aerial or jump rows with no fighter contact where only last-hit, combo, or "
            "last-attack bookkeeping remains visible."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_800764DC}",
            "refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm",
        ),
    ),
    "F09c_aerial_action_entry_adjacency": FamilyMeta(
        label="Aerial Action Entry / Input Adjacency",
        owner_module="action",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Aerial or jump rows where the remaining mismatch is Jump/KneeBend/CliffJump -> "
            "AttackAir or JumpAerial selection and motion-entry ordering, with no hitstun or "
            "damage-owner proof."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c",
            "refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState",
        ),
    ),
    "F09d_aerial_contact_hitlag_residual": FamilyMeta(
        label="Aerial Contact Hitlag Residual",
        owner_module="combat",
        fix_type="instrumentation first",
        risk="med",
        confidence="med",
        hypothesis=(
            "AttackAir rows where action state is already aligned but hitlag or the hitlag state "
            "flag differs without hitstun/percent evidence; likely shield, item, clank, or "
            "deal-hitlag lane ownership rather than BODY damage continuation."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076444,ftColl_80076CBC}",
            "refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC",
            "refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC",
        ),
    ),
    "F09e_aerial_instance_timing_residual": FamilyMeta(
        label="Aerial Instance / Timing Residual",
        owner_module="anim_timebase",
        fix_type="split-first",
        risk="med",
        confidence="low",
        hypothesis=(
            "Aerial rows left after state-flag, bookkeeping, action-entry, and contact-hitlag "
            "splits; these need a narrower audit before being treated as combat followup."
        ),
        refs=(
            "refs/melee/src/melee/ft/ft_0881.c::{ft_800890D0,ft_800892A0}",
            "refs/melee/src/melee/ft/ftanim.c",
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
    "F10a_grounded_selector_transition": FamilyMeta(
        label="Grounded Selector Transition",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "True grounded selector rows where Wait / Dash / Walk / Turn / KneeBend / grounded "
            "attack ordering chooses a different motion-entry bundle."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA",
        ),
    ),
    "F10b_grounded_combat_adjacency": FamilyMeta(
        label="Grounded Combat Adjacency",
        owner_module="combat",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Rows initially caught by grounded action names but whose field bundle is combat-owned "
            "(hitlag, combo, last-hit, or attack-landed state)."
        ),
        refs=(
            "refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC",
            "refs/melee/src/melee/ft/ftcoll.c",
        ),
    ),
    "F10c_collision_landing_edge_adjacency": FamilyMeta(
        label="Collision / Landing / Edge Adjacency",
        owner_module="mpcoll_env",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Rows initially caught by grounded action names but whose field bundle is collision, "
            "landing, cliff/edge, or Ottotto/Fall ownership."
        ),
        refs=(
            "refs/melee/src/melee/mp/mpcoll.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c",
            "refs/melee/src/melee/ft/ft_081B.c",
        ),
    ),
    "F10m_floor_line_identity": FamilyMeta(
        label="Floor-Line Identity Visibility",
        owner_module="stage_collision",
        fix_type="runtime-only",
        risk="low",
        confidence="high",
        hypothesis=(
            "Pure `CollData.floor.index` visibility rows at connected FD floor seams. The action, "
            "ground/air, jump, and collision-transition decisions already agree; only the exported "
            "floor line id differs."
        ),
        refs=(
            "refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor",
            "refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor",
            "refs/melee/src/melee/lb/types.h::CollData",
        ),
    ),
    "F10n_common_fall_landing_timebase": FamilyMeta(
        label="Common Fall / Landing Timebase",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Generic Fall <-> Landing one-frame phase rows owned by common Fall_Coll and "
            "Landing_Enter callback timing, not ledge occupancy or CliffCatch admission."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic",
            "refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_80082B1C}",
        ),
    ),
    "F10o_ottotto_teeter_edge_handoff": FamilyMeta(
        label="Ottotto Teeter Edge Handoff",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Common teeter rows where Wait/Landing/Run-family callbacks disagree between Ottotto "
            "entry and generic Fall. These are the ftCo_8009A3C8 teeter owner, separate from "
            "ledge occupancy, ledge grab masks, or CliffCatch."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410,ftCo_Ottotto_Coll}",
            "refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}",
        ),
    ),
    "F10p_specialhi_bound_collision_callback": FamilyMeta(
        label="SpecialAirHi / Bound Collision Callback Timing",
        owner_module="mpcoll_env",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "SpecialAirHi <-> SpecialHiBound one-frame callback timing rows from shared collision "
            "callback ordering. Keeping this outside F22 preserves the closed Firefox/Firebird "
            "state-machine owner."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Coll}",
            "refs/melee/src/melee/mp/mpcoll.c",
        ),
    ),
    "F10d_hurtbox_stateflag_adjacency": FamilyMeta(
        label="Hurtbox / State-Flag Adjacency",
        owner_module="state_flags",
        fix_type="runtime-only",
        risk="low",
        confidence="med",
        hypothesis=(
            "Rows initially caught by grounded action names but reduced to hurtbox-state or "
            "state-flag parity rather than selector timing."
        ),
        refs=(
            "refs/melee/src/melee/ft/fighter.c::Fighter_8006A360",
            "refs/melee/src/melee/ft/ftmaterial.c",
        ),
    ),
    "F10e_special_move_adjacency": FamilyMeta(
        label="Grounded Special-Move Adjacency",
        owner_module="specials",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Rows touching grounded selector names only because a special-move entry or exit is on "
            "one side of the transition; these are outside the shared Wait/Dash/Walk/Turn owner."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c",
        ),
    ),
    "F19_specialn_blaster_article": FamilyMeta(
        label="SpecialN / Blaster Article",
        owner_module="specials",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Fox/Falco Neutral-B rows owned by the SpecialN Start/Loop/End state machine and its "
            "blaster gun / laser article callbacks, including aerial landing handoff and combat "
            "exit bookkeeping."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c",
            "refs/melee/src/melee/it/items/itfoxblaster.c",
            "refs/melee/src/melee/it/items/itfoxlaser.c",
            "src/blaster.c",
            "src/items.c",
        ),
    ),
    "F20_speciallw_shine_reflector": FamilyMeta(
        label="SpecialLw / Shine Reflector",
        owner_module="specials",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Fox/Falco Shine rows owned by the SpecialLw Start/Loop/Hit/Turn/End state machine, "
            "release-lag latch, ground-air collision handoff, reflector bubble lifetime, and "
            "reflect/contact surfaces."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c",
            "refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit",
            "src/shine.c",
            "src/reflector_bubbles.c",
            "src/items.c",
        ),
    ),
    "F21_specials_illusion_phantasm": FamilyMeta(
        label="SpecialS / Illusion-Phantasm",
        owner_module="specials",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Fox/Falco Side-B rows owned by SpecialS Start/Main/End, air-ground collision "
            "handoffs, LandingFallSpecial source lag, ghost-ring article spawn/position, and "
            "Illusion/Phantasm contact persistence."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c",
            "refs/melee/src/melee/it/items/itfoxillusion.c",
            "src/locomotion.c",
            "src/items.c",
        ),
    ),
    "F22_specialhi_firefox_firebird": FamilyMeta(
        label="SpecialHi / FireFox-FireBird",
        owner_module="specials",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Fox/Falco Up-B rows owned by SpecialHi hold/launch/fall/landing/bound callbacks, "
            "XRotN pose rotation, launch travel frames, and SpecialHiFall landing continuation."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c",
            "src/locomotion.c",
            "src/hitboxes.c",
            "src/hurtboxes.c",
        ),
    ),
    "F23_special_common_entry_dispatch": FamilyMeta(
        label="Common Special Entry Dispatch",
        owner_module="action",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Rows where a common IASA/input owner dispatches into a Fox/Falco special start state. "
            "The boundary is the shared grounded/aerial special-selection chain, not the steady "
            "per-special state machine."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput",
            "src/blaster.c",
            "src/shine.c",
        ),
    ),
    "F24_special_adjacent_instance_order": FamilyMeta(
        label="Special-Adjacent Instance Counter Order",
        owner_module="anim_timebase",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "Instance-id-only rows with source-backed Fox/Falco special callback ownership, such "
            "as SpecialN Loop restarts that install x21EC/OnChangeAction and call ft_80089824. "
            "Direct special-boundary entries/exits without that callback stay in the generic "
            "ft_800895E0 / plAttack_80037B08 adjacent instance bucket."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824",
            "refs/melee/src/melee/pl/plattack.c::plAttack_80037B08",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction",
        ),
    ),
    "F10f_grounded_attack_adjacency": FamilyMeta(
        label="Grounded Attack-State Adjacency",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "Grounded attack action rows whose transition is owned by attack Anim/IASA internals "
            "rather than the common grounded locomotion selector."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c",
        ),
    ),
    "F10g_runbrake_adjacency": FamilyMeta(
        label="RunBrake / Run Exit Adjacency",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="med",
        hypothesis=(
            "RunBrake/Run exit rows adjacent to Wait/Walk/Turn, owned by RunBrake/Run callbacks "
            "rather than the common grounded selector entry path."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c",
        ),
    ),
    "F10h_appeal_adjacency": FamilyMeta(
        label="Appeal / Taunt Adjacency",
        owner_module="locomotion",
        fix_type="split-first",
        risk="low",
        confidence="med",
        hypothesis=(
            "Wait/Turn rows that touch AppealSR/AppealSL admission through ftCo_800DE9D8; this is "
            "a separate Wait_IASA command branch, not Dash/Walk/Turn/KneeBend ownership."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c",
        ),
    ),
    "F10i_turn_hidden_microphase": FamilyMeta(
        label="Turn Hidden Microphase",
        owner_module="locomotion",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "Turn rows whose visible mismatch is the hidden ftCo_Turn_Anim_Inner / "
            "ftCo_Turn_IASA microphase (`has_turned`, `just_turned`, temporary facing) rather than "
            "the common Wait/Dash/Walk/KneeBend selector owner."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Anim_Inner,ftCo_Turn_IASA}",
            "refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Turn.s::ftCo_Turn_IASA",
        ),
    ),
    "F10j_turnrun_exit_microphase": FamilyMeta(
        label="TurnRun Exit Microphase",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "TurnRun rows governed by ftCo_TurnRun_Anim's anim-end branch into Run/Wait before any "
            "destination grounded selector can run; this is TurnRun exit ownership, not shared "
            "Wait/Dash/Walk/Turn selector timing."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644",
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
    "F12a_grounded_instance_counter_order": FamilyMeta(
        label="Grounded Motion-Entry Instance Counter Order",
        owner_module="anim_timebase",
        fix_type="runtime-only",
        risk="low",
        confidence="med",
        hypothesis=(
            "Instance-id-only rows on grounded locomotion selector entries where ft_800895E0/x2073 "
            "is wired, but the global plAttack_80037B08 counter ordering differs under simultaneous "
            "same-frame motion entries."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/pl/plattack.c::plAttack_80037B08",
        ),
    ),
    "F12b_adjacent_instance_counter_order": FamilyMeta(
        label="Adjacent-Family Instance Counter Order",
        owner_module="anim_timebase",
        fix_type="split-first",
        risk="med",
        confidence="med",
        hypothesis=(
            "Instance-id-only rows whose visible action transition belongs to special, landing, "
            "cliff, aerial, or damage families; these affect the same global instance counter but "
            "are outside the grounded locomotion selector owner."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/ft/ftmotionstates.c",
        ),
    ),
    "F12c_attack_instance_counter_order": FamilyMeta(
        label="Attack-State Instance Counter Order",
        owner_module="combat",
        fix_type="split-first",
        risk="med",
        confidence="high",
        hypothesis=(
            "Instance-id-only rows where an attack motion entry/exit consumes the same global "
            "plAttack_80037B08 counter; these are attack owner rows, not locomotion motion-entry "
            "x2073 debt."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/pl/plattack.c::plAttack_80037B08",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c",
        ),
    ),
    "F12d_squat_escape_instance_counter_order": FamilyMeta(
        label="Squat / Escape Instance Counter Order",
        owner_module="locomotion",
        fix_type="split-first",
        risk="med",
        confidence="high",
        hypothesis=(
            "Instance-id-only rows whose visible transition is crouch/squat or escape ownership "
            "adjacent to grounded locomotion. They share the global counter but are outside the "
            "Dash/Walk/Turn/KneeBend motion-entry owner."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c",
        ),
    ),
    "F12e_cross_player_instance_counter_order": FamilyMeta(
        label="Cross-Player Instance Counter Order",
        owner_module="anim_timebase",
        fix_type="split-first",
        risk="med",
        confidence="high",
        hypothesis=(
            "Local grounded instance-id-only rows where the visible actor is a grounded selector "
            "entry, but another player consumes the same global plAttack_80037B08 counter in the "
            "same frame through a non-grounded or adjacent-family transition."
        ),
        refs=(
            "refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0",
            "refs/melee/src/melee/pl/plattack.c::plAttack_80037B08",
            "refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate",
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
    "F13a_common_fallspecial_landing": FamilyMeta(
        label="Common FallSpecial / LandingFallSpecial",
        owner_module="locomotion",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "FallSpecial, LandingFallSpecial, and EscapeAir landing rows that do not touch Fox/Falco "
            "SpecialHi state. These are common landing/freefall collision rows, not FireFox-owned "
            "continuations."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c",
            "src/locomotion.c",
        ),
    ),
    "F14b_per_throw_pulse_bookkeeping": FamilyMeta(
        label="Per-Throw Blaster Pulse / Bookkeeping",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "ThrowB/ThrowHi/ThrowLw and ThrownLw rows owned by the per-throw blaster pulse/article "
            "and combo/source bookkeeping callbacks, not by Fox/Falco B-special state machines."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c",
            "src/throw_flow.c",
            "src/items.c",
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
    "F14c_throw_article_lifetime": FamilyMeta(
        label="Throw Article Pulse / Lifetime",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "Throw-side blaster article rows where the remaining residual is item spawn, despawn, "
            "slot, or xDA8 identity for set_throw_spawn_projectile pulses."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim",
            "refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}",
            "data/moves/{fox,falco}.json set_throw_spawn_projectile events",
        ),
    ),
    "F14d_throw_source_scoreboard": FamilyMeta(
        label="Throw Item Source / Scoreboard Bookkeeping",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "Action-aligned Throw*/Thrown* player rows whose remaining fields are source, contact, "
            "hitlag, or combo bookkeeping from item-domain throw hits rather than item slot life."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c",
            "refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}",
            "refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem",
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
    "F15a_reflect_owner_transfer": FamilyMeta(
        label="Reflect Owner / xDA8 Transfer",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="med",
        confidence="high",
        hypothesis=(
            "GuardReflect item rows reduced to reflected owner and xDA8_short transfer timing."
        ),
        refs=(
            "refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464",
            "refs/melee/src/melee/it/item.c::Item_80269F14",
            "refs/slippi-ssbm-asm/Recording/SendItemInfo.s",
        ),
    ),
    "F15b_guard_laser_lifetime": FamilyMeta(
        label="Guard Laser Shield-Bounce / Lifetime",
        owner_module="items",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Guard / GuardSetOff item rows where shield hit, shield bounce, or despawn lifetime "
            "differs while fighter-side guard ownership is already separate."
        ),
        refs=(
            "refs/melee/src/melee/it/item.c::Item_80269DC8",
            "refs/melee/src/melee/it/items/itfoxlaser.c::{it_2725_Logic94_HitShield,itFoxLaser_Logic94_ShieldBounced}",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C",
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
    "F16a_item_slot_compaction_identity": FamilyMeta(
        label="Item Slot / Compaction Identity",
        owner_module="items",
        fix_type="seed/schema + runtime",
        risk="low",
        confidence="high",
        hypothesis=(
            "Item-only rows where the same live item key is compacted into a different fixed compare "
            "slot after spawn/despawn ordering."
        ),
        refs=(
            "tools/slippi/make_dataset_from_slp.py::_fill_items_fixed",
            "refs/slippi-ssbm-asm/Recording/SendItemInfo.s",
        ),
    ),
    "F16b_blaster_article_identity": FamilyMeta(
        label="Blaster Gun / Shot Identity",
        owner_module="items",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Neutral-B blaster gun and shot item identity/lifetime rows outside throw or guard contexts."
        ),
        refs=(
            "refs/melee/src/melee/it/items/itfoxblaster.c",
            "refs/melee/src/melee/it/items/itfoxlaser.c",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c",
        ),
    ),
    "F16c_illusion_phantasm_lifetime": FamilyMeta(
        label="Illusion / Phantasm Article Lifetime",
        owner_module="items",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Side-B ghost article item state/despawn rows for Fox Illusion / Falco Phantasm."
        ),
        refs=(
            "refs/melee/src/melee/it/items/itfoxillusion.c",
            "refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c",
        ),
    ),
    "F16d_item_body_lifetime": FamilyMeta(
        label="Item BODY Hit / Lifetime",
        owner_module="items",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Item projectile BODY-hit admission/lifetime rows where an item persists, despawns, or "
            "misses despawn outside guard and throw contexts."
        ),
        refs=(
            "refs/melee/src/melee/it/itcoll.c::it_80272460",
            "refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4",
            "refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC",
        ),
    ),
    "F17_mpcoll_ledge_ecb_residual": FamilyMeta(
        label="mpColl / Ledge / ECB Contact Residual",
        owner_module="mpcoll_env",
        fix_type="runtime-only",
        risk="med",
        confidence="high",
        hypothesis=(
            "Rows whose remaining mismatch is persistent floor-line identity (`CollData.floor.index`) "
            "in damage or Cliff option contexts. Damage/passive/wall-tech action bundles are split "
            "to their damage/action owners rather than kept in the ledge bucket."
        ),
        refs=(
            "refs/melee/src/melee/mp/mpcoll.c",
            "refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor",
            "refs/melee/src/melee/lb/types.h::CollData",
        ),
    ),
    "F18_damage_tech_timer_seed_surface": FamilyMeta(
        label="Damage Tech Timer Seed Surface",
        owner_module="seed",
        fix_type="seed/schema investigation",
        risk="med",
        confidence="high",
        hypothesis=(
            "DamageFly contact reached the grounded follow-up selector, but replay-derived tech "
            "timer lanes make ftCo_800986B0 admit PassiveStand while the replay shows DownBound."
        ),
        refs=(
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0",
            "refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928",
            "tools/slippi/seed_history.py::compute_fighter_button_timers",
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
    seed_item_type: int = 0
    ref_item_type: int = 0
    out_item_type: int = 0
    prev_actions: tuple[int, ...] = ()


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
    if int(action_id) == 245:
        return "OTTOTTO"
    if int(action_id) == 246:
        return "OTTOTTO_WAIT"
    if int(action_id) == 254:
        return "CLIFF_CLIMB_SLOW"
    if int(action_id) == 256:
        return "CLIFF_ATTACK_SLOW"
    if int(action_id) == 258:
        return "CLIFF_ESCAPE_SLOW"
    if int(action_id) == 260:
        return "CLIFF_JUMP_SLOW1"
    if int(action_id) == 261:
        return "CLIFF_JUMP_SLOW2"
    if int(action_id) == 264:
        return "APPEAL_SR"
    if int(action_id) == 265:
        return "APPEAL_SL"
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


def _looks_like_damage_admission_disagreement(names: tuple[str, ...], field_set: set[str]) -> bool:
    # BODY admission disagreements have one side in a Damage* state and the other side still in a
    # non-damage action. Do not classify pure Down*/Passive selector rows here; those are
    # knockdown/mpColl adjacency unless an actual Damage* action is present.
    if not any(name.startswith("DAMAGE_") for name in names):
        return False
    if all(name.startswith("DAMAGE_") for name in names):
        return False
    if names[1] == names[2] and names[1].startswith("DAMAGE_"):
        return False
    return bool(
        field_set
        & {
            "action_id",
            "animation_index",
            "hitstun",
            "instance_hit_by",
            "last_hit_by",
            "percent",
            "on_ground",
            "jumps_left",
        }
    )


def _looks_like_current_frame_body_admission_disagreement(
    row: PlayerRow, names: tuple[str, str, str], field_set: set[str]
) -> bool:
    # F08b is the pre-admission BODY geometry owner. Keep it scoped to rows where the current
    # frame can newly admit or suppress BODY damage from a non-damage seed action. Rows already in
    # Damage*/Down*/Passive are damage continuation, floor contact, tech, or state-transition
    # ownership unless a focused primitive probe later proves a new BODY hit happened in that state.
    if not _looks_like_damage_admission_disagreement(names, field_set):
        return False
    seed_name, _ref_name, _out_name = names
    if _looks_like_damage(seed_name):
        return False
    if row.hitlag != 0:
        return False
    return True


def _damage_admission_outcome(ref_name: str, out_name: str) -> str:
    ref_damage = _looks_like_damage(ref_name)
    out_damage = _looks_like_damage(out_name)
    if out_damage and not ref_damage:
        return "sim_false_body_or_damage"
    if ref_damage and not out_damage:
        return "sim_missed_body_or_damage"
    if ref_damage and out_damage and ref_name != out_name:
        return "wrong_damage_selection"
    return "transition_or_scalar"


def _family_for_debug_body_contact_residual(
    *,
    seed_name: str = "",
    precombat_name: str = "",
    ref_name: str,
    out_name: str,
    selected_body_count: int,
    body_candidate_count: int,
    filtered_body_candidate_count: int,
    active_fighter_hitbox_count: int = 0,
    active_special_attacker_hitbox_count: int = 0,
    live_nonvictim_item_count: int = 0,
    first_msid: int | None = None,
) -> str:
    outcome = _damage_admission_outcome(ref_name, out_name)

    if outcome == "wrong_damage_selection":
        return "F05b_damage_hurt_height_selection_residual"

    if body_candidate_count <= 0 and selected_body_count <= 0:
        if active_fighter_hitbox_count <= 0:
            if live_nonvictim_item_count > 0:
                return "F16d_item_body_lifetime"
            special_family = _special_owner_family_for_names(
                (seed_name, precombat_name, ref_name, out_name),
                include_entry_dispatch=False,
            )
            if special_family is not None:
                return special_family
            return "F08c_damage_state_transition_adjacency"
        if active_special_attacker_hitbox_count > 0:
            special_family = _special_owner_family_for_names(
                (seed_name, precombat_name, ref_name, out_name),
                include_entry_dispatch=False,
            )
            return special_family or _special_owner_family_for_msid(first_msid) or "F08f_body_contact_candidate_filter_residual"
        return "F10k_body_no_candidate_action_timing"

    if outcome == "sim_missed_body_or_damage":
        _ = first_msid
        return "F08f_body_contact_candidate_filter_residual"

    if outcome == "sim_false_body_or_damage" and selected_body_count > 0:
        has_timebase_divergence = (
            (precombat_name and precombat_name != ref_name)
            or (not precombat_name and seed_name and seed_name != ref_name)
        )
        if first_msid in {46, 52, 55, 58, 59}:
            if ref_name == "REBOUND_STOP":
                return "F08h1_body_selected_false_rebound_clank_residual"
            if has_timebase_divergence:
                return "F10l_body_selected_false_action_timebase"
            return "F08h_body_selected_false_grounded_attack_pose"
        if first_msid in {68, 70, 71, 72}:
            if has_timebase_divergence:
                return "F10l_body_selected_false_action_timebase"
            return "F08i_body_selected_false_aerial_attack_pose"
        if first_msid is not None and first_msid >= 300:
            # A special hitbox being the current BODY candidate does not by itself make the row a
            # special state-machine owner. If vanilla did not enter damage but the sim selected a
            # special BODY candidate, the remaining owner is the candidate filter / narrowphase
            # selection surface, not the SpecialLw/SpecialN/... Anim/IASA/Coll callback.
            # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
            return "F08f_body_contact_candidate_filter_residual"

    # The residual still has a pre-combat BODY candidate/selection, but it is not one of the
    # currently split Fox/Falco RL1.0 current-frame clusters.
    _ = filtered_body_candidate_count
    return "F08b_body_contact_geometry_residual"


def _looks_like_damagefly(name: str) -> bool:
    return name.startswith("DAMAGE_FLY_")


def _looks_like_knockdown_contact_destination(name: str) -> bool:
    return name in {
        "PASSIVE",
        "PASSIVE_STAND_F",
        "PASSIVE_STAND_B",
        "DOWN_BOUND_U",
        "DOWN_BOUND_D",
    } or _looks_like_damagefly(name)


def _looks_like_passive_wall(name: str) -> bool:
    return name.startswith("PASSIVE_WALL")


def _is_knockdown_selector_disagreement(names: tuple[str, str, str]) -> bool:
    # F07 is now reserved for the shared selector itself: contact is accepted, but the grounded
    # follow-up differs. DamageFly-vs-grounded disagreements are contact timing residuals.
    ref_name = names[1]
    out_name = names[2]
    grounded_ref = ref_name in {
        "PASSIVE",
        "PASSIVE_STAND_F",
        "PASSIVE_STAND_B",
        "DOWN_BOUND_U",
        "DOWN_BOUND_D",
    }
    grounded_out = out_name in {
        "PASSIVE",
        "PASSIVE_STAND_F",
        "PASSIVE_STAND_B",
        "DOWN_BOUND_U",
        "DOWN_BOUND_D",
    }
    return grounded_ref and grounded_out and ref_name != out_name


def _is_mpcoll_ledge_ecb_residual(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> bool:
    contact_fields = {"on_ground", "ground_id", "jumps_left", "hurtbox_state"}
    if not (field_set & contact_fields):
        return False

    seed_name, ref_name, out_name = names
    if seed_name == ref_name == out_name:
        # Same-action damage rows with only jump/on-ground/hurtbox drift are visible
        # damage/knockdown bookkeeping. Keep only persistent floor-line identity (`floor.index`)
        # in the mpColl/ledge bucket.
        # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
        # refs/melee/src/melee/lb/types.h::CollData
        return "ground_id" in field_set

    # Floor-contact timing: one side accepts DamageFly contact into Passive/DownBound while the
    # other side continues DamageFly. The Passive/DownBound selector itself is not disagreeing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    damagefly_vs_grounded_contact = (
        any(_looks_like_damagefly(name) for name in names)
        and any(
            name in {"PASSIVE", "PASSIVE_STAND_F", "PASSIVE_STAND_B", "DOWN_BOUND_U", "DOWN_BOUND_D"}
            for name in names
        )
        and not _is_knockdown_selector_disagreement(names)
    )
    if damagefly_vs_grounded_contact:
        return False

    # Wall-contact timing: PassiveWall* is admitted by the wall/ceiling contact callback surface,
    # not by the shared floor Passive/PassiveStand/DownBound selector.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c
    if any(_looks_like_passive_wall(name) for name in names):
        return False

    return False


def _is_damage_tech_timer_seed_surface(row: PlayerRow, names: tuple[str, str, str]) -> bool:
    # This is not a contact-owner miss: both ref/out accepted floor contact and chose a grounded
    # follow-up, but the visible disagreement is exactly the tech-admission branch. Keep this
    # separate from F07 so the remaining work is seed/internal provenance, not another runtime
    # selector patch.
    ref_name = names[1]
    out_name = names[2]
    return (
        any(_looks_like_damagefly(name) for name in names)
        and {ref_name, out_name} <= {
            "PASSIVE",
            "PASSIVE_STAND_F",
            "PASSIVE_STAND_B",
            "DOWN_BOUND_U",
            "DOWN_BOUND_D",
        }
        and ref_name != out_name
        and row.on_ground == 0
        and row.hitlag == 0
    )


def _looks_like_special_hi(name: str) -> bool:
    return "SPECIAL_HI" in name or "FALL_SPECIAL" in name


def _looks_like_any_special(name: str) -> bool:
    return "SPECIAL" in name


def _looks_like_specialn(name: str) -> bool:
    return "SPECIAL_N" in name or "SPECIAL_AIR_N" in name


def _looks_like_speciallw(name: str) -> bool:
    return "SPECIAL_LW" in name or "SPECIAL_AIR_LW" in name


def _looks_like_specials(name: str) -> bool:
    return "SPECIAL_S" in name or "SPECIAL_AIR_S" in name


def _looks_like_special_air_s(name: str) -> bool:
    return "SPECIAL_AIR_S" in name


def _looks_like_firefox(name: str) -> bool:
    return "SPECIAL_HI" in name


def _looks_like_common_fallspecial(name: str) -> bool:
    return name in {
        "FALL_SPECIAL",
        "FALL_SPECIAL_F",
        "FALL_SPECIAL_B",
        "LANDING_FALL_SPECIAL",
        "ESCAPE_AIR",
    }


def _looks_like_throw_or_thrown(name: str) -> bool:
    return name.startswith("THROW_") or name.startswith("THROWN_")


def _looks_like_special_entry(name: str) -> bool:
    if not _looks_like_any_special(name):
        return False
    return name.endswith("_START") or name.endswith("_HOLD") or name.endswith("_HOLD_AIR")


def _is_common_special_dispatch_source(name: str) -> bool:
    # Keep F23 scoped to source actions whose IASA owners actually call the common B-special
    # dispatchers. In particular, KneeBend and LandingFallSpecial do not; assigning those rows to
    # F23 hides their true per-special or adjacent owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
    if name in {
        "WAIT",
        "SQUAT",
        "SQUAT_WAIT",
        "SQUAT_RV",
        "RUN",
        "RUN_DIRECT",
        "LANDING",
        "OTTOTTO",
        "OTTOTTO_WAIT",
        "FALL",
        "FALL_F",
        "FALL_B",
        "FALL_AERIAL",
        "FALL_AERIAL_F",
        "FALL_AERIAL_B",
        "DAMAGE_FALL",
        "PASSIVE_WALL",
        "PASSIVE_WALL_JUMP",
    }:
        return True
    if name.startswith("WALK_") or name.startswith("JUMP_"):
        return True
    if name.startswith("DAMAGE_AIR_") or name.startswith("DAMAGE_FLY_"):
        return True
    return False


def _special_owner_family_for_names(
    names: tuple[str, ...],
    *,
    field_set: set[str] | frozenset[str] | None = None,
    include_entry_dispatch: bool = True,
) -> str | None:
    fields = set(field_set or ())

    if include_entry_dispatch:
        current_names = names[:3]
        seed_name = current_names[0] if current_names else "NONE"
        any_entry = any(_looks_like_special_entry(name) for name in current_names)
        any_non_special_source = any(
            (_looks_like_common_fallspecial(name) or (not _looks_like_any_special(name) and name != "NONE"))
            for name in current_names
        )
        if (
            _is_common_special_dispatch_source(seed_name)
            and
            any_entry
            and any_non_special_source
            and bool(fields & SPECIAL_ENTRY_DISPATCH_FIELDS)
            and bool(fields & SPECIAL_ENTRY_ACTION_FIELDS)
        ):
            return "F23_special_common_entry_dispatch"

    if any(_looks_like_specialn(name) for name in names):
        return "F19_specialn_blaster_article"
    if any(_looks_like_speciallw(name) for name in names):
        return "F20_speciallw_shine_reflector"
    if any(_looks_like_specials(name) for name in names):
        return "F21_specials_illusion_phantasm"
    if any(_looks_like_firefox(name) for name in names):
        return "F22_specialhi_firefox_firebird"
    if any(_looks_like_common_fallspecial(name) for name in names):
        return "F13a_common_fallspecial_landing"
    return None


def _special_owner_family_for_msid(msid: int | None) -> str | None:
    if msid is None:
        return None
    # Fox/Falco submotion ids from data/special_msids/{fox,falco}.json and
    # refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion.
    if 295 <= int(msid) <= 300:
        return "F19_specialn_blaster_article"
    if 301 <= int(msid) <= 306:
        return "F21_specials_illusion_phantasm"
    if 307 <= int(msid) <= 312:
        return "F22_specialhi_firefox_firebird"
    if 313 <= int(msid) <= 322:
        return "F20_speciallw_shine_reflector"
    return None


def _looks_like_landing_cliff_or_fall(name: str) -> bool:
    return (
        name.startswith("LANDING")
        or name.startswith("CLIFF")
        or name in {"FALL", "OTTOTTO", "OTTOTTO_WAIT"}
    )


def _looks_like_landing_air(name: str) -> bool:
    return name.startswith("LANDING_AIR_")


def _is_common_fall_landing_timebase(names: tuple[str, str, str], field_set: set[str]) -> bool:
    if not set(names) <= {"FALL", "LANDING"}:
        return False
    return bool(field_set & {"action_id", "animation_index", "on_ground"}) and field_set <= {
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "jumps_left",
        "on_ground",
        "ground_id",
    }


def _is_ottotto_teeter_edge_handoff(names: tuple[str, str, str], field_set: set[str]) -> bool:
    if "OTTOTTO" not in names and "OTTOTTO_WAIT" not in names:
        return False
    if not any(name == "FALL" for name in names):
        return False
    if not any(
        name in {"WAIT", "RUN", "RUN_BRAKE", "LANDING"} or _looks_like_landing_air(name)
        for name in names
    ):
        return False
    return bool(field_set & {"action_id", "animation_index", "on_ground", "jumps_left"}) and field_set <= {
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "jumps_left",
        "on_ground",
    }


def _looks_like_grounded_attack(name: str) -> bool:
    return name.startswith("ATTACK_") and not name.startswith("ATTACK_AIR")


def _looks_like_squat(name: str) -> bool:
    return name in {"SQUAT", "SQUAT_WAIT", "SQUAT_RV"}


def _looks_like_escape(name: str) -> bool:
    return name.startswith("ESCAPE_")


def _looks_like_runbrake(name: str) -> bool:
    return name == "RUN_BRAKE"


def _looks_like_turnrun(name: str) -> bool:
    return name == "TURN_RUN"


def _looks_like_appeal(name: str) -> bool:
    return name.startswith("APPEAL_")


def _looks_like_aerial(name: str) -> bool:
    return name.startswith("ATTACK_AIR") or name.startswith("JUMP_") or name.startswith("FX_SPECIAL_AIR_")


def _specials_hard_moved_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    if not any(_looks_like_specials(name) for name in names):
        return None
    if _looks_like_current_frame_body_admission_disagreement(row, names, field_set):
        return "F08f_body_contact_candidate_filter_residual"
    if any(_looks_like_special_air_s(name) for name in names):
        if field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state", "action_frame"}):
            return "F09a_aerial_stateflag_hurtbox_adjacency"
        if field_set & {"last_hit_by", "combo_count", "last_attack_landed"}:
            return "F09b_aerial_bookkeeping_adjacency"
    return None


def _specialhi_hard_moved_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    if not any(_looks_like_firefox(name) for name in names):
        return None
    if any(_looks_like_damage(name) for name in names):
        # Damage* context means this row has left Firefox/Firebird launch/travel ownership; remaining
        # scalar or transition fields are shared damage/locomotion ownership.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
        if field_set & {"hitlag", "hitstun", "facing", "speed_x_attack", "speed_y_attack", "state_flags[1]"}:
            return "F08d_damage_timer_scalar_residual"
        return "F08c_damage_state_transition_adjacency"
    if field_set and field_set <= {"ground_id"}:
        # A pure ground-id mismatch in SpecialHiLanding is shared mpColl floor-line identity, not
        # Firefox launch or Bound ownership.
        # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Coll
        return "F10m_floor_line_identity"
    if (
        any(name == "FX_SPECIAL_AIR_HI" for name in names)
        and any(name == "FX_SPECIAL_HI_BOUND" for name in names)
        and field_set <= {"action_id", "action_frame", "animation_index"}
    ):
        # Remaining SpecialAirHi <-> Bound rows are one-frame collision callback ordering around
        # SpecialAirHi_Coll/Bound_Coll. They carry no Hold/launch travel, velocity, ledge, hitbox,
        # or pose fields, so keep them with shared collision/landing timing ownership.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        #   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Coll}
        # refs/melee/src/melee/mp/mpcoll.c
        return "F10p_specialhi_bound_collision_callback"
    if all(name == "FX_SPECIAL_HI_BOUND" for name in names) and field_set <= {"jumps_left"}:
        # Pure jumps-left drift inside steady Bound is common jump/bookkeeping visibility. Bound
        # entry and anim-end action rows stay runtime-locked separately; no launch/travel fields
        # remain here.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        #   ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Anim}
        return "F09a_aerial_stateflag_hurtbox_adjacency"
    if (
        "KNEE_BEND" in names
        and any(name == "FX_SPECIAL_HI_HOLD" for name in names)
        and field_set <= {"action_id", "animation_index", "instance_id"}
    ):
        # The last grounded KneeBend -> SpecialHiHold row is a common grounded action/dispatch
        # adjacency after DamageAir, not Firefox launch or Bound travel.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_800D68C0
        return "F10a_grounded_selector_transition"
    if all(name == "FX_SPECIAL_HI_HOLD_AIR" for name in names) and field_set <= (
        STATE_FLAG_FIELDS | {"hurtbox_state", "action_frame"}
    ):
        # HoldAir has an empty IASA and these rows have no launch/travel/collision fields; keep
        # them with the shared aerial hurtbox/state-flag owner instead of the Firefox movement owner.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiHoldAir_IASA
        return "F09a_aerial_stateflag_hurtbox_adjacency"
    return None


def _speciallw_hard_moved_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    if not any(_looks_like_speciallw(name) for name in names):
        return None
    if _looks_like_damage(names[1]) and names[0] == names[2] and _looks_like_speciallw(names[0]):
        # A replay Damage* destination while the sim remains in the same SpecialLw state is a missed
        # shared ProcessHit/damage transition. The Shine state machine did not choose a release,
        # turn, Hit, End, ground/air handoff, or reflector callback here; it is merely the defender
        # context when common combat applies damage.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
        return "F08c_damage_state_transition_adjacency"
    if field_set and field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state", "action_frame"}):
        # Pure visible hit-status/state-flag tails in Shine context are x1988/x198C composition
        # or shared state-flag ownership. Keep action/contact/source bundles in F20; move only
        # rows with no reflector state-machine fields left.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B868}
        if any(_looks_like_aerial(name) for name in names):
            return "F09a_aerial_stateflag_hurtbox_adjacency"
        if row.on_ground == 1 or any(_looks_like_grounded(name) for name in names):
            return "F10d_hurtbox_stateflag_adjacency"
    if field_set and field_set <= {"hitlag"}:
        # A lone hitlag scalar with Shine actions already aligned is combat/contact bookkeeping,
        # not SpecialLw release, turn, collision handoff, or reflector state-machine flow.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        if any(_looks_like_aerial(name) for name in names):
            return "F09d_aerial_contact_hitlag_residual"
        return "F10b_grounded_combat_adjacency"
    if (
        field_set
        and field_set <= {"hitlag", "state_flags[1]"}
        and any(_looks_like_aerial(name) for name in names)
    ):
        # Aerial Shine entry rows with action/on-ground already aligned and only hitlag plus the
        # Slippi hitlag bit remaining are shared BODY/contact bookkeeping. Keep action/contact-state
        # bundles in F20; these rows no longer contain a Shine release, turn, Hit/End, or reflector
        # callback disagreement.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        return "F09d_aerial_contact_hitlag_residual"
    if (
        field_set
        and field_set <= {"hitlag", "state_flags[1]", "last_attack_landed", "combo_count", "instance_id"}
        and (row.on_ground == 1 or any(_looks_like_grounded(name) for name in names))
    ):
        # Grounded Shine entry rows whose only remaining fields are contact scalar, Slippi hitlag bit,
        # stale/source scoreboard values, and generic action-instance identity are common combat
        # bookkeeping. The Shine state machine supplied the hitbox, but it did not choose a release,
        # turn, Hit/End transition, or reflector callback on these rows.
        # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_8007BE3C}
        # refs/melee/src/melee/pl/plstale.c::{
        #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
        return "F10b_grounded_combat_adjacency"
    if field_set and field_set <= {"last_hit_by", "combo_count", "last_attack_landed"}:
        # Pure source/scoreboard rows are owned by combat bookkeeping, even when the current
        # action context is Shine. Keep mixed hitlag/contact/action/hurtbox bundles in F20 until
        # their Shine-side callback or reflector-contact owner is proven.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
        # refs/melee/src/melee/pl/plstale.c::{
        #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
        if any(_looks_like_aerial(name) for name in names):
            return "F09b_aerial_bookkeeping_adjacency"
        if row.on_ground == 1 or any(_looks_like_grounded(name) for name in names):
            return "F10b_grounded_combat_adjacency"
    return None


def _specialn_hard_moved_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    if not any(_looks_like_specialn(name) for name in names):
        return None
    if any(name.startswith("FX_SPECIAL_AIR_N") for name in names) and any(_looks_like_damage(name) for name in names):
        if field_set & {"hitlag", "hitstun", "facing", "speed_x_attack", "speed_y_attack", "state_flags[1]"}:
            # SpecialAirNLoop can be the defender context when ordinary BODY damage is applied.
            # Hitlag/facing/KB scalar mismatches on the resulting Damage* row are owned by shared
            # damage/contact resolution, not by SpecialN's blaster article or shot lifecycle.
            # Keep article/item/source/contact bundles with F19 unless the row has entered Damage*.
            # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
            # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
            return "F08d_damage_timer_scalar_residual"
    if any(name.startswith("FX_SPECIAL_AIR_N") for name in names) and field_set <= STATE_FLAG_FIELDS:
        # SpecialAirN Phys delegates to the shared air physics helper; pure fastfall/state-flag
        # tails belong with aerial state-flag ownership, not the blaster article/shot owner.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        #   ftFx_SpecialAirNStart_Phys,ftFx_SpecialAirNLoop_Phys,ftFx_SpecialAirNEnd_Phys}
        # refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
        return "F09a_aerial_stateflag_hurtbox_adjacency"
    if any(name.startswith("FX_SPECIAL_AIR_N") for name in names) and field_set <= {"hurtbox_state"}:
        # Pure visible hurtbox-state tails in aerial SpecialN context are shared x1988/x198C
        # composition, not laser article or loop/end ownership.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        return "F09a_aerial_stateflag_hurtbox_adjacency"
    if field_set and field_set <= {"last_hit_by", "combo_count", "last_attack_landed"}:
        # Pure source/scoreboard rows are shared combat bookkeeping even when SpecialN is the
        # neighboring action context. Article, item, shot, and loop/end action bundles stay in F19.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
        # refs/melee/src/melee/pl/plstale.c::{
        #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
        if any(_looks_like_aerial(name) for name in names):
            return "F09b_aerial_bookkeeping_adjacency"
        return "F10b_grounded_combat_adjacency"
    return None


def _special_instance_callback_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    if field_set != {"instance_id"}:
        return None
    seed_name, ref_name, out_name = names
    if (
        seed_name in {"FX_SPECIAL_N_LOOP", "FX_SPECIAL_AIR_N_LOOP"}
        and seed_name == ref_name
        and ref_name == out_name
        and row.ref_action_frame == 0
        and row.seed_action_frame > 0
    ):
        # SpecialN Loop -> same Loop restart is a real special callback owner:
        # the Loop Anim callback installs ftFx_SpecialN_OnChangeAction, which calls ft_80089824.
        # Direct special-boundary entries/exits do not install that callback and remain generic
        # ft_800895E0/plAttack adjacent instance rows.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        #   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialN_OnChangeAction}
        # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
        return "F24_special_adjacent_instance_order"
    return None


def _common_fallspecial_hard_moved_family(
    row: PlayerRow, names: tuple[str, str, str], field_set: set[str]
) -> str | None:
    if not any(_looks_like_common_fallspecial(name) for name in names):
        return None
    if any(_looks_like_damage(name) for name in names):
        # LandingFallSpecial/EscapeAir can be the neighboring context when ordinary BODY damage
        # resolves or is missed. Once a Damage* state is present, scalar/contact/identity fields are
        # shared damage/combat ownership rather than common FallSpecial collision ownership.
        # Keep non-damage EscapeAir/LandingFallSpecial collision bundles in F13a.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
        if field_set <= {"instance_id", "instance_hit_by", "last_hit_by"}:
            return "F08a_damage_identity_bookkeeping_residual"
        if field_set & {"hitlag", "hitstun", "facing", "speed_x_attack", "speed_y_attack", "state_flags[1]"}:
            return "F08d_damage_timer_scalar_residual"
        return "F08c_damage_state_transition_adjacency"
    if field_set and field_set <= {"ground_id"}:
        # A lone floor id mismatch in LandingFallSpecial/EscapeAir context is the shared mpColl
        # floor-line owner. Rows with action/on_ground/jump bundles remain in F13a because they are
        # common FallSpecial landing timing.
        # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        return "F10m_floor_line_identity"
    if field_set == {"action_frame"} and all(name == "LANDING_FALL_SPECIAL" for name in names):
        # Pure LandingFallSpecial action-frame drift is common landing-lag/timebase ownership, not
        # Fox/Falco special state-machine ownership.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic
        # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        return "F13a_common_fallspecial_landing"
    if field_set and field_set <= {
        "action_id",
        "action_frame",
        "animation_index",
        "ground_id",
        "instance_id",
        "jumps_left",
        "on_ground",
        "state_flags[1]",
    } and bool(field_set & {"action_id", "animation_index", "ground_id", "jumps_left", "on_ground"}):
        # The remaining non-damage FallSpecial/LandingFallSpecial/EscapeAir rows are common landing
        # and collision timing: action/on-ground/jump/ground-id bundles around ftCo_FallSpecial_Coll,
        # ftCo_LandingFallSpecial_Enter, and ftCo_EscapeAir_Coll. They are not ledge-family
        # CliffCatch/edge-suppression rows and they are not Fox/Falco special move state-machine
        # rows; keep them with the existing common FallSpecial/LandingFallSpecial owner.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
        #   ftCo_FallSpecial_Coll,ftCo_80096D28}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80083090}
        return "F13a_common_fallspecial_landing"
    if field_set and field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state"}):
        # Pure state-flag/hurtbox tails are shared visible-state composition; keep action/collision
        # bundles in the common FallSpecial owner.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        if row.on_ground == 0 or any(_looks_like_aerial(name) for name in names):
            return "F09a_aerial_stateflag_hurtbox_adjacency"
        return "F10d_hurtbox_stateflag_adjacency"
    if field_set and field_set <= {"last_hit_by", "combo_count", "last_attack_landed"}:
        # Pure source/scoreboard rows in LandingFallSpecial/FallSpecial/EscapeAir contexts are combat
        # bookkeeping. Keep collision, landing, action, hurtbox, and mixed contact bundles in F13a.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
        # refs/melee/src/melee/pl/plstale.c::{
        #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
        if row.on_ground == 0 or any(_looks_like_aerial(name) for name in names):
            return "F09b_aerial_bookkeeping_adjacency"
        return "F10b_grounded_combat_adjacency"
    return None


def _throw_hard_moved_family(row: PlayerRow, names: tuple[str, str, str], field_set: set[str]) -> str | None:
    _ = row
    if not any(_looks_like_throw_or_thrown(name) for name in names):
        return None
    if field_set and field_set <= {
        "hitlag",
        "state_flags[1]",
        "state_flags[3]",
        "hurtbox_state",
        "instance_hit_by",
        "last_hit_by",
        "combo_count",
        "last_attack_landed",
    }:
        # Action-aligned Throw*/Thrown* rows whose only residual fields are hitlag, Slippi contact
        # bits, hurtbox visibility, or source/scoreboard ids share the common throw/item bookkeeping
        # owner. Keep action/article/lifetime bundles out of this hard move; the rejected ThrowLw
        # current-pulse bridge is not restored.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
        #   ftCo_ThrowHi_Anim,ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_8007BE3C}
        # refs/melee/src/melee/pl/plstale.c::{
        #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
        return "F14d_throw_source_scoreboard"
    return None


def _looks_like_grounded_selector(name: str) -> bool:
    return (
        name.startswith("WALK_")
        or name.startswith("LANDING_AIR_")
        or name
        in {
            "WAIT",
            "DASH",
            "TURN",
            "TURN_RUN",
            "RUN",
            "RUN_DIRECT",
            "RUN_BRAKE",
            "KNEE_BEND",
            "SQUAT",
            "SQUAT_WAIT",
            "SQUAT_RV",
            "LANDING",
            "LANDING_FALL_SPECIAL",
            "OTTOTTO",
            "OTTOTTO_WAIT",
        }
    )


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
    prev_name = _action_name(action_names, row.prev_action_id)
    names = (seed_name, ref_name, out_name)
    context_names = (seed_name, ref_name, out_name, prev_name)
    field_set = {_base_field(field) for field in row.fields}

    if field_set == {"state_flags[4]"}:
        return "F25_camera_box_visibility_x221f"
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
    specials_hard_move = _specials_hard_moved_family(row, names, field_set)
    if specials_hard_move is not None:
        return specials_hard_move
    specialhi_hard_move = _specialhi_hard_moved_family(row, names, field_set)
    if specialhi_hard_move is not None:
        return specialhi_hard_move
    specialn_hard_move = _specialn_hard_moved_family(row, names, field_set)
    if specialn_hard_move is not None:
        return specialn_hard_move
    special_instance_callback = _special_instance_callback_family(row, names, field_set)
    if special_instance_callback is not None:
        return special_instance_callback
    speciallw_hard_move = _speciallw_hard_moved_family(row, names, field_set)
    if speciallw_hard_move is not None:
        return speciallw_hard_move
    common_fallspecial_hard_move = _common_fallspecial_hard_moved_family(row, names, field_set)
    if common_fallspecial_hard_move is not None:
        return common_fallspecial_hard_move
    throw_hard_move = _throw_hard_moved_family(row, names, field_set)
    if throw_hard_move is not None:
        return throw_hard_move
    if any(_looks_like_damage(name) for name in names):
        if field_set == {"ground_id"}:
            # Pure floor.index visibility in Damage* rows is the floor-line identity owner, not
            # ledge occupancy/ECB admission. Action, ground/air, jump, and hurtbox fields agree.
            # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            # refs/melee/src/melee/lb/types.h::CollData
            return "F10m_floor_line_identity"
        if field_set and field_set <= STATE_FLAG_FIELDS:
            return "F05_damage_state_flags"
        if field_set and field_set <= {"hurtbox_state"} and any(
            _looks_like_passive_wall(name) for name in names
        ):
            # Pure PassiveWall* hit-status visibility is shared visible-state composition. Keep
            # action/on-ground/jump bundles in F17 where the wall-contact callback itself differs.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c
            # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            return "F09a_aerial_stateflag_hurtbox_adjacency"
        if field_set <= {"instance_id", "instance_hit_by", "last_hit_by"}:
            return "F08a_damage_identity_bookkeeping_residual"
        if _is_damage_tech_timer_seed_surface(row, names):
            return "F18_damage_tech_timer_seed_surface"
        if (
            any(_looks_like_damagefly(name) for name in names)
            and all(_looks_like_knockdown_contact_destination(name) for name in names)
            and _is_knockdown_selector_disagreement(names)
        ):
            return "F07_knockdown_grounding"
        if _is_mpcoll_ledge_ecb_residual(row, names, field_set):
            return "F17_mpcoll_ledge_ecb_residual"
        special_family = _special_owner_family_for_names(context_names, field_set=field_set)
        if special_family is not None:
            return special_family
        if _looks_like_current_frame_body_admission_disagreement(row, names, field_set):
            return "F08b_body_contact_geometry_residual"
        if any(_looks_like_landing_cliff_or_fall(name) for name in names) or any(
            name in {"DASH", "TURN", "KNEE_BEND", "WAIT", "WALK_SLOW"} for name in names
        ):
            return "F08c_damage_state_transition_adjacency"
        if any(name.startswith("DOWN_") or name.startswith("PASSIVE") for name in names):
            return "F08c_damage_state_transition_adjacency"
        if field_set & {"action_id", "animation_index", "on_ground", "ground_id", "hurtbox_state"}:
            return "F08c_damage_state_transition_adjacency"
        if field_set & {"hitlag", "hitstun", "action_frame", "combo_count", "last_attack_landed", "state_flags[1]"}:
            return "F08d_damage_timer_scalar_residual"
        return "F08a_damage_identity_bookkeeping_residual"
    if field_set == {"instance_id"}:
        special_family = _special_instance_callback_family(row, names, field_set)
        if special_family is not None:
            return special_family
        if any(_looks_like_grounded_attack(name) for name in context_names):
            return "F12c_attack_instance_counter_order"
        if any(
            _looks_like_squat(name) or _looks_like_escape(name) or _looks_like_runbrake(name)
            for name in context_names
        ):
            return "F12d_squat_escape_instance_counter_order"
        if any(
            _looks_like_any_special(name)
            or _looks_like_landing_cliff_or_fall(name)
            or _looks_like_aerial(name)
            or _looks_like_damage(name)
            for name in context_names
        ):
            return "F12b_adjacent_instance_counter_order"
        if any(_looks_like_grounded_selector(name) for name in names):
            return "F12a_grounded_instance_counter_order"
        return "F12_instance_id_transition_only"
    if field_set == {"action_frame"} and any(_looks_like_grounded(name) for name in names):
        return "F11_locomotion_action_frame"
    if _looks_like_any_special(prev_name) and not any(_looks_like_any_special(name) for name in names):
        if field_set and field_set <= {"last_hit_by", "combo_count", "last_attack_landed"}:
            # Once the current row is back in generic grounded/aerial state, a previous SpecialN or
            # Shine neighbor is just source-bookkeeping context; it is not the article/reflector
            # state-machine owner. Keep active special current-action bundles in their special family.
            # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
            # refs/melee/src/melee/pl/plstale.c::{
            #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
            if any(_looks_like_aerial(name) for name in names) or row.on_ground == 0:
                return "F09b_aerial_bookkeeping_adjacency"
            return "F10b_grounded_combat_adjacency"
        if any(_looks_like_landing_cliff_or_fall(name) for name in names):
            # A previous special can explain why the player reached Landing, but the observed
            # Landing/Ottotto/Fall disagreement itself is the shared collision/edge owner.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c
            # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
            if _is_ottotto_teeter_edge_handoff(names, field_set):
                return "F10o_ottotto_teeter_edge_handoff"
            return "F10c_collision_landing_edge_adjacency"
    special_family = _special_owner_family_for_names(context_names, field_set=field_set)
    if special_family is not None:
        return special_family
    if any(_looks_like_aerial(name) for name in names):
        if field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state", "action_frame"}):
            return "F09a_aerial_stateflag_hurtbox_adjacency"
        if field_set & {"last_hit_by", "combo_count", "last_attack_landed"}:
            return "F09b_aerial_bookkeeping_adjacency"
        if field_set & {"hitlag", "hitstun", "state_flags[1]"} and any(
            name.startswith("ATTACK_AIR") for name in names
        ):
            return "F09d_aerial_contact_hitlag_residual"
        if field_set & {
            "action_id",
            "animation_index",
            "instance_id",
            "jumps_left",
            "on_ground",
            "l_cancel",
            "state_flags[0]",
        } or any(name in {"KNEE_BEND", "JUMP_F", "JUMP_B", "CLIFF_JUMP_QUICK2"} for name in names):
            return "F09c_aerial_action_entry_adjacency"
        return "F09e_aerial_instance_timing_residual"
    if any(_looks_like_grounded(name) for name in names):
        if field_set == {"ground_id"}:
            # Lone `ground_id` rows are CollData.floor.index visibility across connected FD floor
            # seams. Keep action/on_ground/jump edge handoff bundles in their action owners.
            # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
            # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            return "F10m_floor_line_identity"
        if any(_looks_like_appeal(name) for name in names):
            return "F10h_appeal_adjacency"
        if any(_looks_like_turnrun(name) for name in names):
            return "F10j_turnrun_exit_microphase"
        if (
            "TURN" in names
            and (field_set == {"facing"} or ("DASH" in names and bool(field_set & {"action_id", "animation_index"})))
        ):
            return "F10i_turn_hidden_microphase"
        if any(_looks_like_runbrake(name) for name in names):
            return "F10g_runbrake_adjacency"
        if any(_looks_like_grounded_attack(name) for name in names):
            return "F10f_grounded_attack_adjacency"
        if field_set & {
            "hitlag",
            "hitstun",
            "combo_count",
            "last_attack_landed",
            "last_hit_by",
            "instance_hit_by",
            "state_flags[1]",
        }:
            return "F10b_grounded_combat_adjacency"
        if field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state"}):
            return "F10d_hurtbox_stateflag_adjacency"
        if _is_common_fall_landing_timebase(names, field_set):
            # Common Fall_Coll / Landing_Enter one-frame callback timing. These rows have no ledge
            # grab, CliffCatch, or floor-line identity fields beyond the ordinary landing bundle.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
            return "F10n_common_fall_landing_timebase"
        if _is_ottotto_teeter_edge_handoff(names, field_set):
            # Common teeter entry vs Fall handoff through ftCo_8009A3C8. This is not ledge
            # occupancy/CliffCatch ownership and is kept out of the generic collision bucket.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
            # refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
            return "F10o_ottotto_teeter_edge_handoff"
        if (
            any(_looks_like_landing_cliff_or_fall(name) for name in names)
            and field_set <= {"action_id", "action_frame", "animation_index", "instance_id"}
            and bool(field_set & {"action_id", "animation_index", "action_frame"})
        ):
            # Landing/Ottotto action-only tails are grounded IASA/Anim selector ordering. Keep
            # on_ground/jumps/ground_id edge handoff bundles in F10c.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
            #   ftCo_Ottotto_Anim,ftCo_Ottotto_IASA}
            return "F10a_grounded_selector_transition"
        if any(_looks_like_landing_cliff_or_fall(name) for name in names) or field_set & {
            "ground_id",
            "on_ground",
            "jumps_left",
        }:
            return "F10c_collision_landing_edge_adjacency"
        return "F10a_grounded_selector_transition"
    if any(_looks_like_throw_or_thrown(name) for name in context_names):
        return "F14b_per_throw_pulse_bookkeeping"
    if any(_looks_like_landing_cliff_or_fall(name) for name in context_names):
        if field_set == {"ground_id"}:
            return "F10m_floor_line_identity"
        if field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state"}):
            return "F10d_hurtbox_stateflag_adjacency"
        if field_set and field_set <= {"last_hit_by", "combo_count", "last_attack_landed"}:
            if row.on_ground == 0 or any(_looks_like_aerial(name) for name in names):
                return "F09b_aerial_bookkeeping_adjacency"
            return "F10b_grounded_combat_adjacency"
        if field_set == {"action_frame"}:
            return "F11_locomotion_action_frame"
        if any(name.startswith("CLIFF") for name in context_names):
            return "F17_mpcoll_ledge_ecb_residual"
        return "F10c_collision_landing_edge_adjacency"
    if any(_looks_like_turnrun(name) for name in context_names):
        return "F10j_turnrun_exit_microphase"
    return "F99_misc_other"


def _classify_item_slot_row(row: ItemSlotRow, action_names: dict[int, str]) -> str:
    names = [
        _action_name(action_names, action_id)
        for triplet in (row.player_actions, row.ref_actions, row.out_actions)
        for action_id in triplet
    ]
    context_names = [
        _action_name(action_names, action_id)
        for triplet in (row.player_actions, row.ref_actions, row.out_actions, row.prev_actions)
        for action_id in triplet
    ]
    item_types = {int(row.seed_item_type), int(row.ref_item_type), int(row.out_item_type)}
    field_set = _row_fields(row)
    if (
        item_types & {74, 75}
        and any(name.startswith("REBIRTH") for name in names)
        and any(_looks_like_specialn(name) for name in names)
        and field_set
        & {"item_exists", "item_type", "item_owner", "item_state"}
    ):
        return "F04_match_flow_rebirth"
    if (
        item_types & {74, 75}
        and any(_looks_like_aerial(name) for name in names)
        and any(_looks_like_damage(name) for name in names)
        and field_set
        & {"item_exists", "item_type", "item_owner", "item_state"}
    ):
        return "F09c_aerial_action_entry_adjacency"
    if (
        item_types & {54, 55, 74, 75}
        and any(_looks_like_specialn(name) for name in context_names)
        and any(_looks_like_landing_cliff_or_fall(name) for name in names)
        and field_set
        & {"item_exists", "item_type", "item_owner", "item_state", "item_instance_id"}
    ):
        # SpecialN Loop/AirLoop -> Landing can reshuffle the blaster gun and shot slots during the
        # action-entry handoff. Rows like HIS:6603/6604 and PJO:2235 have gun/shot identity echoes,
        # not an independent item BODY consume-vs-persist owner.
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        #   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialNEnd_Anim}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
        return "F09c_aerial_action_entry_adjacency"
    if any(name.startswith("THROW_") or name.startswith("THROWN_") or _looks_like_capture(name) for name in names):
        return "F14c_throw_article_lifetime"
    if field_set and field_set <= {"item_instance_id"} and item_types & {74, 75}:
        # Blaster gun `item.instance_id` is Slippi's item->xDA8_short. For fighter-parent spawns,
        # the generic item spawn path copies the owner's fp->x2088 into xDA8, so pure gun xDA8
        # rows are adjacent fighter instance-counter order, not item lifetime/ownership. Keep this
        # before guard routing so pure Guard/GuardReflect gun xDA8 echoes do not masquerade as
        # reflect owner-transfer rows.
        # refs/melee/src/melee/it/it_2725.c::it_8027B070
        # refs/slippi-ssbm-asm/Recording/SendItemInfo.s
        return "F12b_adjacent_instance_counter_order"
    if any(_looks_like_guard(name) for name in names):
        if field_set and field_set <= {"item_owner", "item_instance_id"}:
            return "F15a_reflect_owner_transfer"
        return "F15b_guard_laser_lifetime"
    if field_set and field_set <= {"item_instance_id"} and item_types & {54, 55}:
        return "F16d_item_body_lifetime"
    if item_types & {74, 75}:
        return "F16b_blaster_article_identity"
    if item_types & {56, 57}:
        return "F16c_illusion_phantasm_lifetime"
    if field_set and field_set <= {"item_instance_id"}:
        return "F16a_item_slot_compaction_identity"
    return "F16d_item_body_lifetime"


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
    if row.family_id == "F25_camera_box_visibility_x221f":
        ok = field_set == {"state_flags[4]"}
        return ok, "camera-box visibility row reduced to fp+0x221F byte"
    if row.family_id == "F06_damageflyroll_rng_gate":
        ok = (
            row.ref_action_id == 91
            or row.out_action_id == 91
            or row.seed_action_id == 91
            or (row.seed_action_id == 90 and row.ref_action_id == 91)
        )
        return ok, "DamageFlyRoll admission/gate context"
    if row.family_id == "F07_knockdown_grounding":
        ok = (
            any(_looks_like_damagefly(name) for name in names)
            and all(_looks_like_knockdown_contact_destination(name) for name in names)
            and _is_knockdown_selector_disagreement(names)
            and bool(field_set & {"on_ground", "ground_id", "jumps_left", "hurtbox_state"})
        )
        return ok, "DamageFly contact row with grounded follow-up selector disagreement"
    if row.family_id == "F08_damage_resolution_combat":
        ok = any(_looks_like_damage(name) for name in names) and bool(
            field_set & {"action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by", "instance_id"}
        )
        return ok, "damage family with combat followup fields"
    if row.family_id == "F08a_damage_identity_bookkeeping_residual":
        ok = any(_looks_like_damage(name) for name in names) and field_set <= {
            "instance_id",
            "instance_hit_by",
            "last_hit_by",
        }
        return ok, "damage row reduced to identity/source bookkeeping"
    if row.family_id == "F08b_body_contact_geometry_residual":
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "current-frame BODY admission geometry row"
    if row.family_id == "F08e_body_contact_no_candidate_adjacency":
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "debug pre-combat BODY selector had no candidate for this admission-shaped row"
    if row.family_id == "F08f_body_contact_candidate_filter_residual":
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "debug pre-combat BODY candidate exists but admission/filtering differs"
    if row.family_id == "F05b_damage_hurt_height_selection_residual":
        ok = (
            any(_looks_like_damage(name) for name in names)
            and _damage_admission_outcome(names[1], names[2]) == "wrong_damage_selection"
        )
        return ok, "BODY was admitted but damage-state selection differs"
    if row.family_id in {
        "F08h_body_selected_false_grounded_attack_pose",
        "F08i_body_selected_false_aerial_attack_pose",
        "F08h1_body_selected_false_rebound_clank_residual",
        "F08h2_body_selected_false_grounded_timebase_residual",
        "F08i1_body_selected_false_aerial_timebase_residual",
        "F08j_body_selected_false_special_entry_pose",
    }:
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "debug pre-combat BODY selector selected a false-positive primitive"
    if row.family_id == "F08c_damage_state_transition_adjacency":
        ok = any(_looks_like_damage(name) for name in names) and (
            any(_looks_like_landing_cliff_or_fall(name) for name in names)
            or any(name in {"DASH", "TURN", "KNEE_BEND", "WAIT", "WALK_SLOW"} for name in names)
            or any(name.startswith("DOWN_") or name.startswith("PASSIVE") for name in names)
        )
        return ok, "damage row with transition/landing/Down/Passive adjacency"
    if row.family_id == "F08d_damage_timer_scalar_residual":
        ok = any(_looks_like_damage(name) for name in names) and bool(
            field_set & {"hitlag", "hitstun", "action_frame", "combo_count", "last_attack_landed", "state_flags[1]"}
        )
        return ok, "damage row with accepted-hit scalar/timer residual fields"
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
    if row.family_id == "F09a_aerial_stateflag_hurtbox_adjacency":
        ok = any(_looks_like_aerial(name) for name in names) and field_set <= (
            STATE_FLAG_FIELDS | {"hurtbox_state", "action_frame"}
        )
        return ok, "aerial/jump row with state-flag or hurtbox-only residual"
    if row.family_id == "F09b_aerial_bookkeeping_adjacency":
        ok = any(_looks_like_aerial(name) for name in names) and bool(
            field_set & {"last_hit_by", "combo_count", "last_attack_landed", "hitlag"}
        )
        return ok, "aerial/jump row with source/combo bookkeeping residual"
    if row.family_id == "F09c_aerial_action_entry_adjacency":
        ok = any(_looks_like_aerial(name) for name in names) and (
            bool(
                field_set
                & {
                    "action_id",
                    "animation_index",
                    "instance_id",
                    "jumps_left",
                    "on_ground",
                    "l_cancel",
                    "state_flags[0]",
                }
            )
            or any(name in {"KNEE_BEND", "JUMP_F", "JUMP_B", "CLIFF_JUMP_QUICK2"} for name in names)
        )
        return ok, "aerial/jump row with action-entry or input-selection residual"
    if row.family_id == "F09d_aerial_contact_hitlag_residual":
        ok = (
            any(name.startswith("ATTACK_AIR") for name in names)
            and bool(field_set & {"hitlag", "hitstun", "state_flags[1]"})
            and not any(_looks_like_damage(name) for name in names)
        )
        return ok, "AttackAir row with contact hitlag/state-flag residual and no damage state"
    if row.family_id == "F09e_aerial_instance_timing_residual":
        ok = any(_looks_like_aerial(name) for name in names)
        return ok, "aerial/jump residual after finer F09 splits"
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
    if row.family_id == "F10a_grounded_selector_transition":
        ok = any(_looks_like_grounded_selector(name) or name.startswith("ATTACK_") for name in names) and bool(
            field_set & {"action_id", "animation_index", "instance_id", "action_frame"}
        )
        return ok, "true grounded selector transition row"
    if row.family_id == "F10b_grounded_combat_adjacency":
        ok = bool(
            field_set
            & {
                "hitlag",
                "hitstun",
                "combo_count",
                "last_attack_landed",
                "last_hit_by",
                "instance_hit_by",
                "state_flags[1]",
            }
        )
        return ok, "grounded-name row with combat-owned fields"
    if row.family_id == "F10c_collision_landing_edge_adjacency":
        ok = any(_looks_like_landing_cliff_or_fall(name) for name in names) or bool(
            field_set & {"ground_id", "on_ground", "jumps_left"}
        )
        return ok, "grounded-name row with collision/landing/edge fields"
    if row.family_id == "F10m_floor_line_identity":
        ok = field_set == {"ground_id"}
        return ok, "pure CollData.floor.index visibility row"
    if row.family_id == "F10n_common_fall_landing_timebase":
        ok = _is_common_fall_landing_timebase(names, set(field_set))
        return ok, "common Fall/Landing callback timebase row"
    if row.family_id == "F10o_ottotto_teeter_edge_handoff":
        ok = _is_ottotto_teeter_edge_handoff(names, set(field_set))
        return ok, "common Ottotto teeter edge handoff row"
    if row.family_id == "F10p_specialhi_bound_collision_callback":
        ok = (
            any(name == "FX_SPECIAL_AIR_HI" for name in names)
            and any(name == "FX_SPECIAL_HI_BOUND" for name in names)
            and field_set <= {"action_id", "action_frame", "animation_index"}
        )
        return ok, "SpecialAirHi/Bound collision callback timing row"
    if row.family_id == "F10d_hurtbox_stateflag_adjacency":
        ok = field_set <= (STATE_FLAG_FIELDS | {"hurtbox_state"})
        return ok, "grounded-name row with hurtbox/state-flag-only fields"
    if row.family_id == "F10e_special_move_adjacency":
        ok = any(_looks_like_any_special(name) for name in names)
        return ok, "grounded-name row with special-move entry/exit context"
    if row.family_id in {
        "F19_specialn_blaster_article",
        "F20_speciallw_shine_reflector",
        "F21_specials_illusion_phantasm",
        "F22_specialhi_firefox_firebird",
        "F23_special_common_entry_dispatch",
        "F24_special_adjacent_instance_order",
        "F13a_common_fallspecial_landing",
    }:
        context_names = (
            names[0],
            names[1],
            names[2],
            _action_name(action_names, row.prev_action_id),
        )
        if row.family_id == "F19_specialn_blaster_article":
            ok = any(_looks_like_specialn(name) for name in context_names)
            return ok, "SpecialN / blaster action or article-adjacent row"
        if row.family_id == "F20_speciallw_shine_reflector":
            ok = any(_looks_like_speciallw(name) for name in context_names)
            return ok, "SpecialLw / shine reflector action row"
        if row.family_id == "F21_specials_illusion_phantasm":
            ok = any(_looks_like_specials(name) for name in context_names)
            return ok, "SpecialS / Illusion-Phantasm action row"
        if row.family_id == "F22_specialhi_firefox_firebird":
            ok = any(_looks_like_firefox(name) for name in context_names)
            return ok, "SpecialHi / Firefox-Firebird action row"
        if row.family_id == "F23_special_common_entry_dispatch":
            ok = any(_looks_like_special_entry(name) for name in context_names)
            return ok, "common IASA/input special-entry dispatch row"
        if row.family_id == "F24_special_adjacent_instance_order":
            ok = _special_instance_callback_family(row, names, field_set) == row.family_id
            return ok, "source-backed special callback instance-id counter row"
        ok = any(_looks_like_common_fallspecial(name) for name in context_names)
        return ok, "common FallSpecial/LandingFallSpecial row"
    if row.family_id == "F10f_grounded_attack_adjacency":
        ok = any(_looks_like_grounded_attack(name) for name in names)
        return ok, "grounded-name row with attack-state transition context"
    if row.family_id == "F10g_runbrake_adjacency":
        ok = any(_looks_like_runbrake(name) for name in names)
        return ok, "grounded-name row with RunBrake/Run exit context"
    if row.family_id == "F10h_appeal_adjacency":
        ok = any(_looks_like_appeal(name) for name in names)
        return ok, "grounded-name row with Appeal/Taunt command context"
    if row.family_id == "F10i_turn_hidden_microphase":
        ok = "TURN" in names and (
            field_set == {"facing"} or ("DASH" in names and bool(field_set & {"action_id", "animation_index"}))
        )
        return ok, "Turn hidden has_turned/just_turned/facing microphase"
    if row.family_id == "F10j_turnrun_exit_microphase":
        ok = any(_looks_like_turnrun(name) for name in names)
        return ok, "TurnRun anim-end Run/Wait exit microphase"
    if row.family_id == "F10k_body_no_candidate_action_timing":
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "debug pre-combat BODY selector had no candidate; action/hitbox timing owner"
    if row.family_id == "F10l_body_selected_false_action_timebase":
        ok = _looks_like_current_frame_body_admission_disagreement(row, names, set(field_set))
        return ok, "debug pre-combat BODY selector selected a false primitive after action-timebase divergence"
    if row.family_id == "F11_locomotion_action_frame":
        ok = field_set == {"action_frame"} and any(_looks_like_grounded(name) for name in names)
        return ok, "grounded callback/timebase action_frame-only row"
    if row.family_id == "F12_instance_id_transition_only":
        ok = field_set == {"instance_id"}
        return ok, "instance_id-only motion entry residual"
    if row.family_id == "F12a_grounded_instance_counter_order":
        ok = field_set == {"instance_id"} and any(_looks_like_grounded_selector(name) for name in names)
        return ok, "grounded selector instance-id counter-order row"
    if row.family_id == "F12b_adjacent_instance_counter_order":
        ok = field_set == {"instance_id"} and any(
            _looks_like_any_special(name)
            or _looks_like_landing_cliff_or_fall(name)
            or _looks_like_aerial(name)
            or _looks_like_damage(name)
            for name in names
        )
        return ok, "adjacent-family instance-id counter-order row"
    if row.family_id == "F12c_attack_instance_counter_order":
        ok = field_set == {"instance_id"} and any(_looks_like_grounded_attack(name) for name in names)
        return ok, "attack-state instance-id counter-order row"
    if row.family_id == "F12d_squat_escape_instance_counter_order":
        ok = field_set == {"instance_id"} and any(
            _looks_like_squat(name) or _looks_like_escape(name) or _looks_like_runbrake(name) for name in names
        )
        return ok, "squat/escape instance-id counter-order row"
    if row.family_id == "F12e_cross_player_instance_counter_order":
        ok = field_set == {"instance_id"} and any(_looks_like_grounded_selector(name) for name in names)
        return ok, "cross-player global instance counter-order row"
    if row.family_id == "F13_specialhi_landing":
        ok = any(_looks_like_special_hi(name) for name in names) and bool(
            field_set
            & {"jumps_left", "on_ground", "ground_id", "action_id", "animation_index", "instance_id", "hurtbox_state", "state_flags[4]"}
        )
        return ok, "SpecialHi/FallSpecial landing continuation row"
    if row.family_id == "F14b_per_throw_pulse_bookkeeping":
        ok = any(_looks_like_throw_or_thrown(name) for name in names)
        return ok, "per-throw pulse/bookkeeping residual row"
    if row.family_id == "F14d_throw_source_scoreboard":
        ok = any(_looks_like_throw_or_thrown(name) for name in names)
        return ok, "throw item source/scoreboard residual row"
    if row.family_id == "F17_mpcoll_ledge_ecb_residual":
        ok = any(_looks_like_damage(name) for name in names) and bool(
            field_set & {"on_ground", "ground_id", "jumps_left", "hurtbox_state"}
        )
        return ok, "damage-adjacent row with collision/ledge/ECB contact residual fields"
    if row.family_id == "F18_damage_tech_timer_seed_surface":
        ok = _is_damage_tech_timer_seed_surface(row, names) and bool(
            field_set & {"action_id", "animation_index", "hurtbox_state", "state_flags[3]"}
        )
        return ok, "grounded DamageFly contact row blocked on tech-timer seed provenance"
    if row.family_id == "F99_misc_other":
        ok = any(
            ("SPECIAL_LW" in name) or (name == "THROWN_LW") or (name == "TURN_RUN")
            for name in names
        ) or bool(field_set & {"hurtbox_state", "state_flags[4]"})
        return ok, "mixed residual head is SpecialLw/ThrownLw/TurnRun or hurtbox-state residual"
    return False, "unhandled player family"


def _audit_item_row(row: ItemSlotRow, action_names: dict[int, str]) -> tuple[bool, str]:
    names = _item_names(row, action_names)
    if row.family_id in {"F14_throw_item_bookkeeping", "F14c_throw_article_lifetime"}:
        ok = any(name.startswith("THROW_") or name.startswith("THROWN_") or _looks_like_capture(name) for name in names)
        return ok, "throw/capture article lifetime item row"
    if row.family_id in {"F15_guard_item_ownership", "F15a_reflect_owner_transfer", "F15b_guard_laser_lifetime"}:
        ok = any(_looks_like_guard(name) for name in names)
        return ok, "guard/reflect context item row"
    if row.family_id in {
        "F16_item_identity_residual",
        "F16a_item_slot_compaction_identity",
        "F16b_blaster_article_identity",
        "F16c_illusion_phantasm_lifetime",
        "F16d_item_body_lifetime",
    }:
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


def _split_cross_player_instance_counter_rows(
    all_events: list[MismatchEvent],
    player_rows: dict[tuple[str, int, int], PlayerRow],
) -> tuple[list[MismatchEvent], dict[tuple[str, int, int], PlayerRow]]:
    cross_player_keys: set[tuple[str, int, int]] = set()
    rows_by_record: dict[tuple[str, int], list[PlayerRow]] = defaultdict(list)
    for row in player_rows.values():
        rows_by_record[(row.dataset, row.record)].append(row)

    adjacent_families = {
        "F07_knockdown_grounding",
        "F08_damage_resolution_combat",
        "F08a_damage_identity_bookkeeping_residual",
        "F08b_body_contact_geometry_residual",
        "F08c_damage_state_transition_adjacency",
        "F08d_damage_timer_scalar_residual",
        "F08e_body_contact_no_candidate_adjacency",
        "F08f_body_contact_candidate_filter_residual",
        "F05b_damage_hurt_height_selection_residual",
        "F08h_body_selected_false_grounded_attack_pose",
        "F08h1_body_selected_false_rebound_clank_residual",
        "F08h2_body_selected_false_grounded_timebase_residual",
        "F08i_body_selected_false_aerial_attack_pose",
        "F08i1_body_selected_false_aerial_timebase_residual",
        "F08j_body_selected_false_special_entry_pose",
        "F09_aerial_combat_resolution",
        "F09a_aerial_stateflag_hurtbox_adjacency",
        "F09b_aerial_bookkeeping_adjacency",
        "F09c_aerial_action_entry_adjacency",
        "F09d_aerial_contact_hitlag_residual",
        "F09e_aerial_instance_timing_residual",
        "F17_mpcoll_ledge_ecb_residual",
        "F18_damage_tech_timer_seed_surface",
        "F10b_grounded_combat_adjacency",
        "F10c_collision_landing_edge_adjacency",
        "F10d_hurtbox_stateflag_adjacency",
        "F10e_special_move_adjacency",
        "F19_specialn_blaster_article",
        "F20_speciallw_shine_reflector",
        "F21_specials_illusion_phantasm",
        "F22_specialhi_firefox_firebird",
        "F23_special_common_entry_dispatch",
        "F24_special_adjacent_instance_order",
        "F10f_grounded_attack_adjacency",
        "F10g_runbrake_adjacency",
        "F10h_appeal_adjacency",
        "F10i_turn_hidden_microphase",
        "F10j_turnrun_exit_microphase",
        "F12b_adjacent_instance_counter_order",
        "F12c_attack_instance_counter_order",
        "F12d_squat_escape_instance_counter_order",
        "F13_specialhi_landing",
        "F13a_common_fallspecial_landing",
        "F14_throw_item_bookkeeping",
        "F14b_per_throw_pulse_bookkeeping",
        "F15_guard_item_ownership",
    }
    for key, row in player_rows.items():
        if row.family_id != "F12a_grounded_instance_counter_order":
            continue
        peers = [peer for peer in rows_by_record[(row.dataset, row.record)] if peer.p != row.p]
        if any(peer.family_id in adjacent_families for peer in peers):
            cross_player_keys.add(key)

    if not cross_player_keys:
        return all_events, player_rows

    new_player_rows: dict[tuple[str, int, int], PlayerRow] = {}
    for key, row in player_rows.items():
        if key in cross_player_keys:
            row = PlayerRow(**{**asdict(row), "family_id": "F12e_cross_player_instance_counter_order"})
        new_player_rows[key] = row

    new_events: list[MismatchEvent] = []
    for ev in all_events:
        if ev.subject.startswith("p"):
            try:
                p = int(ev.subject[1:])
            except ValueError:
                p = -1
            if (ev.dataset, ev.record, p) in cross_player_keys:
                ev = MismatchEvent(**{**asdict(ev), "family_id": "F12e_cross_player_instance_counter_order"})
        new_events.append(ev)
    return new_events, new_player_rows


def _split_body_contact_debug_residuals(
    all_events: list[MismatchEvent],
    player_rows: dict[tuple[str, int, int], PlayerRow],
    *,
    dataset_paths: dict[str, Path],
    action_names: dict[int, str],
    binding: Any,
) -> tuple[list[MismatchEvent], dict[tuple[str, int, int], PlayerRow]]:
    # F08b is the pre-admission primitive owner. Keep only rows that actually have a current
    # pre-combat BODY candidate/selection in the debug selector; rows with no candidate are action
    # timing/adjacency until primitive probes prove otherwise.
    # refs:
    # - src/api.c::msl_batch_debug_step_input_pre_combat
    # - src/combat.c::combat_resolve
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    f08b_keys = [
        key
        for key, row in player_rows.items()
        if row.fields and row.family_id == "F08b_body_contact_geometry_residual"
    ]
    if not f08b_keys:
        return all_events, player_rows

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    datasets: dict[str, Any] = {}
    family_by_key: dict[tuple[str, int, int], str] = {}

    for dataset_name, record, victim in f08b_keys:
        ds = datasets.get(dataset_name)
        if ds is None:
            ds_path = dataset_paths.get(dataset_name)
            if ds_path is None:
                continue
            ds = read_dataset(str(ds_path))
            datasets[dataset_name] = ds

        sample = ds.samples[record : record + 1]  # type: ignore[attr-defined]
        if int(sample.shape[0]) != 1:
            continue

        seed_bytes = np.frombuffer(sample["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
        prev_bytes = np.frombuffer(sample["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        in_bytes = np.frombuffer(sample["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
        precombat_out = np.empty((1, compare_stride), dtype=np.uint8)
        active_fighter_hitbox_count = 0
        active_special_attacker_hitbox_count = 0

        handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))  # type: ignore[index]
        try:
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_step_input_pre_combat(handle, prev_bytes, in_bytes)
            binding.write_compare(handle, precombat_out)
            selected_raw, selected_count = binding.debug_combat_select_body_hits(handle, 0, 256)
            classified_raw, classified_count = binding.debug_combat_contacts_classified(handle, 0, 256)
            filtered_raw, filtered_count = binding.debug_combat_contacts_classified_filtered(handle, 0, 256)
            for attacker in range(int(ds.header["num_players"])):  # type: ignore[index]
                if attacker == int(victim):
                    continue
                hitboxes_raw, _hitbox_count = binding.hitboxes_world_full(handle, 0, attacker)
                attacker_active = 0
                for hb in hitboxes_raw:
                    if int(hb[15]) != 0:
                        active_fighter_hitbox_count += 1
                        attacker_active += 1
                if attacker_active > 0:
                    precombat_tmp = precombat_out.view(COMPARE_DTYPE).reshape(-1)[0]
                    if _looks_like_any_special(_action_name(action_names, int(precombat_tmp["action_id"][attacker]))):
                        active_special_attacker_hitbox_count += attacker_active
        finally:
            binding.destroy(handle)

        selected = selected_raw.reshape(-1).view(_DEBUG_SELECTED_BODY_DTYPE)[:selected_count]
        classified = classified_raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:classified_count]
        filtered = filtered_raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:filtered_count]

        selected_body = [c for c in selected if int(c["defender"]) == int(victim)]
        selected_body_count = len(selected_body)
        filtered_body = [c for c in filtered if int(c["defender"]) == int(victim) and int(c["contact_kind"]) == 0]
        body_candidate_count = sum(
            1 for c in classified if int(c["defender"]) == int(victim) and int(c["contact_kind"]) == 0
        )
        filtered_body_candidate_count = len(filtered_body)
        seed_row = sample["seed_t"][0]
        live_nonvictim_item_count = 0
        for item in seed_row["items"]:
            if int(item["exists"]) == 0:
                continue
            owner = int(item["owner"])
            if owner != int(victim):
                live_nonvictim_item_count += 1

        row = player_rows[(dataset_name, record, victim)]
        precombat_row = precombat_out.view(COMPARE_DTYPE).reshape(-1)[0]
        precombat_action_id = int(precombat_row["action_id"][victim])
        family_by_key[(dataset_name, record, victim)] = _family_for_debug_body_contact_residual(
            seed_name=_action_name(action_names, row.seed_action_id),
            precombat_name=_action_name(action_names, precombat_action_id),
            ref_name=_action_name(action_names, row.ref_action_id),
            out_name=_action_name(action_names, row.out_action_id),
            selected_body_count=selected_body_count,
            body_candidate_count=body_candidate_count,
            filtered_body_candidate_count=filtered_body_candidate_count,
            active_fighter_hitbox_count=active_fighter_hitbox_count,
            active_special_attacker_hitbox_count=active_special_attacker_hitbox_count,
            live_nonvictim_item_count=live_nonvictim_item_count,
            first_msid=int((selected_body or filtered_body)[0]["attacker_msid"])
            if (selected_body or filtered_body)
            else None,
        )

    if not family_by_key:
        return all_events, player_rows

    new_player_rows: dict[tuple[str, int, int], PlayerRow] = {}
    for key, row in player_rows.items():
        family_id = family_by_key.get(key)
        if family_id is not None and family_id != row.family_id:
            row = PlayerRow(**{**asdict(row), "family_id": family_id})
        new_player_rows[key] = row

    new_events: list[MismatchEvent] = []
    for ev in all_events:
        if ev.subject.startswith("p"):
            try:
                p = int(ev.subject[1:])
            except ValueError:
                p = -1
            family_id = family_by_key.get((ev.dataset, ev.record, p))
            if family_id is not None and family_id != ev.family_id:
                ev = MismatchEvent(**{**asdict(ev), "family_id": family_id})
        new_events.append(ev)

    return new_events, new_player_rows


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
    dataset_paths: dict[str, Path] = {}

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
            dataset_paths[dataset_name] = ds_path

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
                            if field == "is_dead":
                                # `is_dead` is a compare-only lane derived from stocks in C
                                # (`msl_is_dead_from_stocks`). Seed rows do not carry a separate
                                # field; derive the same value here so taxonomy can observe
                                # death-state mismatches instead of failing when a gameplay change
                                # exposes one.
                                # refs: src/api.c::msl_is_dead_from_stocks
                                seed_v = 1 if int(seed["stocks"][i, p]) == 0 else 0
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
                        prev_actions=tuple(int(seed["seed_prev_action_id"][i, p]) for p in range(num_players)),
                        fields=tuple(sorted(slot_fields)),
                        family_id="F99_misc_other",
                        seed_item_type=int(seed["items"]["type"][i, slot]),
                        ref_item_type=int(ref["items"]["type"][i, slot]),
                        out_item_type=int(out["items"]["type"][i, slot]),
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

    all_events, player_rows = _split_body_contact_debug_residuals(
        all_events,
        player_rows,
        dataset_paths=dataset_paths,
        action_names=action_names,
        binding=binding,
    )
    all_events, player_rows = _split_cross_player_instance_counter_rows(all_events, player_rows)
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

        if unique_rows and unique_rows[0][2].startswith("item"):
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
