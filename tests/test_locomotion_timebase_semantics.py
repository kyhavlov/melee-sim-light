from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Character ids (GALE01): tools/slippi/make_dataset_from_slp.py and Slippi post-frame `character`.
CHAR_FOX = 1

# Stage ids (GALE01): Final Destination = 32.
STAGE_FD = 32

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h.
ACT_WAIT = 0x000E
ACT_WALK_SLOW = 0x000F
ACT_RUN = 0x0015

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
SM_WAIT1_0 = 2
SM_WALK_SLOW = 7
SM_RUN = 13


def _require_local_artifacts_or_skip() -> None:
    paths = [
        Path("data/stages/final_destination.json"),
        Path("data/common/ft_common_data.json"),
        Path("data/characters/fox.json"),
        Path("data/anims/fox.tracks.bin"),
    ]
    if any(not p.exists() for p in paths):
        pytest.skip("requires local data/ artifacts (stage + common + character + anim tracks)")


def _tracks_end_frame(path: Path, msid: int) -> float:
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
                return float(np.float32(end_frame))
    raise KeyError(f"msid {msid} not found in {path}")

def _tracks_end_frame_and_loop_flag(path: Path, msid: int) -> tuple[float, bool]:
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
            (end_frame_raw,) = struct.unpack("<f", f.read(4))
            aobj_loop = 0
            if version >= 2:
                (aobj_loop,) = struct.unpack("<B", f.read(1))
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
                return (float(np.float32(end_frame_raw)), bool(int(aobj_loop) != 0))
    raise KeyError(f"msid {msid} not found in {path}")


@pytest.mark.integration
def test_walk_entry_ticks_once_action_frame() -> None:
    _require_local_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
        seed = seed_bytes.view(SEED_DTYPE).reshape((1,))
        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)

        # P0: grounded Wait with a neutral walk velocity so WalkSlow is selected.
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["facing"][0, 0] = np.uint8(1)  # right
        seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 0] = np.int16(0)
        seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
        seed["anim_frame_f32"][0, 0] = np.float32(0.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
        seed["speed_ground_x_self"][0, 0] = np.float32(0.0)

        # P1: stable Wait (don't care).
        seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["facing"][0, 1] = np.uint8(1)
        seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 1] = np.int16(0)
        seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
        seed["anim_frame_f32"][0, 1] = np.float32(0.0)
        seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Use a moderate tilt to avoid Dash-flick gating (Wait IASA checks Dash before Walk).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        inp_view["p"]["main_x"][0, 0] = np.int8(50)

        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(out["action_id"][0]) == ACT_WALK_SLOW
        assert int(out["animation_index"][0]) == SM_WALK_SLOW
        # Decomp: walk entry calls ftAnim_8006EBA4 immediately after ChangeMotionState, so the
        # post-frame `state_age` advances by 1 and action_frame=floor(state_age)=1.
        # refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFCA4
        assert int(out["action_frame"][0]) == 1
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_run_aobj_loop_wraps_action_frame_at_end_frame() -> None:
    _require_local_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    tracks = Path("data/anims/fox.tracks.bin")
    end_frame = _tracks_end_frame(tracks, SM_RUN)
    if not (end_frame > 1.0):
        pytest.skip("local tracks missing/invalid Run end_frame; regenerate via extract_fighter_anims")

    _end_frame2, loops = _tracks_end_frame_and_loop_flag(tracks, SM_RUN)
    assert loops, "expected Run to be AOBJ_LOOP in extracted tracks"

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
        seed = seed_bytes.view(SEED_DTYPE).reshape((1,))
        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)

        # P0: Run with timebase at the last in-range frame so advancing by 1 reaches end_frame and
        # should wrap to 0 under AOBJ_LOOP.
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["facing"][0, 0] = np.uint8(1)
        seed["action_id"][0, 0] = np.uint16(ACT_RUN)
        seed["animation_index"][0, 0] = np.uint32(SM_RUN)
        seed["anim_frame_f32"][0, 0] = np.float32(end_frame - 1.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
        seed["action_frame"][0, 0] = np.int16(int(end_frame - 1.0))
        seed["speed_ground_x_self"][0, 0] = np.float32(1.0)

        # P1: stable Wait (don't care).
        seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["facing"][0, 1] = np.uint8(1)
        seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 1] = np.int16(0)
        seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
        seed["anim_frame_f32"][0, 1] = np.float32(0.0)
        seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["main_x"][0, 0] = np.int8(127)  # hold run so IASA doesn't enter RunBrake

        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(out["action_id"][0]) == ACT_RUN
        assert int(out["animation_index"][0]) == SM_RUN
        assert int(out["action_frame"][0]) == 0
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_non_looping_timebase_clamps_at_end_frame_and_stops_advancing() -> None:
    _require_local_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")

    # Use a known non-looping submotion (DamageFlyN) and isolate the timebase behavior by using
    # an out-of-domain action_id that no action module owns.
    #
    # Scope: validates that the anim_timebase layer clamps curr_frame at end_frame when AOBJ_LOOP
    # is not enabled, and that the timebase stops advancing once clamped.
    ACT_UNUSED = 0xFFFF
    SM_DAMAGE_FLY_N = 178

    tracks = Path("data/anims/fox.tracks.bin")
    end_frame, loops = _tracks_end_frame_and_loop_flag(tracks, SM_DAMAGE_FLY_N)
    assert not loops, "expected DamageFlyN to be non-looping in extracted tracks"
    if not (end_frame > 1.0):
        pytest.skip("local tracks missing/invalid DamageFlyN end_frame; regenerate via extract_fighter_anims")

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
        seed = seed_bytes.view(SEED_DTYPE).reshape((1,))
        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)

        # P0: non-looping timeline near end, advancing would overshoot without clamp.
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 0] = np.uint8(0)
        seed["facing"][0, 0] = np.uint8(1)
        seed["action_id"][0, 0] = np.uint16(ACT_UNUSED)
        seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FLY_N)
        seed["anim_frame_f32"][0, 0] = np.float32(end_frame - 1.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(2.0)
        seed["action_frame"][0, 0] = np.int16(int(end_frame - 1.0))

        # P1: stable Wait (don't care).
        seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["facing"][0, 1] = np.uint8(1)
        seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 1] = np.int16(0)
        seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
        seed["anim_frame_f32"][0, 1] = np.float32(0.0)
        seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)

        # Step 1: clamp to end_frame.
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out_bytes)
        out1 = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out1["action_id"][0]) == ACT_UNUSED
        assert int(out1["animation_index"][0]) == SM_DAMAGE_FLY_N
        assert int(out1["action_frame"][0]) == int(end_frame)

        # Step 2: stay clamped (rate should be 0 after reaching end_frame).
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out_bytes)
        out2 = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out2["action_id"][0]) == ACT_UNUSED
        assert int(out2["animation_index"][0]) == SM_DAMAGE_FLY_N
        assert int(out2["action_frame"][0]) == int(end_frame)
    finally:
        binding.destroy(handle)
