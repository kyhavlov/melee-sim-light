from __future__ import annotations

# Dataset file format + NumPy dtypes.

import dataclasses
import struct
from typing import Final

import numpy as np


MAGIC: Final[bytes] = b"MSLDSLT "


HEADER_DTYPE = np.dtype(
    [
        ("magic", "S8"),
        ("record_size", "<u4"),
        ("num_records", "<u4"),
        ("num_players", "<u1"),
        ("_pad0", "V3"),
    ],
    align=False,
)


def _arr(dtype: str, n: int):
    return (dtype, (n,))


MAX_PLAYERS: Final[int] = 4
MAX_ITEMS: Final[int] = 15

INPUT_PLAYER_DTYPE = np.dtype(
    [
        ("buttons", "<u2"),
        ("main_x", "i1"),
        ("main_y", "i1"),
        ("c_x", "i1"),
        ("c_y", "i1"),
        ("l", "u1"),
        ("r", "u1"),
    ],
    align=False,
)

INPUT_DTYPE = np.dtype([("p", INPUT_PLAYER_DTYPE, (MAX_PLAYERS,))], align=False)

ITEM_DTYPE = np.dtype(
    [
        ("exists", "u1"),
        ("state", "u1"),
        ("type", "<u2"),
        ("owner", "i1"),
        ("_pad0", "V1"),
        ("instance_id", "<u2"),
        ("direction", "<f4"),
        ("vel_x", "<f4"),
        ("vel_y", "<f4"),
        ("pos_x", "<f4"),
        ("pos_y", "<f4"),
        ("damage", "<u2"),
        ("_pad1", "V2"),
        ("timer", "<f4"),
        ("spawn_id", "<u4"),
        ("misc0", "u1"),
        ("misc1", "u1"),
        ("misc2", "u1"),
        ("misc3", "u1"),
    ],
    align=False,
)

