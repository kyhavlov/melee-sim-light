from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_L = 0x0040
TRIGGER_FULL = np.uint8(255)

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD_REFLECT = 0x00B6

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _load_laser_shot_itkind_and_first_offset_x_and_lifetime(char_id_target: int) -> tuple[int, float, int]:
    # data/items/lasers.bin layout: tools/extraction/extract_lasers.py (MSLLASR1 v2/v3).
    path = "data/items/lasers.bin"
    if not Path(path).exists():
        pytest.skip(f"missing local artifact: {path}")
    buf = open(path, "rb").read()
    if buf[:8] != b"MSLLASR1":
        raise AssertionError(f"{path}: bad magic")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver not in (1, 2, 3, 4):
        raise AssertionError(f"{path}: unsupported ver={ver}")
    (count,) = struct.unpack_from("<H", buf, 12)
    off = 16

    # Record packing (see tools/extraction/extract_lasers.py::_pack_record).
    record_bytes = {1: 158, 2: 166, 3: 254, 4: 254}[int(ver)]

    for _ in range(int(count)):
        base = off
        char_id = int(buf[off])
        shot_itkind = int(struct.unpack_from("<H", buf, off + 2)[0])

        # Match src/laser_params.c offsets:
        # - v1 header ends at +38, v2+ header ends at +46.
        # - then 16 u16 ground frames + 16 u16 air frames (+32 bytes).
        off_header = 38 if int(ver) == 1 else 46
        off_part2 = base + off_header + (8 * 2) + (8 * 2)

        hitbox_offsets_x_count = int(buf[off_part2 + 20])
        offs0 = off_part2 + 24
        first = float(struct.unpack_from("<f", buf, offs0)[0]) if hitbox_offsets_x_count > 0 else 0.0

        if int(char_id) == int(char_id_target):
            lifetime = int(struct.unpack_from("<H", buf, base + 40)[0]) if int(ver) >= 2 else int(
                struct.unpack_from("<H", buf, base + 32)[0]
            )
            return int(shot_itkind), first, lifetime

        # Defensive: ensure we don't desync parsing if record size changes.
        off += record_bytes
        if off <= base:
            raise AssertionError("record parse did not advance")

    raise AssertionError(f"laser record not found in lasers.bin for char_id={char_id_target}")


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


