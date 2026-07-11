from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import INPUT_DTYPE, SEED_DTYPE
from tools.extraction.extract_fighter_hitboxes import FORMAT_VERSION as HITBOX_VERSION

from tests.test_anim_pose import _MAT_BYTES, _find_anim_base_offset, _read_header
from tests.test_hurtboxes_pose import _mtx34_mul_point

HITBOX_MAGIC = b"MSLHITB1"
_HITBOX_EVENT_BYTES = 44
_HITBOX_INDEX_BYTES = 12


def _read_hitbox_events(path: Path) -> dict[int, list[dict]]:
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("MSLHITB1: file too small for header")
    if buf[:8] != HITBOX_MAGIC:
        raise ValueError(f"MSLHITB1: bad magic: {buf[:8]!r}")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != HITBOX_VERSION:
        raise ValueError(f"MSLHITB1: unsupported version: {ver} (want {HITBOX_VERSION})")
    entry_count = int.from_bytes(buf[12:16], "little", signed=False)
    if entry_count <= 0:
        raise ValueError("MSLHITB1: empty index")

    index_base = 16
    index_end = index_base + entry_count * _HITBOX_INDEX_BYTES
    if len(buf) < index_end:
        raise ValueError("MSLHITB1: truncated index")

    out: dict[int, list[dict]] = {}
    for i in range(entry_count):
        off = index_base + i * _HITBOX_INDEX_BYTES
        msid, rec_count, rec_bytes, payload_off = struct.unpack_from("<HHII", buf, off)
        if int(rec_bytes) != int(rec_count) * _HITBOX_EVENT_BYTES:
            raise ValueError("MSLHITB1: bad rec_bytes")
        if int(payload_off) < index_end or int(payload_off) + int(rec_bytes) > len(buf):
            raise ValueError("MSLHITB1: record payload out of range")

        events: list[dict] = []
        for ri in range(int(rec_count)):
            roff = int(payload_off) + ri * _HITBOX_EVENT_BYTES
            frame = int.from_bytes(buf[roff + 0 : roff + 2], "little", signed=False)
            kind = int(buf[roff + 2])
            hitbox_id = int(buf[roff + 3])
            bone_part_id = int.from_bytes(buf[roff + 4 : roff + 8], "little", signed=False)
            x, y, z, radius, damage = struct.unpack_from("<fffff", buf, roff + 8)
            u16s = struct.unpack_from("<8H", buf, roff + 28)
            events.append(
                {
                    "frame": int(frame),
                    "kind": int(kind),
                    "hitbox_id": int(hitbox_id),
                    "bone_part_id": int(bone_part_id),
                    "x": np.float32(x),
                    "y": np.float32(y),
                    "z": np.float32(z),
                    "radius": np.float32(radius),
                    "damage": np.float32(damage),
                    "u16_0": int(u16s[0]),
                    "u16_1": int(u16s[1]),
                    "u16_2": int(u16s[2]),
                    "u16_3": int(u16s[3]),
                    "u16_4": int(u16s[4]),
                    "u16_5": int(u16s[5]),
                    "u16_6": int(u16s[6]),
                    "u16_7": int(u16s[7]),
                }
            )
        out[int(msid)] = events
    return out


def _active_hitboxes_at_frame(events: list[dict], frame: int) -> dict[int, dict]:
    # Mirror the C-side event application policy:
    # - kind=0: set slot=hitbox_id
    # - kind=1: clear (hitbox_id==0xFF => clear-all)
    # - kind=2: mutate active slot damage without create-edge side effects
    # - kind=3: mutate active slot x42 interaction flags without create-edge side effects
    active: dict[int, dict] = {}
    for ev in events:
        if int(ev["frame"]) > int(frame):
            break
        if int(ev["kind"]) == 1:
            if int(ev["hitbox_id"]) == 0xFF:
                active.clear()
            else:
                active.pop(int(ev["hitbox_id"]), None)
            continue
        if int(ev["kind"]) == 2:
            hb_id = int(ev["hitbox_id"])
            if hb_id in active:
                active[hb_id] = {**active[hb_id], "damage": ev["damage"]}
            continue
        if int(ev["kind"]) == 3:
            hb_id = int(ev["hitbox_id"])
            if hb_id in active:
                active[hb_id] = {**active[hb_id], "u16_6": ev["u16_6"]}
            continue
        hb_id = int(ev["hitbox_id"])
        if 0 <= hb_id < 4:
            active[hb_id] = ev
    return active


