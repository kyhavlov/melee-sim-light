from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_DOWN_WAIT_U = 0x00B8
ACT_DOWN_FOWARD_U = 0x00BC
ACT_DOWN_BACK_U = 0x00BD
ACT_GUARD_ON = 0x00B2
ACT_SQUAT = 0x0027

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_DOWN_BACK_U = 189

CHAR_FOX = 1
STAGE_FD = 32


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _require_local_artifacts_or_skip() -> None:
    stage_path = Path("data/stages/final_destination.json")
    fox_pose_path = Path("data/anims/fox.bin")
    fox_tracks_path = Path("data/anims/fox.tracks.bin")

    if not stage_path.exists() or not fox_pose_path.exists() or not fox_tracks_path.exists():
        pytest.skip("requires local data/ artifacts (stage + anims); run tools/extraction to generate")

    # Ensure roll msids exist in local anim artifacts. Older extractions omitted these, which makes
    # TransN sampling fail and the roll Phys fall back to friction.
    msids = _read_tracks_msids(fox_tracks_path)
    required = {188, 189, 196, 197}  # refs/melee/.../forward.h ftCo_SM_DownFoward/DownBack (U/D)
    if not required.issubset(set(msids)):
        pytest.skip("local anims missing DownFoward/DownBack msids; regenerate via extract_fighter_anims")


def _read_tracks_msids(path: Path) -> list[int]:
    # Minimal SSANIMT1 reader (mirrors tests/test_data_contract.py).
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

        msids: list[int] = []
        for _ in range(anim_count):
            (msid,) = struct.unpack("<H", f.read(2))
            f.read(4)  # end_frame
            if version >= 2:
                f.read(1)  # aobj_loop
            msids.append(int(msid))
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
        return msids


def _read_tracks_end_frame(path: Path, target_msid: int) -> float | None:
    # Minimal SSANIMT1 reader: return end_frame for a given msid if present.
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
            (msid,) = struct.unpack("<H", f.read(2))
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
            if int(msid) == int(target_msid):
                return float(end_frame)
        return None


def _seed_ground_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["downwait_timer"][0, :2] = np.int16(220)

    # P2: stable grounded idle.
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


def _step_sequence(seed: np.ndarray, inputs: list[np.ndarray]) -> list[np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert len(inputs) > 0
    for inp in inputs:
        assert inp.shape == (1, input_stride)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        outs: list[np.ndarray] = []

        msl_binding.reseed_seed(handle, seed_bytes)
        prev_inp = _mk_input_bytes(1, input_stride)
        for inp in inputs:
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)
            outs.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            prev_inp = inp
        return outs
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
def test_down_roll_forward_applies_root_motion_on_entry() -> None:
    _require_local_artifacts_or_skip()
    seed = _seed_ground_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DOWN_WAIT_U)
    seed["action_frame"][0, 0] = np.int16(0)

    import msl_binding

    input_stride = int(msl_binding.sizes()["input"])
    inp = _mk_input_bytes(1, input_stride)

    # Hold stick forward (right) to trigger DownFoward from DownWaitU via lstick roll hold gate.
    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    inp_view["p"]["main_x"][0, 0] = np.int8(80)
    inp_view["p"]["main_y"][0, 0] = np.int8(0)

    neutral = _mk_input_bytes(1, input_stride)
    outs = _step_sequence(seed, [inp] + [neutral] * 5)
    out0 = outs[0]
    assert int(out0["action_id"][0]) == ACT_DOWN_FOWARD_U
    assert int(out0["on_ground"][0]) == 1

    xs = [float(o["pos_x"][0]) for o in outs]
    assert all(b >= a for a, b in zip(xs, xs[1:]))
    assert xs[-1] > xs[0]
    assert all(int(o["on_ground"][0]) == 1 for o in outs)


@pytest.mark.integration
def test_down_roll_back_applies_root_motion_on_entry() -> None:
    _require_local_artifacts_or_skip()
    seed = _seed_ground_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DOWN_WAIT_U)
    seed["action_frame"][0, 0] = np.int16(0)

    import msl_binding

    input_stride = int(msl_binding.sizes()["input"])
    inp = _mk_input_bytes(1, input_stride)

    # Hold stick back (left) to trigger DownBack from DownWaitU via lstick roll hold gate.
    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    inp_view["p"]["main_x"][0, 0] = np.int8(-80)
    inp_view["p"]["main_y"][0, 0] = np.int8(0)

    neutral = _mk_input_bytes(1, input_stride)
    outs = _step_sequence(seed, [inp] + [neutral] * 5)
    out0 = outs[0]
    assert int(out0["action_id"][0]) == ACT_DOWN_BACK_U
    assert int(out0["on_ground"][0]) == 1

    xs = [float(o["pos_x"][0]) for o in outs]
    assert all(b <= a for a, b in zip(xs, xs[1:]))
    assert xs[-1] < xs[0]
    assert all(int(o["on_ground"][0]) == 1 for o in outs)


@pytest.mark.integration
@pytest.mark.parametrize("kind", ["shield", "crouch"])
def test_down_roll_anim_end_does_not_apply_extra_root_motion(kind: str) -> None:
    _require_local_artifacts_or_skip()
    seed = _seed_ground_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DOWN_BACK_U)

    # Put the roll on its anim-end frame after the pre-input anim-timebase advance (+1.0f).
    # This test protects against applying one extra frame of TransN root motion on the roll end frame
    # (a common mismatch source around FD floor boundaries).
    end_frame = _read_tracks_end_frame(Path("data/anims/fox.tracks.bin"), SM_DOWN_BACK_U)
    assert end_frame is not None and end_frame > 1.0
    seed["anim_frame_f32"][0, 0] = np.float32(end_frame - 1.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.0)
    seed["speed_x_attack"][0, 0] = np.float32(0.0)
    seed["shield_hp"][0, 0] = np.float32(60.0)

    import msl_binding

    input_stride = int(msl_binding.sizes()["input"])
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape(-1)

    if kind == "shield":
        # Hold analog trigger so shield_held is true without requiring a pressed-edge L/R.
        inp_view["p"]["l"][0, 0] = np.uint8(255)
    else:
        # Hold down to enter Squat immediately after the roll ends (Wait IASA -> Squat).
        inp_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_sequence(seed, [inp])[0]

    assert int(out["action_id"][0]) != ACT_DOWN_BACK_U
    if kind == "shield":
        assert int(out["action_id"][0]) == ACT_GUARD_ON
    else:
        assert int(out["action_id"][0]) == ACT_SQUAT

    assert abs(float(out["pos_x"][0])) < 1e-6
    assert abs(float(out["speed_ground_x_self"][0])) < 1e-6
