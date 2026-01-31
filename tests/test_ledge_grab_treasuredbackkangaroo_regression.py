from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_ledge_grab_treasuredbackkangaroo_1806_1807_regression() -> None:
    # Locks in the ledge-grab timing ordering around TreasuredBackKangaroo records 1806/1807:
    # - record 1806: stay in 352 (no CliffCatch)
    # - record 1807: transition into 252 (CliffCatch)
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > 1807, f"dataset too short for regression check: num_records={num_records}"

    chunk_view = samples[1806:1808]
    assert int(chunk_view.shape[0]) == 2

    # Seed assertions (record t state).
    assert int(chunk_view["seed_t"]["action_id"][0, 0]) == 352
    assert int(chunk_view["seed_t"]["action_id"][1, 0]) == 352

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=2, num_players=int(ds.header["num_players"]))

    seed_bytes = np.empty((2, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((2, input_stride), dtype=np.uint8)
    input_bytes = np.empty((2, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((2, compare_stride), dtype=np.uint8)

    seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(2, seed_stride)
    prev_input_bytes[:] = np.frombuffer(chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        2, input_stride
    )
    input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(2, input_stride)

    binding.reseed_seed(handle, seed_bytes)
    binding.step_input(handle, prev_input_bytes, input_bytes)
    binding.write_compare(handle, out_compare_bytes)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    out_action_1806_p0 = int(out["action_id"][0, 0])
    out_action_1807_p0 = int(out["action_id"][1, 0])

    assert out_action_1806_p0 == 352, f"record=1806 p=0 expected action_id=352, got {out_action_1806_p0}"
    assert out_action_1807_p0 == 252, f"record=1807 p=0 expected action_id=252 (CliffCatch), got {out_action_1807_p0}"
