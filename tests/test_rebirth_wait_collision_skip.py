from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE

ACT_REBIRTH_WAIT = 13
ACT_WAIT = 14
CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32
SM_WAIT1_0 = 2


def _require_local_artifacts_or_skip() -> None:
    if not Path("data/anims/fox.bin").exists() or not Path("data/hurtcaps/fox.bin").exists():
        pytest.skip("requires local Fox anim/hurtcap artifacts")


def _rebirth_wait_seed(action_frame: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8([3, 4])
    seed["char_id"][0, :2] = np.uint8([CHAR_FOX, CHAR_FALCO])
    seed["action_id"][0, :2] = np.uint16([ACT_REBIRTH_WAIT, ACT_WAIT])
    seed["animation_index"][0, :2] = np.uint32([SM_WAIT1_0, SM_WAIT1_0])
    seed["action_frame"][0, :2] = np.int16([action_frame, 0])
    seed["anim_frame_f32"][0, :2] = np.float32([action_frame, 0.0])
    seed["frame_speed_mul_f32"][0, :2] = np.float32([1.0, 1.0])
    seed["pos_x"][0, :2] = np.float32([16.0, 60.0])
    seed["pos_y"][0, :2] = np.float32([45.0, 0.0])
    seed["facing"][0, :2] = np.uint8([1, 0])
    seed["match_flow_timer"][0, 0] = np.uint8(240)
    return seed


def test_rebirth_wait_terminal_wait1_does_not_build_collision_capsules() -> None:
    # Rebirth/RebirthWait set fp->x2219_b1. Vanilla skips common fighter collision while that bit
    # is live, and item-vs-fighter collision rejects x2219_b1 targets. The sim should preserve the
    # visible RebirthWait state without sampling BODY/catch capsules at terminal Wait1 frames.
    # refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    _require_local_artifacts_or_skip()
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _rebirth_wait_seed(action_frame=120)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    input_t = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype

    colldata_dtype = _colldata_ecb_dtype()
    assert int(sizes["colldata_ecb"]) == colldata_dtype.itemsize
    out_colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, prev_input, input_t)
        binding.write_compare(handle, out_compare)
        binding.debug_write_colldata_ecb(handle, out_colldata)
        hurtcaps, hurtcap_count = binding.hurtcaps_world(handle, 0, 0)
        hitboxes, hitbox_count = binding.hitboxes_world_full(handle, 0, 0)
    finally:
        binding.destroy(handle)

    compare = out_compare.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(compare["action_id"][0]) == ACT_REBIRTH_WAIT
    assert int(compare["action_frame"][0]) == 120
    assert int(hurtcap_count) == 0
    assert int(hitbox_count) == 0
    assert not np.any(hurtcaps)
    assert not np.any(hitboxes)
    colldata = out_colldata.view(colldata_dtype).reshape((1,))[0]
    assert int(colldata["floor_result_valid"][0]) == 0
    assert int(colldata["floor_skip_valid"][0]) == 0
