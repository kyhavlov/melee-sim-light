from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAX_PLAYERS = 4
MAX_HITBOXES = 4
MAX_HURTCAPS = 32

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

# src/buttons.h (Melee/HSD PAD bits)
BUTTON_LR = 0x0040 | 0x0020

# src/action_ids.h (guard actions)
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_REFLECT = 0x00B6
ACT_GUARD_SET_OFF = 0x00B4


def _char_key_from_char_id(char_id: int) -> str | None:
    # Character id mapping follows Slippi post-frame `character` (GALE01):
    # - Fox   = 1
    # - Falco = 22
    if int(char_id) == 1:
        return "fox"
    if int(char_id) == 22:
        return "falco"
    return None


def _sphere_sphere_intersects(
    ax: float,
    ay: float,
    az: float,
    ar: float,
    bx: float,
    by: float,
    bz: float,
    br: float,
) -> bool:
    dx = ax - bx
    dy = ay - by
    dz = az - bz
    rr = ar + br
    return (dx * dx + dy * dy + dz * dz) <= (rr * rr)


def _point_segment_dist2(
    px: float,
    py: float,
    pz: float,
    ax: float,
    ay: float,
    az: float,
    bx: float,
    by: float,
    bz: float,
) -> float:
    # Mirror src/combat_geom.h::combat_point_segment_dist2.
    abx = bx - ax
    aby = by - ay
    abz = bz - az
    apx = px - ax
    apy = py - ay
    apz = pz - az

    denom = abx * abx + aby * aby + abz * abz
    if denom > 0.0:
        t = (apx * abx + apy * aby + apz * abz) / denom
        if t < 0.0:
            t = 0.0
        elif t > 1.0:
            t = 1.0
    else:
        t = 0.0

    qx = ax + t * abx
    qy = ay + t * aby
    qz = az + t * abz

    dx = px - qx
    dy = py - qy
    dz = pz - qz
    return dx * dx + dy * dy + dz * dz


def _sphere_capsule_intersects(
    sx: float,
    sy: float,
    sz: float,
    r_sphere: float,
    ax: float,
    ay: float,
    az: float,
    bx: float,
    by: float,
    bz: float,
    r_capsule: float,
) -> bool:
    d2 = _point_segment_dist2(sx, sy, sz, ax, ay, az, bx, by, bz)
    r = r_sphere + r_capsule
    return d2 <= (r * r)


def _clamp01(x: float) -> float:
    if x < 0.0:
        return 0.0
    if x > 1.0:
        return 1.0
    return x


def _trigger_unit_from_input(buttons: int, l: int, r: int) -> float:
    # Mirror src/shields.c::trigger_unit_from_input.
    if (int(buttons) & int(BUTTON_LR)) != 0:
        return 1.0
    m = int(l) if int(l) > int(r) else int(r)
    return float(m) * (1.0 / 255.0)


def _is_shield_active_action(action_id: int) -> bool:
    a = int(action_id)
    return a in (ACT_GUARD_ON, ACT_GUARD, ACT_GUARD_REFLECT, ACT_GUARD_SET_OFF)


def _calc_hitlag_frames(hitlag_dmg_mul: float, hitlag_base: float, dmg_int: int) -> int:
    # Mirror src/combat.c::combat_calc_hitlag_frames (mul=1.0, squat TODO omitted).
    tmp_f = float(dmg_int) * float(hitlag_dmg_mul) + float(hitlag_base)
    tmp = int(tmp_f)
    if tmp < 0:
        tmp = 0
    if tmp > 0xFFFF:
        tmp = 0xFFFF
    return tmp


@dataclass(frozen=True)
class _AnimEntry:
    frame_count: int
    mats_off: int  # start of [frame][joint] matrices
    joint_count: int


