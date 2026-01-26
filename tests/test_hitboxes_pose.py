from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

from tests.test_anim_pose import _MAT_BYTES, _find_anim_base_offset, _read_header
from tests.test_hurtboxes_pose import _mtx34_mul_point

HITBOX_MAGIC = b"MSLHITB1"
HITBOX_VERSION = 1
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

    c = _mtx34_mul_point(py_m, np.array([ev["x"], ev["y"], ev["z"]], dtype=np.float32))
    c[0] = np.float32(c[0] + pos_x)
    c[1] = np.float32(c[1] + pos_y)

    ref_row = np.array(
        [c[0], c[1], c[2], ev["radius"], ev["damage"]],
        dtype=np.float32,
    )

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    assert input_stride == INPUT_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(1)  # Fox
    seed["char_id"][0, 1] = np.uint8(1)
    # Keep action_update/match_flow out of the way; we only care about pose-driven refresh output.
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)  # right (pose-space X mirror parity)
    seed["pos_x"][0, 0] = pos_x
    seed["pos_y"][0, 0] = pos_y
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
