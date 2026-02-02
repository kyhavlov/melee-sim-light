from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_LANDING = 0x002A
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_LANDING_AIR_N = 0x0046
ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_REFLECT = 0x00B6

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_LANDING = 35
SM_LANDING_FALL_SPECIAL = 36
SM_LANDING_AIR_N = 73

CHAR_FOX = 1
STAGE_FD = 32


def _fox_attr(name: str) -> float:
    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox[name])


def _common_attr(name: str) -> float:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])

def _tracks_end_frame(path: Path, msid: int) -> float:
    import struct

    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version not in (1, 2):
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        for _ in range(anim_count):
            (mid,) = struct.unpack("<H", f.read(2))
            (end_frame,) = struct.unpack("<f", f.read(4))
            if version >= 2:
                f.read(1)  # aobj_loop
            for _lp in range(local_count):
                part_u8 = f.read(1)
                if not part_u8:
                    raise ValueError("unexpected EOF in tracks parts")
                (n_tracks,) = struct.unpack("<B", f.read(1))
                for _t in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError("unexpected EOF in tracks header")
                    (_obj_type, _frac_value, _frac_slope, _pad, _startframe, length) = struct.unpack("<BBBBHH", hdr)
                    f.read(int(length))
            if int(mid) == int(msid):
                return float(end_frame)

    raise KeyError(f"msid {msid} not found in {path}")


def _scaled_anim_inc(end_frame: float, lag_frames: float) -> int:
    # Decomp:
    # - LandingAir: (ftAnim_8006F484(gobj) + 0.1f) / lag
    # - LandingFallSpecial: (0.1f + fp->x2EC) / landing_lag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c and ftCo_Landing.c
    inc = int((float(end_frame) + 0.1) / float(lag_frames))
    return max(1, inc)


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = seed["action_frame"][0, :2].astype(np.float32)

    # Default P2 to a stable grounded idle.
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


def _step_once(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def test_landing_air_n_exits_to_wait_after_lag_frames() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    tracks_path = Path("data/anims/fox.tracks.bin")
    end_frame = _tracks_end_frame(tracks_path, SM_LANDING_AIR_N)
    lag = int(_fox_attr("landing_airn_lag_frames"))
    assert end_frame > 0.0 and lag > 0
    rate = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_LANDING_AIR_N)
    seed["frame_speed_mul_f32"][0, 0] = rate
    seed["anim_frame_f32"][0, 0] = np.float32(np.float32(end_frame) - rate)
    seed["action_frame"][0, 0] = np.int16(int(math.floor(float(seed["anim_frame_f32"][0, 0]))))
    seed["animation_index"][0, 0] = np.uint32(SM_LANDING_AIR_N)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["animation_index"][0]) == SM_WAIT1_0
    assert int(out["action_frame"][0]) == 0


def test_landing_fall_special_exits_to_wait_after_lag_frames() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    tracks_path = Path("data/anims/fox.tracks.bin")
    end_frame = _tracks_end_frame(tracks_path, SM_LANDING_FALL_SPECIAL)
    lag = float(_common_attr("landing_fall_special_lag_frames"))
    assert end_frame > 0.0 and lag > 0.0
    rate = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_LANDING_FALL_SPECIAL)
    seed["frame_speed_mul_f32"][0, 0] = rate
    seed["anim_frame_f32"][0, 0] = np.float32(np.float32(end_frame) - rate)
    seed["action_frame"][0, 0] = np.int16(int(math.floor(float(seed["anim_frame_f32"][0, 0]))))
    seed["animation_index"][0, 0] = np.uint32(SM_LANDING_FALL_SPECIAL)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["animation_index"][0]) == SM_WAIT1_0
    assert int(out["action_frame"][0]) == 0


def test_landing_iasa_allows_shield_entry_after_landing_lag_gate() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    lag = int(_fox_attr("landing_lag_frames"))
    assert lag > 0

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_LANDING)
    seed["action_frame"][0, 0] = np.int16(lag)
    seed["anim_frame_f32"][0, 0] = np.float32(lag)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_LANDING)
    seed["shield_hp"][0, 0] = np.float32(_common_attr("start_shield_health"))

    hold_l_prev = _mk_input_bytes(1, input_stride)
    hold_l = _mk_input_bytes(1, input_stride)

    hold_l_prev_view = hold_l_prev.view(INPUT_DTYPE).reshape((1,))
    hold_l_prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    hold_l_view = hold_l.view(INPUT_DTYPE).reshape((1,))
    hold_l_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    out = _step_once(seed, hold_l_prev, hold_l)
    assert int(out["action_id"][0]) in (ACT_GUARD_REFLECT, ACT_GUARD_ON, ACT_GUARD)
    assert int(out["animation_index"][0]) == 0xFFFFFFFF