class AnimPoseDB:
    # data/anims/<char>.bin (SSANIM01 v3)
    _MAGIC = b"SSANIM01"
    _VERSION = 3
    _MAT_BYTES = 12 * 4

    def __init__(self, buf: bytes):
        if len(buf) < 16:
            raise ValueError("SSANIM01: file too small for header")
        if buf[:8] != self._MAGIC:
            raise ValueError(f"SSANIM01: bad magic: {buf[:8]!r}")
        ver = int.from_bytes(buf[8:12], "little", signed=False)
        if ver != self._VERSION:
            raise ValueError(f"SSANIM01: unsupported version: {ver} (want {self._VERSION})")

        joint_count = int.from_bytes(buf[12:14], "little", signed=False)
        anim_count = int.from_bytes(buf[14:16], "little", signed=False)
        off = 16
        if len(buf) < off + joint_count:
            raise ValueError("SSANIM01: truncated joint_parts")
        joint_parts = [int(x) for x in buf[off : off + joint_count]]
        self._buf = buf
        self._joint_count = int(joint_count)
        self._part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

        off = 16 + joint_count
        entries: dict[int, _AnimEntry] = {}
        for _ in range(int(anim_count)):
            if off + 4 > len(buf):
                raise ValueError("SSANIM01: truncated anim table")
            msid = int.from_bytes(buf[off : off + 2], "little", signed=False)
            frame_count = int.from_bytes(buf[off + 2 : off + 4], "little", signed=False)
            off += 4

            mats_bytes = int(frame_count) * int(joint_count) * self._MAT_BYTES
            transn_bytes = int(frame_count) * 3 * 4  # v3 tail
            need = mats_bytes + transn_bytes
            if off + need > len(buf):
                raise ValueError(f"SSANIM01: truncated anim payload: msid={msid} frame_count={frame_count}")

            entries[int(msid)] = _AnimEntry(
                frame_count=int(frame_count),
                mats_off=int(off),
                joint_count=int(joint_count),
            )
            off += need

        if off != len(buf):
            raise ValueError(f"SSANIM01: unexpected trailing bytes: {len(buf) - off}")

        self._by_msid = entries

    def try_get_matrix(self, *, msid: int, frame: int, part_id: int) -> np.ndarray | None:
        ent = self._by_msid.get(int(msid))
        if ent is None:
            return None
        if int(frame) < 0 or int(frame) >= int(ent.frame_count):
            return None
        ji = self._part_to_joint_index.get(int(part_id))
        if ji is None:
            return None
        off = int(ent.mats_off) + int(frame) * int(ent.joint_count) * self._MAT_BYTES + int(ji) * self._MAT_BYTES
        return np.frombuffer(self._buf, dtype="<f4", count=12, offset=off)


@dataclass(frozen=True)
class HurtCap:
    bone_part_id: int
    a_offset: np.ndarray  # (3,) f32
    b_offset: np.ndarray  # (3,) f32
    scale: float


def _read_hurtcaps(path: Path) -> list[HurtCap]:
    # data/hurtcaps/<char>.bin (MSLHURT1 v1)
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("MSLHURT1: file too small for header")
    if buf[:8] != b"MSLHURT1":
        raise ValueError(f"MSLHURT1: bad magic: {buf[:8]!r}")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != 1:
        raise ValueError(f"MSLHURT1: unsupported version: {ver} (want 1)")
    capsule_count = int.from_bytes(buf[12:14], "little", signed=False)
    reserved = int.from_bytes(buf[14:16], "little", signed=False)
    if reserved != 0:
        raise ValueError("MSLHURT1: nonzero reserved")
    rec_bytes = 34
    need = 16 + capsule_count * rec_bytes
    if need != len(buf):
        raise ValueError(f"MSLHURT1: unexpected size: got={len(buf)} want={need}")

    out: list[HurtCap] = []
    off = 16
    for _ in range(capsule_count):
        bone_part_id = int.from_bytes(buf[off + 0 : off + 2], "little", signed=False)
        a = np.array(struct.unpack_from("<fff", buf, off + 6), dtype=np.float32)
        b = np.array(struct.unpack_from("<fff", buf, off + 18), dtype=np.float32)
        (scale,) = struct.unpack_from("<f", buf, off + 30)
        out.append(
            HurtCap(
                bone_part_id=int(bone_part_id),
                a_offset=a,
                b_offset=b,
                scale=float(np.float32(scale)),
            )
        )
        off += rec_bytes
    return out


@dataclass(frozen=True)
class HitboxEvent:
    frame: int
    kind: int  # 0=set, 1=clear
    hitbox_id: int  # 0..3 or 0xFF for clear-all
    bone_part_id: int
    x: float
    y: float
    z: float
    radius: float
    damage: float
    u16_6: int


