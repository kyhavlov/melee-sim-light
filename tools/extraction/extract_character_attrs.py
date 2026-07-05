from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive
from melee_sim.iso import extract_file, find_files, list_files
from tools.extraction.char_registry import CHARS
from tools.extraction.extract_fighter_moves import _parse_subaction_events

ARTICLE_HITBOX_FLAG_TARGET_GROUNDED = 1
ARTICLE_HITBOX_FLAG_TARGET_AERIAL = 2
ARTICLE_HITBOX_FLAG_BODY_ENABLED = 4
ARTICLE_HITBOX_FLAG_GRABBABLE_ONLY = 8
ARTICLE_HITBOX_FLAG_CLANK = 16


def _ptr32_or_none(arc, abs_off: int) -> int | None:
    ptr = _u32_be(arc.buf, abs_off)
    if ptr == 0:
        return None
    return arc.data_base + ptr


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _i32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=True)


def _s16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=True)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def _source_can_walljump(character: str) -> bool:
    info = CHARS.get(character)
    if info is None:
        return False
    src_dir = Path("refs/melee/src/melee/ft/chara") / info.decomp_dir
    for path in src_dir.glob("*.c"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "can_walljump" in text and "can_walljump = true" in text:
            return True
    return False


def _rot_xyz_mul_vec(rx: float, ry: float, rz: float, x: float, y: float, z: float) -> tuple[float, float, float]:
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)

    y, z = y * cx - z * sx, y * sx + z * cx
    x, z = x * cy + z * sy, -x * sy + z * cy
    x, y = x * cz - y * sz, x * sz + y * cz
    return (float(x), float(y), float(z))


