from __future__ import annotations

import ctypes
import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.modelplay.sim_env import RL_OBS_DTYPE
from tools.modelplay.state_adapter import INPUT_DTYPE, _controller_from_input_player, empty_controller


MELEE_INPUT_HISTORY = 12
MELEE_OBS_SIZE = 133


def _melee_clip(x: float, lo: float, hi: float) -> float:
    return min(max(x, lo), hi)


def _melee_safe_div(x: float, denom: float) -> float:
    return 0.0 if denom == 0.0 else x / denom


def _melee_raw_axis(x: float) -> int:
    v = int(round(x))
    return min(max(v, -80), 80)


def _decode_polar_label(label: int, angle_counts: tuple[int, ...]) -> tuple[int, int]:
    if label == 0:
        return 0, 0

    radius_bucket = 0
    inner = label
    offset = 1
    for r, n in enumerate(angle_counts):
        if label < offset + n:
            radius_bucket = r + 1
            inner = label - offset
            break
        offset += n

    min_log_radius = np.log(23.0)
    max_log_radius = np.log(80.0)
    if len(angle_counts) <= 1:
        radius = 23.0
    else:
        t = float(radius_bucket - 1) / float(len(angle_counts) - 1)
        radius = float(np.exp(min_log_radius + t * (max_log_radius - min_log_radius)))
    n_angles = angle_counts[radius_bucket - 1]
    angle = (float(inner) / float(n_angles)) * 2.0 * np.pi - np.pi
    return _melee_raw_axis(radius * np.cos(angle)), _melee_raw_axis(radius * np.sin(angle))


def _decode_action_to_input_player(button_label: int, main_label: int) -> np.ndarray:
    out = np.zeros((), dtype=INPUT_DTYPE["p"].base)

    tmp = int(button_label)
    c_stick = tmp % 13
    tmp //= 13
    z_a_shoulder = tmp % 7
    tmp //= 7
    lr = tmp % 2
    tmp //= 2
    xy = tmp % 2
    tmp //= 2
    b = tmp % 2

    buttons = 0
    if b:
        buttons |= 0x0200
    if xy:
        buttons |= 0x0800
    if lr:
        buttons |= 0x0040

    shoulder = 0.0
    if z_a_shoulder == 6:
        buttons |= 0x0010
        buttons |= 0x0100
        shoulder = 0.35
    else:
        shoulder_label = z_a_shoulder % 3
        a = z_a_shoulder // 3
        if a:
            buttons |= 0x0100
        if shoulder_label == 1:
            shoulder = 0.35
        elif shoulder_label == 2:
            shoulder = 1.0

    out["buttons"] = np.uint16(buttons)
    out["l"] = np.uint8(_melee_clip(round(shoulder * 255.0), 0.0, 255.0))
    out["r"] = np.uint8(0)
    out["c_x"], out["c_y"] = _decode_polar_label(c_stick, (4, 8))
    out["main_x"], out["main_y"] = _decode_polar_label(int(main_label), (4, 16, 64))
    return out


def _pack_slot(out: np.ndarray, slot: np.void) -> None:
    c = 0
    out[c] = float(slot["present"]); c += 1
    out[c] = 1.0 if int(slot["team_relation"]) == 0 else 0.0; c += 1
    out[c] = 1.0 if int(slot["team_relation"]) == 1 else 0.0; c += 1
    out[c] = 1.0 if int(slot["team_relation"]) == 2 else 0.0; c += 1
    out[c] = _melee_safe_div(float(slot["source_player"]), 3.0); c += 1
    out[c] = _melee_safe_div(float(slot["team_id"]), 3.0); c += 1
    out[c] = _melee_clip(float(slot["pos_x"]) / 100.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["pos_y"]) / 100.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["speed_air_x_self"]) / 10.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["speed_ground_x_self"]) / 10.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["speed_y_self"]) / 10.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["speed_x_attack"]) / 10.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["speed_y_attack"]) / 10.0, -4.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["percent"]) / 100.0, 0.0, 10.0); c += 1
    out[c] = _melee_clip(float(slot["shield_hp"]) / 60.0, 0.0, 2.0); c += 1
    out[c] = _melee_safe_div(float(slot["action_id"]), 400.0); c += 1
    out[c] = _melee_clip(float(slot["action_frame"]) / 60.0, -1.0, 10.0); c += 1
    out[c] = _melee_clip(float(slot["hitlag"]) / 60.0, 0.0, 4.0); c += 1
    out[c] = _melee_clip(float(slot["hitstun"]) / 120.0, 0.0, 10.0); c += 1
    out[c] = _melee_safe_div(float(slot["char_id"]), 32.0); c += 1
    out[c] = _melee_safe_div(float(slot["stocks"]), 4.0); c += 1
    out[c] = 1.0 if int(slot["facing"]) else -1.0; c += 1
    out[c] = 1.0 if int(slot["on_ground"]) else 0.0; c += 1
    out[c] = _melee_safe_div(float(slot["jumps_left"]), 6.0); c += 1
    out[c] = _melee_safe_div(float(slot["hurtbox_state"]), 2.0); c += 1
    out[c] = 1.0