def _read_hitbox_events(path: Path) -> dict[int, list[HitboxEvent]]:
    # data/hitboxes/<char>.bin (MSLHITB1 v1)
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("MSLHITB1: file too small for header")
    if buf[:8] != b"MSLHITB1":
        raise ValueError(f"MSLHITB1: bad magic: {buf[:8]!r}")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != 1:
        raise ValueError(f"MSLHITB1: unsupported version: {ver} (want 1)")
    entry_count = int.from_bytes(buf[12:16], "little", signed=False)
    if entry_count <= 0:
        return {}

    index_base = 16
    idx_bytes = 12
    rec_bytes = 44
    index_end = index_base + entry_count * idx_bytes
    if len(buf) < index_end:
        raise ValueError("MSLHITB1: truncated index")

    out: dict[int, list[HitboxEvent]] = {}
    for i in range(entry_count):
        off = index_base + i * idx_bytes
        msid, rec_count, rec_len, payload_off = struct.unpack_from("<HHII", buf, off)
        if int(rec_len) != int(rec_count) * rec_bytes:
            raise ValueError("MSLHITB1: bad rec_bytes")
        if int(payload_off) < index_end or int(payload_off) + int(rec_len) > len(buf):
            raise ValueError("MSLHITB1: record payload out of range")

        evs: list[HitboxEvent] = []
        for ri in range(int(rec_count)):
            roff = int(payload_off) + ri * rec_bytes
            frame = int.from_bytes(buf[roff + 0 : roff + 2], "little", signed=False)
            kind = int(buf[roff + 2])
            hitbox_id = int(buf[roff + 3])
            bone_part_id = int.from_bytes(buf[roff + 4 : roff + 8], "little", signed=False)
            x, y, z, radius, damage = struct.unpack_from("<fffff", buf, roff + 8)
            u16s = struct.unpack_from("<8H", buf, roff + 28)
            evs.append(
                HitboxEvent(
                    frame=int(frame),
                    kind=int(kind),
                    hitbox_id=int(hitbox_id),
                    bone_part_id=int(bone_part_id),
                    x=float(np.float32(x)),
                    y=float(np.float32(y)),
                    z=float(np.float32(z)),
                    radius=float(np.float32(radius)),
                    damage=float(np.float32(damage)),
                    u16_6=int(u16s[6]),
                )
            )
        out[int(msid)] = evs
    return out


@dataclass(frozen=True)
class _CharCombatData:
    key: str
    pose: AnimPoseDB
    hurtcaps: list[HurtCap]
    hitboxes_by_msid: dict[int, list[HitboxEvent]]
    initial_shield_size: float


def _load_common_params(data_root: Path) -> dict[str, float]:
    common = json.loads((data_root / "common" / "ft_common_data.json").read_text())
    keys = [
        "trigger_deadzone",
        "shield_size_lightshield_min",
        "shield_size_lightshield_max",
        "shield_size_min_scale",
        "start_shield_health",
        "hitlag_dmg_mul",
        "hitlag_base",
    ]
    out: dict[str, float] = {}
    for k in keys:
        out[k] = float(common[k])
    return out


def _load_char_data(*, char_id: int, data_root: Path) -> _CharCombatData | None:
    key = _char_key_from_char_id(char_id)
    if key is None:
        return None
    pose = AnimPoseDB((data_root / "anims" / f"{key}.bin").read_bytes())
    hurtcaps = _read_hurtcaps(data_root / "hurtcaps" / f"{key}.bin")
    hitboxes_by_msid = _read_hitbox_events(data_root / "hitboxes" / f"{key}.bin")
    attrs = json.loads((data_root / "characters" / f"{key}.json").read_text())
    initial_shield_size = float(attrs["initial_shield_size"])
    return _CharCombatData(
        key=key,
        pose=pose,
        hurtcaps=hurtcaps,
        hitboxes_by_msid=hitboxes_by_msid,
        initial_shield_size=initial_shield_size,
    )


def _mtx34_mul_point(m: np.ndarray, v: np.ndarray) -> np.ndarray:
    # Mirror src/hitboxes.c and src/hurtboxes.c (row-major 3x4).
    x = np.float32(v[0])
    y = np.float32(v[1])
    z = np.float32(v[2])
    return np.array(
        [
            np.float32(m[0] * x + m[1] * y + m[2] * z + m[3]),
            np.float32(m[4] * x + m[5] * y + m[6] * z + m[7]),
            np.float32(m[8] * x + m[9] * y + m[10] * z + m[11]),
        ],
        dtype=np.float32,
    )


