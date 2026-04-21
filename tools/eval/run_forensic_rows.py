from __future__ import annotations

"""Single-row forensic runner for deterministic phase-by-phase triage.

For each (dataset, record, p), this tool emits:
- pre-input seed/input context
- post-timebase snapshot (debug pre-combat step)
- pre-combat geometry + contact classification/filtering
- combat selection/containment context
- post-step compare/internals with per-field mismatch status

Outputs are written under reports/triage/ by default as JSON + text.
"""

import argparse
import importlib
import json
import math
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset
from tools.slippi.suite_io import repo_root


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

_DEBUG_SHIELD_CANDIDATE_DTYPE = np.dtype(
    [
        ("source_kind", "u1"),
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("reject_reason", "u1"),
        ("attacker_hitlag_started_frame", "u1"),
        ("defender_hitlag_started_frame", "u1"),
        ("shield_active", "u1"),
        ("hitbox_enabled", "u1"),
        ("defender_on_ground", "u1"),
        ("hitlist_allows", "u1"),
        ("overlap_shield", "u1"),
        ("element", "u1"),
        ("hb_flags", "<u2"),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_damage", "<f4"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
        ("shield_overlap_margin", "<f4"),
    ],
    align=False,
)

_DEBUG_HITBOX_EVENT_TIMING_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("hb_id", "u1"),
        ("char_id", "u1"),
        ("_pad0", "u1"),
        ("msid", "<u2"),
        ("pose_frame", "<u2"),
        ("anim_frame_f32", "<f4"),
        ("frame_speed_mul_f32", "<f4"),
        ("start_frame", "<i2"),
        ("end_frame", "<i2"),
        ("enabled_prev", "u1"),
        ("enabled_cur", "u1"),
        ("prev_hit_group", "u1"),
        ("cur_hit_group", "u1"),
        ("pose_create_count", "u1"),
        ("pose_clear_count", "u1"),
        ("pose_clear_all_count", "u1"),
        ("enable_edge", "u1"),
        ("last_affect_kind_le", "u1"),
        ("last_affect_kind_eq", "u1"),
        ("last_affect_frame_le", "<u2"),
        ("last_affect_frame_eq", "<u2"),
        ("last_affect_u16_7_le", "<u2"),
        ("last_affect_u16_7_eq", "<u2"),
    ],
    align=False,
)

