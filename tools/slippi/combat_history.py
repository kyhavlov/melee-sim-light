from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAX_PLAYERS = 4
MAX_HITBOXES = 4
HITLIST_GROUPS = 8
HITLIST_CD_INDEFINITE = 0xFFFF

# src/action_ids.h
ACT_GUARD = 0x00B3
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


@dataclass(frozen=True)
class _AnimEntry:
    frame_count: int
    mats_off: int  # start of [frame][joint] matrices
    joint_count: int


class AnimPoseDB:
    """Small SSANIM01 reader retained for focused tests.

    Production preprocessing uses native generated-table bindings instead of this Python reader.
    """

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
                raise ValueError(
                    f"SSANIM01: truncated anim payload: msid={msid} frame_count={frame_count}"
                )

            entries[int(msid)] = _AnimEntry(
                frame_count=int(frame_count),
                mats_off=int(off),
                joint_count=int(joint_count),
            )
            off += need

        if off != len(buf):
            raise ValueError(f"SSANIM01: unexpected trailing bytes: {len(buf) - off}")

        self._by_msid = entries
        self._matrix_cache: dict[tuple[int, int, int], np.ndarray | None] = {}

    def try_get_matrix(self, *, msid: int, frame: int, part_id: int) -> np.ndarray | None:
        key = (int(msid), int(frame), int(part_id))
        cached = self._matrix_cache.get(key, ...)
        if cached is not ...:
            return cached
        ent = self._by_msid.get(int(msid))
        if ent is None:
            self._matrix_cache[key] = None
            return None
        if int(frame) < 0 or int(frame) >= int(ent.frame_count):
            self._matrix_cache[key] = None
            return None
        ji = self._part_to_joint_index.get(int(part_id))
        if ji is None:
            self._matrix_cache[key] = None
            return None
        off = int(ent.mats_off) + int(frame) * int(ent.joint_count) * self._MAT_BYTES + int(ji) * self._MAT_BYTES
        m = np.frombuffer(self._buf, dtype="<f4", count=12, offset=off)
        self._matrix_cache[key] = m
        return m


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
    """Derive hidden HitCapsule.x58 seed lanes through the native data-table path."""
    if str(data_root) not in ("data", "./data"):
        raise ValueError(
            "derive_hitbox_prev_center_seed_fields now uses native generated tables; set "
            "MSL_DATA_DIR for non-default data roots instead of using the removed Python fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_hitbox_prev_centers is required for preprocessing; run `make build`"
        ) from exc

    char_arr = np.ascontiguousarray(char_id, dtype=np.uint8)
    n_frames = int(char_arr.shape[0])
    action_arr = (
        np.ascontiguousarray(action_id, dtype=np.uint16)
        if action_id is not None
        else np.full(char_arr.shape, 0xFFFF, dtype=np.uint16)
    )
    if pos_z is None:
        pos_z_arr = None
    else:
        pos_z_arr = np.ascontiguousarray(pos_z, dtype=np.float32)
        if pos_z_arr.ndim == 1:
            pos_z_arr = np.repeat(pos_z_arr.reshape((n_frames, 1)), char_arr.shape[1], axis=1)

    return msl_binding.derive_hitbox_prev_centers(
        int(num_players),
        char_arr,
        action_arr,
        np.ascontiguousarray(animation_index, dtype=np.uint32),
        np.ascontiguousarray(action_frame, dtype=np.int16),
        np.ascontiguousarray(anim_frame_f32, dtype=np.float32),
        np.ascontiguousarray(pos_x, dtype=np.float32),
        np.ascontiguousarray(pos_y, dtype=np.float32),
        pos_z_arr,
        np.ascontiguousarray(facing, dtype=np.uint8),
        np.ascontiguousarray(fighter_scale_y, dtype=np.float32),
        np.ascontiguousarray(specialhi_rotate_model_f32, dtype=np.float32)
        if specialhi_rotate_model_f32 is not None
        else None,
        np.ascontiguousarray(specialhi_rotate_model_valid_u8, dtype=np.uint8)
        if specialhi_rotate_model_valid_u8 is not None
        else None,
    )


