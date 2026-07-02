from __future__ import annotations

import numpy as np


def test_no_allocations_in_stage_collision_step() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = np.zeros((1, seed_stride), dtype=np.uint8)
        from tools.eval.validation_dtypes import SEED_DTYPE

        seed_t = seed.view(SEED_DTYPE).reshape((1,))
        seed_t["stage_id"][0] = np.uint32(32)
        seed_t["num_players"][0] = np.uint8(2)
        seed_t["stocks"][0, :2] = np.uint8(4)
        seed_t["action_id"][0, :2] = np.uint16(0x000E)  # Wait

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)

        msl_binding.alloc_reset()
        msl_binding.reseed_seed(handle, seed)
        msl_binding.step_input(handle, prev_inp, inp)

        stats = msl_binding.alloc_stats()
        assert int(stats["calls"]) == 0
        assert int(stats["bytes"]) == 0
    finally:
        msl_binding.destroy(handle)
