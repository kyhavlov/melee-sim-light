from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import SEED_DTYPE


def test_no_allocations_after_init() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    batch = 8
    handle = msl_binding.init(batch_size=batch, num_players=2)

    seed = np.zeros((batch, seed_stride), dtype=np.uint8)
    seed.view(SEED_DTYPE).reshape(-1)["stage_id"] = np.uint32(32)
    prev_inp = np.zeros((batch, input_stride), dtype=np.uint8)
    inp = np.zeros((batch, input_stride), dtype=np.uint8)
    out = np.zeros((batch, compare_stride), dtype=np.uint8)

    # Ignore init allocations (expected); enforce no allocations in reseed/step/write.
    msl_binding.alloc_reset()
    msl_binding.reseed_seed(handle, seed)
    for _ in range(32):
        msl_binding.step_input(handle, prev_inp, inp)
    for _ in range(8):
        msl_binding.write_compare(handle, out)

    stats = msl_binding.alloc_stats()
    assert int(stats["calls"]) == 0
    assert int(stats["bytes"]) == 0