SEED_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("num_players", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V2"),
        ("team_id", _arr("u1", MAX_PLAYERS)),
        ("char_id", _arr("u1", MAX_PLAYERS)),
        ("pos_x", _arr("<f4", MAX_PLAYERS)),
        ("pos_y", _arr("<f4", MAX_PLAYERS)),
        ("pos_z", _arr("<f4", MAX_PLAYERS)),
        ("speed_air_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_ground_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_x_attack", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_attack", _arr("<f4", MAX_PLAYERS)),
        ("fighter_scale_y", _arr("<f4", MAX_PLAYERS)),
        ("facing", _arr("u1", MAX_PLAYERS)),
        ("on_ground", _arr("u1", MAX_PLAYERS)),
        ("_pad1", "V2"),
        ("action_id", _arr("<u2", MAX_PLAYERS)),
        ("action_frame", _arr("<i2", MAX_PLAYERS)),
        ("match_flow_timer", _arr("u1", MAX_PLAYERS)),
        ("anim_frame_f32", _arr("<f4", MAX_PLAYERS)),
        ("frame_speed_mul_f32", _arr("<f4", MAX_PLAYERS)),
        ("guard_tilt_x8", _arr("<u2", MAX_PLAYERS)),
        ("guard_tilt_x4", _arr("<f4", MAX_PLAYERS)),
        ("jumps_left", _arr("u1", MAX_PLAYERS)),
        ("stocks", _arr("u1", MAX_PLAYERS)),
        ("kneebend_jump_input", _arr("u1", MAX_PLAYERS)),
        ("kneebend_is_short_hop", _arr("u1", MAX_PLAYERS)),
        ("tilt_timer_x", _arr("u1", MAX_PLAYERS)),
        ("tilt_timer_y", _arr("u1", MAX_PLAYERS)),
        ("fall_fast", _arr("u1", MAX_PLAYERS)),
        ("turn_frames_to_turn", _arr("u1", MAX_PLAYERS)),
        ("turn_has_turned", _arr("u1", MAX_PLAYERS)),
        ("lr_press_timer", _arr("u1", MAX_PLAYERS)),
        ("x672_input_timer", _arr("u1", MAX_PLAYERS)),
        ("x673", _arr("u1", MAX_PLAYERS)),
        ("x674", _arr("u1", MAX_PLAYERS)),
        ("x675", _arr("u1", MAX_PLAYERS)),
        ("x676_x", _arr("u1", MAX_PLAYERS)),
        ("x677_y", _arr("u1", MAX_PLAYERS)),
        ("x678", _arr("u1", MAX_PLAYERS)),
        ("x679_x", _arr("u1", MAX_PLAYERS)),
        ("x67A_y", _arr("u1", MAX_PLAYERS)),
        ("x67B", _arr("u1", MAX_PLAYERS)),
        ("x67C", _arr("u1", MAX_PLAYERS)),
        ("x67D", _arr("u1", MAX_PLAYERS)),
        ("x67E", _arr("u1", MAX_PLAYERS)),
        ("x680", _arr("u1", MAX_PLAYERS)),
        ("x681", _arr("u1", MAX_PLAYERS)),
        ("x682", _arr("u1", MAX_PLAYERS)),
        ("x683", _arr("u1", MAX_PLAYERS)),
        ("x684", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_index", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_sdrop_up_frames", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_stick_x", ("i1", (MAX_PLAYERS, 4))),
        ("ucf_padbuf_stick_y", ("i1", (MAX_PLAYERS, 4))),
        ("percent", _arr("<f4", MAX_PLAYERS)),
        ("shield_hp", _arr("<f4", MAX_PLAYERS)),
        ("hitlag", _arr("<u2", MAX_PLAYERS)),
        ("hitstun", _arr("<u2", MAX_PLAYERS)),
        ("l_cancel", _arr("u1", MAX_PLAYERS)),
        ("hurtbox_state", _arr("u1", MAX_PLAYERS)),
        ("ground_id", _arr("<u2", MAX_PLAYERS)),
        ("animation_index", _arr("<u4", MAX_PLAYERS)),
        ("instance_hit_by", _arr("<u2", MAX_PLAYERS)),
        ("instance_id", _arr("<u2", MAX_PLAYERS)),
        ("last_attack_landed", _arr("u1", MAX_PLAYERS)),
        ("combo_count", _arr("u1", MAX_PLAYERS)),
        ("last_hit_by", _arr("u1", MAX_PLAYERS)),
        ("_pad2", "V1"),
        ("state_flags", ("u1", (MAX_PLAYERS, 5))),
        ("combat_rehit_active", ("u1", (MAX_PLAYERS, MAX_PLAYERS))),
        ("combat_rehit_hitbox_id", ("u1", (MAX_PLAYERS, MAX_PLAYERS))),
        ("combat_rehit_attacker_msid", ("<u2", (MAX_PLAYERS, MAX_PLAYERS))),
        ("combat_rehit_defender_instance_id", ("<u2", (MAX_PLAYERS, MAX_PLAYERS))),
        ("items", ITEM_DTYPE, (MAX_ITEMS,)),
    ],
    align=False,
)

COMPARE_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("num_players", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V2"),
        ("team_id", _arr("u1", MAX_PLAYERS)),
        ("char_id", _arr("u1", MAX_PLAYERS)),
        ("pos_x", _arr("<f4", MAX_PLAYERS)),
        ("pos_y", _arr("<f4", MAX_PLAYERS)),
        ("speed_air_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_ground_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_x_attack", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_attack", _arr("<f4", MAX_PLAYERS)),
        ("facing", _arr("u1", MAX_PLAYERS)),
        ("on_ground", _arr("u1", MAX_PLAYERS)),
        ("is_dead", _arr("u1", MAX_PLAYERS)),
        ("_pad1", "V1"),
        ("action_id", _arr("<u2", MAX_PLAYERS)),
        ("action_frame", _arr("<i2", MAX_PLAYERS)),
        ("jumps_left", _arr("u1", MAX_PLAYERS)),
        ("stocks", _arr("u1", MAX_PLAYERS)),
        ("percent", _arr("<f4", MAX_PLAYERS)),
        ("shield_hp", _arr("<f4", MAX_PLAYERS)),
        ("hitlag", _arr("<u2", MAX_PLAYERS)),
        ("hitstun", _arr("<u2", MAX_PLAYERS)),
        ("l_cancel", _arr("u1", MAX_PLAYERS)),
        ("hurtbox_state", _arr("u1", MAX_PLAYERS)),
        ("ground_id", _arr("<u2", MAX_PLAYERS)),
        ("animation_index", _arr("<u4", MAX_PLAYERS)),
        ("instance_hit_by", _arr("<u2", MAX_PLAYERS)),
        ("instance_id", _arr("<u2", MAX_PLAYERS)),
        ("last_attack_landed", _arr("u1", MAX_PLAYERS)),
        ("combo_count", _arr("u1", MAX_PLAYERS)),
        ("last_hit_by", _arr("u1", MAX_PLAYERS)),
        ("_pad2", "V1"),
        ("state_flags", ("u1", (MAX_PLAYERS, 5))),
        ("items", ITEM_DTYPE, (MAX_ITEMS,)),
    ],
    align=False,
)

SAMPLE_DTYPE = np.dtype(
    [
        ("seed_t", SEED_DTYPE),
        ("prev_input_t", INPUT_DTYPE),
        ("input_t", INPUT_DTYPE),
        ("ref_t1", COMPARE_DTYPE),
    ],
    align=False,
)


@dataclasses.dataclass(frozen=True)
class Dataset:
    header: np.ndarray
    samples: np.ndarray


def write_dataset(path: str, num_players: int, samples: np.ndarray) -> None:
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")
    if samples.dtype != SAMPLE_DTYPE:
        raise ValueError(f"samples dtype mismatch: got {samples.dtype}, want {SAMPLE_DTYPE}")

    header = np.zeros((), dtype=HEADER_DTYPE)
    header["magic"] = MAGIC
    header["record_size"] = samples.dtype.itemsize
    header["num_records"] = samples.shape[0]
    header["num_players"] = num_players

    with open(path, "wb") as f:
        f.write(header.tobytes(order="C"))
        f.write(samples.tobytes(order="C"))


def read_dataset(path: str) -> Dataset:
    with open(path, "rb") as f:
        header_bytes = f.read(HEADER_DTYPE.itemsize)
        if len(header_bytes) != HEADER_DTYPE.itemsize:
            raise ValueError("file too small for header")
        header = np.frombuffer(header_bytes, dtype=HEADER_DTYPE, count=1)[0]
        if bytes(header["magic"]) != MAGIC:
            raise ValueError(f"bad magic: {header['magic']!r}")

        record_size = int(header["record_size"])
        if record_size != SAMPLE_DTYPE.itemsize:
            raise ValueError(
                f"record_size mismatch: file={record_size} dtype={SAMPLE_DTYPE.itemsize}"
            )
        num_records = int(header["num_records"])
        samples_bytes = f.read(record_size * num_records)
        if len(samples_bytes) != record_size * num_records:
            raise ValueError("file truncated")
        samples = np.frombuffer(samples_bytes, dtype=SAMPLE_DTYPE, count=num_records)

    return Dataset(header=header, samples=samples)
