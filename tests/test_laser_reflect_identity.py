from __future__ import annotations

import struct

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_L = 0x0040
TRIGGER_FULL = np.uint8(255)

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_GUARD_REFLECT = 0x00B6

CHAR_FOX = 1
STAGE_FD = 32


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _load_fox_laser_shot_itkind_and_first_offset_x() -> tuple[int, float]:
    # data/items/lasers.bin layout: tools/extraction/extract_lasers.py (MSLLASR1 v2).
    path = "data/items/lasers.bin"
    buf = open(path, "rb").read()
    if buf[:8] != b"MSLLASR1":
        raise AssertionError(f"{path}: bad magic")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver < 2:
        raise AssertionError(f"{path}: unsupported ver={ver}")
    (count,) = struct.unpack_from("<H", buf, 12)
    off = 16

    # Header + record packing (see tools/extraction/extract_lasers.py::_pack_record).
    part1 = struct.Struct("<BBHHHHHHHHHff3fHBBH")
    part2 = struct.Struct("<ffHHHHb3xB3x")
    shoot_frames_bytes = 8 * 2  # 8 u16
    offs_bytes = 16 * 4  # 16 f32

    for _ in range(int(count)):
        char_id, _pad = struct.unpack_from("<BB", buf, off)
        base = off
        (  # noqa: ECE001 (deliberate unpack for layout sanity)
            _char_id,
            _pad0,
            shot_itkind,
            _gun_itkind,
            _spawn_bone_part_id,
            _ground_start_msid,
            _ground_loop_msid,
            _ground_end_msid,
            _air_start_msid,
            _air_loop_msid,
            _air_end_msid,
            _blaster_angle,
            _blaster_speed,
            _spawn_x,
            _spawn_y,
            _spawn_z,
            _lifetime_frames,
            _shoot_frame_count_ground,
            _shoot_frame_count_air,
            _reserved,
        ) = part1.unpack_from(buf, off)
        off += part1.size
        off += shoot_frames_bytes  # ground shoot_frames
        off += shoot_frames_bytes  # air shoot_frames
        (
            _damage,
            _size,
            _angle,
            _kbg,
            _wsk,
            _bkb,
            _shield_damage,
            hitbox_offsets_x_count,
        ) = part2.unpack_from(buf, off)
        off += part2.size
        offsets = struct.unpack_from("<" + "f" * 16, buf, off)
        off += offs_bytes

        if int(char_id) == CHAR_FOX:
            first = float(offsets[0]) if int(hitbox_offsets_x_count) > 0 else 0.0
            return int(shot_itkind), first

        # Defensive: ensure we don't desync parsing if record size changes.
        if off <= base:
            raise AssertionError("record parse did not advance")

    raise AssertionError("Fox laser record not found in lasers.bin")


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def test_reflected_laser_updates_owner_instance_and_staling_identity() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, off0 = _load_fox_laser_shot_itkind_and_first_offset_x()

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
        assert int(it0["owner"]) == 1
        # Spawn-latched staling identity (v1): does not transfer on reflect.
        assert int(it0["attack_id"]) == int(seed2["items"][0, 0]["attack_id"])
        assert int(it0["attack_instance"]) == int(seed2["items"][0, 0]["attack_instance"])
    finally:
        msl_binding.destroy(handle)