def _extract_article_jobj_root_offsets(pl_buf: bytes, arc, model_desc_abs: int) -> list[tuple[float, float, float]]:
    """Return pre-order item-JObj root-space XYZ offsets for an Article::x10_modelDesc tree.

    `it_802790C0` binds item command-11 hitboxes to dynamic-bone JObjs. For source BODY contact,
    `it_8027137C -> lb_8000B1CC` publishes HitCapsule x58/x4C from that JObj, not the item root.
    The root JObj translation is the item position, so bone 0 has no root-space offset; child bones
    are their parent-rotated HSD_Joint positions.
    refs/melee/src/melee/it/types.h::{Article,ItemModelDesc}
    refs/melee/src/melee/it/itanimlist.c::it_802790C0
    refs/melee/src/melee/it/itcoll.c::it_8027137C
    """
    root_abs = _ptr32_or_none(arc, model_desc_abs + 0x00)
    if root_abs is None:
        return []
    out: list[tuple[float, float, float]] = []
    stack: list[tuple[int, tuple[float, float, float], tuple[float, float, float]]] = [
        (root_abs, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    ]
    while stack and len(out) < 64:
        node_abs, parent_pos, parent_rot = stack.pop()
        local_pos = (
            float(_f32_be(pl_buf, node_abs + 0x2C)),
            float(_f32_be(pl_buf, node_abs + 0x30)),
            float(_f32_be(pl_buf, node_abs + 0x34)),
        )
        if out:
            px, py, pz = _rot_xyz_mul_vec(
                parent_rot[0], parent_rot[1], parent_rot[2], local_pos[0], local_pos[1], local_pos[2]
            )
            pos = (parent_pos[0] + px, parent_pos[1] + py, parent_pos[2] + pz)
        else:
            pos = (0.0, 0.0, 0.0)
        local_rot = (
            float(_f32_be(pl_buf, node_abs + 0x14)),
            float(_f32_be(pl_buf, node_abs + 0x18)),
            float(_f32_be(pl_buf, node_abs + 0x1C)),
        )
        rot = (parent_rot[0] + local_rot[0], parent_rot[1] + local_rot[1], parent_rot[2] + local_rot[2])
        out.append((float(pos[0]), float(pos[1]), float(pos[2])))
        next_abs = _ptr32_or_none(arc, node_abs + 0x0C)
        child_abs = _ptr32_or_none(arc, node_abs + 0x08)
        if next_abs is not None:
            stack.append((next_abs, parent_pos, parent_rot))
        if child_abs is not None:
            stack.append((child_abs, pos, rot))
    return out


def _extract_fox_falco_laser(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Fox/Falco blaster shot (laser) params from fighter item article data (decomp-first).

    Source of truth:
    - ftData.x48_items[0] is the blaster shot Article* (registered via `it_8026B3F8` in ftFx_Init/ftFc_Init).
    - Article.x4_specialAttributes is FoxLaserAttr (lifetime/scale/etc).
    - Article.xC_itemStates[0].xC_script defines the hitbox (damage/size/kb params).
    """
    out: dict = {}

    # ftData.x48_items is a void** list of Article* pointers (ft/types.h +0x48).
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return out

    # items[0] => blaster shot.
    shot_article_abs = arc.ptr32(items_abs + 0x00)
    if shot_article_abs == arc.data_base:
        return out

    # struct Article { ItemAttr* x0_common_attr; void* x4_specialAttributes; ... ItemStateArray* xC_itemStates; ... }
    special_abs = arc.ptr32(shot_article_abs + 0x04)
    states_abs = arc.ptr32(shot_article_abs + 0x0C)
    if special_abs == arc.data_base or states_abs == arc.data_base:
        return out

    lifetime = float(_f32_be(pl_buf, special_abs + 0x00))
    out["laser_lifetime_frames"] = int(max(0, round(lifetime)))
    # FoxLaserAttr.scale (it/items/itfoxlaser.c): max visual stretch for the beam.
    out["laser_scale_max"] = float(_f32_be(pl_buf, special_abs + 0x04))

    def _extract_state_hitbox(state_index: int) -> tuple[dict, list[dict], list] | None:
        # ItemStateDesc stride is 0x10 (it/types.h). xC_script is at offset 0x0C.
        # refs/melee/src/melee/it/types.h::ItemStateDesc
        script_abs = arc.ptr32(states_abs + 0x0C + int(state_index) * 0x10)
        if script_abs == arc.data_base:
            return None
        events = _parse_subaction_events(
            arc, script_abs, max_frames=80, max_steps_per_frame=500, item_hitbox_layout=True
        )
        hitboxes: list[dict] = []
        for ev in events:
            if ev.kind == "create_hitbox":
                hb = ev.data.get("hitbox")
                if isinstance(hb, dict):
                    hitboxes.append(hb)
        hb0 = hitboxes[0] if hitboxes else None
        if not isinstance(hb0, dict):
            return None
        return hb0, hitboxes, events

    # State 0 is spawned by it_8029C6A4 (msid=0).
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6A4
    st0 = _extract_state_hitbox(0)
    if st0 is None:
        return out
    hb0, hbs0, evs0 = st0

    out["laser_damage"] = float(hb0.get("damage", 0.0))
    out["laser_size"] = float(hb0.get("size", 0.0))
    out["laser_angle"] = int(hb0.get("angle", 0))
    out["laser_kbg"] = int(hb0.get("kbg", 0))
    out["laser_wsk"] = int(hb0.get("wsk", 0))
    out["laser_bkb"] = int(hb0.get("bkb", 0))
    out["laser_element"] = int(hb0.get("element", 0))
    out["laser_shield_damage"] = int(hb0.get("shield_damage", 0))

    # The blaster shot article uses multiple hitboxes spaced along the beam. Preserve the X offsets
    # so the simulator can reproduce early hits without inflating radius.
    #
    # (Decomp: Pl*.dat article state script; parsed via `_parse_subaction_events`.)
    x_offs0: list[float] = []
    x138_mask0 = 0
    for hb in hbs0:
        try:
            x_offs0.append(float(hb.get("x_offset", 0.0)))
            if bool(hb.get("item_match_start_x138", False)):
                x138_mask0 |= 1 << (len(x_offs0) - 1)
        except Exception:
            pass
    out["laser_hitbox_offsets_x"] = x_offs0
    out["laser_hitbox_x138_mask"] = int(x138_mask0)
    # State-0 Fox blaster scripts update already-created HitCapsule damage on a later script
    # frame. Preserve the compact first uniform set_hitbox_damage group so runtime can consume
    # `it_80279544 -> it_80272460` without a local item-kind/action predicate.
    damage_update_frame: int | None = None
    damage_update_damage: float | None = None
    damage_update_mask = 0
    for ev in evs0:
        if ev.kind != "set_hitbox_damage":
            continue
        try:
            frame_i = int(ev.frame)
            idx_i = int(ev.data.get("idx", -1))
            damage_f = float(ev.data.get("damage", 0.0))
        except Exception:
            continue
        if idx_i < 0 or idx_i >= 16 or not damage_f > 0.0:
            continue
        if damage_update_frame is None:
            damage_update_frame = frame_i
            damage_update_damage = damage_f
        if frame_i != damage_update_frame or abs(float(damage_update_damage) - damage_f) > 1e-6:
            continue
        damage_update_mask |= 1 << idx_i
    if damage_update_frame is not None and damage_update_damage is not None:
        out["laser_damage_update_frame"] = int(damage_update_frame)
        out["laser_damage_update_damage"] = float(damage_update_damage)
        out["laser_damage_update_hitbox_mask"] = int(damage_update_mask)

    # State 1 is spawned by it_8029C6CC (msid=1).
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    st1 = _extract_state_hitbox(1)
    if st1 is not None:
        hb1, hbs1, _evs1 = st1
        out["laser_state1_damage"] = float(hb1.get("damage", 0.0))
        out["laser_state1_size"] = float(hb1.get("size", 0.0))
        out["laser_state1_angle"] = int(hb1.get("angle", 0))
        out["laser_state1_kbg"] = int(hb1.get("kbg", 0))
        out["laser_state1_wsk"] = int(hb1.get("wsk", 0))
        out["laser_state1_bkb"] = int(hb1.get("bkb", 0))
        out["laser_state1_element"] = int(hb1.get("element", 0))
        out["laser_state1_shield_damage"] = int(hb1.get("shield_damage", 0))

        x_offs1: list[float] = []
        x138_mask1 = 0
        for hb in hbs1:
            try:
                x_offs1.append(float(hb.get("x_offset", 0.0)))
                if bool(hb.get("item_match_start_x138", False)):
                    x138_mask1 |= 1 << (len(x_offs1) - 1)
            except Exception:
                pass
        out["laser_state1_hitbox_offsets_x"] = x_offs1
        out["laser_state1_hitbox_x138_mask"] = int(x138_mask1)
    return out


def _q8(v: float) -> float:
    return float(round(float(v) * 256.0) / 256.0)


def _extract_fox_falco_illusion_item(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Fox/Falco Illusion/Phantasm item params from fighter article data.

    Decomp anchors:
    - refs/melee/src/melee/it/items/itfoxillusion.c
    - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_OnLoad
    - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_OnLoad
    """
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return {}

    def _state_hitbox(states_abs: int, state_index: int) -> dict | None:
        script_abs = arc.ptr32(states_abs + 0x0C + int(state_index) * 0x10)
        if script_abs == arc.data_base:
            return None
        events = _parse_subaction_events(
            arc, script_abs, max_frames=8, max_steps_per_frame=500, item_hitbox_layout=True
        )
        for ev in events:
            if ev.kind == "create_hitbox":
                hb = ev.data.get("hitbox")
                if isinstance(hb, dict):
                    return hb
        return None

    # ftData.x48_items[0] is the blaster shot. The Illusion/Phantasm item is the later
    # Fox/Falco article with state 0 and state 1 hitbox scripts.
    for item_index in range(1, 4):
        article_abs = arc.ptr32(items_abs + item_index * 4)
        if article_abs == arc.data_base or article_abs < 0 or article_abs + 0x10 > len(pl_buf):
            continue
        special_abs = arc.ptr32(article_abs + 0x04)
        states_abs = arc.ptr32(article_abs + 0x0C)
        if special_abs == arc.data_base or states_abs == arc.data_base:
            continue
        lifetime01 = int(max(0, min(255, int(round(float(_f32_be(pl_buf, special_abs + 0x00)))))))
        lifetime2 = int(max(0, min(255, int(round(float(_f32_be(pl_buf, special_abs + 0x04)))))))
        hb0 = _state_hitbox(states_abs, 0)
        hb1 = _state_hitbox(states_abs, 1)
        if hb0 is None or hb1 is None:
            continue

        return {
            "illusion_item_lifetime_state01_frames": lifetime01,
            "illusion_item_lifetime_state2_frames": lifetime2,
            "illusion_item_hitbox_size": _q8(float(hb0.get("size", 0.0))),
            "illusion_item_state0_hitbox_y_offset": _q8(float(hb0.get("y_offset", 0.0))),
            "illusion_item_state0_damage": float(hb0.get("damage", 0.0)),
            "illusion_item_state0_angle": int(hb0.get("angle", 0)),
            "illusion_item_state0_kbg": int(hb0.get("kbg", 0)),
            "illusion_item_state0_wsk": int(hb0.get("wsk", 0)),
            "illusion_item_state0_bkb": int(hb0.get("bkb", 0)),
            "illusion_item_state0_element": int(hb0.get("element", 0)),
            "illusion_item_state0_shield_damage": int(hb0.get("shield_damage", 0)),
            "illusion_item_state1_hitbox_y_offset": _q8(float(hb1.get("y_offset", 0.0))),
            "illusion_item_state1_damage": float(hb1.get("damage", 0.0)),
            "illusion_item_state1_angle": int(hb1.get("angle", 0)),
            "illusion_item_state1_kbg": int(hb1.get("kbg", 0)),
            "illusion_item_state1_wsk": int(hb1.get("wsk", 0)),
            "illusion_item_state1_bkb": int(hb1.get("bkb", 0)),
            "illusion_item_state1_element": int(hb1.get("element", 0)),
            "illusion_item_state1_shield_damage": int(hb1.get("shield_damage", 0)),
        }
    return {}


def _extract_seak_needle_article(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Sheik thrown-Needle article data from ftData.x48_items[0].

    Decomp anchors:
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_OnLoad
    - refs/melee/src/melee/it/items/itseakneedlethrown.c
    - refs/melee/src/melee/it/types.h::{Article,ItHurtBoneList,ItHurtBoneDesc}
    """
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return {}
    article_abs = arc.ptr32(items_abs + 0x00)
    if article_abs == arc.data_base:
        return {}
    special_abs = arc.ptr32(article_abs + 0x04)
    hurt_abs = arc.ptr32(article_abs + 0x08)
    states_abs = arc.ptr32(article_abs + 0x0C)
    model_desc_abs = arc.ptr32(article_abs + 0x10)
    if special_abs == arc.data_base or hurt_abs == arc.data_base:
        return {}

    out: dict = {
        # ItemKind values are source-owned constants from ftSk_Init_OnLoad's it_8026B3F8 calls.
        "needle_throw_itkind": 79,
        "needle_held_itkind": 80,
        "needle_lifetime_frames": int(max(0, round(float(_f32_be(pl_buf, special_abs + 0x00))))),
        "needle_bounce_lifetime_frames": int(
            max(0, round(float(_f32_be(pl_buf, special_abs + 0x04))))
        ),
        "needle_launch_speed": float(_f32_be(pl_buf, special_abs + 0x08)),
    }

    hurt_count = int(_i32_be(pl_buf, hurt_abs + 0x00))
    desc_abs = arc.ptr32(hurt_abs + 0x04)
    out["needle_hurtbox_count"] = int(max(0, min(2, hurt_count)))
    if hurt_count > 0 and desc_abs != arc.data_base:
        # ItHurtBoneDesc stride: s32 bone_id, Vec3 a, Vec3 b, f32 scale.
        out["needle_hurtbox_bone_id"] = int(_i32_be(pl_buf, desc_abs + 0x00))
        out["needle_hurtbox_a_offset"] = [
            float(_f32_be(pl_buf, desc_abs + 0x04)),
            float(_f32_be(pl_buf, desc_abs + 0x08)),
            float(_f32_be(pl_buf, desc_abs + 0x0C)),
        ]
        out["needle_hurtbox_b_offset"] = [
            float(_f32_be(pl_buf, desc_abs + 0x10)),
            float(_f32_be(pl_buf, desc_abs + 0x14)),
            float(_f32_be(pl_buf, desc_abs + 0x18)),
        ]
        out["needle_hurtbox_scale"] = float(_f32_be(pl_buf, desc_abs + 0x1C))
    if states_abs != arc.data_base:
        script_abs = arc.ptr32(states_abs + 0x0C)
        if script_abs != arc.data_base:
            events = _parse_subaction_events(
                arc, script_abs, max_frames=8, max_steps_per_frame=500, item_hitbox_layout=True
            )
            hitboxes: list[dict] = []
            for ev in events:
                if ev.kind != "create_hitbox":
                    continue
                hb = ev.data.get("hitbox")
                if isinstance(hb, dict):
                    hitboxes.append(hb)
            if hitboxes:
                jobj_offsets = (
                    _extract_article_jobj_root_offsets(pl_buf, arc, model_desc_abs)
                    if model_desc_abs != arc.data_base
                    else []
                )
                hitbox_bones = [int(hb.get("bone", 0)) for hb in hitboxes[:4]]
                out["needle_hitbox_count"] = int(min(4, len(hitboxes)))
                out["needle_hitbox_damage"] = float(hitboxes[0].get("damage", 0.0))
                out["needle_hitbox_damage_by_id"] = [
                    float(hb.get("damage", 0.0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_bone_id"] = hitbox_bones
                out["needle_hitbox_jobj_x_offset"] = [
                    float(jobj_offsets[bone][0]) if 0 <= bone < len(jobj_offsets) else 0.0
                    for bone in hitbox_bones
                ]
                out["needle_hitbox_jobj_y_offset"] = [
                    float(jobj_offsets[bone][1]) if 0 <= bone < len(jobj_offsets) else 0.0
                    for bone in hitbox_bones
                ]
                out["needle_hitbox_jobj_z_offset"] = [
                    float(jobj_offsets[bone][2]) if 0 <= bone < len(jobj_offsets) else 0.0
                    for bone in hitbox_bones
                ]
                out["needle_hitbox_size"] = [float(hb.get("size", 0.0)) for hb in hitboxes[:4]]
                out["needle_hitbox_x_offset"] = [
                    float(hb.get("x_offset", 0.0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_y_offset"] = [
                    float(hb.get("y_offset", 0.0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_z_offset"] = [
                    float(hb.get("z_offset", 0.0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_angle"] = [int(hb.get("angle", 0)) for hb in hitboxes[:4]]
                out["needle_hitbox_kbg"] = [int(hb.get("kbg", 0)) for hb in hitboxes[:4]]
                out["needle_hitbox_wsk"] = [int(hb.get("wsk", 0)) for hb in hitboxes[:4]]
                out["needle_hitbox_bkb"] = [int(hb.get("bkb", 0)) for hb in hitboxes[:4]]
                out["needle_hitbox_element"] = [
                    int(hb.get("element", 0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_shield_damage"] = [
                    int(hb.get("shield_damage", 0)) for hb in hitboxes[:4]
                ]
                out["needle_hitbox_flags"] = [
                    (ARTICLE_HITBOX_FLAG_TARGET_GROUNDED if bool(hb.get("hit_grounded", False)) else 0)
                    | (ARTICLE_HITBOX_FLAG_TARGET_AERIAL if bool(hb.get("hit_aerial", False)) else 0)
                    | (ARTICLE_HITBOX_FLAG_BODY_ENABLED if bool(hb.get("item_body_enabled", False)) else 0)
                    | (ARTICLE_HITBOX_FLAG_GRABBABLE_ONLY if bool(hb.get("item_grabbable_only", False)) else 0)
                    | (ARTICLE_HITBOX_FLAG_CLANK if bool(hb.get("clank", False)) else 0)
                    for hb in hitboxes[:4]
                ]
    return out


def _extract_seak_vanish_article(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Sheik Vanish smoke article hitbox data from ftData.x48_items[2].

    Source:
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_80112F48
    - refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    """
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return {}
    article_abs = arc.ptr32(items_abs + 0x08)
    if article_abs == arc.data_base:
        return {}
    states_abs = arc.ptr32(article_abs + 0x0C)
    if states_abs == arc.data_base:
        return {}
    script_abs = arc.ptr32(states_abs + 0x0C)
    if script_abs == arc.data_base:
        return {}

    events = _parse_subaction_events(
        arc, script_abs, max_frames=80, max_steps_per_frame=500, item_hitbox_layout=True
    )
    hitboxes: list[dict] = []
    size_keyframes: list[tuple[int, float]] = []
    remove_frame: int | None = None
    for ev in events:
        if ev.kind != "create_hitbox":
            if ev.kind == "set_hitbox_size" and int(ev.data.get("idx", -1)) == 0:
                size_keyframes.append((int(ev.frame), float(ev.data.get("size", 0.0))))
            elif ev.kind == "remove_hitbox" and int(ev.data.get("idx", -1)) == 0:
                remove_frame = int(ev.frame) if remove_frame is None else min(remove_frame, int(ev.frame))
            elif ev.kind == "clear_hitboxes":
                remove_frame = int(ev.frame) if remove_frame is None else min(remove_frame, int(ev.frame))
            continue
        hb = ev.data.get("hitbox")
        if isinstance(hb, dict):
            hitboxes.append(hb)
    if not hitboxes:
        return {}
    hb0 = hitboxes[0]
    size_keyframes = size_keyframes[:2]
    return {
        "vanish_hitbox_count": int(min(1, len(hitboxes))),
        "vanish_hitbox_damage": float(hb0.get("damage", 0.0)),
        "vanish_hitbox_size": float(hb0.get("size", 0.0)),
        "vanish_hitbox_x_offset": float(hb0.get("x_offset", 0.0)),
        "vanish_hitbox_y_offset": float(hb0.get("y_offset", 0.0)),
        "vanish_hitbox_z_offset": float(hb0.get("z_offset", 0.0)),
        "vanish_hitbox_angle": int(hb0.get("angle", 0)),
        "vanish_hitbox_kbg": int(hb0.get("kbg", 0)),
        "vanish_hitbox_wsk": int(hb0.get("wsk", 0)),
        "vanish_hitbox_bkb": int(hb0.get("bkb", 0)),
        "vanish_hitbox_element": int(hb0.get("element", 0)),
        "vanish_hitbox_shield_damage": int(hb0.get("shield_damage", 0)),
        "vanish_hitbox_flags": (
            ARTICLE_HITBOX_FLAG_TARGET_GROUNDED if bool(hb0.get("hit_grounded", False)) else 0
        )
        | (ARTICLE_HITBOX_FLAG_TARGET_AERIAL if bool(hb0.get("hit_aerial", False)) else 0)
        | (ARTICLE_HITBOX_FLAG_BODY_ENABLED if bool(hb0.get("item_body_enabled", False)) else 0)
        | (ARTICLE_HITBOX_FLAG_GRABBABLE_ONLY if bool(hb0.get("item_grabbable_only", False)) else 0),
        "vanish_hitbox_size_keyframe_count": len(size_keyframes),
        "vanish_hitbox_size_keyframe_frame": [int(frame) for frame, _size in size_keyframes],
        "vanish_hitbox_size_keyframe_value": [float(size) for _frame, size in size_keyframes],
        "vanish_hitbox_remove_frame": int(remove_frame or 0),
    }


def _extract_wait_anim_choices(buf: bytes, wait_abs: int) -> dict:
    """Extract ftData.x24 WaitStruct roulette entries for Wait animation variants.

    Source:
    - refs/melee/src/melee/ft/types.h::ftData.x24
    - refs/melee/src/melee/ft/ftwaitanim.c::getAnimID

    The table is a sequence of `{msid, weight}` s32 pairs terminated by {-1, -1}.
    """
    if wait_abs < 0 or wait_abs + 8 > len(buf):
        return {}
    msids: list[int] = []
    weights: list[int] = []
    for i in range(16):
        off = wait_abs + i * 8
        if off + 8 > len(buf):
            break
        msid = int(_i32_be(buf, off + 0x00))
        weight = int(_i32_be(buf, off + 0x04))
        if msid == -1:
            break
        if msid < 0 or weight <= 0:
            return {}
        msids.append(msid)
        weights.append(weight)
        if len(msids) >= 4:
            break
    if not msids:
        return {}
    return {
        "wait_anim_choice_msids": msids,
        "wait_anim_choice_weights": weights,
    }


# MarsAttributes layout consumed by _extract_mars_sword_attrs: (key, offset, kind).
# kind: "i32" | "f32" | "vec3" . Offsets verified against the parsed decomp struct
# (refs/melee/src/melee/ft/chara/ftMars/types.h::_MarsAttributes) by
# tests/test_decomp_struct_layout.py - transcription errors fail there, not at runtime.
MARS_SWORD_ATTRS_LAYOUT: list[tuple[str, int, str]] = [
    ("specialn_charge_max_seconds", 0x00, "i32"),
    ("specialn_release_damage_base", 0x04, "i32"),
    ("specialn_release_damage_per_second", 0x08, "i32"),
    ("specialn_entry_vel_divisor", 0x0C, "f32"),
    ("specialn_start_friction", 0x10, "f32"),
    ("specials_air_entry_vel_x_divisor", 0x14, "f32"),
    ("specials_air_friction", 0x18, "f32"),
    ("specials_air_entry_vel_y", 0x1C, "f32"),
    ("specials_fall_accel", 0x20, "f32"),
    ("specials_terminal_vel", 0x24, "f32"),
    ("specialhi_freefall_mobility_mul", 0x28, "f32"),
    ("specialhi_landing_lag_frames", 0x2C, "f32"),
    ("specialhi_breverse_stick_threshold", 0x30, "f32"),
    ("specialhi_angle_stick_threshold", 0x34, "f32"),
    ("specialhi_angle_max_degrees", 0x38, "f32"),
    ("specialhi_air_entry_vel_x_mul", 0x3C, "f32"),
    ("specialhi_launch_decay_mul", 0x40, "f32"),
    ("specialhi_fall_accel", 0x44, "f32"),
    ("specialhi_terminal_vel", 0x48, "f32"),
    ("speciallw_air_entry_vel_x_divisor", 0x4C, "f32"),
    ("speciallw_air_friction", 0x50, "f32"),
    ("speciallw_fall_accel", 0x54, "f32"),
    ("speciallw_terminal_vel", 0x58, "f32"),
    ("speciallw_counter_damage_mul", 0x5C, "f32"),
    ("speciallw_counter_shield_strength", 0x60, "f32"),
    ("speciallw_counter_desc_bone", 0x64, "i32"),
    ("speciallw_counter_desc_offset", 0x68, "vec3"),
    ("speciallw_counter_desc_size", 0x74, "f32"),
]


SEAK_SPECIAL_ATTRS_LAYOUT: list[tuple[str, int, str]] = [
    ("sheik_needle_ground_spawn_x_offset", 0x00, "f32"),
    ("sheik_needle_ground_spawn_y_offset", 0x04, "f32"),
    ("sheik_needle_air_spawn_x_offset", 0x08, "f32"),
    ("sheik_needle_air_spawn_y_offset", 0x0C, "f32"),
    ("sheik_needle_air_end_fallspecial_lag_frames", 0x10, "f32"),
    ("sheik_chain_release_min_frames", 0x14, "f32"),
    ("sheik_chain_extension_frames", 0x18, "f32"),
    ("sheik_chain_spawn_frame", 0x1C, "f32"),
    ("sheik_chain_start_end_frame", 0x20, "f32"),
    ("sheik_chain_retract_frame", 0x24, "f32"),
    ("sheik_chain_destroy_frame", 0x28, "f32"),
    ("sheik_vanish_air_entry_vel_y", 0x2C, "f32"),
    ("sheik_vanish_start_air_gravity", 0x30, "f32"),
    ("sheik_vanish_start_air_terminal_vel", 0x34, "f32"),
    ("sheik_vanish_travel_frames", 0x38, "i32"),
    ("sheik_vanish_ground_contact_min_frames", 0x3C, "f32"),
    ("sheik_vanish_stick_mag_min", 0x40, "f32"),
    ("sheik_vanish_travel_speed_stick_mul", 0x44, "f32"),
    ("sheik_vanish_travel_speed_base", 0x48, "f32"),
    ("sheik_vanish_air_end_drift_mul", 0x4C, "f32"),
    ("sheik_vanish_wall_bounce_degrees", 0x50, "i32"),
    ("sheik_vanish_end_vel_mul", 0x54, "f32"),
    ("sheik_vanish_fallspecial_mobility_mul", 0x58, "f32"),
    ("sheik_vanish_landing_lag_frames", 0x5C, "f32"),
    ("sheik_transform_vel_x_divisor", 0x60, "f32"),
    ("sheik_transform_vel_y_divisor", 0x64, "f32"),
    ("sheik_transform_air_gravity", 0x68, "f32"),
    ("sheik_transform_air_terminal_vel", 0x6C, "f32"),
    ("sheik_transform_finish_start_frame", 0x70, "f32"),
]


ZELDA_SPECIAL_ATTRS_LAYOUT: list[tuple[str, int, str]] = [
    ("zelda_transform_vel_x_divisor", 0x70, "f32"),
    ("zelda_transform_vel_y_divisor", 0x74, "f32"),
    ("zelda_transform_air_gravity", 0x78, "f32"),
    ("zelda_transform_air_terminal_vel", 0x7C, "f32"),
    ("zelda_transform_finish_start_frame", 0x80, "f32"),
]


def _extract_mars_sword_attrs(buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract MarsAttributes (ftData.x4 ext block) for Marth-style sword characters.

    Field semantics are derived from the per-special consumers (mechanic-position names so Roy,
    a Marth clone with the same layout, can reuse this extractor):
    - SpecialN (Shield Breaker): refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c
      x0 max charge seconds (cur_frame > x0*30 forces release), x4 + charge_seconds * x8 is the
      ftColl_8007ABD0 damage override on normal release, xC entry velocity divisor,
      x10 start/loop friction.
    - SpecialS (Dancing Blade): refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c
      x14 air-entry vel.x divisor, x18 air friction, x1C air-entry vel.y, x20 fall accel,
      x24 terminal velocity.
    - SpecialHi (Dolphin Slash): refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c
      x28 freefall mobility multiplier (also FallSpecial arg), x2C landing lag,
      x30 B-reverse stick threshold, x34 launch-angle stick threshold, x38 max launch angle
      (degrees), x3C air-entry vel.x multiplier, x40 launch velocity decay (air variant),
      x44 post-launch gravity, x48 post-launch terminal velocity.
    - SpecialLw (Counter): refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c
      x4C air-entry vel.x divisor, x50 air friction, x54 fall accel, x58 terminal velocity,
      x5C counter damage multiplier (consumed by FTKIND_EMBLEM only; extracted for clones),
      x60 counter shield strength (fp->shield_unk0/1), x64 AbsorbDesc intercept descriptor
      (bone, offset, size) installed via ftColl_8007B1B8 while the script holds cmd_vars[1]==1.
    - refs/melee/src/melee/ft/chara/ftMars/types.h::MarsAttributes
    """
    out: dict = {}
    ext_abs = arc.ptr32(ftdata_abs + 0x04)
    if ext_abs == arc.data_base:
        return out
    for key, off, kind in MARS_SWORD_ATTRS_LAYOUT:
        if kind == "i32":
            out[key] = int(_i32_be(buf, ext_abs + off))
        elif kind == "f32":
            out[key] = float(_f32_be(buf, ext_abs + off))
        elif kind == "vec3":
            out[key] = [
                float(_f32_be(buf, ext_abs + off)),
                float(_f32_be(buf, ext_abs + off + 4)),
                float(_f32_be(buf, ext_abs + off + 8)),
            ]
        else:  # pragma: no cover - layout table typo
            raise ValueError(f"unknown layout kind {kind!r} for {key}")
    return out


def _extract_seak_chain_article(pl_buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Sheik Side-B Chain article Verlet-solver attributes.

    The Chain article is ftData.x48_items[3]; its x4_specialAttributes block is an
    `itSeakChain_Attrs` (0x6C bytes, struct in it/itCharItems.h). The Chain has NO
    scripted article hitboxes (xC_itemStates == data_base); the 4 collision capsules
    live on the FIGHTER (fp->x914[]) and are positioned along the solved links by
    `ftSk_SpecialS_UpdateHitboxes`. So only the solver attrs come from here.

    Field semantics from the solver (refs/melee/src/melee/it/items/itseakchain.c):
    - x0  : link count (s32) -- it_802BAF2C spawns `attrs->x0` ItemLink nodes.
    - x4  : segment length -- max per-link separation constraint (it_802BBB0C).
    - x10/x14 : static-friction clamp magnitudes on link vel.x
              (itSeakChain_clamp_x10 / _x14).
    - x18 : per-link gravity (prev->vel.y -= sa->x18).
    - x1C..x48 : extend/whip/retract decay + activation tuning.
    - x4C : hitbox reactivation movement threshold (ftSk_SpecialS_80110BCC).
    - x50 : initial extension velocity (ftSk_SpecialS_CheckInitChain -> it_802BCFC4).
    - x54 : retract clamp scale (it_802BC94C, fn_802BB784).
    - x58 : wall-bounce vel.x reflection factor (link->vel.x *= -sa->x58).
    - x5C/x60 : extend/retract solver tuning.
    """
    items_abs = arc.ptr32(ftdata_abs + 0x48)
    if items_abs == arc.data_base:
        return {}
    article_abs = arc.ptr32(items_abs + 0x0C)  # x48_items[3]
    if article_abs == arc.data_base:
        return {}
    special_abs = arc.ptr32(article_abs + 0x04)
    if special_abs == arc.data_base or special_abs + 0x6C > len(pl_buf):
        return {}

    link_count = _i32_be(pl_buf, special_abs + 0x00)
    if link_count <= 0 or link_count > 64:
        return {}

    def af(off: int) -> float:
        return float(_f32_be(pl_buf, special_abs + off))

    return {
        "sheik_chain_link_count": int(link_count),
        "sheik_chain_segment_length": af(0x04),
        "sheik_chain_friction_x10": af(0x10),
        "sheik_chain_friction_x14": af(0x14),
        "sheik_chain_gravity": af(0x18),
        "sheik_chain_attr_x1c": af(0x1C),
        "sheik_chain_attr_x20": af(0x20),
        "sheik_chain_attr_x24": af(0x24),
        "sheik_chain_attr_x28": af(0x28),
        "sheik_chain_attr_x2c": af(0x2C),
        "sheik_chain_attr_x30": af(0x30),
        "sheik_chain_decay_x34": af(0x34),
        "sheik_chain_attr_x38": af(0x38),
        "sheik_chain_attr_x3c": af(0x3C),
        "sheik_chain_attr_x40": af(0x40),
        "sheik_chain_attr_x44": af(0x44),
        "sheik_chain_attr_x48": af(0x48),
        "sheik_chain_attr_x4c": af(0x4C),
        "sheik_chain_initial_vel_x50": af(0x50),
        "sheik_chain_attr_x54": af(0x54),
        "sheik_chain_wall_bounce_x58": af(0x58),
        "sheik_chain_attr_x5c": af(0x5C),
        "sheik_chain_attr_x60": af(0x60),
    }


def _extract_seak_special_attrs(buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract Sheik's ftData.x4 ftSeakAttributes block.

    Names follow the source consumers instead of using generic `special*` keys because the x4 ext
    block layout is character-specific:
    - refs/melee/src/melee/ft/chara/ftSeak/types.h::ftSeakAttributes
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c (Needles x0..x10)
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c (Chain x14..x28)
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c (Vanish self_vel_y/x30..x5C)
    - refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c (Transform x60..x70)
    """
    out: dict = {}
    ext_abs = arc.ptr32(ftdata_abs + 0x04)
    if ext_abs == arc.data_base:
        return out
    for key, off, kind in SEAK_SPECIAL_ATTRS_LAYOUT:
        if kind == "i32":
            out[key] = int(_i32_be(buf, ext_abs + off))
        elif kind == "f32":
            out[key] = float(_f32_be(buf, ext_abs + off))
        else:  # pragma: no cover - layout table typo
            raise ValueError(f"unknown layout kind {kind!r} for {key}")
    return out


def _extract_zelda_special_attrs(buf: bytes, arc, *, ftdata_abs: int) -> dict:
    """Extract the bounded Zelda transform attrs needed for ftZd_SpecialLw.

    Zelda's ftData.x4 block is `ftZelda_DatAttrs`. For this pass, only expose the Down-B
    transform/fall fields consumed by `ftZd_SpecialLw_{Enter,Phys}` and the finish handoff; the rest
    of Zelda's specials remain out of runtime scope until a replay requires them.
    - refs/melee/src/melee/ft/chara/ftZelda/types.h::ftZelda_DatAttrs
    - refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::{
    -   ftZelda_SpecialLw_StartAction_Helper,ftZd_SpecialAirLw_Phys,ftZd_SpecialLw_8013B4D8}
    """
    out: dict = {}
    ext_abs = arc.ptr32(ftdata_abs + 0x04)
    if ext_abs == arc.data_base:
        return out
    for key, off, kind in ZELDA_SPECIAL_ATTRS_LAYOUT:
        if kind == "f32":
            out[key] = float(_f32_be(buf, ext_abs + off))
        else:  # pragma: no cover - layout table typo
            raise ValueError(f"unknown layout kind {kind!r} for {key}")
    return out


def _extract_ftco_dattrs(pl_dat: Path, *, ftdata_symbol: str, extract_fox_blaster: bool = False,
                         ext_attr_layout: str | None = None) -> dict:
    buf = pl_dat.read_bytes()
    arc = parse_hsd_archive(buf)

    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise ValueError(f"{pl_dat.name}: missing public symbol {ftdata_symbol!r}")

    # Grab/capture victim attachment anchor (decomp-first).
    #
    # Decomp: in fn_800D9CE8, the engine sets `mv.co.capturedamage.x18` by indexing `fp->parts[]`
    # using the u8 stored at `fp->ft_data->x8->x11`.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
    #
    # Store the raw u8 as a pose bone index (index into `fp->parts[]` / SSANIM01 part id space).
    grab_capture_anchor_part_id = 0
    x8_abs = arc.ptr32(ftdata_abs + 0x08)
    if 0 <= x8_abs + 0x12 <= len(buf):
        grab_capture_anchor_part_id = int(buf[x8_abs + 0x11])

    # struct ftData { ftCo_DatAttrs* x0; ... }
    attrs_abs = arc.ptr32(ftdata_abs + 0x00)
    # struct ftData { ... UnkFloat6_Camera* x3C; } consumed by ftCamera_80076018.
    camera_abs = arc.ptr32(ftdata_abs + 0x3C)
    # struct ftData { ... Vec2* x50; } (ft/types.h +0x50) => fp->x2C4 (pushbox center offset + radius).
    pushbox_abs = arc.ptr32(ftdata_abs + 0x50)
    # struct ftData { ... FtSFX* x4C_sfx; }.
    sfx_abs = arc.ptr32(ftdata_abs + 0x4C)

    def f(off: int) -> float:
        return float(_f32_be(buf, attrs_abs + off))

    def i(off: int) -> int:
        return int(_i32_be(buf, attrs_abs + off))

    jump_startup = f(0x38)
    # In practice these are integer-valued floats (e.g. 3.0, 4.0).
    jump_startup_frames = int(round(jump_startup))
    # ftCo_DatAttrs.frames_to_change_direction_on_standing_turn (ft/types.h +0x84)
    turn_frames = int(round(f(0x84)))
    # ftCo_DatAttrs.normal_landing_lag (ft/types.h +0xE4)
    landing_lag_frames = int(round(f(0xE4)))
    landing_airn_lag_frames = int(round(f(0xE8)))
    landing_airf_lag_frames = int(round(f(0xEC)))
    landing_airb_lag_frames = int(round(f(0xF0)))
    landing_airhi_lag_frames = int(round(f(0xF4)))
    landing_airlw_lag_frames = int(round(f(0xF8)))

    x44_abs = arc.ptr32(ftdata_abs + 0x44)
    wait_anim_abs = arc.ptr32(ftdata_abs + 0x24)
    # Probe-backed gameplay overlay:
    # ftCo_800DDDE4 always samples a selected capture/throw anchor, but the observed
    # mpColl_800471F8 floor-publication subset is not equivalent to the anchor part id.
    # Marth ThrowF/ThrowLw and Sheik ThrowLw publish the floor-hit substep root before damage
    # entry; Marth ThrowB and Fox/Falco controls do not. Keep the source-completion discriminator
    # explicit so future characters with the same anchor id do not inherit this path accidentally.
    # Bit order: ThrowF, ThrowB, ThrowHi, ThrowLw.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
    # refs/Ishiiruka engine-dump-v12-probes ftCo_800DDDE4 probe:
    #   IPW 1231..1233/10031, ParallelFamiliarZebra 1427..1429, FSP 9061..9067,
    #   RuralReasonableRat 3168.
    throw_release_mpcoll_floor_publication_mask = 0
    if ftdata_symbol == "ftDataMars":
        throw_release_mpcoll_floor_publication_mask = (1 << 0) | (1 << 3)
    if ftdata_symbol == "ftDataSeak":
        throw_release_mpcoll_floor_publication_mask = 1 << 3
    # Source-callsite gameplay overlay:
    # ftCo_80096900 stores arg1 into mv.co.fallspecial.xC. Marth Dolphin Slash calls it with
    # arg1=0 from SpecialHi/SpecialAirHi, while Fox/Falco Firefox fall/end callsites pass arg1=1.
    # Store the owner as MslMsFxSpecialKind bits so reseed code can ask for source-callsite
    # ownership without a raw character-id branch.
    # Bit domain: src/motion_state_owners.h::MslMsFxSpecialKind.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_80138884
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c
    fallspecial_xc0_source_fx_kind_mask = 0
    if ftdata_symbol == "ftDataMars":
        fallspecial_xc0_source_fx_kind_mask = (1 << 15) | (1 << 16)
    # Probe-backed seed/provenance overlay:
    # Slippi does not expose CollData ECB bottom. Marth FallAerial shallow landing witnesses and
    # Sheik Fall shallow platform/floor witnesses need the seed CollData bottom reconstructed from
    # the CommonFall directional blend hidden lane, but aggregate Fox/Falco controls reject
    # promoting that to a shared free-running mpColl rule. Keep this as an explicit
    # character/action seed mask until a direct mpColl probe proves a wider live-callback owner.
    # Bit order: Fall, FallAerial, FallSpecial.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::{
    #   ftCo_FallAerial_Anim,ftCo_FallAerial_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
    common_fall_blended_ecb_seed_mask = 0
    if ftdata_symbol == "ftDataMars":
        common_fall_blended_ecb_seed_mask = 1 << 1
    if ftdata_symbol == "ftDataSeak":
        common_fall_blended_ecb_seed_mask = 1 << 0
    # Sustained EscapeAir_Coll carried ledge-floor wall publication overlay. This is kept explicit
    # instead of inferred from action id or a stage/replay row: Sheik/Zelda probes require the live
    # carried floor -> adjacent wall owner, while Fox/Falco/Marth validation controls reject
    # promoting the same sustained owner broadly.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80046904,mpColl_80044628_Floor}
    escapeair_carried_floor_wall_source = 0
    if ftdata_symbol in ("ftDataSeak", "ftDataZelda"):
        escapeair_carried_floor_wall_source = 1

    out = {
        "grab_capture_anchor_part_id": grab_capture_anchor_part_id,
        "throw_release_mpcoll_floor_publication_mask": throw_release_mpcoll_floor_publication_mask,
        "fallspecial_xc0_source_fx_kind_mask": fallspecial_xc0_source_fx_kind_mask,
        "common_fall_blended_ecb_seed_mask": common_fall_blended_ecb_seed_mask,
        "escapeair_carried_floor_wall_source": escapeair_carried_floor_wall_source,
        "walk_init_vel": f(0x00),
        "walk_accel": f(0x04),
        "walk_max_vel": f(0x08),
        # Decomp: ftCo_DatAttrs walk animation-rate divisors (ft/types.h +0x0C/+0x10/+0x14),
        # used by ftWalkCommon_800DFDDC (ABS(mv_x0) / {slow,mid,fast}_walk_*).
        # refs/melee/src/melee/ft/types.h::ftCo_DatAttrs
        # refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
        "slow_walk_max": f(0x0C),
        "mid_walk_point": f(0x10),
        "fast_walk_min": f(0x14),
        "gr_friction": f(0x18),
        "dash_initial_velocity": f(0x1C),
        "dash_run_acceleration_a": f(0x20),
        "dash_run_acceleration_b": f(0x24),
        "dash_run_terminal_velocity": f(0x28),
        # Decomp: ftCo_DatAttrs.run_animation_scaling (ft/types.h +0x2C), used by `ftCo_Run_Anim`:
        #   anim_rate = ABS(vel) / fp->co_attrs.run_animation_scaling
        "run_animation_scaling": f(0x2C),
        # Decomp: ftCo_RunBrake_Enter initializes mv.co.runbrake.frames from
        # ftCo_DatAttrs.max_run_brake_frames (ft/types.h +0x30), and ftCo_RunBrake_Anim keeps the
        # state alive while both the AObj and this hidden timer have frames remaining.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
        #   ftCo_RunBrake_Enter,ftCo_RunBrake_Anim}
        "max_run_brake_frames": f(0x30),
        "ground_max_horizontal_velocity": f(0x34),
        "jump_startup_frames": int(max(1, jump_startup_frames)),
        "jump_h_initial_velocity": f(0x3C),
        "jump_v_initial_velocity": f(0x40),
        "hop_v_initial_velocity": f(0x4C),
        "ground_to_air_jump_momentum_multiplier": f(0x44),
        "jump_h_max_velocity": f(0x48),
        # Decomp: ftCo_SpecialS.c::doEnter damps gr_vel by co_attrs.xB8 before dispatching the
        # character-specific grounded Side-B entry (`ftFx_SpecialSStart_Enter`).
        "side_special_ground_entry_vel_mul": f(0xB8),
        # Note: in Melee, max_jumps counts total jumps including the grounded jump.
        "max_jumps": int(i(0x58)),
        "grav": f(0x5C),
        "terminal_vel": f(0x60),
        "air_drift_stick_mul": f(0x64),
        "aerial_drift_base": f(0x68),
        "air_drift_max": f(0x6C),
        "aerial_friction": f(0x70),
        "fast_fall_velocity": f(0x74),
        "air_max_horizontal_velocity": f(0x78),
        "air_jump_v_multiplier": f(0x50),
        "air_jump_h_multiplier": f(0x54),
        "weight": f(0x88),
        # Decomp: ftCo_800DD4B0 checks ftCo_DatAttrs.weight_independent_throws_mask (bitfield by
        # throw index) before applying weight-based throw anim-speed scaling.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
        # refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x180)
        "weight_independent_throws_mask": int(buf[attrs_abs + 0x180]),
        "model_scaling": f(0x8C),
        "initial_shield_size": f(0x90),
        "shield_break_initial_velocity": f(0x94),
        "trophy_scale": f(0x110),
        # Decomp: fp->x2C4 = *fp->ft_data->x50 (ftchangeparam.c: ftCo_800D0FA0 / ftCo_800D105C).
        "pushbox_x": float(_f32_be(buf, pushbox_abs + 0x00)) if pushbox_abs != arc.data_base else 0.0,
        "pushbox_y": float(_f32_be(buf, pushbox_abs + 0x04)) if pushbox_abs != arc.data_base else 0.0,
        "turn_frames": int(max(1, turn_frames)),
        # Rebound anim-speed numerator (ftCo_80099E44):
        # - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
        # - ftCo_80099E44 passes that value as Fighter_ChangeMotionState(..., anim_speed, ...).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
        # refs/melee/src/melee/ft/types.h::ftCo_DatAttrs
        "rebound_anim_numerator_frames": f(0x9C),
        # Rapid-jab mash threshold consumed by ftCo_Attack_800D6A50 (`fp->x1A54 >= rapid_jab_window`).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
        # refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x98)
        "rapid_jab_window": int(max(0, min(255, i(0x98)))),
        "landing_lag_frames": int(max(1, landing_lag_frames)),
        "landing_airn_lag_frames": int(max(1, landing_airn_lag_frames)),
        "landing_airf_lag_frames": int(max(1, landing_airf_lag_frames)),
        "landing_airb_lag_frames": int(max(1, landing_airb_lag_frames)),
        "landing_airhi_lag_frames": int(max(1, landing_airhi_lag_frames)),
        "landing_airlw_lag_frames": int(max(1, landing_airlw_lag_frames)),

        "ledge_jump_horizontal_velocity": f(0xA8),
        "ledge_jump_vertical_velocity": f(0xAC),
        # ft/types.h::ftCo_DatAttrs stores these at struct offsets +0x100/+0x104/+0x108.
        # The adjacent decomp comments also list fighter-relative offsets fp+0x210/+0x214/+0x218;
        # use the struct offsets here because `attrs_abs` already points at `ftData->x0`.
        "passivewall_vel_x": f(0x100),
        "wall_jump_horizontal_velocity": f(0x104),
        "wall_jump_vertical_velocity": f(0x108),
        # Source owner: `ftWallJump_8008169C` first checks `fp->can_walljump`; each character
        # init file sets that bit explicitly if the character can enter PassiveWallJump.
        # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
        # refs/melee/src/melee/ft/chara/*/*_Init.c (`fp->can_walljump = true`)
        "can_walljump": False,
        # ftWallJump_8008169C compares ABS(fp->pos_delta.x - wall_pos.x) against
        # fp->co_attrs.x148 before starting the hidden wall-jump input timer.
        # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
        # refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x148)
        "walljump_setup_x_delta_threshold": f(0x148),
        # Fighter camera subject data:
        # - ftCo_DatAttrs.camera_zoom_target_bone/x170 feeds ftLib_800866DC.
        # - ftData.x3C is copied by ftCamera_80076018 into CmSubject extents.
        # refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
        # refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076064
        "camera_zoom_target_bone_part_id": int(_i32_be(buf, attrs_abs + 0x16C)),
        "camera_zoom_target_offset": [
            float(_f32_be(buf, attrs_abs + 0x170)),
            float(_f32_be(buf, attrs_abs + 0x174)),
            float(_f32_be(buf, attrs_abs + 0x178)),
        ],
        "camera_box_radius": float(_f32_be(buf, camera_abs + 0x14)),

        # Ledge snap parameters: ftData_x44_t (ft/types.h)
        # struct ftData { ... ftData_x44_t* x44; }
        "ledge_snap_x": float(_f32_be(buf, x44_abs + 0x10)),
        "ledge_snap_y": float(_f32_be(buf, x44_abs + 0x14)),
        "ledge_snap_height": float(_f32_be(buf, x44_abs + 0x18)),
        # ECB (environment collision box) joints: ftData_x44_t.
        #
        # These are indices into `fp->parts[]` (the "bones" array in decomp).
        # Decomp: ft_80081B38 -> mpColl_SetECBSource_JObj(..., bones[temp_r29->unk*].joint, ..., temp_r29->unkC * scale_y)
        "ecb_joints": [
            int(_s16_be(buf, x44_abs + 0x00)),
            int(_s16_be(buf, x44_abs + 0x02)),
            int(_s16_be(buf, x44_abs + 0x04)),
            int(_s16_be(buf, x44_abs + 0x06)),
            int(_s16_be(buf, x44_abs + 0x08)),
            int(_s16_be(buf, x44_abs + 0x0A)),
        ],
        "ecb_side_y_offset": float(_f32_be(buf, x44_abs + 0x0C)),
        # Compatibility SFX metadata. Runtime does not currently consume these, but keeping them
        # generated preserves the character JSON contract for downstream tooling.
        #
        # Decomp: refs/melee/src/melee/ft/types.h::FtSFX / ftData.x4C_sfx.
        "smash_sfx_num": int(_i32_be(buf, arc.ptr32(sfx_abs + 0x00) + 0x00)),
    }
    out.update(_extract_wait_anim_choices(buf, wait_anim_abs))
    if ftdata_symbol == "ftDataFox":
        out["damage_post_hitlag_sfx_mid_num"] = 2
        out["damage_post_hitlag_sfx_high_num"] = 2
    elif ftdata_symbol == "ftDataFalco":
        out["damage_post_hitlag_sfx_mid_num"] = 1
        out["damage_post_hitlag_sfx_high_num"] = 2
    if ext_attr_layout == "mars_sword":
        out.update(_extract_mars_sword_attrs(buf, arc, ftdata_abs=ftdata_abs))
    elif ext_attr_layout == "seak_special":
        out.update(_extract_seak_special_attrs(buf, arc, ftdata_abs=ftdata_abs))
        out.update(_extract_seak_needle_article(buf, arc, ftdata_abs=ftdata_abs))
        out.update(_extract_seak_vanish_article(buf, arc, ftdata_abs=ftdata_abs))
        out.update(_extract_seak_chain_article(buf, arc, ftdata_abs=ftdata_abs))
    elif ext_attr_layout == "zelda_special":
        out.update(_extract_zelda_special_attrs(buf, arc, ftdata_abs=ftdata_abs))
    if extract_fox_blaster:
        # struct ftData { ... void* ext_attr; } (ft/types.h +0x4)
        # Fox/Falco ext attrs: struct ftFox_DatAttrs (ft/chara/ftFox/types.h)
        ext_abs = arc.ptr32(ftdata_abs + 0x04)
        # Fox/Falco side special (Illusion/Phantasm) start/end-state parameters.
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        #   `x24_FOX_ILLUSION_GRAVITY_DELAY`
        #   `x28_FOX_ILLUSION_GROUND_VEL_X`
        #   `x2C_FOX_ILLUSION_UNK1`
        #   `x30_FOX_ILLUSION_UNK2`
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSStart_Enter
        #   `fp->gr_vel /= da->x28_FOX_ILLUSION_GROUND_VEL_X;`
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
        #   divides horizontal self velocity similarly.
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
        #     ftFx_SpecialSStart_Phys,ftFx_SpecialAirSStart_Phys}
        out["illusion_gravity_delay_start_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x24))))))
        )
        out["illusion_ground_vel_x"] = float(_f32_be(buf, ext_abs + 0x28))
        out["illusion_air_friction_start"] = float(_f32_be(buf, ext_abs + 0x2C))
        out["illusion_fall_accel_start"] = float(_f32_be(buf, ext_abs + 0x30))
        # End-state velocity + friction parameters (used on main->end transition and in End Phys).
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        #   x34/x38/x3C/x40/x50 fields
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialSEnd_Enter,ftFx_SpecialSEnd_Phys}
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{ftFx_SpecialAirSEnd_Enter,ftFx_SpecialAirSEnd_Phys}
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
        out["illusion_ground_end_vel_x"] = float(_f32_be(buf, ext_abs + 0x34))
        out["illusion_ground_friction"] = float(_f32_be(buf, ext_abs + 0x38))
        out["illusion_air_end_vel_x"] = float(_f32_be(buf, ext_abs + 0x3C))
        out["illusion_air_friction"] = float(_f32_be(buf, ext_abs + 0x40))
        out["illusion_gravity_delay_end_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x44))))))
        )
        out["illusion_fall_accel_end"] = float(_f32_be(buf, ext_abs + 0x48))
        out["illusion_landing_lag_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x50))))))
        )
        out.update(_extract_fox_falco_illusion_item(buf, arc, ftdata_abs=ftdata_abs))
        # Fox/Falco up special HoldAir/Launch/Bound (Firefox/Firebird) attrs.
        #
        # Decomp:
        # - refs/melee/src/melee/ft/chara/ftFox/types.h
        # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        #     ftFx_SpecialHiHoldAir_Phys,ftFx_SpecialAirHi_Enter,ftFx_SpecialHi_Anim,
        #     ftFx_SpecialAirHi_Phys,ftFx_SpecialHiLanding_Phys,ftFx_SpecialHiFall_Anim,
        #     ftFx_SpecialHiBound_Enter
        #   }
        out["firefox_hold_gravity_delay_frames"] = int(max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x54)))))))
        out["firefox_hold_vel_x"] = float(_f32_be(buf, ext_abs + 0x58))
        out["firefox_hold_air_friction"] = float(_f32_be(buf, ext_abs + 0x5C))
        out["firefox_hold_air_fall_accel"] = float(_f32_be(buf, ext_abs + 0x60))
        out["firefox_direction_stick_range_min"] = float(_f32_be(buf, ext_abs + 0x64))
        out["firefox_launch_duration_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x68))))))
        )
        out["firefox_bound_delay_frames"] = int(max(0, min(255, _i32_be(buf, ext_abs + 0x6C))))
        out["firefox_launch_reverse_accel_start_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x70))))))
        )
        out["firefox_launch_speed"] = float(_f32_be(buf, ext_abs + 0x74))
        out["firefox_launch_reverse_accel"] = float(_f32_be(buf, ext_abs + 0x78))
        out["firefox_ground_momentum_end"] = float(_f32_be(buf, ext_abs + 0x7C))
        out["firefox_bound_vel_x"] = float(_f32_be(buf, ext_abs + 0x84))
        out["firefox_facing_stick_range_min"] = float(_f32_be(buf, ext_abs + 0x88))
        out["firefox_freefall_mobility"] = float(_f32_be(buf, ext_abs + 0x8C))
        out["firefox_landing_lag_frames"] = int(
            max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x90))))))
        )
        out["firefox_bound_angle_degrees"] = float(_f32_be(buf, ext_abs + 0x94))
        out["blaster_angle"] = float(_f32_be(buf, ext_abs + 0x10))
        out["blaster_vel"] = float(_f32_be(buf, ext_abs + 0x14))
        out["blaster_shot_itkind"] = int(_u32_be(buf, ext_abs + 0x1C))
        out["blaster_gun_itkind"] = int(_u32_be(buf, ext_abs + 0x20))
        # Fox/Falco reflector (shine) attrs: ftFox_DatAttrs down-special section.
        #
        # Decomp: refs/melee/src/melee/ft/chara/ftFox/types.h (ftFox_DatAttrs):
        # - x98_FOX_REFLECTOR_RELEASE_LAG (float)
        # - x9C_FOX_REFLECTOR_TURN_FRAMES (float)
        # - gravity delay is stored as an s32 but treated as a small frame count.
        try:
            out["reflector_release_lag_frames"] = int(
                max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x98))))))
            )
        except Exception:
            pass
        try:
            out["reflector_turn_frames"] = int(
                max(0, min(255, int(round(float(_f32_be(buf, ext_abs + 0x9C))))))
            )
        except Exception:
            pass
        try:
            out["reflector_gravity_delay_frames"] = int(max(0, min(255, _i32_be(buf, ext_abs + 0xA4))))
        except Exception:
            pass
        out["reflector_momentum_preserve_x"] = float(_f32_be(buf, ext_abs + 0xA8))
        out["reflector_fall_accel"] = float(_f32_be(buf, ext_abs + 0xAC))

        # ReflectDesc (lb/types.h): bone id + offset + size + reflect multipliers.
        refl_abs = ext_abs + 0xB0
        out["reflector_bone_id"] = int(_u32_be(buf, refl_abs + 0x00))
        out["reflector_max_damage"] = int(_i32_be(buf, refl_abs + 0x04))
        out["reflector_offset"] = [
            float(_f32_be(buf, refl_abs + 0x08)),
            float(_f32_be(buf, refl_abs + 0x0C)),
            float(_f32_be(buf, refl_abs + 0x10)),
        ]
        out["reflector_size"] = float(_f32_be(buf, refl_abs + 0x14))
        out["reflector_damage_mul"] = float(_f32_be(buf, refl_abs + 0x18))
        out["reflector_speed_mul"] = float(_f32_be(buf, refl_abs + 0x1C))
        out["reflector_behavior"] = int(buf[refl_abs + 0x20]) if (refl_abs + 0x20) < len(buf) else 0
        out.update(_extract_fox_falco_laser(buf, arc, ftdata_abs=ftdata_abs))
    return out


def _extract_ftparts_rthumb_joint_index(pl_dir: Path, *, character: str) -> int | None:
    """Read ftPartsTable.part_to_joint[FtPart_RThumbNb] from PlCo.dat.

    Decomp/source of truth:
    - refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
    - refs/melee/src/melee/ft/forward.h::FighterKind and Fighter_Part (FtPart_RThumbNb=49)
    - `_iso/PlCo.dat` public symbol `ftLoadCommonData`, field p_ftCommonData->x10
      (`ftPartsTable`) loaded by ftLoadCommonData.
    """
    ftkind_by_name = {
        "fox": 0x01,    # FTKIND_FOX
        "falco": 0x16,  # FTKIND_FALCO
    }
    ftkind = ftkind_by_name.get(character)
    if ftkind is None:
        return None
    plco_path = pl_dir / "PlCo.dat"
    if not plco_path.exists():
        return None
    plco_buf = plco_path.read_bytes()
    plco = parse_hsd_archive(plco_buf)
    ft_load_common_abs = plco.get_public_offset("ftLoadCommonData")
    if ft_load_common_abs is None:
        return None
    # ftLoadCommonData points to a table of common pointers; slot[4] is ftPartsTable.
    # refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
    p_data = [_u32_be(plco_buf, ft_load_common_abs + i * 4) for i in range(23)]
    ft_parts_table_ptr = int(p_data[4])
    if ft_parts_table_ptr == 0:
        return None
    ft_parts_table_abs = plco.data_base + ft_parts_table_ptr
    ft_parts_tbl_ptr = _u32_be(plco_buf, ft_parts_table_abs + int(ftkind) * 4)
    if ft_parts_tbl_ptr == 0:
        return None
    ft_parts_tbl_abs = plco.data_base + ft_parts_tbl_ptr
    part_to_joint_ptr = _u32_be(plco_buf, ft_parts_tbl_abs + 0x04)
    parts_num = _u32_be(plco_buf, ft_parts_tbl_abs + 0x08)
    if part_to_joint_ptr == 0:
        return None
    # Fighter_Part::FtPart_RThumbNb
    ft_part_rthumb_nb = 49
    if parts_num <= ft_part_rthumb_nb:
        return None
    part_to_joint_abs = plco.data_base + part_to_joint_ptr
    if part_to_joint_abs + int(parts_num) > len(plco_buf):
        return None
    return int(plco_buf[part_to_joint_abs + ft_part_rthumb_nb])


def _stable_update(existing: dict, extracted: dict) -> dict:
    out: dict = {}
    # Deprecated keys from previous extractor iterations; drop them on rewrite so downstream
    # consumers don't accidentally treat them as part of the contract.
    drop_keys = {
        "ecb_bone_indices",
    }
    ordered_keys = [
        "walk_init_vel",
        "walk_accel",
        "walk_max_vel",
        "slow_walk_max",
        "mid_walk_point",
        "fast_walk_min",
        "gr_friction",
        "ground_max_horizontal_velocity",
        "turn_frames",
        "rebound_anim_numerator_frames",
        "rapid_jab_window",
        "jump_startup_frames",
        "jump_h_initial_velocity",
        "jump_v_initial_velocity",
        "hop_v_initial_velocity",
        "ground_to_air_jump_momentum_multiplier",
        "jump_h_max_velocity",
        "side_special_ground_entry_vel_mul",
        "max_jumps",
        "grav",
        "terminal_vel",
        "fast_fall_velocity",
        "air_drift_stick_mul",
        "aerial_drift_base",
        "air_drift_max",
        "aerial_friction",
        "air_max_horizontal_velocity",
        "air_jump_v_multiplier",
        "air_jump_h_multiplier",
        "dash_initial_velocity",
        "dash_run_acceleration_a",
        "dash_run_acceleration_b",
        "dash_run_terminal_velocity",
        "run_animation_scaling",
        "max_run_brake_frames",
        "weight",
        "weight_independent_throws_mask",
        "model_scaling",
        "initial_shield_size",
        "shield_break_initial_velocity",
        "trophy_scale",
        "pushbox_x",
        "pushbox_y",
        "grab_capture_anchor_part_id",
        "throw_release_mpcoll_floor_publication_mask",
        "fallspecial_xc0_source_fx_kind_mask",
        "common_fall_blended_ecb_seed_mask",
        "escapeair_carried_floor_wall_source",
        "illusion_gravity_delay_start_frames",
        "illusion_air_friction_start",
        "illusion_fall_accel_start",
        "illusion_ground_vel_x",
        "illusion_ground_end_vel_x",
        "illusion_ground_friction",
        "illusion_air_end_vel_x",
        "illusion_air_friction",
        "illusion_gravity_delay_end_frames",
        "illusion_fall_accel_end",
        "illusion_landing_lag_frames",
        "illusion_item_lifetime_state01_frames",
        "illusion_item_lifetime_state2_frames",
        "illusion_item_hitbox_size",
        "illusion_item_state0_hitbox_y_offset",
        "illusion_item_state0_damage",
        "illusion_item_state0_angle",
        "illusion_item_state0_kbg",
        "illusion_item_state0_wsk",
        "illusion_item_state0_bkb",
        "illusion_item_state0_element",
        "illusion_item_state0_shield_damage",
        "illusion_item_state1_hitbox_y_offset",
        "illusion_item_state1_damage",
        "illusion_item_state1_angle",
        "illusion_item_state1_kbg",
        "illusion_item_state1_wsk",
        "illusion_item_state1_bkb",
        "illusion_item_state1_element",
        "illusion_item_state1_shield_damage",
        "firefox_hold_gravity_delay_frames",
        "firefox_hold_vel_x",
        "firefox_hold_air_friction",
        "firefox_hold_air_fall_accel",
        "firefox_direction_stick_range_min",
        "firefox_launch_duration_frames",
        "firefox_bound_delay_frames",
        "firefox_launch_reverse_accel_start_frames",
        "firefox_launch_speed",
        "firefox_launch_reverse_accel",
        "firefox_ground_momentum_end",
        "firefox_bound_vel_x",
        "firefox_facing_stick_range_min",
        "firefox_freefall_mobility",
        "firefox_landing_lag_frames",
        "firefox_bound_angle_degrees",
        "blaster_angle",
        "blaster_vel",
        "blaster_shot_itkind",
        "blaster_gun_itkind",
        "laser_spawn_joint_part_id",
        "reflector_gravity_delay_frames",
        "reflector_release_lag_frames",
        "reflector_turn_frames",
        "reflector_momentum_preserve_x",
        "reflector_fall_accel",
        "reflector_bone_id",
        "reflector_max_damage",
        "reflector_offset",
        "reflector_size",
        "reflector_damage_mul",
        "reflector_speed_mul",
        "reflector_behavior",
        "laser_lifetime_frames",
        "laser_damage",
        "laser_size",
        "laser_scale_max",
        "laser_hitbox_offsets_x",
        "laser_angle",
        "laser_kbg",
        "laser_wsk",
        "laser_bkb",
        "needle_throw_itkind",
        "needle_held_itkind",
        "needle_lifetime_frames",
        "needle_bounce_lifetime_frames",
        "needle_launch_speed",
        "needle_hurtbox_count",
        "needle_hurtbox_bone_id",
        "needle_hurtbox_a_offset",
        "needle_hurtbox_b_offset",
        "needle_hurtbox_scale",
        "needle_hitbox_damage",
        "needle_hitbox_count",
        "needle_hitbox_damage_by_id",
        "needle_hitbox_bone_id",
        "needle_hitbox_jobj_x_offset",
        "needle_hitbox_jobj_y_offset",
        "needle_hitbox_jobj_z_offset",
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
        "vanish_hitbox_count",
        "vanish_hitbox_damage",
        "vanish_hitbox_size",
        "vanish_hitbox_x_offset",
        "vanish_hitbox_y_offset",
        "vanish_hitbox_z_offset",
        "vanish_hitbox_angle",
        "vanish_hitbox_kbg",
        "vanish_hitbox_wsk",
        "vanish_hitbox_bkb",
        "vanish_hitbox_element",
        "vanish_hitbox_shield_damage",
        "vanish_hitbox_flags",
        "vanish_hitbox_size_keyframe_count",
        "vanish_hitbox_size_keyframe_frame",
        "vanish_hitbox_size_keyframe_value",
        "vanish_hitbox_remove_frame",
        "laser_shield_damage",
        "landing_lag_frames",
        "landing_airn_lag_frames",
        "landing_airf_lag_frames",
        "landing_airb_lag_frames",
        "landing_airhi_lag_frames",
        "landing_airlw_lag_frames",
        "ledge_jump_horizontal_velocity",
        "ledge_jump_vertical_velocity",
        "passivewall_vel_x",
        "wall_jump_horizontal_velocity",
        "wall_jump_vertical_velocity",
        "can_walljump",
        "walljump_setup_x_delta_threshold",
        "camera_zoom_target_bone_part_id",
        "camera_zoom_target_offset",
        "camera_box_radius",
        "wait_anim_choice_msids",
        "wait_anim_choice_weights",
        "ecb_joints",
        "ecb_side_y_offset",
        "ledge_snap_x",
        "ledge_snap_y",
        "ledge_snap_height",
        "smash_sfx_num",
        "damage_post_hitlag_sfx_mid_num",
        "damage_post_hitlag_sfx_high_num",
    ]
    for k in ordered_keys:
        if k in extracted:
            out[k] = extracted[k]
        elif k in existing:
            out[k] = existing[k]
    # Per-character special-attribute families (ext-attr layouts) use mechanic-position
    # prefixes; carry every extracted special* / sheik_* / zelda_* key after the ordered common
    # block.
    for k in sorted(extracted):
        if (k.startswith("special") or k.startswith("sheik_") or k.startswith("zelda_")) and k not in out:
            out[k] = extracted[k]
    for k, v in existing.items():
        if k in drop_keys:
            continue
        if k not in out:
            out[k] = v
    return out


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract per-character ftCo_DatAttrs from Pl*.dat (decomp-first) and update melee_sim character JSONs."
    )
    ap.add_argument("--iso", type=Path, default=None, help="optional path to SSBM.iso (used to extract missing Pl*.dat)")
    ap.add_argument("--pl-dir", type=Path, default=Path("_iso"), help="directory containing extracted Pl*.dat")
    ap.add_argument(
        "--out-dir", type=Path, default=Path("data/characters"), help="directory for character JSON outputs"
    )
    ap.add_argument(
        "--chars",
        type=str,
        default="fox,falco,sheik,zelda,peach,marth,puff,falcon",
        help="comma-separated character set to extract",
    )
    args = ap.parse_args()

    # (pl_dat, ftData symbol, spacie blaster ext-attrs, ext-attr layout tag)
    # Layout tags name the ftData.x4 special-attribute struct family:
    # - "spacie": ftFox_DatAttrs (blaster/illusion/firefox/reflector) - covered by the
    #   extract_fox_blaster flag path.
    # - "mars_sword": MarsAttributes (refs/melee/.../ftMars/types.h) - Marth (and Roy clone).
    # - "seak_special": ftSeakAttributes (refs/melee/.../ftSeak/types.h) - Sheik.
    # - "zelda_special": ftZelda_DatAttrs (refs/melee/.../ftZelda/types.h) - Zelda.
    mapping = {
        "fox": ("PlFx.dat", "ftDataFox", True, None),
        "falco": ("PlFc.dat", "ftDataFalco", True, None),
        "sheik": ("PlSk.dat", "ftDataSeak", False, "seak_special"),
        "zelda": ("PlZd.dat", "ftDataZelda", False, "zelda_special"),
        "peach": ("PlPe.dat", "ftDataPeach", False, None),
        "marth": ("PlMs.dat", "ftDataMars", False, "mars_sword"),
        "puff": ("PlPr.dat", "ftDataPurin", False, None),
        "falcon": ("PlCa.dat", "ftDataCaptain", False, None),
    }
    want = [c.strip() for c in args.chars.split(",") if c.strip()]
    for c in want:
        if c not in mapping:
            raise SystemExit(f"unknown character {c!r} (available: {sorted(mapping)})")
    mapping = {k: mapping[k] for k in want}

    if args.iso is not None:
        files = list_files(args.iso)
        for _, (pl, _, _, _) in mapping.items():
            dst = args.pl_dir / pl
            if dst.exists():
                continue
            matches = find_files(files, f"*{pl}")
            if not matches:
                raise SystemExit(f"missing {pl!r} in ISO")
            extract_file(args.iso, matches[0], dst)
            print(f"wrote {dst} ({matches[0].size} bytes)")

    args.out_dir.mkdir(parents=True, exist_ok=True)

    for name, (pl_name, sym, blaster, ext_layout) in mapping.items():
        pl_path = args.pl_dir / pl_name
        if not pl_path.exists():
            raise SystemExit(f"missing {pl_path} (pass --iso to extract)")

        extracted = _extract_ftco_dattrs(pl_path, ftdata_symbol=sym, extract_fox_blaster=bool(blaster),
                                         ext_attr_layout=ext_layout)
        extracted["can_walljump"] = bool(_source_can_walljump(name))
        if blaster:
            # Decomp ownership: SpecialN spawn joint uses ftParts_GetBoneIndex(fp, FtPart_RThumbNb).
            # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
            # refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
            joint_part = _extract_ftparts_rthumb_joint_index(args.pl_dir, character=name)
            if joint_part is not None:
                extracted["laser_spawn_joint_part_id"] = int(joint_part)
        out_path = args.out_dir / f"{name}.json"
        try:
            existing = json.loads(out_path.read_text())
        except Exception:
            existing = {}
        merged = _stable_update(existing, extracted)
        out_path.write_text(json.dumps(merged, indent=2) + "\n")
        print(f"updated {out_path}")


if __name__ == "__main__":
    main()