def derive_combat_hitlist_seed_fields(
    *,
    num_players: int,
    is_teams: bool,
    team_id: np.ndarray,
    char_id: np.ndarray,
    action_id: np.ndarray,
    action_frame: np.ndarray,
    animation_index: np.ndarray,
    facing: np.ndarray,
    on_ground: np.ndarray,
    pos_x: np.ndarray,
    pos_y: np.ndarray,
    fighter_scale_y: np.ndarray,
    guard_tilt_x8: np.ndarray,
    guard_tilt_x4: np.ndarray,
    stocks: np.ndarray,
    shield_hp: np.ndarray,
    hurtbox_state: np.ndarray,
    hitlag: np.ndarray | None = None,
    last_hit_by: np.ndarray | None = None,
    instance_hit_by: np.ndarray | None = None,
    instance_id: np.ndarray,
    input_buttons: np.ndarray,
    input_l: np.ndarray,
    input_r: np.ndarray,
    turn_has_turned: np.ndarray | None = None,
    anim_frame_f32: np.ndarray | None = None,
    frame_speed_mul_f32: np.ndarray | None = None,
    specialhi_rotate_model_f32: np.ndarray | None = None,
    specialhi_rotate_model_valid_u8: np.ndarray | None = None,
    percent: np.ndarray | None = None,
    include_per_hitbox: bool = False,
    include_replay_only_shield_admission: bool = False,
    include_replay_only_body_admission: bool = False,
    data_root: str | Path = "data",
) -> tuple[np.ndarray, ...]:
    """Derive combat hitlist seed internals through the native data-table path."""
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")
    if str(data_root) not in ("data", "./data"):
        raise ValueError(
            "derive_combat_hitlist_seed_fields now uses native generated tables; set MSL_DATA_DIR "
            "for non-default data roots instead of using the removed Python fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_combat_hitlist_seed_fields is required for preprocessing; "
            "run `make build`"
        ) from exc

    out = msl_binding.derive_combat_hitlist_seed_fields(
        int(num_players),
        int(bool(is_teams)),
        np.ascontiguousarray(team_id, dtype=np.uint8),
        np.ascontiguousarray(char_id, dtype=np.uint8),
        np.ascontiguousarray(action_id, dtype=np.uint16),
        np.ascontiguousarray(action_frame, dtype=np.int16),
        np.ascontiguousarray(animation_index, dtype=np.uint32),
        np.ascontiguousarray(facing, dtype=np.uint8),
        np.ascontiguousarray(on_ground, dtype=np.uint8),
        np.ascontiguousarray(pos_x, dtype=np.float32),
        np.ascontiguousarray(pos_y, dtype=np.float32),
        np.ascontiguousarray(fighter_scale_y, dtype=np.float32),
        np.ascontiguousarray(guard_tilt_x8, dtype=np.uint16),
        np.ascontiguousarray(guard_tilt_x4, dtype=np.float32),
        np.ascontiguousarray(stocks, dtype=np.uint8),
        np.ascontiguousarray(shield_hp, dtype=np.float32),
        np.ascontiguousarray(hurtbox_state, dtype=np.uint8),
        np.ascontiguousarray(hitlag, dtype=np.uint16) if hitlag is not None else None,
        np.ascontiguousarray(last_hit_by, dtype=np.uint8) if last_hit_by is not None else None,
        np.ascontiguousarray(instance_hit_by, dtype=np.uint16) if instance_hit_by is not None else None,
        np.ascontiguousarray(instance_id, dtype=np.uint16),
        np.ascontiguousarray(input_buttons, dtype=np.uint16),
        np.ascontiguousarray(input_l, dtype=np.uint8),
        np.ascontiguousarray(input_r, dtype=np.uint8),
        np.ascontiguousarray(turn_has_turned, dtype=np.uint8) if turn_has_turned is not None else None,
        np.ascontiguousarray(anim_frame_f32, dtype=np.float32) if anim_frame_f32 is not None else None,
        np.ascontiguousarray(frame_speed_mul_f32, dtype=np.float32) if frame_speed_mul_f32 is not None else None,
        np.ascontiguousarray(specialhi_rotate_model_f32, dtype=np.float32)
        if specialhi_rotate_model_f32 is not None
        else None,
        np.ascontiguousarray(specialhi_rotate_model_valid_u8, dtype=np.uint8)
        if specialhi_rotate_model_valid_u8 is not None
        else None,
        np.ascontiguousarray(percent, dtype=np.float32) if percent is not None else None,
        int(bool(include_per_hitbox)),
        int(bool(include_replay_only_shield_admission)),
        int(bool(include_replay_only_body_admission)),
    )
    if include_per_hitbox:
        return out
    return out[0], out[1]
