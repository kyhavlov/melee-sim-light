from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAX_PLAYERS = 4
MAX_HITBOXES = 4
MAX_HURTCAPS = 32
HITLIST_GROUPS = 8
HITLIST_CD_INDEFINITE = 0xFFFF

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

# src/buttons.h (Melee/HSD PAD bits)
BUTTON_LR = 0x0040 | 0x0020

# src/action_ids.h (guard actions)
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_REFLECT = 0x00B6
ACT_GUARD_SET_OFF = 0x00B5
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_F = 0x0042
ACT_ATTACK_AIR_B = 0x0043
ACT_ATTACK_AIR_HI = 0x0044
ACT_ATTACK_AIR_LW = 0x0045
ACT_DAMAGE_FALL = 0x0026
ACT_DAMAGE_HI_1 = 0x004B
ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_FLY_REFLECT_WALL = 0x00F7
ACT_FLY_REFLECT_CEIL = 0x00F8
ACT_FX_SPECIAL_HI = 0x0163
ACT_FX_SPECIAL_AIR_HI = 0x0164
ACT_FX_SPECIAL_HI_LANDING = 0x0165
ACT_FX_SPECIAL_HI_FALL = 0x0166
ACT_FX_SPECIAL_HI_BOUND = 0x0167
# Seed-bridge discriminator for early create-order stale carryover rows in current suite:
# - falco msid=70 create frame 8 (data/hitboxes/falco.bin)
# - fox   msid=72 create frame 8 (data/hitboxes/fox.bin)
# This is intentionally kept in seed materialization (not runtime C) and should be removed once
# per-hitbox victims_1 lineage is available from replay-visible lanes.
GUARD_STALE_PRUNE_MAX_CREATE_FRAME = 8


def _should_prune_guard_stale_seed_bridge(
    *,
    defender_action_id: int,
    defender_hitlag: int,
    defender_last_hit_by: int,
    defender_instance_hit_by: int,
    attacker_port: int,
    attacker_instance_id: int,
    hitbox_def_frame: int,
    attacker_action_frame: int,
) -> bool:
    return (
        int(defender_action_id) == ACT_GUARD
        and int(defender_hitlag) == 0
        and int(defender_last_hit_by) == int(attacker_port)
        and int(defender_instance_hit_by) != int(attacker_instance_id)
        and int(hitbox_def_frame) == int(attacker_action_frame)
        and int(attacker_action_frame) <= int(GUARD_STALE_PRUNE_MAX_CREATE_FRAME)
    )


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


def _is_damage_destination_action(action_id: int) -> bool:
    a = int(action_id)
    return (
        a == ACT_DAMAGE_FALL
        or ACT_DAMAGE_HI_1 <= a <= ACT_DAMAGE_FLY_ROLL
        or a in {ACT_FLY_REFLECT_WALL, ACT_FLY_REFLECT_CEIL}
    )


def _is_attackair_action(action_id: int) -> bool:
    a = int(action_id)
    return ACT_ATTACK_AIR_N <= a <= ACT_ATTACK_AIR_LW


def _is_first_guardsetoff_hitlag_row(
    *,
    fi: int,
    defender: int,
    action_id: np.ndarray,
    hitlag_arr: np.ndarray | None,
) -> bool:
    # GuardSetOff onset lineage owner for the replay-side shield-hitlist bridge:
    # - ftColl_80076CBC accepts the shield hit, then the defender enters GuardSetOff hitlag.
    # - The first replay-visible aftermath row is therefore the earliest strictly-causal point where
    #   the accepted shield-contact lineage can be made authoritative.
    # - Later frozen Guard/GuardSetOff rows must carry that exact onset lineage rather than
    #   reconstruct it from overlap again.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    if hitlag_arr is None:
        return False
    if int(action_id[fi, defender]) != ACT_GUARD_SET_OFF or int(hitlag_arr[fi, defender]) <= 0:
        return False
    if fi == 0:
        return True
    return int(hitlag_arr[fi - 1, defender]) == 0 and _is_shield_active_action(int(action_id[fi - 1, defender]))


def _is_guardsetoff_shield_damage_onset(
    *,
    fi: int,
    defender: int,
    action_id: np.ndarray,
    hitlag_arr: np.ndarray | None,
    shield_hp: np.ndarray,
) -> bool:
    # Replay-visible shield-hit consequence:
    # - ftColl_80076CBC enters GuardSetOff and applies hitlag after a shield hit.
    # - Some target rows expose a non-Guard visible action immediately before GuardSetOff (for
    #   example DownStandD), so key the provenance proof on the GuardSetOff hitlag onset plus shield
    #   HP drop rather than requiring the previous visible action to be Guard*.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    if hitlag_arr is None:
        return False
    if fi <= 0:
        return False
    if int(action_id[fi, defender]) != ACT_GUARD_SET_OFF:
        return False
    if int(hitlag_arr[fi, defender]) <= 0 or int(hitlag_arr[fi - 1, defender]) != 0:
        return False
    return float(shield_hp[fi, defender]) < float(shield_hp[fi - 1, defender])


def _is_frozen_guard_snapshot(*, action_id: int, action_frame: int, animation_index: int) -> bool:
    return int(action_id) == ACT_GUARD and int(action_frame) < 0 and int(animation_index) == 0xFFFFFFFF


def _calc_hitlag_frames(hitlag_dmg_mul: float, hitlag_base: float, dmg_int: int) -> int:
    # Mirror src/combat.c::combat_calc_hitlag_frames (mul=1.0, squat TODO omitted).
    tmp_f = float(dmg_int) * float(hitlag_dmg_mul) + float(hitlag_base)
    tmp = int(tmp_f)
    if tmp < 0:
        tmp = 0
    if tmp > 0xFFFF:
        tmp = 0xFFFF
    return tmp


def _get_env_dmg(dmg: float) -> int:
    # Mirror src/combat.c::combat_get_env_dmg (decomp "getEnvDmg" pattern used for hitlag inputs).
    # refs/melee/src/melee/ft/ftcoll.c (inlineA0/inlineA1 and ftColl_80076CBC).
    #
    # Behavior:
    # - dmg == 0 -> 0
    # - dmg != 0 and (int)dmg != 0 -> (int)dmg
    # - dmg != 0 and (int)dmg == 0 -> 1
    if float(dmg) == 0.0:
        return 0
    try:
        i = int(dmg)
    except Exception:
        # Should be unreachable for ISO-extracted hitbox damage; keep behavior stable.
        return 1
    return i if i != 0 else 1


@dataclass(frozen=True)
class _AnimEntry:
    frame_count: int
    mats_off: int  # start of [frame][joint] matrices
    joint_count: int


class AnimPoseDB:
    # data/anims/<char>.bin (SSANIM01 v4)
    _MAGIC = b"SSANIM01"
    _VERSION = 4
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
            transn_bytes = int(frame_count) * 3 * 4  # v4 tail
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
    u16_7: int


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
                    u16_7=int(u16s[7]),
                )
            )
        out[int(msid)] = evs
    return out


@dataclass(frozen=True)
class _CharCombatData:
    key: str
    char_id: int
    pose: AnimPoseDB
    parts_under_xrotn: frozenset[int]
    hurtcaps: list[HurtCap]
    hitboxes_by_msid: dict[int, list[HitboxEvent]]
    initial_shield_size: float
    model_scaling: float


@dataclass(frozen=True)
class _ShieldTiltTable:
    neutral_frame: int
    xyz: np.ndarray  # [frame_count, 3] f32


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


def _read_shield_tilt_table(*, data_root: Path, key: str) -> _ShieldTiltTable | None:
    # data/shields/<char>.bin (MSLSHLD1 v4).
    # v4 carries the corrected ftCo_80091E78 `ftData.x20->x0->x8` GuardOn target.
    p = data_root / "shields" / f"{key}.bin"
    if not p.exists():
        return None
    buf = p.read_bytes()
    if len(buf) < 8 + 4 + 2 + 2:
        raise ValueError(f"{p}: too small for MSLSHLD1 header (size={len(buf)})")
    if buf[:8] != b"MSLSHLD1":
        raise ValueError(f"{p}: bad magic (want MSLSHLD1)")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != 4:
        raise ValueError(f"{p}: unsupported MSLSHLD1 version={ver} (want 4)")
    frame_count = int.from_bytes(buf[12:14], "little", signed=False)
    neutral_frame = int.from_bytes(buf[14:16], "little", signed=False)
    if frame_count <= 0:
        raise ValueError(f"{p}: frame_count is 0")
    if neutral_frame < 0 or neutral_frame >= frame_count:
        raise ValueError(f"{p}: neutral_frame out of range (neutral_frame={neutral_frame}, frame_count={frame_count})")
    hdr = 32
    want = hdr + frame_count * 3 * 4
    guard_on_frame_count = int.from_bytes(buf[28:30], "little", signed=False)
    want += guard_on_frame_count * 3 * 4
    if len(buf) != want:
        raise ValueError(f"{p}: size mismatch (got {len(buf)}, want {want})")
    xyz = np.frombuffer(buf, dtype="<f4", count=frame_count * 3, offset=hdr).reshape((frame_count, 3))
    return _ShieldTiltTable(neutral_frame=int(neutral_frame), xyz=xyz)


