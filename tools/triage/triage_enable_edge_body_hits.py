from __future__ import annotations

import argparse
import importlib
import json
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

"""
Debug-only triage tool for enable-edge BODY contacts.

WARNING:
- This tool intentionally mutates simulator state during inspection.
- It creates a fresh handle per offender record; do not reuse a single handle across records.
"""


@dataclass(frozen=True)
class Offender:
    dataset_rel: str
    record: int
    victim_port: int
    attacker_port: int
    hb_id: int
    cap_id: int


@dataclass(frozen=True)
class SlabAxisDiag:
    axis: str
    center0: float
    center1: float
    cap0: float
    cap1: float
    temp_f3: float
    lower: float
    upper: float
    reject_low: bool
    reject_high: bool
    reject_axis: bool


@dataclass(frozen=True)
class ScaleVariantDiag:
    name: str
    attacker_scale: float
    defender_scale: float
    dist2: float
    rsum2: float
    sq_margin: float
    depth: float
    t: float
    intersects: int


OFFENDERS: tuple[Offender, ...] = (
    Offender(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
        ),
        record=2221,
        victim_port=0,
        attacker_port=1,
        hb_id=1,
        cap_id=12,
    ),
    Offender(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8222,
        victim_port=1,
        attacker_port=0,
        hb_id=0,
        cap_id=7,
    ),
)


CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),
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

