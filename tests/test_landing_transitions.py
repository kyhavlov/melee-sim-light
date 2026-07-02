from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_ATTACK_AIR_N = 0x0041
ACT_LANDING = 0x002A
ACT_ESCAPE_AIR = 0x00EC
ACT_LANDING_AIR_N = 0x0046
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_CLIFF_JUMP_QUICK2 = 0x0107

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_FALL = 20
SM_LANDING = 35
SM_LANDING_AIR_N = 73
SM_LANDING_FALL_SPECIAL = 36
SM_CLIFF_JUMP_QUICK2 = 228

CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4


_ECB_LOADED = False


def _ensure_ecb_loaded() -> None:
    global _ECB_LOADED
    if _ECB_LOADED:
        return
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    msl_binding.destroy(handle)
    _ECB_LOADED = True


def _fox_ecb_bottom_rel_y(msid: int, action_frame: int) -> float:
    _ensure_ecb_loaded()
    import msl_binding

    return float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, int(msid), int(action_frame)))


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
        if version != 3:
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        for _ in range(anim_count):
            (mid,) = struct.unpack("<H", f.read(2))
            (end_frame,) = struct.unpack("<f", f.read(4))
            f.read(1)  # aobj_loop
            f.read(1)  # uses_root_motion
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


def _fox_attackair_cmd0_on_frame(move_key: str) -> int:
    moves = json.loads(Path("data/moves/fox.json").read_text())
    evs = moves["moves"][move_key]["events"]
    for ev in evs:
        if ev.get("kind") != "set_cmd_var":
            continue
        data = ev.get("data", {})
        if int(data.get("idx", -1)) != 0:
            continue
        if int(data.get("value", 0)) != 1:
            continue
        return int(ev["frame"])
    raise AssertionError(f"missing cmd_var[0] enable event in {move_key}")


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
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = seed["action_frame"][0, :2].astype(np.float32)
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


def test_attack_air_n_lands_enters_landing_air_n_and_refreshes_jumps() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    # Land during the cmd_var[0] "landing lag enabled" window so we enter LandingAirN (not auto-cancel Landing).
    cmd0_on = _fox_attackair_cmd0_on_frame("ftCo_SM_AttackAirN")
    seed["action_frame"][0, 0] = np.int16(max(0, cmd0_on - 1))
    seed["anim_frame_f32"][0, 0] = np.float32(seed["action_frame"][0, 0])
    # Use a stable ECB pose for grounding/ECB evaluation; landing selection in this sim is keyed off action_id
    # (not animation_index) on the collision->grounding transition.
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["ground_id"][0, 0] = np.uint16(1)  # prefer main FD floor segment

    # Ensure we cross the FD floor in one frame under ECB-bottom grounding, accounting for the
    # sim's per-frame ordering (gravity/terminal clamp before integration).
    af0 = int(seed["action_frame"][0, 0])
    af1 = af0 + 1
    bot0 = _fox_ecb_bottom_rel_y(SM_FALL, af0)
    bot1 = _fox_ecb_bottom_rel_y(SM_FALL, af1)
    seed["pos_y"][0, 0] = np.float32(-bot0 + 0.10)
    # Choose a small downward self-velocity so gravity pushes us below the floor in one frame.
    # Fox grav is ISO-extracted: data/characters/fox.json `grav`.
    grav = np.float32(_fox_attr("grav"))
    seed["speed_y_self"][0, 0] = np.float32(min(-0.05, -(0.20 + (bot1 - bot0)) + grav))
    seed["jumps_left"][0, 0] = np.uint8(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 1
    assert int(out["action_id"][0]) == ACT_LANDING_AIR_N
    assert int(out["animation_index"][0]) == SM_LANDING_AIR_N
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == int(_fox_attr("max_jumps"))


def test_escape_air_lands_enters_landing_fall_special_and_refreshes_jumps() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["ground_id"][0, 0] = np.uint16(1)  # prefer main FD floor segment

    # EscapeAir applies `escapeair_decay` to self velocity before integration, and does not run the
    # common gravity/terminal helper on the decay path (decomp: ftCo_EscapeAir_Phys).
    af0 = int(seed["action_frame"][0, 0])
    af1 = af0 + 1
    bot0 = _fox_ecb_bottom_rel_y(SM_FALL, af0)
    bot1 = _fox_ecb_bottom_rel_y(SM_FALL, af1)
    seed["pos_y"][0, 0] = np.float32(-bot0 + 0.10)
    decay = np.float32(_common_attr("escapeair_decay"))
    # Pick a downward speed so (vy * decay) crosses the floor within one frame.
    need_dy = np.float32(-(0.20 + (bot1 - bot0)))
    seed["speed_y_self"][0, 0] = np.float32(min(-0.05, need_dy / decay))
    seed["jumps_left"][0, 0] = np.uint8(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 1
    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["animation_index"][0]) == SM_LANDING_FALL_SPECIAL
    # LandingFallSpecial enters with a scaled anim speed:
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    tracks_path = Path("data/anims/fox.tracks.bin")
    end_frame = _tracks_end_frame(tracks_path, SM_LANDING_FALL_SPECIAL)
    lag = float(_common_attr("landing_fall_special_lag_frames"))
    rate = (float(end_frame) + 0.1) / float(lag)
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == int(_fox_attr("max_jumps"))


def test_cliffjumpquick2_floor_contact_enters_basic_landing() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_CLIFF_JUMP_QUICK2)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(seed["action_frame"][0, 0])
    seed["animation_index"][0, 0] = np.uint32(SM_CLIFF_JUMP_QUICK2)
    seed["ground_id"][0, 0] = np.uint16(1)  # prefer main FD floor segment

    # Decomp: CliffJump2_Coll delegates floor contact to ft_80082B1C through ft_800835B0. Use a
    # clear downward velocity so this fixture covers the Landing branch, not the gentle-contact
    # Wait branch.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800835B0,ft_80082B1C}
    af0 = int(seed["action_frame"][0, 0])
    af1 = af0 + 1
    bot0 = _fox_ecb_bottom_rel_y(SM_CLIFF_JUMP_QUICK2, af0)
    bot1 = _fox_ecb_bottom_rel_y(SM_CLIFF_JUMP_QUICK2, af1)
    seed["pos_y"][0, 0] = np.float32(-bot0 + 0.10)
    grav = np.float32(_fox_attr("grav"))
    seed["speed_y_self"][0, 0] = np.float32(min(-1.0, -(0.20 + (bot1 - bot0)) + grav))
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 1
    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["animation_index"][0]) == SM_LANDING
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == int(_fox_attr("max_jumps"))
