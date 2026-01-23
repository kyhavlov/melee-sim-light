from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE

from tests.test_anim_pose import _find_anim_base_offset, _read_header
from tests.test_hitboxes_pose import _active_hitboxes_at_frame, _read_hitbox_events


@pytest.mark.integration
def test_hitboxes_refresh_full_attrs_match_table() -> None:
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
            frame_count, _base = _find_anim_base_offset(
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
                if int(hb_ev["bone_part_id"]) in part_to_joint_index:
                    picked = (int(msid), int(f), int(hb_id), hb_ev)
                    break
            if picked is not None:
                break
        if picked is not None:
            break

    assert picked is not None, "failed to find any active hitbox with pose coverage"
    msid, frame, hb_id, ev = picked

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
    seed["pos_x"][0, 0] = np.float32(123.25)
    seed["pos_y"][0, 0] = np.float32(-45.5)
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
        hitboxes, count = msl_binding.hitboxes_world_full(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    assert isinstance(hitboxes, np.ndarray)
    assert hitboxes.dtype == np.float32
    assert hitboxes.shape == (4, 16)
    assert 0 <= int(count) <= 4

    enabled = hitboxes[:, 15]
    assert int(np.sum(enabled != np.float32(0.0))) == int(count)

    row = hitboxes[hb_id]
    assert int(row[15]) == 1

    assert row[3] > np.float32(0.0)
    assert row[4] >= np.float32(0.0)

    assert int(row[5]) == int(ev["u16_0"])  # angle
    assert int(row[6]) == int(ev["u16_1"])  # kbg
    assert int(row[7]) == int(ev["u16_2"])  # wsk
    assert int(row[8]) == int(ev["u16_3"])  # bkb

    element = int(ev["u16_4"]) & 0xFF
    shield_u8 = (int(ev["u16_4"]) >> 8) & 0xFF
    shield_s8 = shield_u8 - 256 if shield_u8 >= 128 else shield_u8
    assert int(row[9]) == element
    assert int(row[10]) == shield_s8

    assert int(row[11]) == (int(ev["u16_5"]) & 0xFF)  # sfx_severity
    assert int(row[12]) == ((int(ev["u16_5"]) >> 8) & 0xFF)  # sfx_kind
    assert int(row[13]) == int(ev["u16_6"])  # flags

    assert int(row[14]) == int(ev["bone_part_id"])
