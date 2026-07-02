from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_laser_phantom_hit_record_201_p1() -> None:
    # Locks in a suite offender where a Falco laser was falsely applied to p=1, producing hitlag
    # and a damage-state action transition even though the replay ref has hitlag==0.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {expected_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 201
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: this regression is for false-positive hits when seed==ref (t -> t+1) expects no hit.
    assert int(row["seed_t"]["hitlag"][0, 1]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(row["seed_t"]["action_id"][0, 1]) == int(row["ref_t1"]["action_id"][0, 1])
    assert int(row["seed_t"]["animation_index"][0, 1]) == int(row["ref_t1"]["animation_index"][0, 1])

    expected_action = int(row["ref_t1"]["action_id"][0, 1])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 1])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 1])

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
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
        got_action = int(out["action_id"][0, 1])
        got_hitlag = int(out["hitlag"][0, 1])
        got_anim = int(out["animation_index"][0, 1])

        assert got_action == expected_action, f"record=201 p=1 expected action_id={expected_action}, got {got_action}"
        assert got_hitlag == expected_hitlag, f"record=201 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_anim == expected_anim, (
            f"record=201 p=1 expected animation_index={expected_anim}, got {got_anim}"
        )
    finally:
        binding.destroy(handle)

