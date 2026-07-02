from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


def test_passive_stand_action_owns_animation_index() -> None:
    # Runtime lock for PassiveStand animation-index ownership:
    # - PassiveStand maps to ftCo_SM_PassiveStandF/B.
    # refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_PassiveStandF/B entries)
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["char_id"][0, 0] = np.uint8(1)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["facing"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(200)
    seed["animation_index"][0, 0] = np.uint32(999)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
    cur_input = np.zeros((1,), dtype=INPUT_DTYPE)
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(
            handle,
            prev_input.view(np.uint8).reshape((1, input_stride)),
            cur_input.view(np.uint8).reshape((1, input_stride)),
        )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)

    cmp0 = out.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(cmp0["action_id"][0]) == 200
    assert int(cmp0["animation_index"][0]) == 200