@pytest.mark.integration
def test_hitboxes_refresh_matches_pose_bytes() -> None:
    import msl_binding

    if not Path("data/hitboxes/fox.bin").exists():
        pytest.skip("missing local artifact: data/hitboxes/fox.bin")
    if not Path("data/anims/fox.bin").exists():
        pytest.skip("missing local artifact: data/anims/fox.bin")
    if not Path("data/characters/fox.json").exists():
        pytest.skip("missing local artifact: data/characters/fox.json")

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(anim_buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

    by_msid = _read_hitbox_events(Path("data/hitboxes/fox.bin"))
    assert by_msid

    picked: tuple[int, int, int, dict] | None = None  # (msid, frame, hitbox_id, ev)
    for msid, events in sorted(by_msid.items()):
        try:
            frame_count, base = _find_anim_base_offset(
                buf=anim_buf, joint_count=joint_count, anim_count=anim_count, msid=int(msid)
            )
        except KeyError:
            continue
        if frame_count <= 0:
            continue
        for ev in events:
            f = int(ev["frame"])
            if not (0 <= f < frame_count):
                continue
            active = _active_hitboxes_at_frame(events, f)
            for hb_id, hb_ev in sorted(active.items()):
                bone_part_id = int(hb_ev["bone_part_id"])
                if bone_part_id in part_to_joint_index:
                    picked = (int(msid), int(f), int(hb_id), hb_ev)
                    break
            if picked is not None:
                break
        if picked is not None:
            break

    assert picked is not None, "failed to find any hitbox event with pose coverage"
    msid, frame, hb_id, ev = picked

    frame_count, base = _find_anim_base_offset(buf=anim_buf, joint_count=joint_count, anim_count=anim_count, msid=msid)
    assert 0 <= frame < frame_count
    joint_index = part_to_joint_index[int(ev["bone_part_id"])]
    off = base + frame * joint_count * _MAT_BYTES + joint_index * _MAT_BYTES
    py_m = np.frombuffer(anim_buf, dtype="<f4", count=12, offset=off)

    pos_x = np.float32(123.25)
    pos_y = np.float32(-45.5)
    pos_z = np.float32(9.75)

    with open("data/characters/fox.json", encoding="utf-8") as f:
        model_scaling = np.float32(json.load(f)["model_scaling"])

    c = _mtx34_mul_point(py_m, np.array([ev["x"], ev["y"], ev["z"]], dtype=np.float32))
    c *= model_scaling

    # Decomp-shaped root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z.
    # See src/hitboxes.c and refs/melee/src/melee/ft/fighter.c (ftPartSetRotY).
    facing_dir = np.float32(1.0)
    cx, cy, cz = c[0], c[1], c[2]
    c[0] = np.float32(facing_dir * cz)
    c[1] = np.float32(cy)
    c[2] = np.float32(-facing_dir * cx)

    c[0] = np.float32(c[0] + pos_x)
    c[1] = np.float32(c[1] + pos_y)
    c[2] = np.float32(c[2] + pos_z)

    ref_row = np.array(
        [c[0], c[1], c[2], ev["radius"], ev["damage"]],
        dtype=np.float32,
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
    # Keep action_update/match_flow out of the way; we only care about pose-driven refresh output.
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)  # right (decomp-shaped root rotY90)
    seed["pos_x"][0, 0] = pos_x
    seed["pos_y"][0, 0] = pos_y
    seed["pos_z"][0, 0] = pos_z
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
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

        hitboxes, count = msl_binding.hitboxes_world(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    assert isinstance(hitboxes, np.ndarray)
    assert hitboxes.dtype == np.float32
    assert hitboxes.shape == (4, 10)
    assert 0 <= int(count) <= 4

    enabled = hitboxes[:, 9]
    assert int(np.sum(enabled != np.float32(0.0))) == int(count)

    row = hitboxes[hb_id]
    assert int(row[9]) == 1

    got = row[:5].astype(np.float32, copy=False)
    assert np.array_equal(got.view(np.uint32), ref_row.view(np.uint32))
    assert int(row[5]) == int(ev["u16_0"])
    assert int(row[6]) == int(ev["u16_1"])
    assert int(row[7]) == int(ev["u16_3"])
    assert int(row[8]) == int(ev["bone_part_id"])


@pytest.mark.integration
def test_root_motion_hitbox_pose_does_not_recompose_transn_tail() -> None:
    # Negative lock for src/hitboxes.c::hitboxes_apply_live_transn_tail:
    # Fox AttackS4S has a non-zero TransN tail at the active hitbox frame, but the animation's
    # ftData entry has fp->x594_b0/uses_root_motion set. Source Phys consumes that TransN into
    # cur_pos through ft_80085030, so HitCapsule pose publication must not add the SSANIM01 tail
    # again when sampling lb_8000B1CC-style centers.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    import msl_binding

    if not Path("data/hitboxes/fox.bin").exists():
        pytest.skip("missing local artifact: data/hitboxes/fox.bin")
    if not Path("data/anims/fox.bin").exists():
        pytest.skip("missing local artifact: data/anims/fox.bin")
    if not Path("data/characters/fox.json").exists():
        pytest.skip("missing local artifact: data/characters/fox.json")

    char_id = 1
    msid = 62  # ftCo_SM_AttackS4S
    frame = 12
    hb_id = 0

    events = _read_hitbox_events(Path("data/hitboxes/fox.bin"))[msid]
    active = _active_hitboxes_at_frame(events, frame)
    ev = active[hb_id]
    assert int(ev["bone_part_id"]) == 13

    anim_buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(anim_buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}
    frame_count, base = _find_anim_base_offset(
        buf=anim_buf, joint_count=joint_count, anim_count=anim_count, msid=msid
    )
    assert frame < frame_count
    transn_base = base + frame_count * joint_count * _MAT_BYTES
    transn = np.frombuffer(anim_buf, dtype="<f4", count=3, offset=transn_base + frame * 12)
    assert float(abs(transn[2])) > 1.0

    joint_index = part_to_joint_index[int(ev["bone_part_id"])]
    off = base + frame * joint_count * _MAT_BYTES + joint_index * _MAT_BYTES
    m = np.frombuffer(anim_buf, dtype="<f4", count=12, offset=off)
    local = _mtx34_mul_point(m, np.array([ev["x"], ev["y"], ev["z"]], dtype=np.float32))
    with open("data/characters/fox.json", encoding="utf-8") as f:
        local *= np.float32(json.load(f)["model_scaling"])

    pos_x = np.float32(10.0)
    pos_y = np.float32(20.0)
    pos_z = np.float32(-3.0)
    facing_dir = np.float32(1.0)
    expected = np.array(
        [
            np.float32(pos_x + facing_dir * local[2]),
            np.float32(pos_y + local[1]),
            np.float32(pos_z - facing_dir * local[0]),
            ev["radius"],
            ev["damage"],
        ],
        dtype=np.float32,
    )

    # Negative lock for the native seed-lane owner as well as runtime publication:
    # teacher-forced HitCapsule.x58 derivation uses the same live-JObj policy, but must not add the
    # stripped SSANIM01 TransN tail when ftData says root motion has already consumed it into pos.
    valid, prev_x, prev_y, prev_z = msl_binding.derive_hitbox_prev_centers(
        2,
        np.array([[char_id, char_id]], dtype=np.uint8),
        np.array([[60, 60]], dtype=np.uint16),
        np.array([[msid, msid]], dtype=np.uint32),
        np.array([[frame, frame]], dtype=np.int16),
        np.array([[np.float32(frame), np.float32(frame)]], dtype=np.float32),
        np.array([[pos_x, np.float32(0.0)]], dtype=np.float32),
        np.array([[pos_y, np.float32(0.0)]], dtype=np.float32),
        np.array([[pos_z, np.float32(0.0)]], dtype=np.float32),
        np.array([[1, 1]], dtype=np.uint8),
        np.array([[np.float32(1.0), np.float32(1.0)]], dtype=np.float32),
        None,
        None,
    )
    assert int(valid[0, 0, hb_id]) == 1
    got_seed = np.array([prev_x[0, 0, hb_id], prev_y[0, 0, hb_id], prev_z[0, 0, hb_id]], dtype=np.float32)
    assert np.array_equal(got_seed.view(np.uint32), expected[:3].view(np.uint32))

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(char_id)
    seed["char_id"][0, 1] = np.uint8(char_id)
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = pos_x
    seed["pos_y"][0, 0] = pos_y
    seed["pos_z"][0, 0] = pos_z
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["action_frame"][0, 0] = np.int16(frame)
    seed["anim_frame_f32"][0, 0] = np.float32(frame)
    seed["animation_index"][0, 0] = np.uint32(msid)
    seed["hitlag"][0, :2] = np.uint16(2)

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        hitboxes, _count = msl_binding.hitboxes_world(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    row = hitboxes[hb_id]
    assert int(row[9]) == 1
    assert np.array_equal(row[:5].astype(np.float32, copy=False).view(np.uint32), expected.view(np.uint32))