@pytest.mark.integration
def test_reflected_laser_updates_owner_instance_and_staling_identity() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, off0, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FOX)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    # Attacker P0: idle.
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(2)  # Wait1_0

    # Defender P1: GuardReflect reflect bubble active (Slippi seeds fp->reflecting via state_flags).
    # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C (ftColl_CreateReflectHit).
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)  # shield states commonly report -1 in Slippi
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    # Drive reflect-active via the decomp-shaped GuardReflect timer (mv.co.guard.x14).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (init x14=x2A4)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0 (tick/expire; clears fp->reflecting)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(int(_common_attr("powershield_reflect_frames")) + 1)
    # Powershield-active lifetime (mv.co.guard.x18) must also be active for powershield reflect.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(int(_common_attr("powershield_reflect_total_frames")) + 1)
    # Slippi fp+0x221C powershield-active bit (0x20).
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)

    # Distinct identities so the test can assert the transfer.
    seed["instance_id"][0, 0] = np.uint16(100)
    seed["instance_id"][0, 1] = np.uint16(200)
    seed["attack_instance"][0, 0] = np.uint16(111)
    seed["attack_instance"][0, 1] = np.uint16(222)

    # Place defender at a stable point above the stage floor to avoid stage collision in the same step.
    seed["pos_x"][0, 0] = np.float32(-5.0)
    seed["pos_y"][0, 0] = np.float32(5.0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(5.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Hold shield via analog trigger so GuardReflect doesn't immediately exit.
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        # First: compute the shield-bone center the sim uses as the reflect-bubble center (derived in C).
        # We do this without the laser present to keep the test independent of collision timing.
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy = float(bubbles[1, 0]), float(bubbles[1, 1])

        # Second: reseed and place a laser directly on the reflect bubble center (vel=0 so motion doesn't move it).
        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["attack_id"] = np.uint16(3333)
        seed2["items"][0, 0]["attack_instance"] = np.uint16(111)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(0.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(shx - np.float32(off0))
        seed2["items"][0, 0]["pos_y"] = np.float32(shy)
        seed2["items"][0, 0]["timer"] = np.float32(10.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        it0 = cmp0["items"][0]
        assert int(it0["exists"]) == 1
        # Decomp-split ownership model:
        # - overlap writes reflect snapshot fields (ftColl_80077464),
        # - item pass consumes snapshot ownership (Item_80269F14).
        # In this test harness we observe snapshot staging first, then ownership transfer next step.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
        # refs/melee/src/melee/it/item.c::Item_80269F14
        assert int(it0["owner"]) == int(seed2["items"][0, 0]["owner"])
        assert int(it0["misc2"]) == 255
        assert int(it0["misc3"]) == 2
        # Spawn-latched staling identity (v1): does not transfer on reflect.
        assert int(it0["attack_id"]) == int(seed2["items"][0, 0]["attack_id"])
        assert int(it0["attack_instance"]) == int(seed2["items"][0, 0]["attack_instance"])

        msl_binding.step_input(handle, prev_inp, inp)
        out_cmp2 = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp2)
        cmp1 = out_cmp2.view(COMPARE_DTYPE).reshape((1,))[0]
        it1 = cmp1["items"][0]
        assert int(it1["exists"]) == 1
        assert int(it1["owner"]) == 1
        assert int(it1["misc2"]) == 0
        assert int(it1["misc3"]) == 0
    finally:
        msl_binding.destroy(handle)


def test_guardreflect_stale_x14_shield_hit_clears_reflect_active_and_destroys_laser() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, off0, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FOX)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(2)

    # Decomp-shaped stale-x18 GuardReflect snapshot:
    # - x14 (+1-bias seed) already expired, so projectile overlap should resolve through the regular
    #   shield-hit / GuardSetOff path rather than item reflect ownership.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(-1)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(0)
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(1)
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        # Use the runtime shield bubble center so the synthetic row stays robust to shield-bone data.
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy = float(bubbles[1, 0]), float(bubbles[1, 1])

        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(5.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(shx - np.float32(off0))
        seed2["items"][0, 0]["pos_y"] = np.float32(shy)
        seed2["items"][0, 0]["timer"] = np.float32(10.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(cmp0["action_id"][1]) == 181  # GuardSetOff
        assert int(cmp0["animation_index"][1]) == 40  # GuardDamage
        assert int(cmp0["hitlag"][1]) == 4
        assert int(cmp0["state_flags"][1, 1]) == 33
        assert int(cmp0["items"][0]["exists"]) == 0
    finally:
        msl_binding.destroy(handle)


def test_guardreflect_late_locomotion_shield_hit_keeps_one_frame_old_laser_alive() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, _, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FALCO)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FALCO)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(2)

    # One-frame-late locomotion -> GuardReflect frozen snapshot:
    # - replay can already resolve the projectile contact through GuardSetOff while keeping the
    #   one-frame-old laser alive on the shield-bounce path instead of reflecting or despawning it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_ShieldBounced}
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(-1)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(1)
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(1)
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)

    # Row-shaped synthetic from the kept TBK family:
    # - defender is the grounded Fox on the first shield-admission frame,
    # - attacker-side Falco laser is one-step before the replay-kept bounce.
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(-20.015694)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["pos_x"][0, 1] = np.float32(25.920704)
    seed["pos_y"][0, 1] = np.float32(0.0001)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(5.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(20.115339)
        seed2["items"][0, 0]["pos_y"] = np.float32(13.752838)
        seed2["items"][0, 0]["timer"] = np.float32(94.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(cmp0["action_id"][1]) == 181  # GuardSetOff
        assert int(cmp0["animation_index"][1]) == 40  # GuardDamage
        assert int(cmp0["hitlag"][1]) == 4
        assert int(cmp0["items"][0]["exists"]) == 1
        assert int(cmp0["items"][0]["owner"]) == 0
        assert int(cmp0["items"][0]["instance_id"]) == 999
    finally:
        msl_binding.destroy(handle)