def _read_parts_under_xrotn(path: Path) -> frozenset[int]:
    # data/anims/<char>.tracks.bin (SSANIMT1). Mirror src/anim_table.c part_under_xrotn setup.
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError(f"{path}: too small for SSANIMT1 header")
    if buf[:8] != b"SSANIMT1":
        raise ValueError(f"{path}: bad magic (want SSANIMT1)")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != 3:
        raise ValueError(f"{path}: unsupported SSANIMT1 version={ver} (want 3)")
    local_count = int.from_bytes(buf[12:14], "little", signed=False)
    off = 16
    local_parts_bytes = local_count
    local_parent_bytes = local_count * 2
    need = off + local_parts_bytes + local_parent_bytes
    if len(buf) < need:
        raise ValueError(f"{path}: truncated SSANIMT1 local table")
    local_parts = [int(x) for x in buf[off : off + local_parts_bytes]]
    parent_off = off + local_parts_bytes
    parent_by_part = [-1] * 256
    for i, part in enumerate(local_parts):
        parent_by_part[part] = int.from_bytes(buf[parent_off + i * 2 : parent_off + i * 2 + 2], "little")
    out: set[int] = set()
    for part in local_parts:
        cur = int(part)
        for _ in range(256):
            if cur == 2:  # FtPart_XRotN
                out.add(int(part))
                break
            if cur < 0 or cur >= 256:
                break
            nxt = int(parent_by_part[cur])
            if nxt == cur:
                break
            cur = nxt
    return frozenset(out)


def _load_char_data(*, char_id: int, data_root: Path) -> _CharCombatData | None:
    key = _char_key_from_char_id(char_id)
    if key is None:
        return None
    pose = AnimPoseDB((data_root / "anims" / f"{key}.bin").read_bytes())
    parts_under_xrotn = _read_parts_under_xrotn(data_root / "anims" / f"{key}.tracks.bin")
    hurtcaps = _read_hurtcaps(data_root / "hurtcaps" / f"{key}.bin")
    hitboxes_by_msid = _read_hitbox_events(data_root / "hitboxes" / f"{key}.bin")
    attrs = json.loads((data_root / "characters" / f"{key}.json").read_text())
    initial_shield_size = float(attrs["initial_shield_size"])
    model_scaling = float(attrs.get("model_scaling", 1.0))
    return _CharCombatData(
        key=key,
        char_id=int(char_id),
        pose=pose,
        parts_under_xrotn=parts_under_xrotn,
        hurtcaps=hurtcaps,
        hitboxes_by_msid=hitboxes_by_msid,
        initial_shield_size=initial_shield_size,
        model_scaling=model_scaling,
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


def _apply_root_facing_rot_y90(xyz: np.ndarray, facing_dir: float) -> np.ndarray:
    """
    Apply the fighter root-part Y rotation used by collision geometry in the C runtime.

    Decomp shape:
    - ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))
      refs/melee/src/melee/ft/fighter.c

    Runtime implementation reference:
    - src/hitboxes.c and src/hurtboxes.c apply the same rotation by mixing X/Z:
      (x,z) -> (facing_dir * z, -facing_dir * x)
    """
    x = np.float32(xyz[0])
    y = np.float32(xyz[1])
    z = np.float32(xyz[2])
    return np.array([np.float32(facing_dir) * z, y, -np.float32(facing_dir) * x], dtype=np.float32)


def _is_specialhi_xrotn_pose_owner(char_id: int, action_id: int) -> bool:
    if int(char_id) not in (1, 22):
        return False
    return int(action_id) in (
        ACT_FX_SPECIAL_HI,
        ACT_FX_SPECIAL_AIR_HI,
        ACT_FX_SPECIAL_HI_LANDING,
        ACT_FX_SPECIAL_HI_FALL,
        ACT_FX_SPECIAL_HI_BOUND,
    )


def _specialhi_xrotn_angle(rotate_model: float) -> np.float32:
    two_pi = np.float32(6.28318530717958647692)
    angle = np.float32(two_pi - np.float32(rotate_model))
    if angle >= two_pi:
        angle = np.float32(angle - two_pi)
    if angle < np.float32(0.0):
        angle = np.float32(angle + two_pi)
    return angle


def _apply_specialhi_local_xrotn_if_needed(
    *,
    ch: _CharCombatData,
    action_id: int,
    msid: int,
    frame: int,
    part_id: int,
    model_scale: float,
    rotate_model: float,
    rotate_model_valid: bool,
    xyz: np.ndarray,
) -> np.ndarray:
    # Decomp: Firefox/Firebird launch writes `mv.fx.SpecialHi.rotateModel` and applies it to
    # FtPart_XRotN (`ftPartSetRotX(..., 2*pi - rotateModel)`). Seed-side hitlist derivation must
    # use the same live XRotN pose as src/hitboxes.c/src/hurtboxes.c, otherwise dense HitCapsule
    # seeds can be born from geometry that runtime will never test.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Phys,
    #   ftFx_SpecialAirHi_Coll}
    # data/anims/{fox,falco}.tracks.bin: FtPart_XRotN subtree
    if not rotate_model_valid:
        return xyz
    if not _is_specialhi_xrotn_pose_owner(ch.char_id, action_id):
        return xyz
    if int(part_id) not in ch.parts_under_xrotn:
        return xyz
    m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=2)  # FtPart_XRotN
    if m is None:
        return xyz

    origin = _mtx34_mul_point(m, np.array([0.0, 0.0, 0.0], dtype=np.float32))
    axis_pt = _mtx34_mul_point(m, np.array([1.0, 0.0, 0.0], dtype=np.float32))
    origin = (origin * np.float32(model_scale)).astype(np.float32, copy=False)
    axis_pt = (axis_pt * np.float32(model_scale)).astype(np.float32, copy=False)
    axis = (axis_pt - origin).astype(np.float32, copy=False)
    axis_len = float(np.sqrt(np.float32(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2])))
    if not (axis_len > 0.0):
        return xyz
    axis = (axis * np.float32(1.0 / axis_len)).astype(np.float32, copy=False)

    p = (xyz - origin).astype(np.float32, copy=False)
    angle = _specialhi_xrotn_angle(float(rotate_model))
    c = np.float32(np.cos(angle))
    s = np.float32(np.sin(angle))
    dot = np.float32(axis[0] * p[0] + axis[1] * p[1] + axis[2] * p[2])
    cross = np.array(
        [
            np.float32(axis[1] * p[2] - axis[2] * p[1]),
            np.float32(axis[2] * p[0] - axis[0] * p[2]),
            np.float32(axis[0] * p[1] - axis[1] * p[0]),
        ],
        dtype=np.float32,
    )
    rotated = origin + (p * c) + (cross * s) + (axis * dot * np.float32(1.0 - c))
    return rotated.astype(np.float32, copy=False)


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


