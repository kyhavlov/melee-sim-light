from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_attackairlw_no_spurious_cliffcatch_9152_9154_p1_regression() -> None:
    # Locks in the GracefulAttachedTurtle false ledge grab cluster:
    # replay stays in AttackAirLw (69) at t+1 but the sim previously entered CliffCatch (252).
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > 9154, f"dataset too short for regression check: num_records={num_records}"

    # records 9152..9154 inclusive
    chunk_view = samples[9152:9155]
    assert int(chunk_view.shape[0]) == 3

    p = 1
    for i, record in enumerate((9152, 9153, 9154)):
        seed_a = int(chunk_view["seed_t"]["action_id"][i, p])
        ref_a = int(chunk_view["ref_t1"]["action_id"][i, p])
        ref_hitlag = int(chunk_view["ref_t1"]["hitlag"][i, p])
        ref_hitstun = int(chunk_view["ref_t1"]["hitstun"][i, p])
        assert seed_a == 69, f"record={record} p={p} expected seed_t.action_id=69, got {seed_a}"
        assert ref_a == 69, f"record={record} p={p} expected ref_t1.action_id=69, got {ref_a}"
        assert ref_hitlag == 0, f"record={record} p={p} expected ref_t1.hitlag=0, got {ref_hitlag}"
        assert ref_hitstun == 0, f"record={record} p={p} expected ref_t1.hitstun=0, got {ref_hitstun}"

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=3, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((3, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((3, input_stride), dtype=np.uint8)
        input_bytes = np.empty((3, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((3, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(3, seed_stride)
        prev_input_bytes[:] = np.frombuffer(chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            3, input_stride
        )
        input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            3, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        for i, record in enumerate((9152, 9153, 9154)):
            out_a = int(out["action_id"][i, p])
            assert out_a == 69, f"record={record} p={p} expected out_t1.action_id=69, got {out_a}"
    finally:
        binding.destroy(handle)

