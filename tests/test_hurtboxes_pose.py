from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

from tests.test_anim_pose import _MAT_BYTES, _pick_first_nonempty_anim, _pick_missing_msid, _read_header

HURT_MAGIC = b"MSLHURT1"
HURT_VERSION = 1
_HURT_REC_BYTES = 34


pytestmark = pytest.mark.integration


def _require_local(path: Path) -> None:
    if not path.exists():
        pytest.skip(f"missing local artifact: {path}")


def _require_binding():
    try:
        import msl_binding  # type: ignore[import-not-found]
    except Exception as e:  # pragma: no cover - depends on local build env
        pytest.skip(f"msl_binding extension not available: {e}")
    return msl_binding


def _read_hurtcaps(path: Path) -> list[dict]:
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("MSLHURT1: file too small for header")
    if buf[:8] != HURT_MAGIC:
        raise ValueError(f"MSLHURT1: bad magic: {buf[:8]!r}")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != HURT_VERSION:
        raise ValueError(f"MSLHURT1: unsupported version: {ver} (want {HURT_VERSION})")
    capsule_count = int.from_bytes(buf[12:14], "little", signed=False)
    reserved = int.from_bytes(buf[14:16], "little", signed=False)
    if reserved != 0:
        raise ValueError(f"MSLHURT1: nonzero reserved: {reserved}")
    need = 16 + capsule_count * _HURT_REC_BYTES
    if need != len(buf):
        raise ValueError(f"MSLHURT1: unexpected size: got={len(buf)} want={need}")

    caps: list[dict] = []
    off = 16
    for _ in range(capsule_count):
        bone_part_id = int.from_bytes(buf[off + 0 : off + 2], "little", signed=False)
        height = int(buf[off + 2])
        is_grabbable = int(buf[off + 3])
        pad = int.from_bytes(buf[off + 4 : off + 6], "little", signed=False)
        if pad != 0:
            raise ValueError("MSLHURT1: nonzero record pad")
        a_offset = np.array(struct.unpack_from("<fff", buf, off + 6), dtype=np.float32)
        b_offset = np.array(struct.unpack_from("<fff", buf, off + 18), dtype=np.float32)
        (scale,) = struct.unpack_from("<f", buf, off + 30)
        caps.append(
            {
                "bone_part_id": int(bone_part_id),
                "height": int(height),
                "is_grabbable": int(is_grabbable),
                "a_offset": a_offset,
                "b_offset": b_offset,
                "scale": np.float32(scale),
            }
        )
        off += _HURT_REC_BYTES
    return caps


def _mtx34_mul_point(m: np.ndarray, v: np.ndarray) -> np.ndarray:
    x = np.float32(v[0])
    y = np.float32(v[1])
    z = np.float32(v[2])
    return np.array(
        [
            np.float32(m[0] * x + m[1] * y + m[2] * z + m[3]),
            np.float32(m[4] * x + m[5] * y + m[6] * z + m[7]),
            np.float32(m[8] * x + m[9] * y + m[10] * z + m[11]),
        ],
        dtype=np.float32,
    )