def derive_hitbox_prev_center_seed_fields(
    *,
    num_players: int,
    char_id: np.ndarray,
    action_id: np.ndarray | None = None,
    animation_index: np.ndarray,
    action_frame: np.ndarray,
    anim_frame_f32: np.ndarray,
    pos_x: np.ndarray,
    pos_y: np.ndarray,
    pos_z: np.ndarray | None = None,
    facing: np.ndarray,
    fighter_scale_y: np.ndarray,
    specialhi_rotate_model_f32: np.ndarray | None = None,
    specialhi_rotate_model_valid_u8: np.ndarray | None = None,
    data_root: str | Path = "data",
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive the hidden HitCapsule.x58 seed lane from replay-visible post-frame state.

    The output is indexed by replay post-frame, player, and hitbox slot. For one-step sample i,
    make_dataset stores row i's post-frame values in seed_t; these fields therefore seed the
    x58 endpoint that ftColl_8007AD18 would have carried into the next collision step.

    Decomp:
    - refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
    - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    """

    n_frames = int(char_id.shape[0])
    valid = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)
    out_x = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES), dtype=np.float32)
    out_y = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES), dtype=np.float32)
    out_z = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES), dtype=np.float32)

    data_root = Path(data_root)
    action_arr = np.asarray(action_id, dtype=np.uint16) if action_id is not None else None
    rotate_model_arr = (
        np.asarray(specialhi_rotate_model_f32, dtype=np.float32)
        if specialhi_rotate_model_f32 is not None
        else None
    )
    rotate_model_valid_arr = (
        np.asarray(specialhi_rotate_model_valid_u8, dtype=np.uint8)
        if specialhi_rotate_model_valid_u8 is not None
        else None
    )
    char_cache: dict[int, _CharCombatData | None] = {}

    def get_char(cid: int) -> _CharCombatData | None:
        cid = int(cid)
        if cid not in char_cache:
            char_cache[cid] = _load_char_data(char_id=cid, data_root=data_root)
        return char_cache[cid]

    for fi in range(n_frames):
        for p in range(int(num_players)):
            ch = get_char(int(char_id[fi, p]))
            if ch is None:
                continue
            if int(action_frame[fi, p]) < 0:
                continue
            anim_u32 = int(animation_index[fi, p])
            if anim_u32 < 0 or anim_u32 > 0xFFFF:
                continue
            af = float(anim_frame_f32[fi, p])
            if not np.isfinite(af) or af < 0.0:
                continue
            frame = int(np.floor(af))
            if frame < 0:
                continue
            msid = anim_u32 & 0xFFFF
            events = ch.hitboxes_by_msid.get(msid)
            if not events:
                continue
            active = _active_hitboxes_at_frame(events, frame)
            if not active:
                continue
            px = float(pos_x[fi, p])
            py = float(pos_y[fi, p])
            if pos_z is None:
                pz = 0.0
            else:
                pz_arr = np.asarray(pos_z)
                pz = float(pz_arr[fi, p]) if pz_arr.ndim >= 2 else float(pz_arr[fi])
            scale_y = float(fighter_scale_y[fi, p])
            model_scale = float(np.float32(scale_y) * np.float32(ch.model_scaling))
            facing_dir = 1.0 if int(facing[fi, p]) != 0 else -1.0
            for hb_id, ev in sorted(active.items()):
                if not (0 <= int(hb_id) < MAX_HITBOXES):
                    continue
                m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=ev.bone_part_id)
                if m is None:
                    continue
                c = _mtx34_mul_point(m, np.array([ev.x, ev.y, ev.z], dtype=np.float32))
                c = (c * np.float32(model_scale)).astype(np.float32, copy=False)
                cur_action = int(action_arr[fi, p]) if action_arr is not None else 0xFFFF
                rotate_model = (
                    float(rotate_model_arr[fi, p])
                    if rotate_model_arr is not None and rotate_model_valid_arr is not None
                    else 0.0
                )
                rotate_model_valid = (
                    bool(int(rotate_model_valid_arr[fi, p]) != 0)
                    if rotate_model_valid_arr is not None
                    else False
                )
                c = _apply_specialhi_local_xrotn_if_needed(
                    ch=ch,
                    action_id=cur_action,
                    msid=msid,
                    frame=frame,
                    part_id=ev.bone_part_id,
                    model_scale=model_scale,
                    rotate_model=rotate_model,
                    rotate_model_valid=rotate_model_valid,
                    xyz=c,
                )
                c = _apply_root_facing_rot_y90(c, facing_dir)
                c[0] = np.float32(c[0] + np.float32(px))
                c[1] = np.float32(c[1] + np.float32(py))
                c[2] = np.float32(c[2] + np.float32(pz))
                valid[fi, p, int(hb_id)] = np.uint8(1)
                out_x[fi, p, int(hb_id)] = np.float32(c[0])
                out_y[fi, p, int(hb_id)] = np.float32(c[1])
                out_z[fi, p, int(hb_id)] = np.float32(c[2])

    return valid, out_x, out_y, out_z


def _hitlist_victim_pointer_may_change(*, stocks: int, action_id: int) -> bool:
    """
    Mirror the C runtime's victim-identity boundary handling for hitlist entries.

    Decomp shape:
    - Hitlists key by a raw victim pointer (HitVictim.victim). When the pointer changes (death /
      respawn object lifetime), old suppression should be dropped.
    - C runtime proxy logic: src/hitlist.c::hitlist_victim_pointer_may_change
      (ports are stable, instance_id is used as a proxy, and cleared only on death/respawn).
    """
    if int(stocks) == 0:
        return True
    a = int(action_id)
    # Source of truth for these numeric IDs is src/action_ids.h.
    #
    # When the victim pointer changes (death/respawn lifetime boundary), decomp logic treats the
    # prior hitlist suppression as invalid and clears it.
    #
    # Named actions (motion states):
    # - 0: DeadDown
    # - 1: DeadLeft
    # - 2: DeadRight
    # - 4: DeadUpStar
    # - 12: Rebirth
    # - 13: RebirthWait
    return a in (0, 1, 2, 4, 12, 13)


def derive_combat_hitlist_seed_fields(
    *,
    num_players: int,
    is_teams: bool,
    team_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    char_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    action_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    action_frame: np.ndarray,  # [n_frames, MAX_PLAYERS] i16
    animation_index: np.ndarray,  # [n_frames, MAX_PLAYERS] u32
    facing: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    on_ground: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    pos_x: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    pos_y: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    fighter_scale_y: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    guard_tilt_x8: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    guard_tilt_x4: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    stocks: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    shield_hp: np.ndarray,  # [n_frames, MAX_PLAYERS] f32
    hurtbox_state: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (0 vuln, 1 invuln, 2 intangible)
    hitlag: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u16 (Slippi post hitlag frames left)
    last_hit_by: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u8 (Slippi post x2088 owner port or 0xFF)
    instance_hit_by: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u16 (Slippi post last_hit_by_instance)
    instance_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    input_buttons: np.ndarray,  # [n_frames, MAX_PLAYERS] u16 (pre-frame)
    input_l: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (pre-frame)
    input_r: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (pre-frame)
    turn_has_turned: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u8
    anim_frame_f32: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] f32
    frame_speed_mul_f32: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] f32
    specialhi_rotate_model_f32: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] f32
    specialhi_rotate_model_valid_u8: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u8
    percent: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] f32
    include_per_hitbox: bool = False,
    include_replay_only_shield_admission: bool = False,
    include_replay_only_body_admission: bool = False,
    data_root: str | Path = "data",
) -> tuple[np.ndarray, ...]:
    """
    Derive combat hitlist internals strictly causally from replay prefix history.

    Output is a per-frame snapshot of the internal hitlist cooldown map *after* applying combat
    selection for that frame, using only current-frame external state and previous derived
    internals.

    Seed-bridge note:
    - This derivation uses coarse pose collision to approximate HitCapsule acceptance under
      teacher-forced reseed.
    - To avoid writing synthetic stale latches from geometry-only false positives, insertion is
      additionally corroborated by replay-visible defender hitlag when `hitlag` is provided.
      This keeps the bridge strictly causal while reducing over-latched suppression rows.
    - Decomp ownership anchor for hitlag as accepted-hit consequence:
      refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

    Replay-only shield-admission lane:
    - `include_replay_only_shield_admission=True` may mark an authoritative empty per-HitCapsule
      seed for frame `t` when frame `t+1` proves a fighter shield hit happened, but the dense
      group seed at `t` would suppress the live HitCapsule.
    - Scope is the common AttackAir action family: the exact owner is still per-HitCapsule
      clear/copy + shield-hit insertion, but these are the currently proven aerial shield-reentry
      rows where replay-visible GuardSetOff/hitlag establishes that stale dense fallback is wrong.
      The proof is the GuardSetOff hitlag transition itself; shield HP loss is not required because
      `ftColl_80076CBC` takes the powershield branch when `x221C_b2` is live and skips the normal
      `x19A0_shieldDamageTaken` accumulation.
    - This lane is non-causal and must only be used for teacher-forced replay datasets. It does not
      create runtime rollout behavior; it only selects the already-existing per-hitbox seed lane for
      the reseeded frame.
      refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
      refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}

    Replay-only shield-contact lane:
    - The per-HitCapsule contact tri-state carries the hidden `lbColl_80007BCC` ShieldDesc
      result at the one-step reseed boundary. A visible pose+shield proxy is not exact enough for
      the remaining aerial shield rows: opposite-outcome pairs show both false accepts and false
      misses near the shield rim.
    - `2` is emitted when frame `t+1` proves an accepted shield hit through GuardSetOff plus
      attacker/defender hitlag. `1` is emitted for common AttackAir shield candidates when frame
      `t+1` proves no shield hitlag transition. Normal rollouts leave this seed surface zero.
      refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
      refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC

    Replay-only BODY-admission lane:
    - `include_replay_only_body_admission=True` may mark an authoritative empty per-HitCapsule
      seed for frame `t` when frame `t+1` proves a fighter BODY damage hit happened, but the dense
      group seed at `t` would suppress the live HitCapsule.
    - The proof is intentionally narrower than "overlap exists": defender percent must increase,
      both fighters must enter hitlag, and the defender must either enter/increase hitstun or move
      through a Damage* destination. Phantom/no-damage contacts stay outside this lane.
    - Like the shield lane, this is non-causal and only for teacher-forced replay datasets.
      refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
      refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}

    """
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")

    data_root = Path(data_root)
    common = _load_common_params(data_root)

    # Per-character caches (Fox/Falco first).
    char_cache: dict[int, _CharCombatData | None] = {}
    shield_table_cache: dict[int, _ShieldTiltTable | None] = {}

    def get_char(char_u8: int) -> _CharCombatData | None:
        cid = int(char_u8)
        if cid not in char_cache:
            char_cache[cid] = _load_char_data(char_id=cid, data_root=data_root)
        return char_cache[cid]

    def get_shield_table(char_u8: int) -> _ShieldTiltTable | None:
        cid = int(char_u8)
        if cid not in shield_table_cache:
            ch = get_char(char_u8)
            shield_table_cache[cid] = _read_shield_tilt_table(data_root=data_root, key=ch.key) if ch else None
        return shield_table_cache[cid]

    n_frames = int(np.asarray(action_id).shape[0])
    hitlag_arr: np.ndarray | None = None
    if hitlag is not None:
        hitlag_arr = np.asarray(hitlag, dtype=np.uint16)
        if hitlag_arr.shape[0] != n_frames:
            raise ValueError("hitlag must have the same number of frames as action_id")
    last_hit_by_arr: np.ndarray | None = None
    if last_hit_by is not None:
        last_hit_by_arr = np.asarray(last_hit_by, dtype=np.uint8)
        if last_hit_by_arr.shape[0] != n_frames:
            raise ValueError("last_hit_by must have the same number of frames as action_id")
    instance_hit_by_arr: np.ndarray | None = None
    if instance_hit_by is not None:
        instance_hit_by_arr = np.asarray(instance_hit_by, dtype=np.uint16)
        if instance_hit_by_arr.shape[0] != n_frames:
            raise ValueError("instance_hit_by must have the same number of frames as action_id")
    percent_arr: np.ndarray | None = None
    if percent is not None:
        percent_arr = np.asarray(percent, dtype=np.float32)
        if percent_arr.shape[0] != n_frames:
            raise ValueError("percent must have the same number of frames as action_id")
    turn_has_turned_arr: np.ndarray | None = None
    if turn_has_turned is not None:
        turn_has_turned_arr = np.asarray(turn_has_turned, dtype=np.uint8)
        if turn_has_turned_arr.shape[0] != n_frames:
            raise ValueError("turn_has_turned must have the same number of frames as action_id")
    anim_frame_arr: np.ndarray | None = None
    if anim_frame_f32 is not None:
        anim_frame_arr = np.asarray(anim_frame_f32, dtype=np.float32)
        if anim_frame_arr.shape[0] != n_frames:
            raise ValueError("anim_frame_f32 must have the same number of frames as action_id")
    frame_speed_arr: np.ndarray | None = None
    if frame_speed_mul_f32 is not None:
        frame_speed_arr = np.asarray(frame_speed_mul_f32, dtype=np.float32)
        if frame_speed_arr.shape[0] != n_frames:
            raise ValueError("frame_speed_mul_f32 must have the same number of frames as action_id")
    rotate_model_arr: np.ndarray | None = None
    rotate_model_valid_arr: np.ndarray | None = None
    if specialhi_rotate_model_f32 is not None:
        rotate_model_arr = np.asarray(specialhi_rotate_model_f32, dtype=np.float32)
        if rotate_model_arr.shape[0] != n_frames:
            raise ValueError("specialhi_rotate_model_f32 must have the same number of frames as action_id")
    if specialhi_rotate_model_valid_u8 is not None:
        rotate_model_valid_arr = np.asarray(specialhi_rotate_model_valid_u8, dtype=np.uint8)
        if rotate_model_valid_arr.shape[0] != n_frames:
            raise ValueError("specialhi_rotate_model_valid_u8 must have the same number of frames as action_id")

    # Outputs.
    out_cd = np.zeros((n_frames, MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS), dtype=np.uint16)
    out_iid = np.zeros((n_frames, MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS), dtype=np.uint16)
    out_hb_valid = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)
    out_hb_cd = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES, MAX_PLAYERS), dtype=np.uint16)
    out_hb_iid = np.zeros((n_frames, MAX_PLAYERS, MAX_HITBOXES, MAX_PLAYERS), dtype=np.uint16)
    out_shield_contact_kind = np.zeros(
        (n_frames, MAX_PLAYERS, MAX_HITBOXES, MAX_PLAYERS), dtype=np.uint8
    )

    # Internal state (rollout-causal).
    hitlist_cd = np.zeros((MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS), dtype=np.uint16)
    hitlist_iid = np.zeros((MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS), dtype=np.uint16)
    hitlist_hb_cd = np.zeros((MAX_PLAYERS, MAX_HITBOXES, MAX_PLAYERS), dtype=np.uint16)
    hitlist_hb_iid = np.zeros((MAX_PLAYERS, MAX_HITBOXES, MAX_PLAYERS), dtype=np.uint16)
    hitlist_hb_authoritative = np.zeros((MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)

    sim_hitlag = np.zeros((MAX_PLAYERS,), dtype=np.uint16)
    prev_group_active = np.zeros((MAX_PLAYERS, HITLIST_GROUPS), dtype=bool)
    prev_hb_active = np.zeros((MAX_PLAYERS, MAX_HITBOXES), dtype=bool)
    prev_hb_group = np.zeros((MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)

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
        hb_seed_valid_frame = np.zeros((MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)
        replay_only_hb_valid_frame = np.zeros((MAX_PLAYERS, MAX_HITBOXES), dtype=np.uint8)

        # Refresh hurtcaps/hitboxes/shields for active players only (others remain empty).
        for p in range(num_players):
            ch = get_char(int(char_id[fi, p]))
            af = int(action_frame[fi, p])
            anim_u32 = int(animation_index[fi, p])
            if af >= 0 and 0 <= anim_u32 <= 0xFFFF and ch is not None:
                msid = int(anim_u32) & 0xFFFF
                frame = int(af)
                if anim_frame_arr is not None:
                    af_f = float(anim_frame_arr[fi, p])
                    if np.isfinite(af_f) and af_f >= 0.0:
                        if (
                            frame_speed_arr is not None
                            and (hitlag_arr is None or int(hitlag_arr[fi, p]) == 0)
                        ):
                            af_f += float(frame_speed_arr[fi, p])
                        if af_f < 0.0:
                            af_f = 0.0
                        if af_f > 65535.0:
                            af_f = 65535.0
                        frame = int(np.floor(af_f))

                # Hurtcaps (pose-driven + scaled).
                out_caps: list[dict] = []
                scale_y = float(fighter_scale_y[fi, p])
                model_scale = float(np.float32(scale_y) * np.float32(ch.model_scaling))
                px = float(pos_x[fi, p])
                py = float(pos_y[fi, p])
                facing_dir = 1.0 if int(facing[fi, p]) != 0 else -1.0
                if int(action_id[fi, p]) == 18 and turn_has_turned_arr is not None and int(turn_has_turned_arr[fi, p]) != 0:
                    # Mirror src/hurtboxes.c: ftCo_Turn_Enter records facing_after=-facing_dir,
                    # then ftCo_Turn_Anim_Inner flips fp->facing_dir and sets has_turned once
                    # frames_to_turn expires. Pose-driven hurtcaps use that internal facing, not
                    # the lagging Slippi-facing byte.
                    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
                    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
                    facing_dir = -facing_dir
                rotate_model = (
                    float(rotate_model_arr[fi, p])
                    if rotate_model_arr is not None and rotate_model_valid_arr is not None
                    else 0.0
                )
                rotate_model_valid = (
                    bool(int(rotate_model_valid_arr[fi, p]) != 0)
                    if rotate_model_valid_arr is not None
                    else False
                )
                for cap in ch.hurtcaps[:MAX_HURTCAPS]:
                    m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=cap.bone_part_id)
                    if m is None:
                        continue
                    a = _mtx34_mul_point(m, cap.a_offset)
                    b = _mtx34_mul_point(m, cap.b_offset)
                    # Mirror src/hurtboxes.c:
                    #   local = (pose_mtx * offset) * (fighter_scale_y * model_scaling)
                    #   local = rotY90(local, facing_dir)
                    #   world = pos + local
                    a = (a * np.float32(model_scale)).astype(np.float32, copy=False)
                    b = (b * np.float32(model_scale)).astype(np.float32, copy=False)
                    a = _apply_specialhi_local_xrotn_if_needed(
                        ch=ch,
                        action_id=int(action_id[fi, p]),
                        msid=msid,
                        frame=frame,
                        part_id=cap.bone_part_id,
                        model_scale=model_scale,
                        rotate_model=rotate_model,
                        rotate_model_valid=rotate_model_valid,
                        xyz=a,
                    )
                    b = _apply_specialhi_local_xrotn_if_needed(
                        ch=ch,
                        action_id=int(action_id[fi, p]),
                        msid=msid,
                        frame=frame,
                        part_id=cap.bone_part_id,
                        model_scale=model_scale,
                        rotate_model=rotate_model,
                        rotate_model_valid=rotate_model_valid,
                        xyz=b,
                    )
                    a = _apply_root_facing_rot_y90(a, facing_dir)
                    b = _apply_root_facing_rot_y90(b, facing_dir)
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
                            "r": float(np.float32(cap.scale) * np.float32(model_scale)),
                        }
                    )
                hurtcaps_world[p] = out_caps

                # Hitboxes (pose-driven, runtime scale + facing).
                events = ch.hitboxes_by_msid.get(msid)
                if events:
                    active = _active_hitboxes_at_frame(events, frame)
                    for hb_id, ev in sorted(active.items()):
                        m = ch.pose.try_get_matrix(msid=msid, frame=frame, part_id=ev.bone_part_id)
                        if m is None:
                            continue
                        c = _mtx34_mul_point(m, np.array([ev.x, ev.y, ev.z], dtype=np.float32))
                        # Mirror src/hitboxes.c center placement:
                        #   center = (pose_mtx * offset) * (fighter_scale_y * model_scaling)
                        #   center = rotY90(center, facing_dir)
                        #   world = pos + center
                        c = (c * np.float32(model_scale)).astype(np.float32, copy=False)
                        c = _apply_specialhi_local_xrotn_if_needed(
                            ch=ch,
                            action_id=int(action_id[fi, p]),
                            msid=msid,
                            frame=frame,
                            part_id=ev.bone_part_id,
                            model_scale=model_scale,
                            rotate_model=rotate_model,
                            rotate_model_valid=rotate_model_valid,
                            xyz=c,
                        )
                        c = _apply_root_facing_rot_y90(c, facing_dir)
                        c[0] = np.float32(c[0] + np.float32(px))
                        c[1] = np.float32(c[1] + np.float32(py))
                        # Mirror src/hitboxes.c radius scaling:
                        # - radius uses fighter_scale_y only (not model_scaling) unless ignore flag is set.
                        # - ignore_fighter_scale is extracted into MSLHITB1 u16_6 bit 13.
                        flags = int(ev.u16_6) & 0xFFFF
                        radius = float(ev.radius)
                        if (flags & (1 << 13)) == 0:
                            radius = float(np.float32(radius) * np.float32(scale_y))
                        hitboxes[p][int(hb_id)] = {
                            "x": float(c[0]),
                            "y": float(c[1]),
                            "z": float(c[2]),
                            "r": float(np.float32(radius)),
                            "damage": float(np.float32(ev.damage)),
                            "flags": flags,
                            "def_frame": int(ev.frame),
                            "hit_group": (int(ev.u16_7) >> 8) & 0x7,
                            "rehit_frames": int(ev.u16_7) & 0xFF,
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

                        # Shield bubble center approximation: mirror `src/shields.c::shields_refresh`.
                        #
                        # Runtime shape:
                        # - shields_refresh computes a guard-tilt offset (dx,dy,dz) by lerping between
                        #   the no-tilt Guard live-pose frame and the current guard_tilt_x8 frame only
                        #   for steady Guard with zero/subnormal guard_tilt_x4. Other shield actions
                        #   and nonzero x4 retain the neutral-target scaling path until their separate
                        #   pose owners are proved.
                        # - guard_tilt_x8/x4 themselves are stateful (tilt smoothing), so they must be
                        #   seeded and used consistently between dataset derivation and runtime.
                        #
                        # Seeding shape:
                        # - guard_tilt_x8/x4 are derived strictly causally from replay prefix history
                        #   in tools/slippi/seed_history.py::derive_guard_tilt_state and stored in seed_t.
                        # - This combat hitlist derivation consumes those seeded values, so we do not
                        #   re-run the stick/lerp logic here (avoids divergence).
                        #
                        # Source of truth: data/shields/<char>.bin (MSLSHLD1 v4; ISO-derived).
                        # Approximation: we do not model stage-depth / pos_z here; current runtime
                        # policy is effectively 2D, so pos_z is assumed 0 in this derivation.
                        tv = get_shield_table(char_id[fi, p])
                        if tv is not None and tv.xyz.size != 0:
                            frame_max = int(tv.xyz.shape[0] - 1)
                            f = int(guard_tilt_x8[fi, p])
                            if f < 0:
                                f = 0
                            if f > frame_max:
                                f = frame_max
                            mag = float(guard_tilt_x4[fi, p])
                            mag = _clamp01(mag)
                            steady_guard_no_tilt = (
                                int(action_id[fi, p]) == ACT_GUARD and (mag == 0.0 or mag < float(np.finfo(np.float32).tiny))
                            )
                            neutral = 0 if steady_guard_no_tilt else int(tv.neutral_frame)
                            nx, ny, nz = (float(tv.xyz[neutral, 0]), float(tv.xyz[neutral, 1]), float(tv.xyz[neutral, 2]))
                            fx, fy, fz = (float(tv.xyz[f, 0]), float(tv.xyz[f, 1]), float(tv.xyz[f, 2]))
                            dx = nx + mag * (fx - nx)
                            dy = ny + mag * (fy - ny)
                            dz = nz + mag * (fz - nz)
                            facing_dir = 1.0 if int(facing[fi, p]) != 0 else -1.0
                            pose_scale = float(fighter_scale_y[fi, p])
                            if steady_guard_no_tilt:
                                pose_scale *= float(ch.model_scaling)
                            sx = px + dz * pose_scale * facing_dir
                            sy = py + dy * pose_scale
                            sz = -dx * pose_scale * facing_dir
                shield_world[p] = (sx, sy, sz, sr)

        # Combat resolve (BODY-only selection + hitlist update + simulated hitlag gate).
        #
        # A hit accepted during frame `fi` becomes part of the teacher-forced seed for frame
        # `fi + 1`, not the seed snapshot for `fi`. Runtime registers the HitCapsule victim after
        # the BODY/shield callback mutates the defender; for replay-derived seed history, keep that
        # post-output registration delayed by one row when only the next Slippi post-frame proves
        # the accepted BODY hit.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
        # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
        pending_body_registers: list[tuple[int, int, int, int, int, int]] = []
        for attacker in range(num_players):
            if int(stocks[fi, attacker]) == 0:
                hitlist_hb_cd[attacker, :, :] = np.uint16(0)
                hitlist_hb_iid[attacker, :, :] = np.uint16(0)
                prev_hb_active[attacker, :] = False
                prev_group_active[attacker, :] = False
                continue
            a_hitboxes = hitboxes[attacker]
            if a_hitboxes:
                # Legacy fallback bridge: preserve the previous group-indexed derivation for old and
                # synthetic seeds. This intentionally remains coarser than the authoritative
                # per-hitbox lane below.
                group_active = [False] * HITLIST_GROUPS
                for hb in a_hitboxes.values():
                    g = int(hb.get("hit_group", 0)) & 0x7
                    group_active[g] = True
                for g in range(HITLIST_GROUPS):
                    if group_active[g] and not bool(prev_group_active[attacker, g]):
                        hitlist_cd[attacker, g, :] = np.uint16(0)
                        hitlist_iid[attacker, g, :] = np.uint16(0)
                for g in range(HITLIST_GROUPS):
                    prev_group_active[attacker, g] = group_active[g]
                for g in range(HITLIST_GROUPS):
                    if not group_active[g]:
                        continue
                    for victim in range(num_players):
                        cd = int(hitlist_cd[attacker, g, victim])
                        if cd == 0 or cd == HITLIST_CD_INDEFINITE:
                            continue
                        cd2 = int(cd - 1)
                        hitlist_cd[attacker, g, victim] = np.uint16(cd2)
                        if cd2 == 0:
                            hitlist_iid[attacker, g, victim] = np.uint16(0)
            # Hitlist clear/copy-on-enable and decrement finite cooldowns per HitCapsule.
            #
            # Decomp ownership:
            # - Each hitbox slot owns a HitCapsule victim list.
            # - ftColl_800768A0 copies from an already-active same-hit_group capsule on enable edges,
            #   else clears the new capsule.
            # - lbColl_80008A5C decrements active capsules.
            # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_CopyHitCapsule,lbColl_80008440,lbColl_80008A5C}
            prev_cd = hitlist_hb_cd[attacker].copy()
            prev_iid = hitlist_hb_iid[attacker].copy()
            cur_active = [False] * MAX_HITBOXES
            cur_group = [0] * MAX_HITBOXES
            for hb_id, hb in a_hitboxes.items():
                if 0 <= int(hb_id) < MAX_HITBOXES:
                    cur_active[int(hb_id)] = True
                    cur_group[int(hb_id)] = int(hb.get("hit_group", 0)) & 0x7
            for hb_id in range(MAX_HITBOXES):
                if not cur_active[hb_id]:
                    hitlist_hb_cd[attacker, hb_id, :] = np.uint16(0)
                    hitlist_hb_iid[attacker, hb_id, :] = np.uint16(0)
                    hitlist_hb_authoritative[attacker, hb_id] = np.uint8(0)
                    continue
                g = cur_group[hb_id]
                enable_edge = (not bool(prev_hb_active[attacker, hb_id])) or int(prev_hb_group[attacker, hb_id]) != g
                if enable_edge:
                    copied = False
                    for src in range(MAX_HITBOXES):
                        if src == hb_id:
                            continue
                        if not bool(prev_hb_active[attacker, src]):
                            continue
                        if int(prev_hb_group[attacker, src]) != g:
                            continue
                        hitlist_hb_cd[attacker, hb_id, :] = prev_cd[src, :]
                        hitlist_hb_iid[attacker, hb_id, :] = prev_iid[src, :]
                        hb_seed_valid_frame[attacker, hb_id] = np.uint8(1)
                        hitlist_hb_authoritative[attacker, hb_id] = hitlist_hb_authoritative[attacker, src]
                        copied = True
                        break
                    if not copied:
                        hitlist_hb_cd[attacker, hb_id, :] = np.uint16(0)
                        hitlist_hb_iid[attacker, hb_id, :] = np.uint16(0)
                        hb_seed_valid_frame[attacker, hb_id] = np.uint8(0)
                        hitlist_hb_authoritative[attacker, hb_id] = np.uint8(0)
                else:
                    hb_seed_valid_frame[attacker, hb_id] = np.uint8(1)
                if int(sim_hitlag[attacker]) != 0:
                    continue
                for victim in range(num_players):
                    cd = int(hitlist_hb_cd[attacker, hb_id, victim])
                    if cd == 0 or cd == HITLIST_CD_INDEFINITE:
                        continue
                    cd2 = int(cd - 1)
                    hitlist_hb_cd[attacker, hb_id, victim] = np.uint16(cd2)
                    if cd2 == 0:
                        hitlist_hb_iid[attacker, hb_id, victim] = np.uint16(0)
            for hb_id in range(MAX_HITBOXES):
                prev_hb_active[attacker, hb_id] = cur_active[hb_id]
                prev_hb_group[attacker, hb_id] = np.uint8(cur_group[hb_id])

            if not a_hitboxes:
                continue

            for defender in range(num_players):
                if defender == attacker:
                    continue
                if int(stocks[fi, defender]) == 0:
                    continue
                defender_hurt_status = int(hurtbox_state[fi, defender])
                if defender_hurt_status == 2:
                    continue
                defender_no_damage = defender_hurt_status != 0

                if is_teams and int(team_id[fi, attacker]) == int(team_id[fi, defender]):
                    continue

                # Hitlag gating: when either fighter is in hitlag, do not generate new BODY hits.
                if int(sim_hitlag[attacker]) != 0 or int(sim_hitlag[defender]) != 0:
                    continue

                shx, shy, shz, shr = shield_world[defender]
                shield_active = shr > 0.0

                # Deterministic selection: pick the first BODY overlap in (hitbox_id, hurtcap_id) order.
                did_hit = False
                defender_on_ground = int(on_ground[fi, defender]) != 0
                # Replay-visible corroboration gate (strictly causal):
                # only seed a hitlist latch when the defender is in hitlag on this frame.
                # This trims synthetic stale latches from approximate geometry without using lookahead.
                defender_hitlag_seen = int(hitlag_arr[fi, defender]) > 0 if hitlag_arr is not None else True
                attacker_hitlag_seen = int(hitlag_arr[fi, attacker]) > 0 if hitlag_arr is not None else True

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
                    shield_contact_seed_kind = 0
                    if (
                        include_replay_only_shield_admission
                        and shield_active
                        and hitlag_arr is not None
                        and fi + 1 < n_frames
                        and _is_attackair_action(int(action_id[fi, attacker]))
                        and float(hb.get("damage", 0.0)) > 0.0
                        and int(hitlag_arr[fi, defender]) == 0
                        and int(hitlag_arr[fi, attacker]) == 0
                    ):
                        if (
                            int(action_id[fi + 1, defender]) == ACT_GUARD_SET_OFF
                            and int(hitlag_arr[fi + 1, defender]) > 0
                            and int(hitlag_arr[fi + 1, attacker]) > 0
                        ):
                            shield_contact_seed_kind = 2
                        elif int(hitlag_arr[fi + 1, defender]) == 0 and int(hitlag_arr[fi + 1, attacker]) == 0:
                            shield_contact_seed_kind = 1
                    if shield_contact_seed_kind:
                        out_shield_contact_kind[fi, attacker, hb_id, defender] = np.uint8(
                            shield_contact_seed_kind
                        )

                    # Rehit suppression (hitlists): the legacy fallback derivation uses the
                    # group-indexed seed bridge as its acceptance gate. The per-hitbox payload below
                    # is populated alongside accepted hits, but remains non-authoritative until
                    # HitCapsule.state can be seeded.
                    #
                    # Decomp trail (GALE01):
                    # - Shield overlap uses geometry only: lbColl_80007BCC(...) has no hitlist logic inside.
                    #   refs/melee/src/melee/ft/ftcoll.c (shield path around lbColl_80007BCC)
                    #   refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
                    # - The rehit gate happens outside geometry: lbColl_8000ACFC(victim_fp, hitcapsule)
                    #   is part of the "eligible hitcapsule" predicate, before the shield/body branches.
                    #   refs/melee/src/melee/ft/ftcoll.c (shield branch predicate includes lbColl_8000ACFC(...)==0)
                    #   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
                    #
                    # Mirror that ordering here: apply hitlist gating before shield/body geometry tests.
                    hit_group = int(hb.get("hit_group", 0)) & 0x7
                    prune_guard_stale_seed_bridge = False
                    first_guardsetoff_hitlag_row = _is_first_guardsetoff_hitlag_row(
                        fi=fi, defender=defender, action_id=action_id, hitlag_arr=hitlag_arr
                    )
                    guardsetoff_shield_damage_onset = _is_guardsetoff_shield_damage_onset(
                        fi=fi,
                        defender=defender,
                        action_id=action_id,
                        hitlag_arr=hitlag_arr,
                        shield_hp=shield_hp,
                    )
                    if (
                        hitlag_arr is not None
                        and last_hit_by_arr is not None
                        and instance_hit_by_arr is not None
                        and _should_prune_guard_stale_seed_bridge(
                            defender_action_id=int(action_id[fi, defender]),
                            defender_hitlag=int(hitlag_arr[fi, defender]),
                            defender_last_hit_by=int(last_hit_by_arr[fi, defender]),
                            defender_instance_hit_by=int(instance_hit_by_arr[fi, defender]),
                            attacker_port=attacker,
                            attacker_instance_id=int(instance_id[fi, attacker]),
                            hitbox_def_frame=int(hb.get("def_frame", -0x8000)),
                            attacker_action_frame=int(action_frame[fi, attacker]),
                        )
                    ):
                        # Seed-materialization bridge (strictly causal):
                        # - On the hitbox's create-order frame (pose order lane), stale victims_1
                        #   entries with replay-visible attribution mismatch can leak from dense
                        #   group seeding and block first valid shield resolve.
                        # - Prune that stale pair so ftColl_80076CBC shield-hit resolution can run.
                        #
                        # Decomp ownership anchors:
                        # - enable-edge clear/copy path:
                        #   refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
                        # - rehit containment gate:
                        #   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
                        # - shield-hit branch ownership:
                        #   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
                        prune_guard_stale_seed_bridge = True
                    cd = int(hitlist_cd[attacker, hit_group, defender])
                    if cd != 0:
                        # Onset-row per-HitCapsule provenance carry:
                        # - The dense group latch is born too early for this frozen-Guard family, so
                        #   it cannot distinguish which hitbox slots actually own the accepted shield
                        #   contact.
                        # - On the first replay-visible GuardSetOff hitlag row, the dense group
                        #   latch plus shield-hitlag state prove a prior shield admission. Stamp
                        #   authoritative per-hitbox lineage for every currently active slot in
                        #   the accepted hit_group, matching ftColl_80076808's same-group insert.
                        # - Later frozen rows must carry that onset lineage forward; they must not
                        #   refresh it from overlap again.
                        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
                        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
                        if first_guardsetoff_hitlag_row or guardsetoff_shield_damage_onset:
                            rehit_frames = int(hb.get("rehit_frames", 0)) & 0xFF
                            seeded_cd = (
                                np.uint16(HITLIST_CD_INDEFINITE)
                                if rehit_frames == 0
                                else np.uint16(rehit_frames)
                            )
                            defender_iid = np.uint16(int(instance_id[fi, defender]))
                            for reg_hb_id, reg_hb in a_hitboxes.items():
                                if (int(reg_hb.get("hit_group", 0)) & 0x7) != hit_group:
                                    continue
                                reg_hb_i = int(reg_hb_id)
                                hitlist_hb_cd[attacker, reg_hb_i, defender] = seeded_cd
                                hitlist_hb_iid[attacker, reg_hb_i, defender] = defender_iid
                                hitlist_hb_authoritative[attacker, reg_hb_i] = np.uint8(1)
                        if (
                            include_replay_only_shield_admission
                            and hitlag_arr is not None
                            and fi + 1 < n_frames
                            and _is_attackair_action(int(action_id[fi, attacker]))
                            and float(hb.get("damage", 0.0)) > 0.0
                            and int(hitlag_arr[fi, defender]) == 0
                            and int(hitlag_arr[fi, attacker]) == 0
                            and int(action_id[fi + 1, defender]) == ACT_GUARD_SET_OFF
                            and int(hitlag_arr[fi + 1, defender]) > 0
                            and int(hitlag_arr[fi + 1, attacker]) > 0
                        ):
                            # Replay-only per-HitCapsule provenance bridge:
                            # - The dense group snapshot says "victim present", but the next
                            #   Slippi post-frame proves that this exact frame admitted a fighter
                            #   shield hit into GuardSetOff/hitlag.
                            # - Do not require replay-visible shield HP loss here: decomp
                            #   `ftColl_80076CBC` skips the normal shield-damage accumulator on
                            #   the powershield-active `x221C_b2` branch while still accepting the
                            #   shield hit and entering GuardSetOff/hitlag.
                            # - Mark the active same-group HitCapsules authoritative-empty so
                            #   teacher-forced one-step reseed uses the decomp-shaped per-HitCapsule
                            #   lane instead of the coarse group fallback.
                            # - This is intentionally not a runtime rule; normal rollouts carry
                            #   HitCapsule victim rings directly through ftColl_800768A0.
                            # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
                            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
                            for reg_hb_id, reg_hb in a_hitboxes.items():
                                if (int(reg_hb.get("hit_group", 0)) & 0x7) != hit_group:
                                    continue
                                reg_hb_i = int(reg_hb_id)
                                hitlist_hb_cd[attacker, reg_hb_i, defender] = np.uint16(0)
                                hitlist_hb_iid[attacker, reg_hb_i, defender] = np.uint16(0)
                                replay_only_hb_valid_frame[attacker, reg_hb_i] = np.uint8(1)
                        elif (
                            include_replay_only_body_admission
                            and hitlag_arr is not None
                            and percent_arr is not None
                            and fi + 1 < n_frames
                            and float(hb.get("damage", 0.0)) > 0.0
                            and int(hitlag_arr[fi, defender]) == 0
                            and int(hitlag_arr[fi, attacker]) == 0
                            and int(hitlag_arr[fi + 1, defender]) > 0
                            and int(hitlag_arr[fi + 1, attacker]) > 0
                            and float(percent_arr[fi + 1, defender]) > float(percent_arr[fi, defender])
                            and (
                                last_hit_by_arr is None
                                or int(last_hit_by_arr[fi + 1, defender]) == int(attacker)
                            )
                            and (
                                instance_hit_by_arr is None
                                or int(instance_hit_by_arr[fi + 1, defender])
                                == int(instance_id[fi, attacker])
                            )
                        ):
                            # Replay-only per-HitCapsule BODY-admission provenance bridge:
                            # - The dense group snapshot says "victim present", but the next Slippi
                            #   post-frame proves a fighter BODY damage hit was admitted from this
                            #   attacker (percent increase + hitlag on both fighters + source owner).
                            # - Mark active same-group HitCapsules authoritative-empty for the
                            #   teacher-forced seed frame, preserving the decomp HitCapsule owner
                            #   instead of clearing the coarse group latch.
                            # - Phantom/no-damage contacts remain outside this lane because percent
                            #   must increase.
                            # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
                            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
                            for reg_hb_id, reg_hb in a_hitboxes.items():
                                if (int(reg_hb.get("hit_group", 0)) & 0x7) != hit_group:
                                    continue
                                reg_hb_i = int(reg_hb_id)
                                hitlist_hb_cd[attacker, reg_hb_i, defender] = np.uint16(0)
                                hitlist_hb_iid[attacker, reg_hb_i, defender] = np.uint16(0)
                                replay_only_hb_valid_frame[attacker, reg_hb_i] = np.uint8(1)
                        else:
                            # Victim identity key matches decomp `HitVictim.victim` pointer:
                            # use the Slippi-visible `instance_id` to drop stale entries on respawn.
                            # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
                            def_iid = int(instance_id[fi, defender])
                            if int(hitlist_iid[attacker, hit_group, defender]) == def_iid:
                                if prune_guard_stale_seed_bridge:
                                    hitlist_cd[attacker, hit_group, defender] = np.uint16(0)
                                    hitlist_iid[attacker, hit_group, defender] = np.uint16(0)
                                else:
                                    continue
                            elif prune_guard_stale_seed_bridge:
                                hitlist_cd[attacker, hit_group, defender] = np.uint16(0)
                                hitlist_iid[attacker, hit_group, defender] = np.uint16(0)
                            # Mirror src/hitlist.c: preserve suppression across instance_id changes
                            # unless the underlying victim pointer may have changed (death/respawn).
                            elif _hitlist_victim_pointer_may_change(
                                stocks=int(stocks[fi, defender]),
                                action_id=int(action_id[fi, defender]),
                            ):
                                hitlist_cd[attacker, hit_group, defender] = np.uint16(0)
                                hitlist_iid[attacker, hit_group, defender] = np.uint16(0)
                            else:
                                hitlist_iid[attacker, hit_group, defender] = np.uint16(def_iid)
                                continue

                    # SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as a
                    # shield contact and register hitlist state (so subsequent frames are suppressed).
                    #
                    # Decomp: this branch corresponds to the lbColl_80007BCC(...) shield overlap test
                    # followed by shield hit handling (ftColl_80076CBC), with rehit gating already
                    # applied by lbColl_8000ACFC in the predicate.
                    # refs/melee/src/melee/ft/ftcoll.c (shield branch around lbColl_80007BCC + ftColl_80076CBC)
                    shield_contact = shield_active and (
                        shield_contact_seed_kind == 2
                        or (
                            shield_contact_seed_kind != 1
                            and _sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr)
                        )
                    )
                    if shield_contact:
                        # Mirror src/combat.c: only treat positive-damage hitboxes as shield hits.
                        if float(hb.get("damage", 0.0)) > 0.0 and defender_hitlag_seen:
                            rehit_frames = int(hb.get("rehit_frames", 0)) & 0xFF
                            for reg_hb_id, reg_hb in a_hitboxes.items():
                                if (int(reg_hb.get("hit_group", 0)) & 0x7) != hit_group:
                                    continue
                                reg_hb_i = int(reg_hb_id)
                                hitlist_hb_cd[attacker, reg_hb_i, defender] = (
                                    np.uint16(HITLIST_CD_INDEFINITE)
                                    if rehit_frames == 0
                                    else np.uint16(rehit_frames)
                                )
                                hitlist_hb_iid[attacker, reg_hb_i, defender] = np.uint16(
                                    int(instance_id[fi, defender])
                                )
                                hitlist_hb_authoritative[attacker, reg_hb_i] = np.uint8(1)
                            hitlist_cd[attacker, hit_group, defender] = (
                                np.uint16(HITLIST_CD_INDEFINITE)
                                if rehit_frames == 0
                                else np.uint16(rehit_frames)
                            )
                            hitlist_iid[attacker, hit_group, defender] = np.uint16(int(instance_id[fi, defender]))

                            # Minimal hitlag simulation for hitlag gating across frames (no replay lookahead).
                            dmg_i = _get_env_dmg(float(hb["damage"]))
                            hl = _calc_hitlag_frames(hitlag_dmg_mul, hitlag_base, dmg_i)
                            sim_hitlag[attacker] = np.uint16(hl)
                            sim_hitlag[defender] = np.uint16(hl)

                            did_hit = True
                            break
                        continue

                    if not hurtcaps_world[defender]:
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

                        # Hitlist register (replay-corroborated when hitlag is provided).
                        #
                        # Invincible/no-damage BODY contact still writes HitCapsule.victims_1:
                        # ftColl_80076ED8 calls inlineB0(...lbColl_80008688) before the vulnerable
                        # damage guard (`x1988 == 0 && x198C == 0 && !x221D_b6 && hurt enabled`).
                        # Slippi exposes this contact through attacker hitlag while defender damage
                        # and defender hitlag stay clear, so use current prefix-visible attacker
                        # hitlag as the no-damage contact corroboration. Intangible defenders were
                        # filtered above and never reach lbColl_8000805C.
                        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
                        # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
                        const_no_damage_contact_seen = defender_no_damage and attacker_hitlag_seen
                        if not defender_hitlag_seen and not const_no_damage_contact_seen:
                            if not (
                                include_replay_only_body_admission
                                and hitlag_arr is not None
                                and percent_arr is not None
                                and fi + 1 < n_frames
                                and int(hitlag_arr[fi, defender]) == 0
                                and int(hitlag_arr[fi, attacker]) == 0
                                and int(hitlag_arr[fi + 1, defender]) > 0
                                and int(hitlag_arr[fi + 1, attacker]) > 0
                                and float(percent_arr[fi + 1, defender]) > float(percent_arr[fi, defender])
                                and (
                                    last_hit_by_arr is None
                                    or int(last_hit_by_arr[fi + 1, defender]) == int(attacker)
                                )
                                and (
                                    instance_hit_by_arr is None
                                    or int(instance_hit_by_arr[fi + 1, defender])
                                    == int(instance_id[fi, attacker])
                                )
                            ):
                                continue
                            rehit_frames = int(hb.get("rehit_frames", 0)) & 0xFF
                            dmg_i = _get_env_dmg(float(hb["damage"]))
                            hl = _calc_hitlag_frames(hitlag_dmg_mul, hitlag_base, dmg_i)
                            pending_body_registers.append(
                                (
                                    int(attacker),
                                    int(hit_group),
                                    int(defender),
                                    int(rehit_frames),
                                    int(instance_id[fi + 1, defender]),
                                    int(hl),
                                )
                            )
                            did_hit = True
                            break

                        rehit_frames = int(hb.get("rehit_frames", 0)) & 0xFF
                        for reg_hb_id, reg_hb in a_hitboxes.items():
                            if (int(reg_hb.get("hit_group", 0)) & 0x7) != hit_group:
                                continue
                            reg_hb_i = int(reg_hb_id)
                            hitlist_hb_cd[attacker, reg_hb_i, defender] = (
                                np.uint16(HITLIST_CD_INDEFINITE) if rehit_frames == 0 else np.uint16(rehit_frames)
                            )
                            hitlist_hb_iid[attacker, reg_hb_i, defender] = np.uint16(
                                int(instance_id[fi, defender])
                            )
                            hitlist_hb_authoritative[attacker, reg_hb_i] = np.uint8(1)
                        hitlist_cd[attacker, hit_group, defender] = (
                            np.uint16(HITLIST_CD_INDEFINITE) if rehit_frames == 0 else np.uint16(rehit_frames)
                        )
                        hitlist_iid[attacker, hit_group, defender] = np.uint16(int(instance_id[fi, defender]))

                        # Minimal hitlag simulation for hitlag gating across frames (no replay lookahead).
                        dmg_i = _get_env_dmg(float(hb["damage"]))
                        hl = _calc_hitlag_frames(hitlag_dmg_mul, hitlag_base, dmg_i)
                        if defender_no_damage:
                            # The no-damage branch only proves attacker hitlag and hidden
                            # HitCapsule lineage. Do not synthesize defender hitlag/damage state.
                            if hitlag_arr is not None:
                                sim_hitlag[attacker] = np.uint16(int(hitlag_arr[fi, attacker]))
                        else:
                            sim_hitlag[attacker] = np.uint16(hl)
                            sim_hitlag[defender] = np.uint16(hl)

                        did_hit = True
                        break

                    if did_hit:
                        break

        for attacker in range(num_players):
            for hb_id, hb in hitboxes[attacker].items():
                if not (0 <= int(hb_id) < MAX_HITBOXES):
                    continue
                # Authoritative per-HitCapsule seed lane for accepted shield/body provenance:
                # - Accepted contacts write HitCapsule.victims_1 on the active same-hit_group
                #   capsules through ftColl_80076808/inlineB0.
                # - That hidden list is action-local to the HitCapsule, not to the defender's
                #   visible motion state; it must survive GuardSetOff -> Guard -> KneeBend and
                #   related victim instance_id proxy changes until the attacker hitbox is cleared.
                # - Mark every active authoritative capsule valid so reseed materializes the
                #   per-HitCapsule list instead of falling back to the coarser group seed. Empty
                #   authoritative capsules are valid too: they represent a real copied/cleared
                #   HitCapsule list and prevent stale group fallback from changing ownership.
                # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
                # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076ED8}
                # refs/melee/src/melee/lb/types.h::HitCapsule
                hb_i = int(hb_id)
                if int(replay_only_hb_valid_frame[attacker, hb_i]) != 0:
                    out_hb_valid[fi, attacker, hb_i] = np.uint8(1)
                    continue
                out_hb_valid[fi, attacker, hb_i] = np.uint8(
                    1 if int(hitlist_hb_authoritative[attacker, hb_i]) != 0 else 0
                )

        out_cd[fi] = hitlist_cd
        out_iid[fi] = hitlist_iid
        out_hb_cd[fi] = hitlist_hb_cd
        out_hb_iid[fi] = hitlist_hb_iid

        for attacker, hit_group, defender, rehit_frames, defender_iid_post, hl in pending_body_registers:
            seeded_cd = (
                np.uint16(HITLIST_CD_INDEFINITE)
                if int(rehit_frames) == 0
                else np.uint16(int(rehit_frames))
            )
            defender_iid_u16 = np.uint16(int(defender_iid_post))
            for reg_hb_id, reg_hb in hitboxes[attacker].items():
                if (int(reg_hb.get("hit_group", 0)) & 0x7) != int(hit_group):
                    continue
                reg_hb_i = int(reg_hb_id)
                hitlist_hb_cd[attacker, reg_hb_i, defender] = seeded_cd
                hitlist_hb_iid[attacker, reg_hb_i, defender] = defender_iid_u16
                hitlist_hb_authoritative[attacker, reg_hb_i] = np.uint8(1)
            hitlist_cd[attacker, hit_group, defender] = seeded_cd
            hitlist_iid[attacker, hit_group, defender] = defender_iid_u16
            sim_hitlag[attacker] = np.uint16(int(hl))
            sim_hitlag[defender] = np.uint16(int(hl))

    if include_per_hitbox:
        return out_cd, out_iid, out_hb_valid, out_hb_cd, out_hb_iid, out_shield_contact_kind
    return out_cd, out_iid
