from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_shine_loop_release_lag_holds_loop_until_countdown_expires() -> None:
    # Regression lock for seed==ref action_id triple:
    # - seed/ref/out was 361/361/363 at TBK rec=4455 p=1
    # - decomp model uses {releaseLag,isRelease}; Loop exits only when (releaseLag<=0 && isRelease).
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwHit_Check}
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    record = 4455
    p = 1

    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 361
    assert int(row["ref_t1"]["action_id"][0, p]) == 361
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["shine_is_release"][0, p]) == 1
    assert int(row["seed_t"]["shine_release_lag"][0, p]) > 0

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(out["action_id"][p]) == 361
        assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][0, p])
        assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    finally:
        binding.destroy(handle)
