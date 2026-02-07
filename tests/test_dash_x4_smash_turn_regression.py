from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_dash_x4_seed_eq_ref_rows_stay_dash() -> None:
    # Regression lock for seed==ref action_id rows:
    # - seed action_id == 20 (Dash)
    # - ref_t1 action_id == 20 (Dash)
    # - previous sim path turned early to 18 (Turn) due missing mv.co.dash.x4 latch semantics.
    #
    # Locked rows from baseline triage (suite aggregate 20/20/18 count=2):
    # - AttachedGoodNaturedGuanaco.msl rec=5738 p=0
    # - TreasuredBackKangaroo.msl rec=5521 p=1
    root = Path(__file__).resolve().parents[1]
    base = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
    cases = [
        (f"{base}/AttachedGoodNaturedGuanaco.msl", 5738, 0),
        (f"{base}/TreasuredBackKangaroo.msl", 5521, 1),
    ]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for rel, record, p in cases:
        dataset_path = root / rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {rel}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        assert int(samples.shape[0]) > record, f"dataset too short for regression check: {rel}"
        row = samples[record : record + 1]

        assert int(row["seed_t"]["action_id"][0, p]) == 20
        assert int(row["ref_t1"]["action_id"][0, p]) == 20
        assert int(row["seed_t"]["on_ground"][0, p]) == 1
        assert int(row["ref_t1"]["on_ground"][0, p]) == 1
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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
            assert int(out["action_id"][p]) == 20, f"{rel} rec={record} p={p}"
            assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][0, p]), (
                f"{rel} rec={record} p={p}"
            )
            assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][0, p]), (
                f"{rel} rec={record} p={p}"
            )
        finally:
            binding.destroy(handle)