def test_hurtboxes_refresh_matches_pose_bytes() -> None:
    _require_local(Path("data/anims/fox.bin"))
    _require_local(Path("data/hurtcaps/fox.bin"))
    _require_local(Path("data/characters/fox.json"))
    msl_binding = _require_binding()

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(anim_buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

    hurtcaps = _read_hurtcaps(Path("data/hurtcaps/fox.bin"))
    assert hurtcaps

    # Choose a stable target capsule whose bone exists in SSANIM joint_parts.
    cap_i = -1
    for i, cap in enumerate(hurtcaps):
        if cap["bone_part_id"] in part_to_joint_index:
            cap_i = i
            break
    assert cap_i >= 0, "no hurt capsule bone ids are present in SSANIM joint_parts"
    cap = hurtcaps[cap_i]

    # Choose a stable, data-driven (msid, frame) from the anim file itself.
    msid, frame_count, base = _pick_first_nonempty_anim(buf=anim_buf, joint_count=joint_count, anim_count=anim_count)
    assert frame_count > 0
    frame = 0

    joint_index = part_to_joint_index[int(cap["bone_part_id"])]
    off = base + frame * joint_count * _MAT_BYTES + joint_index * _MAT_BYTES
    py_m = np.frombuffer(anim_buf, dtype="<f4", count=12, offset=off)

    pos_x = np.float32(123.25)
    pos_y = np.float32(-45.5)
    pos_z = np.float32(0.0)
    scale_y = np.float32(1.0)
    model_scaling = np.float32(json.loads(Path("data/characters/fox.json").read_text())["model_scaling"])
    facing_dir = np.float32(1.0)  # seed["facing"]=1 => facing_dir=+1

    a = _mtx34_mul_point(py_m, cap["a_offset"])
    b = _mtx34_mul_point(py_m, cap["b_offset"])

    # Match hurtboxes_refresh:
    # - scale by (fighter_scale_y * model_scaling)
    # - apply root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z:
    #   x' = facing_dir * z, z' = -facing_dir * x
    scale = np.float32(scale_y * model_scaling)
    a *= scale
    b *= scale
    ax = np.float32(facing_dir * a[2])
    az = np.float32(-facing_dir * a[0])
    bx = np.float32(facing_dir * b[2])
    bz = np.float32(-facing_dir * b[0])
    a[0] = ax
    a[2] = az
    b[0] = bx
    b[2] = bz
    a[0] = np.float32(a[0] + pos_x)
    a[1] = np.float32(a[1] + pos_y)
    a[2] = np.float32(a[2] + pos_z)
    b[0] = np.float32(b[0] + pos_x)
    b[1] = np.float32(b[1] + pos_y)
    b[2] = np.float32(b[2] + pos_z)

    ref_row = np.array([a[0], a[1], a[2], b[0], b[1], b[2], np.float32(cap["scale"] * scale)], dtype=np.float32)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert input_stride == INPUT_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)  # Fox
    seed["char_id"][0, 1] = np.uint8(1)
    # Keep action_update/match_flow out of the way; we only care about pose-driven refresh output.
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)  # right (pose-space X mirror parity)
    seed["pos_x"][0, 0] = pos_x
    seed["pos_y"][0, 0] = pos_y
    seed["pos_z"][0, 0] = pos_z
    seed["fighter_scale_y"][0, 0] = scale_y
    seed["action_frame"][0, 0] = np.int16(frame)
    seed["anim_frame_f32"][0, 0] = np.float32(frame)
    seed["animation_index"][0, 0] = np.uint32(msid)
    seed["hitlag"][0, 0] = np.uint16(2)  # timers_update decrements first; keep hitlag > 0 this frame

    # Default P2 to a stable, frozen state.
    seed["hitlag"][0, 1] = np.uint16(2)

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        caps_world, count = msl_binding.hurtcaps_world(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    assert isinstance(caps_world, np.ndarray)
    assert caps_world.dtype == np.float32
    assert caps_world.shape == (32, 7)
    assert 0 <= int(count) <= 32
    assert int(count) >= len(hurtcaps)

    got = caps_world[cap_i]
    assert np.array_equal(got.view(np.uint32), ref_row.view(np.uint32))


def test_hurtboxes_refresh_falls_back_on_missing_msid() -> None:
    _require_local(Path("data/anims/fox.bin"))
    _require_local(Path("data/hurtcaps/fox.bin"))
    msl_binding = _require_binding()

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, _joint_parts = _read_header(anim_buf)
    missing_msid = _pick_missing_msid(buf=anim_buf, joint_count=joint_count, anim_count=anim_count)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert input_stride == INPUT_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)
    seed["char_id"][0, 1] = np.uint8(1)
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)  # right (pose-space X mirror parity)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0)
    seed["animation_index"][0, 0] = np.uint32(missing_msid)
    seed["hitlag"][0, 0] = np.uint16(2)
    seed["hitlag"][0, 1] = np.uint16(2)

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        caps_world, count = msl_binding.hurtcaps_world(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    # Slot identity is preserved: count stays equal to init capsule_count, but all missing-pose
    # capsules are disabled and their world rows remain zero.
    hurtcaps = _read_hurtcaps(Path("data/hurtcaps/fox.bin"))
    assert int(count) >= len(hurtcaps)
    assert np.all(caps_world == np.float32(0.0))


def test_hurtboxes_refresh_applies_fighter_scale_y() -> None:
    _require_local(Path("data/anims/fox.bin"))
    _require_local(Path("data/hurtcaps/fox.bin"))
    _require_local(Path("data/characters/fox.json"))
    msl_binding = _require_binding()

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(anim_buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

    hurtcaps = _read_hurtcaps(Path("data/hurtcaps/fox.bin"))
    assert hurtcaps

    # Choose a stable target capsule whose bone exists in SSANIM joint_parts.
    cap_i = -1
    for i, cap in enumerate(hurtcaps):
        if cap["bone_part_id"] in part_to_joint_index:
            cap_i = i
            break
    assert cap_i >= 0, "no hurt capsule bone ids are present in SSANIM joint_parts"
    cap = hurtcaps[cap_i]
    out_row_i = cap_i

    msid, frame_count, base = _pick_first_nonempty_anim(buf=anim_buf, joint_count=joint_count, anim_count=anim_count)
    assert frame_count > 0
    frame = 0

    joint_index = part_to_joint_index[int(cap["bone_part_id"])]
    off = base + frame * joint_count * _MAT_BYTES + joint_index * _MAT_BYTES
    py_m = np.frombuffer(anim_buf, dtype="<f4", count=12, offset=off)

    pos_x = np.float32(123.25)
    pos_y = np.float32(-45.5)
    pos_z = np.float32(0.0)
    scale_y = np.float32(2.0)
    model_scaling = np.float32(json.loads(Path("data/characters/fox.json").read_text())["model_scaling"])
    facing_dir = np.float32(1.0)  # seed["facing"]=1 => facing_dir=+1

    a_local = _mtx34_mul_point(py_m, cap["a_offset"])
    b_local = _mtx34_mul_point(py_m, cap["b_offset"])

    scale = np.float32(scale_y * model_scaling)
    a = np.array([np.float32(a_local[0] * scale), np.float32(a_local[1] * scale), np.float32(a_local[2] * scale)], dtype=np.float32)
    b = np.array([np.float32(b_local[0] * scale), np.float32(b_local[1] * scale), np.float32(b_local[2] * scale)], dtype=np.float32)

    ax = np.float32(facing_dir * a[2])
    az = np.float32(-facing_dir * a[0])
    bx = np.float32(facing_dir * b[2])
    bz = np.float32(-facing_dir * b[0])
    a[0] = ax
    a[2] = az
    b[0] = bx
    b[2] = bz
    a[0] = np.float32(a[0] + pos_x)
    a[1] = np.float32(a[1] + pos_y)
    a[2] = np.float32(a[2] + pos_z)
    b[0] = np.float32(b[0] + pos_x)
    b[1] = np.float32(b[1] + pos_y)
    b[2] = np.float32(b[2] + pos_z)

    ref_row = np.array(
        [a[0], a[1], a[2], b[0], b[1], b[2], np.float32(cap["scale"] * scale)], dtype=np.float32
    )

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert input_stride == INPUT_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)  # Fox
    seed["char_id"][0, 1] = np.uint8(1)
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)  # right (pose-space X mirror parity)
    seed["pos_x"][0, 0] = pos_x
    seed["pos_y"][0, 0] = pos_y
    seed["pos_z"][0, 0] = pos_z
    seed["fighter_scale_y"][0, 0] = scale_y
    seed["fighter_scale_y"][0, 1] = np.float32(1.0)
    seed["action_frame"][0, 0] = np.int16(frame)
    seed["anim_frame_f32"][0, 0] = np.float32(frame)
    seed["animation_index"][0, 0] = np.uint32(msid)
    seed["hitlag"][0, 0] = np.uint16(2)
    seed["hitlag"][0, 1] = np.uint16(2)

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        caps_world, count = msl_binding.hurtcaps_world(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    assert 0 <= int(count) <= 32
    assert int(count) >= len(hurtcaps)
    got = caps_world[out_row_i]
    assert np.array_equal(got.view(np.uint32), ref_row.view(np.uint32))