_DEBUG_CONTACT_BODY_DTYPE = np.dtype(
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

_DEBUG_INTERNALS_DTYPE = np.dtype(
    [
        ("tilt_timer_x", ("u1", (4,))),
        ("turn_frames_to_turn", ("u1", (4,))),
        ("turn_has_turned", ("u1", (4,))),
        ("guard_reflect_timer_x14", ("u1", (4,))),
        ("entry_end_fall_lock", ("u1", (4,))),
        ("attack_id", ("<u2", (4,))),
        ("attack_instance", ("<u2", (4,))),
        ("attack_identity_last_action_id", ("<u2", (4,))),
        ("instance_id", ("<u2", (4,))),
        ("instance_id_x2073", ("u1", (4,))),
        ("instance_identity_last_action_id", ("<u2", (4,))),
        ("instance_id_counter", "<u2"),
        ("throw_pulse_consumed", ("u1", (4,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (4,))),
        ("throw_pending_victim_port", ("u1", (4,))),
        ("throw_pending_hit_idx", ("u1", (4,))),
        ("attached_victim_port", ("u1", (4,))),
    ],
    align=False,
)

_DEBUG_HURTCAP_SLOT_FLAGS_DTYPE = np.dtype(
    [
        ("enabled", "u1"),
        ("height", "u1"),
        ("is_grabbable", "u1"),
        ("mode_can_hit_bit", "u1"),
        ("char_id", "u1"),
        ("_pad0", "u1"),
        ("msid", "<u2"),
        ("frame", "<u2"),
        ("cap_id", "<u2"),
        ("can_hit_mask", "<u4"),
    ],
    align=False,
)

_LASER_ACTION_TURN = 0x0012
_LASER_ACTION_DASH = 0x0014
_LASER_ACTION_FALL = 0x001D
_LASER_ACTION_ATTACK_HI3 = 0x0038


@dataclass(frozen=True)
class RowSpec:
    dataset: str
    record: int
    p: int


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _load_binding():
    return importlib.import_module("msl_binding")


def _dataset_rel(root: Path, p: Path) -> str:
    rp = p.resolve()
    try:
        return str(rp.relative_to(root).as_posix())
    except ValueError:
        return str(rp.as_posix())


def _parse_row_spec(spec: str) -> RowSpec:
    # right-split so dataset paths containing ':' keep left side intact
    parts = spec.rsplit(":", 2)
    if len(parts) != 3:
        raise ValueError(f"invalid --row (expected <dataset>:<record>:<p>): {spec!r}")
    dataset, record_s, p_s = parts
    return RowSpec(dataset=dataset, record=int(record_s), p=int(p_s))


def _resolve_dataset(root: Path, token: str) -> Path:
    p = Path(token)
    if p.is_absolute():
        return p
    return (root / p).resolve()


def _player_inputs(inp: np.ndarray, p: int) -> dict[str, int]:
    view = inp.view(INPUT_DTYPE).reshape((1,))[0]
    return {
        "buttons": int(view["p"]["buttons"][p]),
        "main_x": int(view["p"]["main_x"][p]),
        "main_y": int(view["p"]["main_y"][p]),
        "c_x": int(view["p"]["c_x"][p]),
        "c_y": int(view["p"]["c_y"][p]),
        "l": int(view["p"]["l"][p]),
        "r": int(view["p"]["r"][p]),
    }


def _player_seed(seed: np.ndarray, p: int) -> dict[str, object]:
    return {
        "action_id": int(seed["action_id"][p]),
        "action_frame": int(seed["action_frame"][p]),
        "animation_index": int(seed["animation_index"][p]),
        "on_ground": int(seed["on_ground"][p]),
        "ground_id": int(seed["ground_id"][p]),
        "facing": int(seed["facing"][p]),
        "hitlag": int(seed["hitlag"][p]),
        "hitstun": int(seed["hitstun"][p]),
        "instance_id": int(seed["instance_id"][p]),
        "instance_hit_by": int(seed["instance_hit_by"][p]),
        "last_hit_by": int(seed["last_hit_by"][p]),
        "last_attack_landed": int(seed["last_attack_landed"][p]),
        "combo_count": int(seed["combo_count"][p]),
        "state_flags": [int(x) for x in seed["state_flags"][p].tolist()],
        "guard_reflect_timer_x14": int(seed["guard_reflect_timer_x14"][p]),
        "guard_reflect_timer_x18": int(seed["guard_reflect_timer_x18"][p]),
        "guard_release_latched_xc": int(seed["guard_release_latched_xc"][p]),
        "guard_x10": int(seed["guard_x10"][p]),
        "lightshield_amount": float(seed["lightshield_amount"][p]),
        "pos_x": float(seed["pos_x"][p]),
        "pos_y": float(seed["pos_y"][p]),
        "shield_hp": float(seed["shield_hp"][p]),
        "percent": float(seed["percent"][p]),
    }


def _player_compare(cmp_row: np.ndarray, p: int) -> dict[str, object]:
    return {
        "action_id": int(cmp_row["action_id"][p]),
        "action_frame": int(cmp_row["action_frame"][p]),
        "animation_index": int(cmp_row["animation_index"][p]),
        "on_ground": int(cmp_row["on_ground"][p]),
        "ground_id": int(cmp_row["ground_id"][p]),
        "facing": int(cmp_row["facing"][p]),
        "hitlag": int(cmp_row["hitlag"][p]),
        "hitstun": int(cmp_row["hitstun"][p]),
        "instance_id": int(cmp_row["instance_id"][p]),
        "instance_hit_by": int(cmp_row["instance_hit_by"][p]),
        "last_hit_by": int(cmp_row["last_hit_by"][p]),
        "last_attack_landed": int(cmp_row["last_attack_landed"][p]),
        "combo_count": int(cmp_row["combo_count"][p]),
        "state_flags": [int(x) for x in cmp_row["state_flags"][p].tolist()],
        "pos_x": float(cmp_row["pos_x"][p]),
        "pos_y": float(cmp_row["pos_y"][p]),
        "shield_hp": float(cmp_row["shield_hp"][p]),
        "percent": float(cmp_row["percent"][p]),
    }


def _item_row(row: np.ndarray, slot: int) -> dict[str, object]:
    it = row["items"][slot]
    return {
        "slot": slot,
        "exists": int(it["exists"]),
        "state": int(it["state"]),
        "type": int(it["type"]),
        "owner": int(it["owner"]),
        "instance_id": int(it["instance_id"]),
        "attack_id": int(it["attack_id"]),
        "attack_instance": int(it["attack_instance"]),
        "direction": float(it["direction"]),
        "vel_x": float(it["vel_x"]),
        "vel_y": float(it["vel_y"]),
        "pos_x": float(it["pos_x"]),
        "pos_y": float(it["pos_y"]),
        "damage": int(it["damage"]),
        "timer": float(it["timer"]),
        "spawn_id": int(it["spawn_id"]),
        "misc": [int(it[f"misc{i}"]) for i in range(4)],
    }


def _item_diffs(seed: np.ndarray, ref: np.ndarray, out: np.ndarray, slot: int) -> dict[str, object]:
    diffs: dict[str, object] = {}
    for f in (
        "exists",
        "state",
        "type",
        "owner",
        "instance_id",
        "attack_id",
        "attack_instance",
        "direction",
        "vel_x",
        "vel_y",
        "pos_x",
        "pos_y",
        "damage",
        "timer",
        "spawn_id",
        "misc0",
        "misc1",
        "misc2",
        "misc3",
    ):
        sv = seed["items"][slot][f]
        rv = ref["items"][slot][f]
        ov = out["items"][slot][f]
        if ov != rv:
            if np.issubdtype(np.asarray(ov).dtype, np.floating):
                diffs[f] = {"seed": float(sv), "ref": float(rv), "out": float(ov)}
            else:
                diffs[f] = {"seed": int(sv), "ref": int(rv), "out": int(ov)}
    return diffs


def _timebase_rows(tb: np.ndarray, num_players: int) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for p in range(num_players):
        row = tb[p]
        out.append(
            {
                "p": p,
                "action_id": int(row[0]),
                "animation_index": int(row[1]),
                "action_frame": int(row[2]),
                "anim_frame_f32": float(row[3]),
                "frame_speed_mul_f32": float(row[4]),
                "pose_frame": int(row[5]),
                "hitlag_started_frame": int(row[6]),
                "hurtbox_state": int(row[7]),
            }
        )
    return out


def _contacts_from_raw(raw: np.ndarray, count: int) -> list[dict[str, object]]:
    rows = raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[: int(count)]
    out: list[dict[str, object]] = []
    for r in rows:
        out.append(
            {
                "attacker": int(r["attacker"]),
                "defender": int(r["defender"]),
                "hitbox_id": int(r["hitbox_id"]),
                "contact_kind": int(r["contact_kind"]),
                "hurtcap_id": int(r["hurtcap_id"]),
                "attacker_msid": int(r["attacker_msid"]),
                "attacker_action_frame": int(r["attacker_action_frame"]),
                "hitbox_damage": float(r["hitbox_damage"]),
                "hitbox": [float(r["hitbox_x"]), float(r["hitbox_y"]), float(r["hitbox_z"]), float(r["hitbox_radius"])],
                "hurtcap": [
                    float(r["hurtcap_ax"]),
                    float(r["hurtcap_ay"]),
                    float(r["hurtcap_az"]),
                    float(r["hurtcap_bx"]),
                    float(r["hurtcap_by"]),
                    float(r["hurtcap_bz"]),
                    float(r["hurtcap_radius"]),
                ],
                "shield": [
                    float(r["shield_x"]),
                    float(r["shield_y"]),
                    float(r["shield_z"]),
                    float(r["shield_radius"]),
                ],
            }
        )
    return out


def _body_contacts_from_raw(raw: np.ndarray, count: int) -> list[dict[str, object]]:
    rows = raw.reshape(-1).view(_DEBUG_CONTACT_BODY_DTYPE)[: int(count)]
    out: list[dict[str, object]] = []
    for r in rows:
        out.append(
            {
                "attacker": int(r["attacker"]),
                "defender": int(r["defender"]),
                "hitbox_id": int(r["hitbox_id"]),
                "hurtcap_id": int(r["hurtcap_id"]),
                "attacker_msid": int(r["attacker_msid"]),
                "attacker_action_frame": int(r["attacker_action_frame"]),
                "hitbox_damage": float(r["hitbox_damage"]),
                "hitbox": [float(r["hitbox_x"]), float(r["hitbox_y"]), float(r["hitbox_z"]), float(r["hitbox_radius"])],
                "hurtcap": [
                    float(r["hurtcap_ax"]),
                    float(r["hurtcap_ay"]),
                    float(r["hurtcap_az"]),
                    float(r["hurtcap_bx"]),
                    float(r["hurtcap_by"]),
                    float(r["hurtcap_bz"]),
                    float(r["hurtcap_radius"]),
                ],
            }
        )
    return out


def _point_segment_distance(
    px: float,
    py: float,
    pz: float,
    ax: float,
    ay: float,
    az: float,
    bx: float,
    by: float,
    bz: float,
) -> tuple[float, float]:
    vx = bx - ax
    vy = by - ay
    vz = bz - az
    wx = px - ax
    wy = py - ay
    wz = pz - az
    denom = vx * vx + vy * vy + vz * vz
    if denom <= 0.0:
        t = 0.0
    else:
        t = (wx * vx + wy * vy + wz * vz) / denom
        if t < 0.0:
            t = 0.0
        elif t > 1.0:
            t = 1.0
    cx = ax + t * vx
    cy = ay + t * vy
    cz = az + t * vz
    dx = px - cx
    dy = py - cy
    dz = pz - cz
    return math.sqrt(dx * dx + dy * dy + dz * dz), t


def _segment_segment_distance(
    p0: tuple[float, float, float],
    p1: tuple[float, float, float],
    q0: tuple[float, float, float],
    q1: tuple[float, float, float],
) -> float:
    ux = p1[0] - p0[0]
    uy = p1[1] - p0[1]
    uz = p1[2] - p0[2]
    vx = q1[0] - q0[0]
    vy = q1[1] - q0[1]
    vz = q1[2] - q0[2]
    wx = p0[0] - q0[0]
    wy = p0[1] - q0[1]
    wz = p0[2] - q0[2]

    a = ux * ux + uy * uy + uz * uz
    b = ux * vx + uy * vy + uz * vz
    c = vx * vx + vy * vy + vz * vz
    d = ux * wx + uy * wy + uz * wz
    e = vx * wx + vy * wy + vz * wz
    det = a * c - b * b
    eps = 1.0e-8

    s_n = 0.0
    s_d = det
    t_n = 0.0
    t_d = det

    if det < eps:
        s_n = 0.0
        s_d = 1.0
        t_n = e
        t_d = c
    else:
        s_n = b * e - c * d
        t_n = a * e - b * d
        if s_n < 0.0:
            s_n = 0.0
            t_n = e
            t_d = c
        elif s_n > s_d:
            s_n = s_d
            t_n = e + b
            t_d = c

    if t_n < 0.0:
        t_n = 0.0
        if -d < 0.0:
            s_n = 0.0
        elif -d > a:
            s_n = s_d
        else:
            s_n = -d
            s_d = a
    elif t_n > t_d:
        t_n = t_d
        if (-d + b) < 0.0:
            s_n = 0.0
        elif (-d + b) > a:
            s_n = s_d
        else:
            s_n = -d + b
            s_d = a

    sc = 0.0 if abs(s_n) < eps else s_n / s_d
    tc = 0.0 if abs(t_n) < eps else t_n / t_d
    dx = wx + sc * ux - tc * vx
    dy = wy + sc * uy - tc * vy
    dz = wz + sc * uz - tc * vz
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def _load_laser_probe_params(root: Path) -> dict[int, dict[str, object]]:
    out: dict[int, dict[str, object]] = {}
    for name, lifetime_default in (("fox", 35), ("falco", 100)):
        path = root / "data" / "characters" / f"{name}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        item_type = int(data["blaster_shot_itkind"])
        out[item_type] = {
            "name": name,
            "lifetime": int(data.get("laser_lifetime_frames", lifetime_default)),
            "size": float(data["laser_size"]),
            "scale_max": float(data.get("laser_scale_max", 1.0)),
            "offsets": [float(x) for x in data.get("laser_hitbox_offsets_x", [])],
            "state1_offsets": [
                float(x)
                for x in (
                    data.get("laser_state1_hitbox_offsets_x")
                    or data.get("laser_hitbox_offsets_x", [])
                )
            ],
        }
    return out


def _laser_uses_swept_body(
    player: dict[str, object],
    seed_prev_action_id: int,
    shield_radius: float,
    laser_age_frames: float,
) -> bool:
    if int(player["on_ground"]) == 0:
        return True
    if not (laser_age_frames > 1.0) or shield_radius > 0.0 or int(player["hurtbox_state"]) != 0:
        return False
    action_id = int(player["action_id"])
    if action_id == _LASER_ACTION_ATTACK_HI3:
        return True
    if action_id == _LASER_ACTION_TURN and seed_prev_action_id == _LASER_ACTION_DASH:
        return True
    if action_id == _LASER_ACTION_DASH and int(player["action_frame"]) >= 6:
        return True
    return False


def _collect_item_laser_probes(
    binding: object,
    handle: object,
    seed: np.ndarray,
    ref: np.ndarray,
    out_cmp: np.ndarray,
    post_timebase: list[dict[str, object]],
    defender: int,
    num_players: int,
    root: Path,
    limit: int = 8,
) -> list[dict[str, object]]:
    laser_params = _load_laser_probe_params(root)
    hurtcaps_raw, hurtcap_count = binding.hurtcaps_world(handle, 0, defender)
    shield_world = binding.debug_shield_bubbles_world(handle, 0)
    shield_radius = float(shield_world[defender][3])
    player = post_timebase[defender]
    # Singles FD suites keep fighter cur_pos.z on the gameplay plane; the public compare/seed row
    # does not expose pos_z, but lbColl_8000805C's item BODY path flattens hurtcaps to cur_pos.z.
    defender_pos_z = 0.0
    use_timebase_player = {
        **player,
        "on_ground": int(seed["on_ground"][defender]),
    }

    cap_flags: list[dict[str, object]] = []
    for cap_id in range(int(hurtcap_count)):
        raw = binding.debug_hurtcap_slot_flags(handle, 0, defender, cap_id)
        flags = raw.reshape(-1).view(_DEBUG_HURTCAP_SLOT_FLAGS_DTYPE)[0]
        cap_flags.append(
            {
                "enabled": int(flags["enabled"]),
                "height": int(flags["height"]),
                "is_grabbable": int(flags["is_grabbable"]),
                "mode_can_hit_bit": int(flags["mode_can_hit_bit"]),
                "can_hit_mask": int(flags["can_hit_mask"]),
            }
        )

    probes: list[dict[str, object]] = []
    for slot, it in enumerate(seed["items"]):
        if int(it["exists"]) == 0:
            continue
        item_type = int(it["type"])
        params = laser_params.get(item_type)
        if params is None:
            continue
        owner = int(it["owner"])
        if owner < 0 or owner >= num_players:
            continue
        if owner == defender:
            continue

        vx = float(it["vel_x"])
        vy = float(it["vel_y"])
        speed = math.sqrt(vx * vx + vy * vy)
        if speed > 0.0:
            ux = vx / speed
            uy = vy / speed
        else:
            ux = 1.0 if float(it["direction"]) >= 0.0 else -1.0
            uy = 0.0

        x0 = float(it["pos_x"])
        y0 = float(it["pos_y"])
        x = x0 + vx
        y = y0 + vy
        timer_after = float(it["timer"]) - 1.0
        lifetime = float(params["lifetime"])
        age = max(0.0, lifetime - timer_after)
        scale_z = age * speed / 11.25
        scale_z = min(scale_z, float(params["scale_max"]))
        if scale_z < 1.0e-5:
            scale_z = 1.0e-3
        prev_age = max(0.0, age - 1.0)
        prev_scale_z = prev_age * speed / 11.25
        prev_scale_z = min(prev_scale_z, float(params["scale_max"]))
        if prev_scale_z < 1.0e-5:
            prev_scale_z = 1.0e-3
        state = 1 if int(it["state"]) != 0 else 0
        offsets = params["state1_offsets"] if state else params["offsets"]
        body_shield_adjacent = shield_radius > 0.0
        flatten_body_hurt_z = (
            state == 0
            and int(seed["on_ground"][defender]) == 0
            and int(player["action_id"]) == _LASER_ACTION_FALL
            and int(player["hurtbox_state"]) == 0
            and shield_radius <= 0.0
            and item_type == 55
            and int(seed["last_attack_landed"][defender]) != int(it["attack_id"])
        )
        offset_scale = 1.0 if body_shield_adjacent and scale_z > 1.0 else scale_z
        prev_offset_scale = 1.0 if body_shield_adjacent and prev_scale_z > 1.0 else prev_scale_z
        use_swept = _laser_uses_swept_body(
            use_timebase_player, int(seed["seed_prev_action_id"][defender]), shield_radius, age
        )

        candidates: list[dict[str, object]] = []
        for oi, off_x in enumerate(offsets):
            scaled0 = float(off_x) * prev_offset_scale
            scaled1 = float(off_x) * offset_scale
            unscaled = float(off_x)
            for label, off0, off1 in (
                ("scaled", scaled0, scaled1),
                ("unscaled", unscaled, unscaled),
            ):
                sx0 = x0 + ux * off0
                sy0 = y0 + uy * off0
                sx1 = x + ux * off1
                sy1 = y + uy * off1
                hx0 = sx0 if use_swept else sx1
                hy0 = sy0 if use_swept else sy1
                for cap_id in range(int(hurtcap_count)):
                    cap = hurtcaps_raw[cap_id]
                    cr = float(cap[6])
                    if cr <= 0.0:
                        continue
                    sr = float(params["size"])
                    # Narrow runtime parity lane: item BODY collision calls ftColl_8007925C ->
                    # lbColl_8000805C, passing fp->cur_pos.z; for the promoted Falco-laser/Fall
                    # slice, lbColl's hurtcap-Z rewrite is mirrored here.
                    hurt_z0 = defender_pos_z if flatten_body_hurt_z else float(cap[2])
                    hurt_z1 = defender_pos_z if flatten_body_hurt_z else float(cap[5])
                    dist = _segment_segment_distance(
                        (hx0, hy0, 0.0),
                        (sx1, sy1, 0.0),
                        (float(cap[0]), float(cap[1]), hurt_z0),
                        (float(cap[3]), float(cap[4]), hurt_z1),
                    )
                    margin = sr + cr - dist
                    candidates.append(
                        {
                            "space": label,
                            "offset_index": oi,
                            "offset_x": float(off_x),
                            "offset_applied_prev": float(off0),
                            "offset_applied_cur": float(off1),
                            "cap_id": cap_id,
                            "margin": float(margin),
                            "distance": float(dist),
                            "sphere_radius": sr,
                            "cap_radius": cr,
                            "cap_flags": cap_flags[cap_id],
                            "segment": [float(hx0), float(hy0), float(sx1), float(sy1)],
                            "hurtcap": [
                                float(cap[0]),
                                float(cap[1]),
                                hurt_z0,
                                float(cap[3]),
                                float(cap[4]),
                                hurt_z1,
                                cr,
                            ],
                            "hurtcap_raw": [float(v) for v in cap.tolist()],
                        }
                    )
        candidates.sort(key=lambda r: float(r["margin"]), reverse=True)
        sim_hits = [
            c
            for c in candidates
            if c["space"] == "scaled"
            and bool(c["cap_flags"]["enabled"])
            and float(c["margin"]) >= 0.0
        ]
        probes.append(
            {
                "slot": slot,
                "seed_item": _item_row(seed, slot),
                "ref_item": _item_row(ref, slot),
                "out_item": _item_row(out_cmp, slot),
                "item_diffs": _item_diffs(seed, ref, out_cmp, slot),
                "owner_char": params["name"],
                "laser_state": state,
                "motion": {
                    "x0": x0,
                    "y0": y0,
                    "x": x,
                    "y": y,
                    "vx": vx,
                    "vy": vy,
                    "speed": speed,
                    "timer_after": timer_after,
                    "age_frames": age,
                    "scale_z": scale_z,
                    "prev_scale_z": prev_scale_z,
                    "offset_scale_body": offset_scale,
                    "prev_offset_scale_body": prev_offset_scale,
                    "use_swept_body": use_swept,
                    "shield_radius": shield_radius,
                },
                "defender": {
                    "action_id": int(player["action_id"]),
                    "action_frame": int(player["action_frame"]),
                    "hurtbox_state": int(player["hurtbox_state"]),
                    "on_ground_seed": int(seed["on_ground"][defender]),
                    "seed_prev_action_id": int(seed["seed_prev_action_id"][defender]),
                },
                "sim_scaled_hit_count": len(sim_hits),
                "best_candidates": candidates[:limit],
            }
        )
    return probes


def _closest_body_pairs(
    binding: object,
    handle: object,
    num_players: int,
    defender: int,
    limit: int = 12,
) -> list[dict[str, object]]:
    hurtcaps_raw, hurtcap_count = binding.hurtcaps_world(handle, 0, defender)
    pairs: list[dict[str, object]] = []
    for attacker in range(num_players):
        if attacker == defender:
            continue
        hitboxes_raw, _hitbox_count = binding.hitboxes_world_full(handle, 0, attacker)
        for hb_id, hb in enumerate(hitboxes_raw):
            if int(hb[15]) == 0:
                continue
            hx = float(hb[0])
            hy = float(hb[1])
            hz = float(hb[2])
            hr = float(hb[3])
            for cap_id in range(int(hurtcap_count)):
                cap = hurtcaps_raw[cap_id]
                cr = float(cap[6])
                if cr <= 0.0:
                    continue
                dist, t = _point_segment_distance(
                    hx,
                    hy,
                    hz,
                    float(cap[0]),
                    float(cap[1]),
                    float(cap[2]),
                    float(cap[3]),
                    float(cap[4]),
                    float(cap[5]),
                )
                margin = hr + cr - dist
                pairs.append(
                    {
                        "attacker": attacker,
                        "defender": defender,
                        "hitbox_id": hb_id,
                        "hurtcap_id": cap_id,
                        "margin": float(margin),
                        "distance": float(dist),
                        "segment_t": float(t),
                        "hitbox": [hx, hy, hz, hr],
                        "hurtcap": [
                            float(cap[0]),
                            float(cap[1]),
                            float(cap[2]),
                            float(cap[3]),
                            float(cap[4]),
                            float(cap[5]),
                            cr,
                        ],
                        "element": int(hb[9]),
                        "hb_flags": int(hb[13]),
                        "bone_part_id": int(hb[14]),
                        "damage": float(hb[4]),
                    }
                )
    pairs.sort(key=lambda r: float(r["margin"]), reverse=True)
    return pairs[:limit]


def _shield_candidates_from_raw(raw: np.ndarray, count: int) -> list[dict[str, object]]:
    rows = raw.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[: int(count)]
    out: list[dict[str, object]] = []
    for r in rows:
        out.append(
            {
                "source_kind": int(r["source_kind"]),
                "attacker": int(r["attacker"]),
                "defender": int(r["defender"]),
                "hitbox_id": int(r["hitbox_id"]),
                "reject_reason": int(r["reject_reason"]),
                "attacker_hitlag_started_frame": int(r["attacker_hitlag_started_frame"]),
                "defender_hitlag_started_frame": int(r["defender_hitlag_started_frame"]),
                "shield_active": int(r["shield_active"]),
                "hitbox_enabled": int(r["hitbox_enabled"]),
                "defender_on_ground": int(r["defender_on_ground"]),
                "hitlist_allows": int(r["hitlist_allows"]),
                "overlap_shield": int(r["overlap_shield"]),
                "element": int(r["element"]),
                "hb_flags": int(r["hb_flags"]),
                "attacker_msid": int(r["attacker_msid"]),
                "attacker_action_frame": int(r["attacker_action_frame"]),
                "hitbox_damage": float(r["hitbox_damage"]),
                "hitbox": [
                    float(r["hitbox_x"]),
                    float(r["hitbox_y"]),
                    float(r["hitbox_z"]),
                    float(r["hitbox_radius"]),
                ],
                "shield": [
                    float(r["shield_x"]),
                    float(r["shield_y"]),
                    float(r["shield_z"]),
                    float(r["shield_radius"]),
                ],
                "shield_overlap_margin": float(r["shield_overlap_margin"]),
            }
        )
    return out


def _collect_hitbox_timing(binding: object, handle: object, num_players: int) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for attacker in range(num_players):
        for hb_id in range(4):
            raw = binding.debug_hitbox_event_timing(handle, 0, attacker, hb_id)
            row = raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0]
            if int(row["enabled_cur"]) == 0 and int(row["enabled_prev"]) == 0:
                continue
            out.append(
                {
                    "attacker": attacker,
                    "hb_id": hb_id,
                    "msid": int(row["msid"]),
                    "pose_frame": int(row["pose_frame"]),
                    "start_frame": int(row["start_frame"]),
                    "end_frame": int(row["end_frame"]),
                    "enabled_prev": int(row["enabled_prev"]),
                    "enabled_cur": int(row["enabled_cur"]),
                    "enable_edge": int(row["enable_edge"]),
                    "prev_hit_group": int(row["prev_hit_group"]),
                    "cur_hit_group": int(row["cur_hit_group"]),
                    "last_affect_u16_7_le": int(row["last_affect_u16_7_le"]),
                }
            )
    out.sort(key=lambda r: (r["attacker"], r["hb_id"]))
    return out


def _pack_row_bytes(row: np.ndarray, seed_stride: int, input_stride: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    return seed_bytes, prev_input_bytes, input_bytes


def _diff_focus(ref_p: dict[str, object], out_p: dict[str, object]) -> dict[str, object]:
    fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "hitlag",
        "hitstun",
        "instance_id",
        "instance_hit_by",
        "last_hit_by",
        "last_attack_landed",
        "combo_count",
    )
    diffs: dict[str, object] = {}
    for f in fields:
        if out_p[f] != ref_p[f]:
            diffs[f] = {"out": out_p[f], "ref": ref_p[f]}
    if out_p["state_flags"] != ref_p["state_flags"]:
        diffs["state_flags"] = {"out": out_p["state_flags"], "ref": ref_p["state_flags"]}
    return diffs


def _format_report(payload: dict[str, object]) -> str:
    lines: list[str] = []
    lines.append("# forensic_row_runner")
    lines.append(f"suite_rows: {len(payload['rows'])}")
    lines.append("")

    for i, row in enumerate(payload["rows"], start=1):
        hdr = row["row"]
        lines.append(
            f"## {i}. {Path(hdr['dataset']).name} rec={hdr['record']} p={hdr['p']} frames={hdr['seed_frame']}->{hdr['ref_frame']}"
        )
        lines.append("pre_input:")
        lines.append(
            f"  seed action={row['pre_input']['seed_player']['action_id']} "
            f"af={row['pre_input']['seed_player']['action_frame']} "
            f"anim={row['pre_input']['seed_player']['animation_index']} "
            f"on_ground={row['pre_input']['seed_player']['on_ground']} "
            f"hitlag={row['pre_input']['seed_player']['hitlag']} hitstun={row['pre_input']['seed_player']['hitstun']}"
        )
        lines.append(
            f"  input buttons=0x{row['pre_input']['input_player']['buttons']:04x} "
            f"main=({row['pre_input']['input_player']['main_x']},{row['pre_input']['input_player']['main_y']}) "
            f"c=({row['pre_input']['input_player']['c_x']},{row['pre_input']['input_player']['c_y']}) "
            f"L/R=({row['pre_input']['input_player']['l']},{row['pre_input']['input_player']['r']})"
        )
        lines.append("post_timebase:")
        tb_focus = row["post_timebase"][hdr["p"]]
        lines.append(
            f"  action={tb_focus['action_id']} af={tb_focus['action_frame']} msid={tb_focus['animation_index']} "
            f"anim_f32={tb_focus['anim_frame_f32']:.3f} pose={tb_focus['pose_frame']}"
        )
        lines.append("pre_combat:")
        lines.append(
            f"  contacts classified={row['pre_combat']['classified_count']} "
            f"filtered={row['pre_combat']['filtered_count']} selected_body={row['pre_combat']['selected_body_count']} "
            f"shield_candidates={row['pre_combat']['shield_candidate_count']}"
        )
        lines.append(
            f"  hitboxes_active={len(row['pre_combat']['hitbox_timing_active'])} "
            f"defender_contacts={len(row['pre_combat']['defender_contacts'])}"
        )
        lines.append(
            f"  defender_shield_candidates={len(row['pre_combat']['defender_shield_candidates'])}"
        )
        closest = row["pre_combat"].get("closest_body_pairs", [])
        if closest:
            lines.append("  closest_body_pairs:")
            for c in closest[:5]:
                lines.append(
                    f"    a{c['attacker']} hb{c['hitbox_id']} -> cap{c['hurtcap_id']} "
                    f"margin={c['margin']:.3f} dist={c['distance']:.3f} "
                    f"sum_r={(c['hitbox'][3] + c['hurtcap'][6]):.3f} "
                        f"element={c['element']} flags=0x{c['hb_flags']:04x}"
                )
        probes = row["pre_combat"].get("item_laser_probes", [])
        if probes:
            lines.append("  item_laser_probes:")
            for p in probes:
                motion = p["motion"]
                diffs = p["item_diffs"]
                best = p["best_candidates"][0] if p["best_candidates"] else None
                lines.append(
                    f"    slot{p['slot']} type={p['seed_item']['type']} owner={p['seed_item']['owner']} "
                    f"iid={p['seed_item']['instance_id']} state={p['laser_state']} "
                    f"hits={p['sim_scaled_hit_count']} swept={int(motion['use_swept_body'])} "
                    f"age={motion['age_frames']:.3f} scale={motion['prev_scale_z']:.3f}->{motion['scale_z']:.3f} "
                    f"diffs={','.join(sorted(diffs.keys())) if diffs else '-'}"
                )
                if best:
                    flags = best["cap_flags"]
                    lines.append(
                        f"      best {best['space']} off{best['offset_index']} cap{best['cap_id']} "
                        f"margin={best['margin']:.3f} dist={best['distance']:.3f} "
                        f"height={flags['height']} enabled={flags['enabled']} "
                        f"mode_hit={flags['mode_can_hit_bit']} grabbable={flags['is_grabbable']}"
                    )
        lines.append("post_step:")
        lines.append(
            f"  out action={row['post_step']['out_player']['action_id']} ref={row['post_step']['ref_player']['action_id']} "
            f"hitlag={row['post_step']['out_player']['hitlag']}/{row['post_step']['ref_player']['hitlag']} "
            f"hitstun={row['post_step']['out_player']['hitstun']}/{row['post_step']['ref_player']['hitstun']}"
        )
        diffs = row["post_step"]["focus_diffs"]
        if diffs:
            lines.append(f"  focus_diffs={json.dumps(diffs, sort_keys=True)}")
        else:
            lines.append("  focus_diffs={}")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description="Run deterministic forensic trace for explicit row specs.")
    ap.add_argument(
        "--row",
        action="append",
        default=[],
        help="Row spec <dataset_path>:<record>:<p> (repeatable)",
    )
    ap.add_argument(
        "--rows-json",
        type=Path,
        default=None,
        help="Optional JSON file with array of {dataset,record,p} rows",
    )
    ap.add_argument("--max-contacts", type=int, default=128)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_forensic_rows",
    )
    args = ap.parse_args()

    specs: list[RowSpec] = []
    for s in args.row:
        specs.append(_parse_row_spec(str(s)))

    if args.rows_json is not None:
        payload = json.loads(args.rows_json.read_text(encoding="utf-8"))
        rows = payload if isinstance(payload, list) else payload.get("rows", [])
        for row in rows:
            specs.append(RowSpec(dataset=str(row["dataset"]), record=int(row["record"]), p=int(row["p"])))

    if not specs:
        raise SystemExit("error: provide at least one --row or --rows-json")

    root = repo_root()
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    internals_stride = int(sizes["internals"])

    if internals_stride != int(_DEBUG_INTERNALS_DTYPE.itemsize):
        raise SystemExit(
            f"debug internals stride mismatch: binding={internals_stride} expected={_DEBUG_INTERNALS_DTYPE.itemsize}"
        )

    out_rows: list[dict[str, object]] = []

    for spec in specs:
        dataset_path = _resolve_dataset(root, spec.dataset)
        if not dataset_path.exists():
            raise SystemExit(f"missing dataset: {dataset_path}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        if spec.record < 0 or spec.record >= int(samples.shape[0]):
            raise SystemExit(
                f"record out of range: dataset={dataset_path} record={spec.record} rows={int(samples.shape[0])}"
            )

        row = samples[spec.record : spec.record + 1]
        num_players = int(ds.header["num_players"])
        if spec.p < 0 or spec.p >= num_players:
            raise SystemExit(f"invalid player index p={spec.p} for num_players={num_players}")

        seed_bytes, prev_input_bytes, input_bytes = _pack_row_bytes(row, seed_stride, input_stride)

        handle = binding.init(batch_size=1, num_players=num_players)
        try:
            # Phase 1-3: pre-input -> post-timebase/pre-combat geometry+contacts
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)

            timebase = binding.debug_timebase(handle, 0)
            classified_raw, classified_count = binding.debug_combat_contacts_classified(
                handle, 0, int(args.max_contacts)
            )
            filtered_raw, filtered_count = binding.debug_combat_contacts_classified_filtered(
                handle, 0, int(args.max_contacts)
            )
            selected_raw, selected_count = binding.debug_combat_select_body_hits(
                handle, 0, int(args.max_contacts)
            )
            shield_candidates_raw, shield_candidate_count = binding.debug_shield_candidate_decisions(
                handle, 0, int(args.max_contacts)
            )

            contacts_classified = _contacts_from_raw(classified_raw, int(classified_count))
            contacts_filtered = _contacts_from_raw(filtered_raw, int(filtered_count))
            contacts_selected = _body_contacts_from_raw(selected_raw, int(selected_count))
            shield_candidates = _shield_candidates_from_raw(
                shield_candidates_raw, int(shield_candidate_count)
            )

            defender_contacts = [
                c for c in contacts_classified if int(c["defender"]) == int(spec.p)
            ]
            defender_shield_candidates = [
                c for c in shield_candidates if int(c["defender"]) == int(spec.p)
            ]

            for c in defender_contacts:
                c["hitlist_contains"] = int(
                    binding.debug_hitlist_fighter_contains(
                        handle,
                        0,
                        int(c["attacker"]),
                        int(c["hitbox_id"]),
                        int(c["defender"]),
                    )
                )

            shield_world = binding.debug_shield_bubbles_world(handle, 0)
            hitbox_timing = _collect_hitbox_timing(binding, handle, num_players)
            closest_body_pairs = _closest_body_pairs(binding, handle, num_players, spec.p)

            # Phase 4-5: full step (combat mutations + post timers/state_flags)
            binding.reseed_seed(handle, seed_bytes)
            out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
            out_internals = np.empty((1, internals_stride), dtype=np.uint8)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            binding.debug_write_internals(handle, out_internals)

            # Re-enter the debug pre-combat state after producing full-step output. Item-laser
            # forensics need the collision-time hurtcaps, but also include ref/out item deltas.
            out_cmp_probe = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
            item_laser_probes = _collect_item_laser_probes(
                binding,
                handle,
                row["seed_t"][0],
                row["ref_t1"][0],
                out_cmp_probe,
                _timebase_rows(timebase, num_players),
                spec.p,
                num_players,
                root,
            )
        finally:
            binding.destroy(handle)

        out_cmp = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        out_int = out_internals.view(_DEBUG_INTERNALS_DTYPE).reshape((1,))[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]

        ref_player = _player_compare(ref, spec.p)
        out_player = _player_compare(out_cmp, spec.p)

        out_rows.append(
            {
                "row": {
                    "dataset": _dataset_rel(root, dataset_path),
                    "record": int(spec.record),
                    "p": int(spec.p),
                    "seed_frame": int(seed["frame_id"]),
                    "ref_frame": int(ref["frame_id"]),
                },
                "pre_input": {
                    "seed_player": _player_seed(seed, spec.p),
                    "prev_input_player": _player_inputs(prev_input_bytes, spec.p),
                    "input_player": _player_inputs(input_bytes, spec.p),
                },
                "post_timebase": _timebase_rows(timebase, num_players),
                "pre_combat": {
                    "classified_count": int(classified_count),
                    "filtered_count": int(filtered_count),
                    "selected_body_count": int(selected_count),
                    "shield_candidate_count": int(shield_candidate_count),
                    "defender_contacts": defender_contacts,
                    "defender_shield_candidates": defender_shield_candidates,
                    "classified_contacts": contacts_classified,
                    "filtered_contacts": contacts_filtered,
                    "selected_body_contacts": contacts_selected,
                    "shield_candidate_decisions": shield_candidates,
                    "hitbox_timing_active": hitbox_timing,
                    "closest_body_pairs": closest_body_pairs,
                    "shield_bubbles_world": [
                        [float(v) for v in shield_world[p].tolist()] for p in range(num_players)
                    ],
                    "item_laser_probes": item_laser_probes,
                },
                "post_step": {
                    "ref_player": ref_player,
                    "out_player": out_player,
                    "focus_diffs": _diff_focus(ref_player, out_player),
                    "internals_player": {
                        "guard_reflect_timer_x14": int(out_int["guard_reflect_timer_x14"][spec.p]),
                        "attack_id": int(out_int["attack_id"][spec.p]),
                        "attack_instance": int(out_int["attack_instance"][spec.p]),
                        "instance_id": int(out_int["instance_id"][spec.p]),
                        "instance_id_x2073": int(out_int["instance_id_x2073"][spec.p]),
                        "instance_identity_last_action_id": int(
                            out_int["instance_identity_last_action_id"][spec.p]
                        ),
                    },
                },
            }
        )

    payload = {
        "rows": out_rows,
        "row_count": len(out_rows),
    }

    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / "forensic_rows.json"
    txt_path = out_dir / "forensic_rows.txt"

    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    txt_path.write_text(_format_report(payload), encoding="utf-8")

    print(f"wrote {json_path}")
    print(f"wrote {txt_path}")


if __name__ == "__main__":
    main()
