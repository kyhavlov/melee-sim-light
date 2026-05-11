from __future__ import annotations

from functools import lru_cache

import numpy as np

from . import _native

MAX_PLAYERS = 4


@lru_cache(maxsize=1)
def sizes() -> dict[str, int]:
    return dict(_native.sizes())


def raw_buffer(batch_size: int, kind: str) -> np.ndarray:
    return np.zeros((int(batch_size), sizes()[kind]), dtype=np.uint8)


def raw_sequence_buffer(length: int, batch_size: int, kind: str, *, extra_frames: int = 0) -> np.ndarray:
    return np.zeros((int(length) + int(extra_frames), int(batch_size), sizes()[kind]), dtype=np.uint8)


@lru_cache(maxsize=1)
def controller_buttons_dtype() -> np.dtype:
    return np.dtype(
        [
            ("A", "u1"),
            ("B", "u1"),
            ("X", "u1"),
            ("Y", "u1"),
            ("Z", "u1"),
            ("L", "u1"),
            ("R", "u1"),
            ("D_UP", "u1"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def controller_player_dtype() -> np.dtype:
    return np.dtype(
        [
            ("buttons", controller_buttons_dtype()),
            ("main_stick_x", "<f4"),
            ("main_stick_y", "<f4"),
            ("c_stick_x", "<f4"),
            ("c_stick_y", "<f4"),
            ("shoulder", "<f4"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def controller_input_dtype() -> np.dtype:
    return np.dtype({"names": ["p"], "formats": [(controller_player_dtype(), (MAX_PLAYERS,))]})


@lru_cache(maxsize=1)
def input_player_dtype() -> np.dtype:
    return np.dtype(
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


@lru_cache(maxsize=1)
def input_dtype() -> np.dtype:
    return np.dtype({"names": ["p"], "formats": [(input_player_dtype(), (MAX_PLAYERS,))]})


@lru_cache(maxsize=1)
def match_player_config_dtype() -> np.dtype:
    return np.dtype(
        [
            ("char_id", "u1"),
            ("team_id", "u1"),
            ("facing", "u1"),
            ("_pad0", "u1"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def match_config_dtype() -> np.dtype:
    return np.dtype(
        [
            ("stage_id", "<u4"),
            ("frame_id", "<i4"),
            ("frame_pre_random_seed", "<u4"),
            ("match_damage_ratio", "<f4"),
            ("num_players", "u1"),
            ("is_teams", "u1"),
            ("stock_count", "u1"),
            ("camera_mode", "u1"),
            ("players", match_player_config_dtype(), (MAX_PLAYERS,)),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def item_dtype() -> np.dtype:
    return np.dtype(
        [
            ("exists", "u1"),
            ("state", "u1"),
            ("type", "<u2"),
            ("owner", "i1"),
            ("_pad0", "u1"),
            ("instance_id", "<u2"),
            ("attack_id", "<u2"),
            ("attack_instance", "<u2"),
            ("direction", "<f4"),
            ("vel_x", "<f4"),
            ("vel_y", "<f4"),
            ("pos_x", "<f4"),
            ("pos_y", "<f4"),
            ("damage", "<u2"),
            ("_pad1", "<u2"),
            ("timer", "<f4"),
            ("spawn_id", "<u4"),
            ("misc0", "u1"),
            ("misc1", "u1"),
            ("misc2", "u1"),
            ("misc3", "u1"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def gamestate_player_dtype() -> np.dtype:
    return np.dtype(
        [
            ("present", "u1"),
            ("source_player", "u1"),
            ("team_relation", "u1"),
            ("team_id", "u1"),
            ("pos_x", "<f4"),
            ("pos_y", "<f4"),
            ("speed_air_x_self", "<f4"),
            ("speed_ground_x_self", "<f4"),
            ("speed_y_self", "<f4"),
            ("speed_x_attack", "<f4"),
            ("speed_y_attack", "<f4"),
            ("percent", "<f4"),
            ("shield_hp", "<f4"),
            ("action_id", "<u2"),
            ("action_frame", "<i2"),
            ("hitlag", "<u2"),
            ("hitstun", "<u2"),
            ("char_id", "u1"),
            ("stocks", "u1"),
            ("facing", "u1"),
            ("on_ground", "u1"),
            ("jumps_left", "u1"),
            ("hurtbox_state", "u1"),
            ("invulnerable", "u1"),
            ("_pad0", "u1"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def gamestate_randall_dtype() -> np.dtype:
    return np.dtype(
        [
            ("exists", "u1"),
            ("_pad0", "u1", (3,)),
            ("x", "<f4"),
            ("y", "<f4"),
        ],
        align=False,
    )


@lru_cache(maxsize=1)
def gamestate_stage_dtype() -> np.dtype:
    return np.dtype([("randall", gamestate_randall_dtype())], align=False)


@lru_cache(maxsize=1)
def compare_dtype() -> np.dtype:
    return np.dtype(
        [
            ("frame_id", "<i4"),
            ("frame_pre_random_seed", "<u4"),
            ("stage_id", "<u4"),
            ("num_players", "u1"),
            ("is_teams", "u1"),
            ("_pad0", "u1", (2,)),
            ("team_id", "u1", (MAX_PLAYERS,)),
            ("char_id", "u1", (MAX_PLAYERS,)),
            ("pos_x", "<f4", (MAX_PLAYERS,)),
            ("pos_y", "<f4", (MAX_PLAYERS,)),
            ("speed_air_x_self", "<f4", (MAX_PLAYERS,)),
            ("speed_ground_x_self", "<f4", (MAX_PLAYERS,)),
            ("speed_y_self", "<f4", (MAX_PLAYERS,)),
            ("speed_x_attack", "<f4", (MAX_PLAYERS,)),
            ("speed_y_attack", "<f4", (MAX_PLAYERS,)),
            ("facing", "u1", (MAX_PLAYERS,)),
            ("on_ground", "u1", (MAX_PLAYERS,)),
            ("is_dead", "u1", (MAX_PLAYERS,)),
            ("_pad1", "u1"),
            ("action_id", "<u2", (MAX_PLAYERS,)),
            ("action_frame", "<i2", (MAX_PLAYERS,)),
            ("jumps_left", "u1", (MAX_PLAYERS,)),
            ("stocks", "u1", (MAX_PLAYERS,)),
            ("percent", "<f4", (MAX_PLAYERS,)),
            ("shield_hp", "<f4", (MAX_PLAYERS,)),
            ("hitlag", "<u2", (MAX_PLAYERS,)),
            ("hitstun", "<u2", (MAX_PLAYERS,)),
            ("l_cancel", "u1", (MAX_PLAYERS,)),
            ("hurtbox_state", "u1", (MAX_PLAYERS,)),
            ("ground_id", "<u2", (MAX_PLAYERS,)),
            ("animation_index", "<u4", (MAX_PLAYERS,)),
            ("instance_hit_by", "<u2", (MAX_PLAYERS,)),
            ("instance_id", "<u2", (MAX_PLAYERS,)),
            ("last_attack_landed", "u1", (MAX_PLAYERS,)),
            ("combo_count", "u1", (MAX_PLAYERS,)),
            ("last_hit_by", "u1", (MAX_PLAYERS,)),
            ("_pad2", "u1"),
            ("state_flags", "u1", (MAX_PLAYERS, 5)),
            ("items", item_dtype(), (15,)),
        ],
        align=False,
    )


def _opaque_dtype(kind: str) -> np.dtype:
    return np.dtype((np.void, sizes()[kind]))


def seed_dtype() -> np.dtype:
    return _opaque_dtype("seed")


@lru_cache(maxsize=1)
def gamestate_dtype() -> np.dtype:
    dtype = np.dtype(
        [
            ("frame_id", "<i4"),
            ("frame_pre_random_seed", "<u4"),
            ("stage_id", "<u4"),
            ("num_players", "u1"),
            ("viewpoint_player", "u1"),
            ("is_teams", "u1"),
            ("_pad0", "u1"),
            ("stage", gamestate_stage_dtype()),
            ("slots", gamestate_player_dtype(), (MAX_PLAYERS,)),
            ("items", item_dtype(), (15,)),
        ],
        align=False,
    )
    if dtype.itemsize != sizes()["gamestate"]:
        raise RuntimeError(
            f"gamestate dtype is {dtype.itemsize} bytes, "
            f"native struct is {sizes()['gamestate']} bytes"
        )
    return dtype


def terminal_dtype() -> np.dtype:
    return _opaque_dtype("terminal")


def view_raw(raw: np.ndarray, dtype: np.dtype) -> np.ndarray:
    if raw.dtype != np.uint8 or raw.ndim != 2:
        raise ValueError("raw buffer must be uint8[batch, bytes]")
    if raw.shape[1] < dtype.itemsize:
        raise ValueError(f"raw buffer row has {raw.shape[1]} bytes, dtype needs {dtype.itemsize}")
    return raw[:, : dtype.itemsize].view(dtype).reshape(raw.shape[0])


def view_raw_sequence(raw: np.ndarray, dtype: np.dtype) -> np.ndarray:
    if raw.dtype != np.uint8 or raw.ndim != 3:
        raise ValueError("raw sequence buffer must be uint8[length, batch, bytes]")
    if raw.shape[2] < dtype.itemsize:
        raise ValueError(f"raw sequence row has {raw.shape[2]} bytes, dtype needs {dtype.itemsize}")
    return raw[:, :, : dtype.itemsize].view(dtype).reshape(raw.shape[0], raw.shape[1])