def pack_observation(
    rl_obs: np.void,
    button_hist: np.ndarray,
    main_hist: np.ndarray,
    history_count: int,
    history_cursor: int,
) -> np.ndarray:
    out = np.zeros(MELEE_OBS_SIZE, dtype=np.float32)
    out[0] = _melee_safe_div(float(int(rl_obs["frame_id"]) % 3600), 3600.0)
    out[1] = _melee_safe_div(float(rl_obs["viewpoint_player"]), 3.0)
    out[2] = float(rl_obs["is_teams"])
    out[3] = 1.0

    ptr = 4
    for slot in rl_obs["slots"]:
        if int(slot["present"]):
            _pack_slot(out[ptr : ptr + 26], slot)
        ptr += 26

    out[108] = 0.0
    start = MELEE_INPUT_HISTORY - history_count
    oldest = history_cursor - history_count
    if oldest < 0:
        oldest += MELEE_INPUT_HISTORY
    for i in range(history_count):
        idx = (oldest + i) % MELEE_INPUT_HISTORY
        out[109 + 2 * (start + i)] = _melee_safe_div(float(button_hist[idx]), 415.0)
        out[109 + 2 * (start + i) + 1] = _melee_safe_div(float(main_hist[idx]), 84.0)
    return out


def _build_lib(ssbm_puffer_root: Path) -> Path:
    src = Path(__file__).with_name("puffer_infer.c")
    out = Path(__file__).with_name("_puffer_infer.so")
    header = ssbm_puffer_root / "pufferlib" / "src" / "puffernet.h"
    if out.exists() and out.stat().st_mtime >= max(src.stat().st_mtime, header.stat().st_mtime):
        return out
    cmd = [
        "cc",
        "-O3",
        "-shared",
        "-fPIC",
        str(src),
        "-o",
        str(out),
        "-I",
        str(ssbm_puffer_root / "pufferlib" / "src"),
        "-lm",
    ]
    subprocess.run(cmd, check=True)
    return out


@dataclass
class RandomAgent:
    rng: np.random.Generator

    def step(self, *, needs_reset: bool):
        if needs_reset:
            return empty_controller()
        button = int(self.rng.integers(0, 416))
        main = int(self.rng.integers(0, 85))
        return _controller_from_input_player(_decode_action_to_input_player(button, main))


@dataclass
class IdleAgent:
    def step(self, *, needs_reset: bool):
        del needs_reset
        return empty_controller()


class PufferAgent:
    def __init__(
        self,
        *,
        ssbm_puffer_root: Path,
        checkpoint: Path,
        hidden_size: int = 256,
        num_layers: int = 4,
        stochastic: bool = True,
        seed: int = 0,
    ):
        lib_path = _build_lib(ssbm_puffer_root)
        self._lib = ctypes.CDLL(str(lib_path))
        self._lib.puffer_infer_create.restype = ctypes.c_void_p
        self._lib.puffer_infer_create.argtypes = [
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint32,
        ]
        self._lib.puffer_infer_reset.argtypes = [ctypes.c_void_p]
        self._lib.puffer_infer_step.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
        ]
        self._lib.puffer_infer_destroy.argtypes = [ctypes.c_void_p]
        self._handle = self._lib.puffer_infer_create(
            str(checkpoint).encode(),
            MELEE_OBS_SIZE,
            hidden_size,
            num_layers,
            1 if stochastic else 0,
            np.uint32(seed),
        )
        if not self._handle:
            raise RuntimeError(f"failed to create Puffer inference agent for {checkpoint}")
        self._button_hist = np.zeros(MELEE_INPUT_HISTORY, dtype=np.uint16)
        self._main_hist = np.zeros(MELEE_INPUT_HISTORY, dtype=np.uint8)
        self._cursor = 0
        self._count = 0

    def close(self) -> None:
        if self._handle:
            self._lib.puffer_infer_destroy(self._handle)
            self._handle = None

    def reset(self) -> None:
        self._lib.puffer_infer_reset(self._handle)
        self._button_hist.fill(0)
        self._main_hist.fill(0)
        self._cursor = 0
        self._count = 0

    def _record(self, button_label: int, main_label: int) -> None:
        self._button_hist[self._cursor] = np.uint16(button_label)
        self._main_hist[self._cursor] = np.uint8(main_label)
        self._cursor = (self._cursor + 1) % MELEE_INPUT_HISTORY
        if self._count < MELEE_INPUT_HISTORY:
            self._count += 1

    def step(self, rl_obs: np.void, *, needs_reset: bool):
        if needs_reset:
            self.reset()
        obs = pack_observation(
            rl_obs,
            self._button_hist,
            self._main_hist,
            self._count,
            self._cursor,
        )
        obs_ptr = obs.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        button = ctypes.c_int()
        main = ctypes.c_int()
        err = self._lib.puffer_infer_step(self._handle, obs_ptr, ctypes.byref(button), ctypes.byref(main))
        if err != 0:
            raise RuntimeError(f"puffer_infer_step failed: {err}")
        self._record(button.value, main.value)
        return _controller_from_input_player(_decode_action_to_input_player(button.value, main.value))
