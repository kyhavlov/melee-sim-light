from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_throw_release_transitions_thrownhi_to_damageflytop_record_444() -> None:
    # Locks in throw release -> detach -> apply throw hit -> DamageFly* state entry.
    #
    # replay-buffer record chosen to match a common suite offender shape:
    # - seed_t: p0=ThrowHi (221), p1=ThrownHi (241), grab_owner_port[p1]=0
    # - ref_t1: p1 transitions to DamageFlyTop (90)
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {expected_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 444
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed assertions (record t state).
    assert int(row["seed_t"]["action_id"][0, 0]) == 221
    assert int(row["seed_t"]["action_id"][0, 1]) == 241
    assert int(row["seed_t"]["grab_owner_port"][0, 1]) == 0

    expected = int(row["ref_t1"]["action_id"][0, 1])
    assert expected == 90

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
    prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    )
    input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

    binding.reseed_seed(handle, seed_bytes)
    binding.step_input(handle, prev_input_bytes, input_bytes)
    binding.write_compare(handle, out_compare_bytes)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    got = int(out["action_id"][0, 1])

    assert got == expected, f"record=444 p=1 expected action_id={expected}, got {got}"

