from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_L = 0x0040
BUTTON_X = 0x0400

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_JUMP_AERIAL_F = 0x001B
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_B = 0x0043
ACT_ESCAPE_AIR = 0x00EC

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`
SM_WAIT1_0 = 2
SM_FALL = 20
SM_JUMP_AERIAL_F = 18
SM_ESCAPE_AIR = 44
# AttackAirN immediately precedes LandingAirN (73) in ftCo_Submotion.
SM_ATTACK_AIR_N = 68
SM_ATTACK_AIR_B = 70

CHAR_FOX = 1
STAGE_FD = 32


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


def _attackair_allow_interrupt_frame(move_key: str, char: str = "fox") -> int:
    d = json.loads(Path(f"data/moves/{char}.json").read_text())
    events = d["moves"][move_key]["events"]
    for e in events:
        if e["kind"] == "allow_interrupt":
            return int(e["frame"])
    raise KeyError(f"allow_interrupt event not found for move {move_key}")


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
    seed["pos_y"][0, 0] = np.float32(2000.0)  # stay airborne throughout the test
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


def _step_many(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray, n: int) -> list[np.ndarray]:
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
        outs: list[np.ndarray] = []

        msl_binding.reseed_seed(handle, seed_bytes)
        for _ in range(n):
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)
            outs.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
        return outs
    finally:
        msl_binding.destroy(handle)


def test_attackair_anim_end_enters_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    end_frame = _tracks_end_frame(Path("data/anims/fox.tracks.bin"), SM_ATTACK_AIR_N)
    outs = _step_many(seed, prev_inp, inp, int(math.ceil(end_frame)) + 2)

    first_fall = None
    for i, out in enumerate(outs):
        if int(out["action_id"][0]) == ACT_FALL:
            first_fall = (i, out)
            break
        assert int(out["action_id"][0]) == ACT_ATTACK_AIR_N

    assert first_fall is not None, "AttackAirN did not transition to Fall by animation end"
    _, out = first_fall
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["animation_index"][0]) == SM_FALL
    assert int(out["action_frame"][0]) == 0


def test_grounded_attackair_anim_end_does_not_synthesize_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    end_frame = _tracks_end_frame(Path("data/anims/fox.tracks.bin"), SM_ATTACK_AIR_N)

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(math.ceil(end_frame))
    seed["anim_frame_f32"][0, 0] = np.float32(end_frame)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) != ACT_FALL
    assert int(out["animation_index"][0]) != SM_FALL


def test_attackair_iasa_gates_airdodge_and_double_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    iasa_frame = _attackair_allow_interrupt_frame("ftCo_SM_AttackAirN")
    assert iasa_frame > 0

    # Before IASA: L press should not enter EscapeAir.
    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_N

    # At/after IASA: L press can enter EscapeAir.
    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(iasa_frame - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(iasa_frame - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["animation_index"][0]) == SM_ESCAPE_AIR
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C
    # EscapeAir entry advances anim once on enter.
    assert int(out["action_frame"][0]) == 1

    # Before IASA: X press should not enter double jump.
    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_N

    # At/after IASA: X press can enter a double jump.
    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(iasa_frame - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(iasa_frame - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMP_AERIAL_F
    assert int(out["animation_index"][0]) == SM_JUMP_AERIAL_F
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 1


def test_attackair_iasa_can_enter_fresh_attackair_before_double_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    iasa_frame = _attackair_allow_interrupt_frame("ftCo_SM_AttackAirN")
    assert iasa_frame > 0

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(iasa_frame - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(iasa_frame - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    inp_view["p"]["c_x"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_B
    assert int(out["animation_index"][0]) == SM_ATTACK_AIR_B
    assert int(out["action_frame"][0]) == 1
    assert int(out["jumps_left"][0]) == 2


def test_attackair_iasa_double_jump_carries_allow_interrupt_flag() -> None:
    # AttackAir -> JumpAerial command-bit carry (fp+0x2218 bit0 / Slippi state_flags[0]&0x80):
    # the outgoing AttackAir script fires allow_interrupt on the IASA-exit row and no jump entry
    # clears the bit, so the double-jump destination publishes it post-frame.
    # Witness: puff AttackAirF@34 -> ftPr JumpAerialF2 (Game_20250705T002244 rec 9243), confirmed
    # by a Dolphin PC-trace on ftAction_80071950 attributing the sole op-23 execution of the
    # window to the transitioning fighter with the script cursor inside AttackAirF's subaction.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    # Fox nair IASA -> JumpAerialF carries the bit.
    iasa_frame = _attackair_allow_interrupt_frame("ftCo_SM_AttackAirN")
    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_N)
    seed["action_frame"][0, 0] = np.int16(iasa_frame - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(iasa_frame - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_N)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMP_AERIAL_F
    assert int(out["state_flags"][0, 0] & np.uint8(0x80)) != 0

    # Puff fair IASA -> multijump (ftPr JumpAerialF1..F5, actions 341..345) carries the bit.
    iasa_frame = _attackair_allow_interrupt_frame("ftCo_SM_AttackAirF", char="puff")
    seed = _seed_air_base()
    seed["char_id"][0, :2] = np.uint8(15)  # puff
    seed["action_id"][0, 0] = np.uint16(0x0042)  # ftCo_MS_AttackAirF
    seed["action_frame"][0, 0] = np.int16(iasa_frame - 1)
    seed["anim_frame_f32"][0, 0] = np.float32(iasa_frame - 1)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(69)  # ftCo_SM_AttackAirF
    seed["jumps_left"][0, 0] = np.uint8(5)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert 341 <= int(out["action_id"][0]) <= 345
    assert int(out["state_flags"][0, 0] & np.uint8(0x80)) != 0


def test_attackairb_allow_interrupt_probe_clamps_at_entry_frame() -> None:
    # Underflow guard lock for state_flags[0] allow_interrupt snapshot probe:
    # entry frame has anim_frame_f32==0.0, so probe must clamp (no negative-frame read).
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_air_base()
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_B)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_AIR_B)
    seed["state_flags"][0, 0, 0] = np.uint8(0x80)  # seed carry bit on purpose

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_B
    # At entry (action_frame 1), AttackAirB allow_interrupt window is not active yet.
    assert int(out["state_flags"][0, 0] & np.uint8(0x80)) == 0

    # Extracted script window sanity: AttackAirB allow_interrupt turns on much later.
    iasa_frame = _attackair_allow_interrupt_frame("ftCo_SM_AttackAirB")
    assert iasa_frame > 1
