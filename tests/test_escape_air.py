from __future__ import annotations

import math
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_FALL_SPECIAL = 0x0023
ACT_ESCAPE_AIR = 0x00EC

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_FALL = 20
SM_FALL_SPECIAL = 26
SM_ESCAPE_AIR = 44

CHAR_FOX = 1
STAGE_FD = 32


def _fox_attr(name: str) -> float:
    import json

    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox[name])


def _common_attr(name: str) -> float:
    import json

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _tracks_end_frame(path: Path, msid: int) -> float:
    import struct

    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version != 1:
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        for _ in range(anim_count):
            (mid,) = struct.unpack("<H", f.read(2))
            (end_frame,) = struct.unpack("<f", f.read(4))
            for _lp in range(local_count):
                part_u8 = f.read(1)
                if not part_u8:
                    raise ValueError("unexpected EOF in tracks parts")
                (n_tracks,) = struct.unpack("<B", f.read(1))
                for _t in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError("unexpected EOF in tracks header")
                    (_obj_type, _frac_value, _frac_slope, _pad, _startframe, length) = struct.unpack(
                        "<BBBBHH", hdr
                    )
                    f.read(int(length))
            if int(mid) == int(msid):
                return float(end_frame)

    raise KeyError(f"msid {msid} not found in {path}")


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_air_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(10.0)  # airborne
    seed["pos_y"][0, 1] = np.float32(0.0)  # grounded
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)

    # Default P2 to a stable grounded idle.
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
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
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_air_locomotion_lr_press_enters_escape_air_next_step() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["animation_index"][0]) == SM_ESCAPE_AIR


def test_escape_air_entry_velocity_within_deadzone_is_zero() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    dzx = float(_common_attr("escapeair_deadzone_x"))
    dzy = float(_common_attr("escapeair_deadzone_y"))
    assert dzx > 0.0 and dzy > 0.0

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["speed_air_x_self"][0, 0] = np.float32(1.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    # Choose small raw stick values within the EscapeAir-specific deadzone.
    # PAD stick values are legalized to [-80, 80], so unit = v/80.
    inp_view["p"]["main_x"][0, 0] = np.int8(int(dzx * 80 * 0.5))
    inp_view["p"]["main_y"][0, 0] = np.int8(int(dzy * 80 * 0.5))

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert float(out["speed_air_x_self"][0]) == 0.0
    assert float(out["speed_y_self"][0]) == 0.0


def test_escape_air_deadzone_band_between_escapeair_and_global_deadzone_is_zero_velocity() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    escape_dz = float(_common_attr("escapeair_deadzone_x"))
    global_dz = float(_common_attr("lstick_deadzone_x"))
    assert escape_dz > 0.0 and global_dz > 0.0 and global_dz > escape_dz

    # Pick a stick magnitude strictly between EscapeAir deadzone and global stick deadzone.
    band = (escape_dz + global_dz) * 0.5

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    inp_view["p"]["main_x"][0, 0] = np.int8(int(round(band * 80)))
    inp_view["p"]["main_y"][0, 0] = np.int8(0)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert float(out["speed_air_x_self"][0]) == 0.0
    assert float(out["speed_y_self"][0]) == 0.0


def test_escape_air_entry_velocity_outside_deadzone_uses_force_and_stick_angle() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    force = float(_common_attr("escapeair_force"))
    decay = float(_common_attr("escapeair_decay"))
    dzx = float(_common_attr("escapeair_deadzone_x"))
    dzy = float(_common_attr("escapeair_deadzone_y"))
    assert force > 0.0 and 0.0 < decay < 1.0 and dzx > 0.0 and dzy > 0.0

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    # Use a diagonal stick outside the EscapeAir deadzone.
    # 0.5 is comfortably > 0.25, so it should take the angled-force path.
    raw_x = 0.5
    raw_y = 0.5
    assert abs(raw_x) > dzx and abs(raw_y) > dzy
    inp_view["p"]["main_x"][0, 0] = np.int8(int(round(raw_x * 80)))
    inp_view["p"]["main_y"][0, 0] = np.int8(int(round(raw_y * 80)))

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR

    ang = math.atan2(raw_y, raw_x)
    exp_vx = (force * decay) * math.cos(ang)
    exp_vy = (force * decay) * math.sin(ang)
    got_vx = float(out["speed_air_x_self"][0])
    got_vy = float(out["speed_y_self"][0])
    assert math.isclose(got_vx, exp_vx, rel_tol=0.0, abs_tol=1e-5)
    assert math.isclose(got_vy, exp_vy, rel_tol=0.0, abs_tol=1e-5)
    assert math.isclose(math.hypot(got_vx, got_vy), force * decay, rel_tol=0.0, abs_tol=1e-5)


def test_escape_air_velocity_decays_each_frame_with_escapeair_decay() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    decay = float(_common_attr("escapeair_decay"))
    assert 0.0 < decay < 1.0

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ESCAPE_AIR)
    seed["speed_air_x_self"][0, 0] = np.float32(1.25)
    seed["speed_y_self"][0, 0] = np.float32(-2.5)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert math.isclose(float(out["speed_air_x_self"][0]), 1.25 * decay, rel_tol=0.0, abs_tol=1e-6)
    assert math.isclose(float(out["speed_y_self"][0]), -2.5 * decay, rel_tol=0.0, abs_tol=1e-6)