def _active_hitboxes_at_frame(events: list[HitboxEvent], frame: int) -> dict[int, HitboxEvent]:
    # Mirror src/hitboxes.c::hitboxes_refresh event application policy.
    active: dict[int, HitboxEvent] = {}
    for ev in events:
        if int(ev.frame) > int(frame):
            continue
        if int(ev.kind) == 1:
            if int(ev.hitbox_id) == 0xFF:
                active.clear()
            else:
                active.pop(int(ev.hitbox_id), None)
            continue
        hb_id = int(ev.hitbox_id)
        if 0 <= hb_id < MAX_HITBOXES:
            active[hb_id] = ev
    return active


def derive_combat_rehit_seed_fields(
    *,
    num_players: int,
    is_teams: bool,
    team_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    char_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    action_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    action_frame: np.ndarray,  # [n_frames, MAX_PLAYERS] i16
    animation_index: np.ndarray,  # [n_frames, MAX_PLAYERS] u32
    on_ground: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    pos_x: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    pos_y: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    fighter_scale_y: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    stocks: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    shield_hp: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    hurtbox_state: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (0 vuln, 1 invuln, 2 intangible)
    instance_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    input_buttons: np.ndarray,  # [n_frames, MAX_PLAYERS] u16 (pre-frame)
    input_l: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (pre-frame)
    input_r: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (pre-frame)
    data_root: str | Path = "data",
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive combat rehit latch internals strictly causally from replay prefix history.

    Outputs are per-frame snapshots of the internal rehit state *after* applying combat selection
    for that frame, using only current-frame external state and previous derived internals.
    """
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")

    data_root = Path(data_root)
    common = _load_common_params(data_root)

    # Per-character caches (Fox/Falco first).
    char_cache: dict[int, _CharCombatData | None] = {}

    def get_char(char_u8: int) -> _CharCombatData | None:
        cid = int(char_u8)
        if cid not in char_cache:
            char_cache[cid] = _load_char_data(char_id=cid, data_root=data_root)
        return char_cache[cid]

    n_frames = int(np.asarray(action_id).shape[0])

    # Outputs.
    out_active = np.zeros((n_frames, MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint8)
    out_hb_id = np.full((n_frames, MAX_PLAYERS, MAX_PLAYERS), 0xFF, dtype=np.uint8)
    out_att_msid = np.zeros((n_frames, MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint16)
    out_def_iid = np.zeros((n_frames, MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint16)

    # Internal state (rollout-causal).
    latch_active = np.zeros((MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint8)
    latch_hb_id = np.full((MAX_PLAYERS, MAX_PLAYERS), 0xFF, dtype=np.uint8)
    latch_att_msid = np.zeros((MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint16)
    latch_def_iid = np.zeros((MAX_PLAYERS, MAX_PLAYERS), dtype=np.uint16)

    sim_hitlag = np.zeros((MAX_PLAYERS,), dtype=np.uint16)

    trig_deadzone = float(common["trigger_deadzone"])
    shield_light_min = float(common["shield_size_lightshield_min"])
    shield_light_max = float(common["shield_size_lightshield_max"])
    shield_min_scale = float(common["shield_size_min_scale"])
    start_shield_health = float(common["start_shield_health"])
    hitlag_dmg_mul = float(common["hitlag_dmg_mul"])
    hitlag_base = float(common["hitlag_base"])
    denom = 1.0 - trig_deadzone

    for fi in range(n_frames):
        # timers_update: decrement simulated hitlag first (strictly causal; no replay hitlag use).
        for p in range(num_players):
            if sim_hitlag[p] != 0:
                sim_hitlag[p] = np.uint16(int(sim_hitlag[p]) - 1)

        # Per-player world primitives for this frame.
        # We keep these as Python lists/dicts for correctness and stable ordering (small sizes).
        hitboxes: list[dict[int, dict]] = [dict() for _ in range(MAX_PLAYERS)]
        hurtcaps_world: list[list[dict]] = [[] for _ in range(MAX_PLAYERS)]
        shield_world: list[tuple[float, float, float, float]] = [(0.0, 0.0, 0.0, 0.0) for _ in range(MAX_PLAYERS)]

        # Refresh hurtcaps/hitboxes/shields for active players only (others remain empty).
        for p in range(num_players):
            ch = get_char(int(char_id[fi, p]))
            af = int(action_frame[fi, p])
            anim_u32 = int(animation_index[fi, p])
            if af >= 0 and 0 <= anim_u32 <= 0xFFFF and ch is not None:
                msid = int(anim_u32) & 0xFFFF
                frame = int(af)

                # Hurtcaps (pose-driven + scaled).
                out_caps: list[dict] = []
                scale_y = float(fighter_scale_y[fi, p])
                px = float(pos_x[fi, p])
                py = float(pos_y[fi, p])
                for cap in ch.hurtcaps[:MAX_HURTCAPS]:
                    m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=cap.bone_part_id)
                    if m is None:
                        continue
                    a = _mtx34_mul_point(m, cap.a_offset)
                    b = _mtx34_mul_point(m, cap.b_offset)
                    a = (a * np.float32(scale_y)).astype(np.float32, copy=False)
                    b = (b * np.float32(scale_y)).astype(np.float32, copy=False)
                    a[0] = np.float32(a[0] + np.float32(px))
                    a[1] = np.float32(a[1] + np.float32(py))
                    b[0] = np.float32(b[0] + np.float32(px))
                    b[1] = np.float32(b[1] + np.float32(py))
                    out_caps.append(
                        {
                            "ax": float(a[0]),
                            "ay": float(a[1]),
                            "az": float(a[2]),
                            "bx": float(b[0]),
                            "by": float(b[1]),
                            "bz": float(b[2]),
                            "r": float(np.float32(cap.scale) * np.float32(scale_y)),
                        }
                    )
                hurtcaps_world[p] = out_caps

                # Hitboxes (pose-driven, no fighter_scale_y in current runtime policy).
                events = ch.hitboxes_by_msid.get(msid)
                if events:
                    active = _active_hitboxes_at_frame(events, frame)
                    for hb_id, ev in sorted(active.items()):
                        m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=ev.bone_part_id)
                        if m is None:
                            continue
                        c = _mtx34_mul_point(m, np.array([ev.x, ev.y, ev.z], dtype=np.float32))
                        c[0] = np.float32(c[0] + np.float32(px))
                        c[1] = np.float32(c[1] + np.float32(py))
                        hitboxes[p][int(hb_id)] = {
                            "x": float(c[0]),
                            "y": float(c[1]),
                            "z": float(c[2]),
                            "r": float(np.float32(ev.radius)),
                            "damage": float(np.float32(ev.damage)),
                            "flags": int(ev.u16_6) & 0xFFFF,
                        }

                # Shields (approx center at (pos_x,pos_y,0), ftCo_Guard inlineB0 radius scaling).
                sx = px
                sy = py
                sz = 0.0
                sr = 0.0
                if int(stocks[fi, p]) != 0 and _is_shield_active_action(int(action_id[fi, p])):
                    hp = float(shield_hp[fi, p])
                    if hp > 0.0 and start_shield_health > 0.0:
                        trig = _trigger_unit_from_input(
                            int(input_buttons[fi, p]),
                            int(input_l[fi, p]),
                            int(input_r[fi, p]),
                        )
                        light = _clamp01((trig - trig_deadzone) / denom) if denom > 0.0 else 0.0
                        hp_ratio = _clamp01(hp / start_shield_health)
                        light_scale = (light * (shield_light_max - shield_light_min)) + shield_light_min
                        n1 = hp_ratio * light_scale
                        n2 = 1.0 - shield_min_scale
                        scale = (n2 * n1) + shield_min_scale
                        sr = scale * float(ch.initial_shield_size) * float(fighter_scale_y[fi, p])
                shield_world[p] = (sx, sy, sz, sr)

        # Combat resolve (BODY-only selection + rehit latch update + simulated hitlag gate).
        for attacker in range(num_players):
            if int(stocks[fi, attacker]) == 0:
                continue
            a_hitboxes = hitboxes[attacker]
            if not a_hitboxes:
                # Approximate ClearHitboxes: clear rehit latches for this attacker.
                latch_active[attacker, :] = np.uint8(0)
                continue

            msid_u32 = int(animation_index[fi, attacker])
            msid = np.uint16(msid_u32 & 0xFFFF) if 0 <= msid_u32 <= 0xFFFF else np.uint16(0)
            a_iid = int(instance_id[fi, attacker])

            for defender in range(num_players):
                if defender == attacker:
                    continue
                if int(stocks[fi, defender]) == 0:
                    continue
                if int(hurtbox_state[fi, defender]) != 0:
                    continue

                if is_teams and int(team_id[fi, attacker]) == int(team_id[fi, defender]):
                    continue

                if not hurtcaps_world[defender]:
                    continue

                # Hitlag gating: when either fighter is in hitlag, do not generate new BODY hits.
                if int(sim_hitlag[attacker]) != 0 or int(sim_hitlag[defender]) != 0:
                    continue

                # Clear stale latch entries on msid change, defender instance change, or latched hitbox
                # being disabled (hitbox clear).
                if int(latch_active[attacker, defender]) != 0:
                    latched_msid = int(latch_att_msid[attacker, defender])
                    defender_iid = int(instance_id[fi, defender])
                    if latched_msid != int(msid) or int(latch_def_iid[attacker, defender]) != defender_iid:
                        latch_active[attacker, defender] = np.uint8(0)
                    else:
                        latched_hb = int(latch_hb_id[attacker, defender])
                        if latched_hb != 0xFF and latched_hb not in a_hitboxes:
                            latch_active[attacker, defender] = np.uint8(0)

                shx, shy, shz, shr = shield_world[defender]
                shield_active = shr > 0.0

                # Deterministic selection: pick the first BODY overlap in (hitbox_id, hurtcap_id) order.
                did_hit = False
                defender_on_ground = int(on_ground[fi, defender]) != 0
                defender_iid = int(instance_id[fi, defender])

                for hb_id in range(MAX_HITBOXES):
                    if hb_id not in a_hitboxes:
                        continue
                    hb = a_hitboxes[hb_id]
                    hb_flags = int(hb["flags"])
                    if defender_on_ground:
                        if (hb_flags & HIT_GROUNDED) == 0:
                            continue
                    else:
                        if (hb_flags & HIT_AERIAL) == 0:
                            continue

                    hx = float(hb["x"])
                    hy = float(hb["y"])
                    hz = float(hb["z"])
                    hr = float(hb["r"])

                    # SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
                    # and do not apply BODY selection for this hitbox.
                    if shield_active and _sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr):
                        continue

                    # Rehit suppression.
                    if (
                        int(latch_active[attacker, defender]) != 0
                        and int(latch_att_msid[attacker, defender]) == int(msid)
                        and int(latch_def_iid[attacker, defender]) == defender_iid
                    ):
                        latched_hb = int(latch_hb_id[attacker, defender])
                        if latched_hb == 0xFF or latched_hb == hb_id:
                            continue

                    for cap in hurtcaps_world[defender]:
                        if not _sphere_capsule_intersects(
                            hx,
                            hy,
                            hz,
                            hr,
                            float(cap["ax"]),
                            float(cap["ay"]),
                            float(cap["az"]),
                            float(cap["bx"]),
                            float(cap["by"]),
                            float(cap["bz"]),
                            float(cap["r"]),
                        ):
                            continue

                        # Latch.
                        latch_active[attacker, defender] = np.uint8(1)
                        latch_hb_id[attacker, defender] = np.uint8(hb_id)
                        latch_att_msid[attacker, defender] = np.uint16(msid)
                        latch_def_iid[attacker, defender] = np.uint16(defender_iid)

                        # Minimal hitlag simulation for hitlag gating across frames (no replay lookahead).
                        dmg_i = int(float(hb["damage"]))
                        hl = _calc_hitlag_frames(hitlag_dmg_mul, hitlag_base, dmg_i)
                        sim_hitlag[attacker] = np.uint16(hl)
                        sim_hitlag[defender] = np.uint16(hl)

                        did_hit = True
                        break

                    if did_hit:
                        break

        out_active[fi] = latch_active
        out_hb_id[fi] = latch_hb_id
        out_att_msid[fi] = latch_att_msid
        out_def_iid[fi] = latch_def_iid

    return out_active, out_hb_id, out_att_msid, out_def_iid
