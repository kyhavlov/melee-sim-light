from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_throw_release_victim_enters_damageair3_and_hitlag_record_454() -> None:
    # Locks in a suite offender where a ThrownLw victim transitions into DamageAir* with hitlag on
    # the throw-release frame.
    #
    # Dataset record:
    # - seed_t: p0=ThrowLw (0xDE), p1=ThrownLw (0xF2), grab_owner_port[p1]=0
    # - ref_t1: p1=DamageAir3 (0x56), hitlag=4
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 454
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed assertions (record t state).
    assert int(row["seed_t"]["action_id"][0, 0]) == 0x00DE
    assert int(row["seed_t"]["action_id"][0, 1]) == 0x00F2
    assert int(row["seed_t"]["grab_owner_port"][0, 1]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 1])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 1])
    assert expected_action == 0x0056
    assert expected_hitlag == 4

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 1])
        got_hitlag = int(out["hitlag"][0, 1])

        assert got_action == expected_action, (
            f"record=454 p=1 expected action_id={expected_action}, got {got_action}"
        )
        assert got_hitlag == expected_hitlag, f"record=454 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
    finally:
        binding.destroy(handle)