def test_escape_air_anim_end_transitions_to_fall_special_not_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    tracks_path = Path("data/anims/fox.tracks.bin")
    end_frame = _tracks_end_frame(tracks_path, SM_ESCAPE_AIR)
    assert end_frame > 0.0

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    # locomotion_update_pre advances action_frame by +1 before anim-end gates.
    seed["action_frame"][0, 0] = np.int16(int(math.ceil(end_frame)) - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(int(math.ceil(end_frame)) - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ESCAPE_AIR)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL
    assert int(out["animation_index"][0]) == SM_FALL_SPECIAL
    assert int(out["action_frame"][0]) == 0
    assert int(out["action_id"][0]) != ACT_FALL


def test_fall_special_drift_cap_reduces_horizontal_speed_vs_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    mobility_scalar = float(_common_attr("fall_special_mobility_scalar"))
    assert 0.0 < mobility_scalar < 1.0

    air_drift_max = float(_fox_attr("air_drift_max"))
    assert air_drift_max > 0.0
    terminal_vel = float(_fox_attr("terminal_vel"))
    assert terminal_vel > 0.0

    mobility = air_drift_max * mobility_scalar
    assert mobility > 0.0

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["main_x"][0, 0] = np.int8(80)  # full right

    seed_fall = _seed_air_base()
    seed_fall["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed_fall["action_frame"][0, 0] = np.int16(0)
    seed_fall["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed_fall["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed_fall["animation_index"][0, 0] = np.uint32(SM_FALL)
    # Start between FallSpecial mobility and normal target so the cap affects accel direction.
    seed_fall["speed_air_x_self"][0, 0] = np.float32((air_drift_max + mobility) * 0.5)

    seed_special = _seed_air_base()
    seed_special["action_id"][0, 0] = np.uint16(ACT_FALL_SPECIAL)
    seed_special["action_frame"][0, 0] = np.int16(0)
    seed_special["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed_special["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed_special["animation_index"][0, 0] = np.uint32(SM_FALL_SPECIAL)
    seed_special["speed_air_x_self"][0, 0] = np.float32((air_drift_max + mobility) * 0.5)
    # Trigger xC==0 derivation in core reseed: fall_fast==0 and vy < -terminal_vel.
    seed_special["fall_fast"][0, 0] = np.uint8(0)
    seed_special["speed_y_self"][0, 0] = np.float32(-(terminal_vel + terminal_vel * 0.1))

    out_fall = _step_once(seed_fall, prev_inp, inp)
    out_special = _step_once(seed_special, prev_inp, inp)

    vx_fall = float(out_fall["speed_air_x_self"][0])
    vx_special = float(out_special["speed_air_x_self"][0])
    assert vx_special < vx_fall