CONTACT_BODY_DTYPE = np.dtype(
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

HITLIST_VICTIM_ENTRY_DTYPE = np.dtype(
    [
        ("id32", "<u4"),
        ("id16", "<u2"),
        ("kind_slot", "u1"),
        ("cd", "u1"),
    ],
    align=False,
)

HITLIST_CAPSULE_DTYPE = np.dtype(
    [
        ("ring_1", "u1"),
        ("ring_2", "u1"),
        ("_pad0", "u1", (2,)),
        ("victims_1", HITLIST_VICTIM_ENTRY_DTYPE, (12,)),
        ("victims_2", HITLIST_VICTIM_ENTRY_DTYPE, (12,)),
    ],
    align=False,
)

HITBOX_EVENT_TIMING_DTYPE = np.dtype(
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

HURTCAP_SLOT_FLAGS_DTYPE = np.dtype(
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

HITBOX_SWEEP_PROXY_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("hb_id", "u1"),
        ("enabled_prev", "u1"),
        ("enabled_cur", "u1"),
        ("prev_valid", "u1"),
        ("cur_valid", "u1"),
        ("_pad0", "u1", (2,)),
        ("msid", "<u2"),
        ("pose_prev", "<u2"),
        ("pose_cur", "<u2"),
        ("char_id", "u1"),
        ("_pad1", "u1"),
        ("anim_frame_f32", "<f4"),
        ("prev_anim_frame_f32", "<f4"),
        ("frame_speed_mul_f32", "<f4"),
        ("prev_x", "<f4"),
        ("prev_y", "<f4"),
        ("prev_z", "<f4"),
        ("prev_radius", "<f4"),
        ("cur_x", "<f4"),
        ("cur_y", "<f4"),
        ("cur_z", "<f4"),
        ("cur_radius", "<f4"),
        ("u16_6_prev", "<u2"),
        ("u16_7_prev", "<u2"),
        ("u16_6_cur", "<u2"),
        ("u16_7_cur", "<u2"),
        ("arg3_var_r22_known", "u1"),
        ("arg3_var_r22_from_extracted", "u1"),
        ("arg3_var_r22_gates_collision", "u1"),
        ("_pad2", "u1"),
    ],
    align=False,
)


def _fmt_timebase(tb: np.ndarray, port: int) -> str:
    # Layout:
    # [action_id, animation_index, action_frame, anim_frame_f32_sanitized, frame_speed_mul_f32,
    #  pose_frame, hitlag_started_frame, hurtbox_state]
    row = tb[port]
    return (
        f"p{port}: act={int(row[0])} msid={int(row[1])} af_i16={int(row[2])} "
        f"anim_f32={float(row[3]):.6f} speed={float(row[4]):.6f} pose={int(row[5])} "
        f"hitlag_started={int(row[6])} hurtbox_state={int(row[7])}"
    )


def _write_report(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def _char_name_from_id(char_id: int) -> str:
    if char_id == 1:
        return "fox"
    if char_id == 22:
        return "falco"
    raise ValueError(f"unsupported char_id for moves lookup: {char_id}")


def _lookup_move_hitbox_event(
    root: Path, char_id: int, msid: int, hb_id: int, frame_hint: int
) -> tuple[int, dict] | None:
    moves_path = root / "data" / "moves" / f"{_char_name_from_id(char_id)}.json"
    if not moves_path.exists():
        return None
    moves_json = json.loads(moves_path.read_text())

    def _events_for_msid() -> list[dict] | None:
        for entry in (moves_json.get("moves") or {}).values():
            if int(entry.get("submotion_id", -1)) == msid:
                return list(entry.get("events") or [])
        sp = moves_json.get("specials_by_msid") or {}
        if str(msid) in sp:
            return list((sp[str(msid)] or {}).get("events") or [])
        return None

    events = _events_for_msid()
    if not events:
        return None

    # Pick the last create_hitbox event for this slot at/before frame_hint.
    best: tuple[int, dict] | None = None
    for ev in events:
        if ev.get("kind") != "create_hitbox":
            continue
        ef = int(ev.get("frame", -1))
        hb = (ev.get("data") or {}).get("hitbox") or {}
        if int(hb.get("hitbox_id", -1)) != hb_id:
            continue
        if ef > frame_hint:
            continue
        if best is None or ef >= best[0]:
            best = (ef, hb)
    return best


def _contact_metrics(binding: object, hb_row: np.ndarray, cap_row: np.ndarray) -> tuple[float, float, float, float, float, int]:
    hx, hy, hz = float(hb_row[0]), float(hb_row[1]), float(hb_row[2])
    hr = float(hb_row[3])
    ax, ay, az, bx, by, bz, cr = map(float, cap_row.tolist())
    dist2, t = binding.debug_point_segment_dist2(hx, hy, hz, ax, ay, az, bx, by, bz)
    dist2 = float(dist2)
    t = float(t)
    rsum = hr + cr
    rsum2 = rsum * rsum
    depth = math.sqrt(max(rsum2, 0.0)) - math.sqrt(max(dist2, 0.0))
    intersects = 1 if dist2 <= rsum2 else 0
    return dist2, t, rsum2, depth, hr, intersects


def _decomp_style_slab_axis(
    axis: str, center0: float, center1: float, cap0: float, cap1: float, temp_f3: float
) -> SlabAxisDiag:
    # Decomp anchor: lbColl_80006E58 early reject slabs, with static center modeled as center0==center1.
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58 (x/y/z blocks around lines 1093..1160)
    if center0 > center1:
        upper = center0 + temp_f3
        reject_high = bool((upper < cap0) and (upper < cap1))
        lower = center1 - temp_f3
        reject_low = bool((lower > cap0) and (lower > cap1))
    else:
        lower = center0 - temp_f3
        reject_low = bool((lower > cap0) and (lower > cap1))
        upper = center1 + temp_f3
        reject_high = bool((upper < cap0) and (upper < cap1))
    reject_axis = bool(reject_low or reject_high)
    return SlabAxisDiag(
        axis=axis,
        center0=center0,
        center1=center1,
        cap0=cap0,
        cap1=cap1,
        temp_f3=temp_f3,
        lower=lower,
        upper=upper,
        reject_low=reject_low,
        reject_high=reject_high,
        reject_axis=reject_axis,
    )


def _facing_dir_from_u8(facing: int) -> float:
    return 1.0 if int(facing) != 0 else -1.0


def _point_reproject_with_alt_scale(
    world_xyz: tuple[float, float, float],
    pos_xyz: tuple[float, float, float],
    facing_dir: float,
    current_scale: float,
    alt_scale: float,
) -> tuple[float, float, float]:
    if not (math.isfinite(current_scale) and current_scale > 0.0):
        return world_xyz

    wx, wy, wz = world_xyz
    px, py, pz = pos_xyz
    rel_x = wx - px
    rel_y = wy - py
    rel_z = wz - pz

    # Inverse of our rotY90(facing)-then-scale mapping:
    # x_rot = facing * z_scaled
    # z_rot = -facing * x_scaled
    local_scaled_x = -facing_dir * rel_z
    local_scaled_y = rel_y
    local_scaled_z = facing_dir * rel_x

    inv_cur = 1.0 / current_scale
    local_unscaled_x = local_scaled_x * inv_cur
    local_unscaled_y = local_scaled_y * inv_cur
    local_unscaled_z = local_scaled_z * inv_cur

    local_alt_x = local_unscaled_x * alt_scale
    local_alt_y = local_unscaled_y * alt_scale
    local_alt_z = local_unscaled_z * alt_scale

    x_rot_alt = facing_dir * local_alt_z
    z_rot_alt = -facing_dir * local_alt_x
    return (px + x_rot_alt, py + local_alt_y, pz + z_rot_alt)


def _contact_metrics_raw(
    binding: object,
    hx: float,
    hy: float,
    hz: float,
    hr: float,
    ax: float,
    ay: float,
    az: float,
    bx: float,
    by: float,
    bz: float,
    cr: float,
) -> tuple[float, float, float, float, int]:
    dist2, t = binding.debug_point_segment_dist2(hx, hy, hz, ax, ay, az, bx, by, bz)
    dist2 = float(dist2)
    t = float(t)
    rsum = hr + cr
    rsum2 = rsum * rsum
    depth = math.sqrt(max(rsum2, 0.0)) - math.sqrt(max(dist2, 0.0))
    intersects = 1 if dist2 <= rsum2 else 0
    return dist2, t, rsum2, depth, intersects


def _segment_segment_dist2(
    p0: tuple[float, float, float],
    p1: tuple[float, float, float],
    q0: tuple[float, float, float],
    q1: tuple[float, float, float],
) -> tuple[float, float, float]:
    px0, py0, pz0 = p0
    px1, py1, pz1 = p1
    qx0, qy0, qz0 = q0
    qx1, qy1, qz1 = q1

    ux, uy, uz = (px1 - px0), (py1 - py0), (pz1 - pz0)
    vx, vy, vz = (qx1 - qx0), (qy1 - qy0), (qz1 - qz0)
    wx, wy, wz = (px0 - qx0), (py0 - qy0), (pz0 - qz0)

    a = ux * ux + uy * uy + uz * uz
    b = ux * vx + uy * vy + uz * vz
    c = vx * vx + vy * vy + vz * vz
    d = ux * wx + uy * wy + uz * wz
    e = vx * wx + vy * wy + vz * wz
    den = a * c - b * b
    eps = 1.0e-8

    def _clamp01(x: float) -> float:
        if x < 0.0:
            return 0.0
        if x > 1.0:
            return 1.0
        return x

    if a <= eps and c <= eps:
        sx = px0 - qx0
        sy = py0 - qy0
        sz = pz0 - qz0
        return sx * sx + sy * sy + sz * sz, 0.0, 0.0

    if a <= eps:
        s = 0.0
        t = _clamp01(e / c) if c > eps else 0.0
    else:
        if c <= eps:
            t = 0.0
            s = _clamp01(-d / a)
        else:
            s = _clamp01((b * e - c * d) / den) if den > eps else 0.0
            t = (b * s + e) / c
            if t < 0.0:
                t = 0.0
                s = _clamp01(-d / a)
            elif t > 1.0:
                t = 1.0
                s = _clamp01((b - d) / a)

    cx = wx + s * ux - t * vx
    cy = wy + s * uy - t * vy
    cz = wz + s * uz - t * vz
    return cx * cx + cy * cy + cz * cz, s, t


def _load_model_scaling(root: Path, char_id: int) -> float:
    path = root / "data" / "characters" / f"{_char_name_from_id(char_id)}.json"
    if not path.exists():
        return 1.0
    data = json.loads(path.read_text())
    val = float(data.get("model_scaling", 1.0))
    if not (math.isfinite(val) and val > 0.0):
        return 1.0
    return val


def _fmt_state_flags(flags_u8_5: np.ndarray) -> str:
    vals = [int(x) & 0xFF for x in flags_u8_5.reshape(-1).tolist()]
    return "[" + " ".join(f"{v:02X}" for v in vals) + "]"


def _fmt_hitlist_capsule(label: str, cap: np.void) -> list[str]:
    lines: list[str] = []
    lines.append(f"{label}: ring_1={int(cap['ring_1'])} ring_2={int(cap['ring_2'])}")
    for lane_name in ("victims_1", "victims_2"):
        lane = cap[lane_name]
        present: list[str] = []
        for i in range(int(lane.shape[0])):
            e = lane[i]
            kind_slot = int(e["kind_slot"]) & 0xFF
            if kind_slot == 0xFF:
                continue
            kind = (kind_slot >> 6) & 0x3
            slot = kind_slot & 0x3F
            present.append(
                f"{lane_name}[{i}]=kind{kind}:slot{slot} iid={int(e['id16'])} cd={int(e['cd'])}"
            )
        if not present:
            lines.append(f"  {lane_name}: (empty)")
        else:
            lines.append(f"  {lane_name}:")
            for s in present:
                lines.append(f"    {s}")
    return lines


def _triage_one(root: Path, off: Offender, out_dir: Path) -> Path:
    ds_path = root / off.dataset_rel
    if not ds_path.exists():
        raise FileNotFoundError(f"missing dataset: {off.dataset_rel}")

    ds = read_dataset(str(ds_path))
    samples = ds.samples
    if int(samples.shape[0]) <= off.record:
        raise ValueError(f"dataset too short: records={int(samples.shape[0])} need>{off.record}")

    row = samples[off.record : off.record + 1]
    seed_t = row["seed_t"].reshape(-1)[0]
    ref_t1 = row["ref_t1"].reshape(-1)[0]

    # Seed/ref sanity for "spurious BODY hit": victim expects no hit at t+1.
    assert int(row["seed_t"]["hitlag"][0, off.victim_port]) == 0
    assert int(row["ref_t1"]["hitlag"][0, off.victim_port]) == 0
    assert int(row["seed_t"]["hitstun"][0, off.victim_port]) == 0
    assert int(row["ref_t1"]["hitstun"][0, off.victim_port]) == 0
    assert int(row["seed_t"]["action_id"][0, off.victim_port]) == int(row["ref_t1"]["action_id"][0, off.victim_port])

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)

        # Run full per-frame pipeline up to combat so we can inspect pre-hit geometry / event gating.
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)

        tb = binding.debug_timebase(handle, 0)
        binding.write_compare(handle, out_compare_bytes)
        pre = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

        timing_raw = binding.debug_hitbox_event_timing(handle, 0, off.attacker_port, off.hb_id)
        assert int(timing_raw.shape[1]) == HITBOX_EVENT_TIMING_DTYPE.itemsize
        timing = timing_raw.reshape(-1).view(HITBOX_EVENT_TIMING_DTYPE)[0]

        # Hitlist state dump (post-pre-combat, pre-combat-resolve).
        hitlist_cur_g = int(timing["cur_hit_group"]) if int(timing["enabled_cur"]) else 0
        seed_cd = int(seed_t["combat_hitlist_cd"][off.attacker_port, hitlist_cur_g, off.victim_port])
        seed_iid = int(
            seed_t["combat_hitlist_victim_iid"][off.attacker_port, hitlist_cur_g, off.victim_port]
        )

        hb_capsule_raw = binding.debug_hitlist_fighter_capsule(handle, 0, off.attacker_port, off.hb_id)
        assert int(hb_capsule_raw.shape[1]) == HITLIST_CAPSULE_DTYPE.itemsize
        hb_capsule = hb_capsule_raw.reshape(-1).view(HITLIST_CAPSULE_DTYPE)[0]

        # Same-group enabled hitboxes as potential ftColl_800768A0 copy sources.
        group_capsules: list[tuple[int, np.void, np.void]] = []
        for hb_other in range(4):
            t_raw = binding.debug_hitbox_event_timing(handle, 0, off.attacker_port, hb_other)
            t_other = t_raw.reshape(-1).view(HITBOX_EVENT_TIMING_DTYPE)[0]
            if int(t_other["enabled_cur"]) == 0:
                continue
            if int(t_other["cur_hit_group"]) != hitlist_cur_g:
                continue
            c_raw = binding.debug_hitlist_fighter_capsule(handle, 0, off.attacker_port, hb_other)
            c_other = c_raw.reshape(-1).view(HITLIST_CAPSULE_DTYPE)[0]
            group_capsules.append((hb_other, t_other, c_other))
        sweep_raw = binding.debug_hitbox_sweep_proxy(handle, 0, off.attacker_port, off.hb_id)
        assert int(sweep_raw.shape[1]) == HITBOX_SWEEP_PROXY_DTYPE.itemsize
        sweep = sweep_raw.reshape(-1).view(HITBOX_SWEEP_PROXY_DTYPE)[0]
        cap_flags_raw = binding.debug_hurtcap_slot_flags(handle, 0, off.victim_port, off.cap_id)
        assert int(cap_flags_raw.shape[1]) == HURTCAP_SLOT_FLAGS_DTYPE.itemsize
        cap_flags = cap_flags_raw.reshape(-1).view(HURTCAP_SLOT_FLAGS_DTYPE)[0]

        hitboxes, _ = binding.hitboxes_world_full(handle, 0, off.attacker_port)
        hb = hitboxes[off.hb_id]
        hx, hy, hz = float(hb[0]), float(hb[1]), float(hb[2])
        hr = float(hb[3])
        hdmg = float(hb[4])
        hb_enabled = int(hb[15])  # enabled is last lane in hitboxes_world_full

        hurtcaps, _ = binding.hurtcaps_world(handle, 0, off.victim_port)
        cap = hurtcaps[off.cap_id]
        ax, ay, az, bx, by, bz, cr = map(float, cap.tolist())

        dist2, t, rsum2, depth, _hr, intersects = _contact_metrics(binding, hb, cap)
        assert abs(_hr - hr) < 1e-6

        attacker_char_id_from_compare = int(pre["char_id"][off.attacker_port])
        defender_char_id_from_compare = int(pre["char_id"][off.victim_port])
        attacker_model_scaling = _load_model_scaling(root, attacker_char_id_from_compare)
        defender_model_scaling = _load_model_scaling(root, defender_char_id_from_compare)

        attacker_scale_y = float(row["seed_t"]["fighter_scale_y"][0, off.attacker_port])
        defender_scale_y = float(row["seed_t"]["fighter_scale_y"][0, off.victim_port])
        attacker_cur_scale = attacker_scale_y * attacker_model_scaling
        defender_cur_scale = defender_scale_y * defender_model_scaling

        attacker_facing = _facing_dir_from_u8(int(pre["facing"][off.attacker_port]))
        defender_facing = _facing_dir_from_u8(int(pre["facing"][off.victim_port]))
        attacker_pos = (
            float(pre["pos_x"][off.attacker_port]),
            float(pre["pos_y"][off.attacker_port]),
            float(row["seed_t"]["pos_z"][0, off.attacker_port]),
        )
        defender_pos = (
            float(pre["pos_x"][off.victim_port]),
            float(pre["pos_y"][off.victim_port]),
            float(row["seed_t"]["pos_z"][0, off.victim_port]),
        )

        hb_world = (hx, hy, hz)
        cap_a_world = (ax, ay, az)
        cap_b_world = (bx, by, bz)

        def _variant(name: str, att_scale: float, def_scale: float) -> ScaleVariantDiag:
            hb_alt = _point_reproject_with_alt_scale(
                hb_world, attacker_pos, attacker_facing, attacker_cur_scale, att_scale
            )
            cap_a_alt = _point_reproject_with_alt_scale(
                cap_a_world, defender_pos, defender_facing, defender_cur_scale, def_scale
            )
            cap_b_alt = _point_reproject_with_alt_scale(
                cap_b_world, defender_pos, defender_facing, defender_cur_scale, def_scale
            )
            cap_r_alt = cr
            if defender_cur_scale > 0.0:
                cap_r_alt = cr * (def_scale / defender_cur_scale)
            d2, t_alt, r2, depth_alt, inter = _contact_metrics_raw(
                binding,
                hb_alt[0],
                hb_alt[1],
                hb_alt[2],
                hr,
                cap_a_alt[0],
                cap_a_alt[1],
                cap_a_alt[2],
                cap_b_alt[0],
                cap_b_alt[1],
                cap_b_alt[2],
                cap_r_alt,
            )
            return ScaleVariantDiag(
                name=name,
                attacker_scale=att_scale,
                defender_scale=def_scale,
                dist2=d2,
                rsum2=r2,
                sq_margin=r2 - d2,
                depth=depth_alt,
                t=t_alt,
                intersects=inter,
            )

        scale_variants = [
            _variant("current", attacker_cur_scale, defender_cur_scale),
            _variant("attacker_model1_only", attacker_scale_y, defender_cur_scale),
            _variant("defender_model1_only", attacker_cur_scale, defender_scale_y),
            _variant("both_model1", attacker_scale_y, defender_scale_y),
        ]

        raw_contacts, count = binding.debug_combat_contacts_classified_filtered(handle, 0, 512)
        assert int(raw_contacts.shape[1]) == CONTACT_CLASSIFIED_DTYPE.itemsize
        contacts = raw_contacts.reshape(-1).view(CONTACT_CLASSIFIED_DTYPE)[:count]

        body_contacts = contacts[contacts["contact_kind"] == 0]
        body_match = body_contacts[
            (body_contacts["attacker"] == off.attacker_port)
            & (body_contacts["defender"] == off.victim_port)
            & (body_contacts["hitbox_id"] == off.hb_id)
            & (body_contacts["hurtcap_id"] == off.cap_id)
        ]

        sel_raw, sel_count = binding.debug_combat_select_body_hits(handle, 0, 16)
        assert int(sel_raw.shape[1]) == CONTACT_BODY_DTYPE.itemsize
        selected = sel_raw.reshape(-1).view(CONTACT_BODY_DTYPE)[:sel_count]
        sel_match = selected[
            (selected["attacker"] == off.attacker_port)
            & (selected["defender"] == off.victim_port)
            & (selected["hitbox_id"] == off.hb_id)
            & (selected["hurtcap_id"] == off.cap_id)
        ]

        strict_lt_enabled = int(timing["enabled_prev"])
        strict_lt_disappears = int((int(body_match.shape[0]) > 0) and strict_lt_enabled == 0)

        # Decomp mapping note:
        # - ftAction_80073240 uses frame_count = cur_anim_frame + x898_unk.
        # - Current sim has no x898_unk field/state; model offset is fixed 0.0.
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        attacker_anim_f32 = float(tb[off.attacker_port, 3])
        frame_count_offset = 0.0
        frame_count_equiv = attacker_anim_f32 + frame_count_offset
        pose_floor_offset = 0.0
        pose_frame_used = int(timing["pose_frame"])

        attacker_char_id = int(row["seed_t"]["char_id"][0, off.attacker_port])
        attacker_msid = int(timing["msid"])
        attacker_start_frame = int(timing["start_frame"]) if int(timing["start_frame"]) >= 0 else int(timing["pose_frame"])
        move_event = _lookup_move_hitbox_event(
            root, attacker_char_id, attacker_msid, off.hb_id, attacker_start_frame
        )
        move_event_frame = -1
        move_event_bone = -1
        move_event_use_common = False
        if move_event is not None:
            move_event_frame = int(move_event[0])
            move_event_bone = int(move_event[1].get("bone", -1))
            move_event_use_common = bool(move_event[1].get("use_common_bone_ids", False))

        hb_bone_part_id = int(hb[14])
        hb_u16_6 = int(hb[13]) & 0xFFFF
        hb_ignore_scale = 1 if (hb_u16_6 & (1 << 13)) != 0 else 0

        decomp_arg9_scl_infer = hr
        decomp_arg10_base_infer = float("nan")
        if defender_cur_scale > 0.0:
            decomp_arg10_base_infer = cr / defender_cur_scale
        decomp_arg10_base_if_no_model_scaling = float("nan")
        if defender_scale_y > 0.0:
            decomp_arg10_base_if_no_model_scaling = cr / defender_scale_y
        decomp_arg11_infer = 3.0 * defender_scale_y
        decomp_temp_f3_infer = float("nan")
        if math.isfinite(decomp_arg10_base_infer):
            decomp_temp_f3_infer = decomp_arg9_scl_infer + decomp_arg10_base_infer * decomp_arg11_infer

        sweep_prev_xyz = (float(sweep["prev_x"]), float(sweep["prev_y"]), float(sweep["prev_z"]))
        sweep_cur_xyz = (float(sweep["cur_x"]), float(sweep["cur_y"]), float(sweep["cur_z"]))
        if int(sweep["prev_valid"]) == 0:
            sweep_prev_xyz = (hx, hy, hz)
        if int(sweep["cur_valid"]) == 0:
            sweep_cur_xyz = (hx, hy, hz)
        sweep_radius = max(float(sweep["prev_radius"]), float(sweep["cur_radius"]), hr)
        sweep_dist2, sweep_t_hit, sweep_t_cap = _segment_segment_dist2(
            sweep_prev_xyz,
            sweep_cur_xyz,
            (ax, ay, az),
            (bx, by, bz),
        )
        sweep_rsum2 = (sweep_radius + cr) * (sweep_radius + cr)
        sweep_depth = math.sqrt(max(sweep_rsum2, 0.0)) - math.sqrt(max(sweep_dist2, 0.0))
        sweep_intersects = 1 if sweep_dist2 <= sweep_rsum2 else 0

        decomp_slab_axes_infer: list[SlabAxisDiag] = []
        decomp_slab_reject_infer = False
        decomp_slab_axes_sweep: list[SlabAxisDiag] = []
        decomp_slab_reject_sweep = False
        if math.isfinite(decomp_temp_f3_infer):
            # lbColl_80006E58 receives two hit endpoints (`arg0/arg1` == hit.x58/hit.x4C).
            # Static-center probe isolates temp_f3/axis-slab effect with center0==center1.
            decomp_slab_axes_infer = [
                _decomp_style_slab_axis("x", hx, hx, ax, bx, decomp_temp_f3_infer),
                _decomp_style_slab_axis("y", hy, hy, ay, by, decomp_temp_f3_infer),
                _decomp_style_slab_axis("z", hz, hz, az, bz, decomp_temp_f3_infer),
            ]
            decomp_slab_reject_infer = any(a.reject_axis for a in decomp_slab_axes_infer)
            decomp_slab_axes_sweep = [
                _decomp_style_slab_axis("x", sweep_prev_xyz[0], sweep_cur_xyz[0], ax, bx, decomp_temp_f3_infer),
                _decomp_style_slab_axis("y", sweep_prev_xyz[1], sweep_cur_xyz[1], ay, by, decomp_temp_f3_infer),
                _decomp_style_slab_axis("z", sweep_prev_xyz[2], sweep_cur_xyz[2], az, bz, decomp_temp_f3_infer),
            ]
            decomp_slab_reject_sweep = any(a.reject_axis for a in decomp_slab_axes_sweep)

        event_frame_for_timing = move_event_frame
        if event_frame_for_timing < 0:
            event_frame_for_timing = int(timing["last_affect_frame_eq"])
        pose_frame_for_timing = int(timing["pose_frame"])
        cur_frame_count = frame_count_equiv
        prev_frame_count = cur_frame_count - float(timing["frame_speed_mul_f32"])
        pure_int_le = int(event_frame_for_timing <= pose_frame_for_timing)
        pure_int_lt = int(event_frame_for_timing < pose_frame_for_timing)
        crossing_fire = int(
            (prev_frame_count < float(event_frame_for_timing))
            and (float(event_frame_for_timing) <= cur_frame_count)
        )

        qgd_floor_ceil_lines: list[str] = []
        victim_anim_f32 = float(tb[off.victim_port, 3])
        if Path(off.dataset_rel).stem == "QuerulousGrandDinosaur":
            f0 = int(math.floor(victim_anim_f32))
            f1 = f0 + 1
            attacker_pose = int(tb[off.attacker_port, 5])
            attacker_speed = float(tb[off.attacker_port, 4])
            victim_speed = float(tb[off.victim_port, 4])

            # Clamp f1 by probing whether pose matrices exist at that frame for victim msid.
            victim_msid = int(tb[off.victim_port, 1])
            victim_char_id = int(row["seed_t"]["char_id"][0, off.victim_port])
            try:
                binding.anim_pose_matrix(victim_char_id, victim_msid, f1, 0)
            except Exception:
                f1 = f0

            qgd_floor_ceil_lines.append("")
            qgd_floor_ceil_lines.append("QGD floor/ceil pose probe (debug-only geometry refresh):")
            qgd_floor_ceil_lines.append(
                f"  victim_anim_f32={victim_anim_f32:.6f} -> f0={f0} f1={f1} (attacker fixed at pose={attacker_pose})"
            )
            for label, vf in (("f0", f0), ("f1", f1)):
                binding.debug_force_anim_timebase_enter(handle, 0, off.attacker_port, float(attacker_pose), attacker_speed)
                binding.debug_force_anim_timebase_enter(handle, 0, off.victim_port, float(vf), victim_speed)
                binding.debug_refresh_combat_geometry(handle)
                hb_probe, _ = binding.hitboxes_world_full(handle, 0, off.attacker_port)
                cap_probe, _ = binding.hurtcaps_world(handle, 0, off.victim_port)
                d2_p, t_p, r2_p, depth_p, _hr_p, inter_p = _contact_metrics(
                    binding, hb_probe[off.hb_id], cap_probe[off.cap_id]
                )
                qgd_floor_ceil_lines.append(
                    f"  {label}: dist2={d2_p:.9f} r_sum^2={r2_p:.9f} sq_margin={(r2_p - d2_p):.9f} "
                    f"depth={depth_p:.9f} t={t_p:.6f} intersects={inter_p}"
                )

        # Apply combat only (mutating) so we can record the actual spurious outcome for the victim.
        #
        # NOTE: This tool performs debug-only probing above that intentionally mutates simulator
        # state (timebase/geometry refresh). Reset to the original seeded pre-combat state before
        # applying combat so the post-combat outcome corresponds to the same state used for the
        # earlier contact/hitlist dumps.
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        binding.debug_combat_resolve(handle)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

        got_action = int(out["action_id"][off.victim_port])
        got_hitlag = int(out["hitlag"][off.victim_port])
        got_hitstun = int(out["hitstun"][off.victim_port])

        exp_action = int(row["ref_t1"]["action_id"][0, off.victim_port])
        exp_hitlag = int(row["ref_t1"]["hitlag"][0, off.victim_port])
        exp_hitstun = int(row["ref_t1"]["hitstun"][0, off.victim_port])

        lines: list[str] = []
        lines.append(f"dataset: {off.dataset_rel}")
        lines.append(f"record: {off.record} victim_port={off.victim_port}")
        lines.append(f"attacker_port={off.attacker_port} hb_id={off.hb_id} cap_id={off.cap_id}")
        lines.append("WARNING: mutates simulator state; this report is generated from a fresh handle.")
        lines.append("")
        lines.append("seed/ref/out snapshot (required fields):")
        for p in range(int(ds.header["num_players"])):
            lines.append(
                "  "
                f"p{p} seed: act={int(seed_t['action_id'][p])} hitlag={int(seed_t['hitlag'][p])} "
                f"hitstun={int(seed_t['hitstun'][p])} percent={float(seed_t['percent'][p]):.3f} "
                f"state_flags={_fmt_state_flags(seed_t['state_flags'][p])}"
            )
            lines.append(
                "  "
                f"p{p} ref:  act={int(ref_t1['action_id'][p])} hitlag={int(ref_t1['hitlag'][p])} "
                f"hitstun={int(ref_t1['hitstun'][p])} percent={float(ref_t1['percent'][p]):.3f} "
                f"state_flags={_fmt_state_flags(ref_t1['state_flags'][p])}"
            )
            lines.append(
                "  "
                f"p{p} out:  act={int(out['action_id'][p])} hitlag={int(out['hitlag'][p])} "
                f"hitstun={int(out['hitstun'][p])} percent={float(out['percent'][p]):.3f} "
                f"state_flags={_fmt_state_flags(out['state_flags'][p])}"
            )
        lines.append("")
        lines.append("pre-combat timebase (runtime):")
        lines.append(_fmt_timebase(tb, off.attacker_port))
        lines.append(_fmt_timebase(tb, off.victim_port))
        lines.append(
            f"frame_count_equiv(model)=anim_f32 + x898_offset = {frame_count_equiv:.6f} + {frame_count_offset:.6f}"
        )
        lines.append(
            f"pose_frame_used=floor(anim_f32 + pose_offset) = floor({attacker_anim_f32:.6f} + {pose_floor_offset:.6f}) = {pose_frame_used}"
        )
        lines.append("")
        lines.append("hitbox event timing (attacker hb slot):")
        lines.append(
            "  "
            f"char_id={int(timing['char_id'])} msid={int(timing['msid'])} "
            f"anim_f32={float(timing['anim_frame_f32']):.6f} speed={float(timing['frame_speed_mul_f32']):.6f} "
            f"pose={int(timing['pose_frame'])}"
        )
        lines.append(
            "  "
            f"enabled_prev={int(timing['enabled_prev'])} enabled_cur={int(timing['enabled_cur'])} "
            f"prev_g={int(timing['prev_hit_group'])} cur_g={int(timing['cur_hit_group'])} "
            f"enable_edge={int(timing['enable_edge'])}"
        )
        lines.append(
            "  "
            f"pose_create={int(timing['pose_create_count'])} pose_clear={int(timing['pose_clear_count'])} "
            f"pose_clear_all={int(timing['pose_clear_all_count'])}"
        )
        lines.append(
            "  "
            f"window_start={int(timing['start_frame'])} window_end={int(timing['end_frame'])}"
        )
        lines.append(
            "  "
            f"last_affect_le=(kind={int(timing['last_affect_kind_le'])}, frame={int(timing['last_affect_frame_le'])}, "
            f"u16_7={int(timing['last_affect_u16_7_le'])})"
        )
        lines.append(
            "  "
            f"last_affect_eq=(kind={int(timing['last_affect_kind_eq'])}, frame={int(timing['last_affect_frame_eq'])}, "
            f"u16_7={int(timing['last_affect_u16_7_eq'])})"
        )
        lines.append(
            "  "
            f"timing_pure_integer: ev_frame={event_frame_for_timing} pose_frame={pose_frame_for_timing} "
            f"fire_if_ev<=pose={pure_int_le} fire_if_ev<pose={pure_int_lt}"
        )
        lines.append(
            "  "
            f"timing_float_crossing: prev_frame_count={prev_frame_count:.6f} "
            f"cur_frame_count={cur_frame_count:.6f} ev_frame={event_frame_for_timing:.6f} "
            f"fire_if_prev<ev<=cur={crossing_fire} (x898_offset={frame_count_offset:.6f})"
        )
        lines.append(
            "  "
            f"timing_counterfactual(ev.frame < pose only): enabled={strict_lt_enabled} "
            f"would_remove_offender_contact={strict_lt_disappears}"
        )
        lines.append("  decomp_collision_args_trace (fighter-vs-fighter body path):")
        lines.append(
            "  "
            "ftColl_80078C70 -> lbColl_8000805C(hit, hurt, ..., "
            f"arg4=attacker_scale_y={attacker_scale_y:.6f}, "
            f"arg5=defender_scale_y={defender_scale_y:.6f}, arg6=defender_pos_z)"
        )
        lines.append(
            "  "
            "lbColl_8000805C -> lbColl_80006E58(... "
            f"scl(arg9)~hit_scale={decomp_arg9_scl_infer:.6f}, "
            f"arg10~hurt_scale_base={decomp_arg10_base_infer:.6f}, "
            f"arg11=3.0*def_scale={decomp_arg11_infer:.6f}, "
            f"temp_f3=(arg10*arg11)+scl={decomp_temp_f3_infer:.6f})"
        )
        lines.append(
            "  "
            f"arg10_if_model_scaling_cancelled={decomp_arg10_base_if_no_model_scaling:.6f} "
            f"(hb_ignore_scale_flag={hb_ignore_scale}, arg3_var_r22_fastpath=unknown)"
        )
        lines.append(
            "  "
            "var_r22(arg3) source in decomp: temp_r23->x43_b2 "
            "(refs/melee/src/melee/ft/ftcoll.c:1065); "
            "lbColl_8000805C short-circuits hit when arg3!=0 "
            "(refs/melee/src/melee/lb/lbcollision.c:1642)"
        )
        lines.append(
            "  "
            f"sim_equiv(var_r22): known={int(sweep['arg3_var_r22_known'])} "
            f"from_extracted={int(sweep['arg3_var_r22_from_extracted'])} "
            f"gates_collision={int(sweep['arg3_var_r22_gates_collision'])} "
            f"(MSLHITB1 u16_6=0x{hb_u16_6:04X} excludes x43_b2); current model assumes arg3=0"
        )
        lines.append(
            "  "
            "x43_b2 write sites (fighter): create_hitbox initializes x43_b2=0 "
            "(refs/melee/src/melee/ft/ftaction.c:359); "
            "character runtime can flip it (e.g., Puff Rest sets x43_b2=1: "
            "refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialHi.c:96)"
        )
        lines.append(
            "  "
            "x43_b2 counterfactuals: decomp arg3 fastpath (x43_b2=1 => arg3!=0) would_accept=1; "
            "anti-decomp hypothetical 'require x43_b2=1 to accept' would_accept=0"
        )
        lines.append(
            "  "
            "extractability: x43_b2 is script-created as 0 then can be runtime-mutated by char code "
            "(refs/melee/src/melee/ft/ftaction.c:359), so static extracted tables alone are insufficient"
        )
        lines.append(
            "  "
            "ftCommon_8007F804(arg2 mtx): non-NULL only if fighter x34_scale.z != 1 "
            "(refs/melee/src/melee/ft/ftcommon.c:1453); sim has no fighter_scale_z lane, "
            "so equivalent mtx is always NULL today"
        )
        lines.append(
            "  "
            f"hitbox sweep proxy (hb={off.hb_id}): enabled_prev={int(sweep['enabled_prev'])} "
            f"enabled_cur={int(sweep['enabled_cur'])} prev_valid={int(sweep['prev_valid'])} "
            f"cur_valid={int(sweep['cur_valid'])} pose_prev={int(sweep['pose_prev'])} "
            f"pose_cur={int(sweep['pose_cur'])}"
        )
        lines.append(
            "  "
            f"sweep centers: prev=({sweep_prev_xyz[0]:.6f},{sweep_prev_xyz[1]:.6f},{sweep_prev_xyz[2]:.6f}) "
            f"cur=({sweep_cur_xyz[0]:.6f},{sweep_cur_xyz[1]:.6f},{sweep_cur_xyz[2]:.6f}) "
            f"r_prev={float(sweep['prev_radius']):.6f} r_cur={float(sweep['cur_radius']):.6f}"
        )
        lines.append(
            "  "
            f"sweep flags: u16_6_prev=0x{int(sweep['u16_6_prev']):04X} u16_7_prev=0x{int(sweep['u16_7_prev']):04X} "
            f"u16_6_cur=0x{int(sweep['u16_6_cur']):04X} u16_7_cur=0x{int(sweep['u16_7_cur']):04X}"
        )
        lines.append("  victim_hurtcap_eligibility (runtime + mode mask):")
        lines.append(
            "  "
            f"enabled={int(cap_flags['enabled'])} height={int(cap_flags['height'])} "
            f"is_grabbable={int(cap_flags['is_grabbable'])} "
            f"mode_can_hit_bit={int(cap_flags['mode_can_hit_bit'])}"
        )
        lines.append(
            "  "
            f"mask_ctx: char_id={int(cap_flags['char_id'])} msid={int(cap_flags['msid'])} "
            f"frame={int(cap_flags['frame'])} can_hit_mask=0x{int(cap_flags['can_hit_mask']):08X}"
        )
        lines.append("  bone/remap evidence:")
        lines.append(
            "  "
            f"runtime_hitbox_lane: bone_part_id={hb_bone_part_id} u16_6=0x{hb_u16_6:04X} "
            "(MSLHITB1 v1 u16_6 has no use_common_bone_ids bit)"
        )
        lines.append(
            "  "
            f"moves_event: frame={move_event_frame} bone={move_event_bone} use_common_bone_ids={int(move_event_use_common)}"
        )
        lines.append(
            "  extractor policy: use_common_bone_ids=1 is rejected during extraction "
            "(tools/extraction/extract_fighter_hitboxes.py)"
        )
        lines.append("")
        lines.append("narrowphase geometry (pre-combat):")
        lines.append(f"  hitbox: center=({hx:.6f},{hy:.6f},{hz:.6f}) r={hr:.6f} dmg={hdmg:.3f} enabled={hb_enabled}")
        lines.append(
            f"  hurtcap[{off.cap_id}]: a=({ax:.6f},{ay:.6f},{az:.6f}) b=({bx:.6f},{by:.6f},{bz:.6f}) r={cr:.6f}"
        )
        lines.append(
            f"  closest t={t:.6f} dist2={dist2:.9f} (r_sum^2={rsum2:.9f}) "
            f"sq_margin={(rsum2 - dist2):.9f} depth={depth:.9f} intersects={int(intersects)}"
        )
        lines.append("")
        lines.append("decomp slab probe (inferred temp_f3):")
        if math.isfinite(decomp_temp_f3_infer):
            lines.append(
                "  "
                f"temp_f3_infer={decomp_temp_f3_infer:.6f} "
                "(uses inferred scl/arg10/arg11)"
            )
            lines.append("  static-center slab (center0=center1=current center):")
            for a in decomp_slab_axes_infer:
                lines.append(
                    "  "
                    f"{a.axis}: center=[{a.center0:.6f},{a.center1:.6f}] cap=[{a.cap0:.6f},{a.cap1:.6f}] "
                    f"expanded=[{a.lower:.6f},{a.upper:.6f}] reject_low={int(a.reject_low)} "
                    f"reject_high={int(a.reject_high)} reject_axis={int(a.reject_axis)}"
                )
            lines.append(
                f"  decomp_slab_reject_overall={int(decomp_slab_reject_infer)} "
                f"narrowphase_intersects={int(intersects)}"
            )
            lines.append("  sweep slab (center0=prev proxy, center1=cur proxy):")
            for a in decomp_slab_axes_sweep:
                lines.append(
                    "  "
                    f"{a.axis}: center=[{a.center0:.6f},{a.center1:.6f}] cap=[{a.cap0:.6f},{a.cap1:.6f}] "
                    f"expanded=[{a.lower:.6f},{a.upper:.6f}] reject_low={int(a.reject_low)} "
                    f"reject_high={int(a.reject_high)} reject_axis={int(a.reject_axis)}"
                )
            lines.append(
                f"  sweep_slab_reject={int(decomp_slab_reject_sweep)} "
                f"sweep_narrowphase_intersects={int(sweep_intersects)}"
            )
            lines.append(
                "  "
                f"sweep_narrowphase_detail: segseg_dist2={sweep_dist2:.9f} "
                f"r_sum^2={sweep_rsum2:.9f} sq_margin={(sweep_rsum2 - sweep_dist2):.9f} "
                f"depth={sweep_depth:.9f} t_hit={sweep_t_hit:.6f} t_cap={sweep_t_cap:.6f}"
            )
        else:
            lines.append("  temp_f3_infer=nan (missing inferred arg mapping); decomp slab reject unknown")
        lines.append("")
        lines.append("model-scaling sensitivity probe (debug-only):")
        lines.append(
            f"  attacker scale_y={attacker_scale_y:.6f} model_scaling={attacker_model_scaling:.6f} current={attacker_cur_scale:.6f}"
        )
        lines.append(
            f"  defender scale_y={defender_scale_y:.6f} model_scaling={defender_model_scaling:.6f} current={defender_cur_scale:.6f}"
        )
        for v in scale_variants:
            lines.append(
                "  "
                f"{v.name}: att_scale={v.attacker_scale:.6f} def_scale={v.defender_scale:.6f} "
                f"dist2={v.dist2:.9f} r_sum^2={v.rsum2:.9f} sq_margin={v.sq_margin:.9f} "
                f"depth={v.depth:.9f} t={v.t:.6f} intersects={v.intersects}"
            )
        lines.extend(qgd_floor_ceil_lines)
        lines.append("")
        lines.append("debug contact enumeration (pre-combat):")
        lines.append(f"  classified_filtered count={int(count)} body_count={int(body_contacts.shape[0])}")
        lines.append(f"  body match (attacker/victim/hb/cap) count={int(body_match.shape[0])}")
        lines.append(f"  select_body_hits count={int(sel_count)} selected match count={int(sel_match.shape[0])}")
        if int(count) > 0:
            lines.append("  classified_filtered contacts:")
            for c in contacts:
                kind = "BODY" if int(c["contact_kind"]) == 0 else "SHIELD"
                lines.append(
                    "    "
                    f"{kind} a={int(c['attacker'])} d={int(c['defender'])} hb={int(c['hitbox_id'])} "
                    f"cap={int(c['hurtcap_id'])} dmg={float(c['hitbox_damage']):.3f} "
                    f"hb_r={float(c['hitbox_radius']):.6f} cap_r={float(c['hurtcap_radius']):.6f}"
                )
        lines.append("")
        lines.append(
            "hitlist state (pre-combat): gate=victims_1 (lbColl_8000ACFC), BODY insert type=0 (lbColl_80008688)"
        )
        lines.append(
            "  "
            f"seed_dense[att={off.attacker_port} g={hitlist_cur_g} vic={off.victim_port}]: "
            f"cd={seed_cd} victim_iid={seed_iid} present={(1 if seed_cd != 0 else 0)}"
        )
        lines.extend(["  " + s for s in _fmt_hitlist_capsule(f"hb{off.hb_id}", hb_capsule)])
        if group_capsules:
            lines.append("  same-group enabled hitboxes (potential ftColl_800768A0 copy sources):")
            any_sources = False
            for hb_other, t_other, c_other in group_capsules:
                if hb_other == off.hb_id:
                    continue
                any_sources = True
                lines.append(
                    "    "
                    f"hb{hb_other}: enabled_prev={int(t_other['enabled_prev'])} enabled_cur={int(t_other['enabled_cur'])} "
                    f"cur_g={int(t_other['cur_hit_group'])} enable_edge={int(t_other['enable_edge'])}"
                )
                lines.extend(["    " + s for s in _fmt_hitlist_capsule(f"hb{hb_other}", c_other)])
            if not any_sources:
                lines.append("    (none)")
        lines.append("")
        lines.append("post-combat outcome (sim vs ref_t1 for victim):")
        lines.append(f"  got: action_id={got_action} hitlag={got_hitlag} hitstun={got_hitstun}")
        lines.append(f"  ref: action_id={exp_action} hitlag={exp_hitlag} hitstun={exp_hitstun}")
        lines.append("")

        report_name = f"enable_edge_body_hit_{Path(off.dataset_rel).stem}_rec{off.record}_p{off.victim_port}.txt"
        out_path = out_dir / report_name
        _write_report(out_path, "\n".join(lines))
        return out_path
    finally:
        binding.destroy(handle)


def main() -> None:
    ap = argparse.ArgumentParser(description="Triage enable-edge-only spurious BODY hits (debug-only).")
    ap.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Repo root (default: inferred from this file).",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage"),
        help="Output directory for reports (default: reports/triage).",
    )
    args = ap.parse_args()

    out_dir = args.root / args.out_dir
    for off in OFFENDERS:
        out_path = _triage_one(args.root, off, out_dir)
        print(str(out_path))


if __name__ == "__main__":
    main()
