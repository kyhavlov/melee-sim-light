from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_hitlist_does_not_clear_on_instance_id_change_record_4356() -> None:
    # Regression lock: prevent false-positive fighter hitlag/hitstun when the victim's Slippi-visible
    # `instance_id` changes due to motion-state entry inside the step.
    #
    # The underlying fix keeps rehit suppression latched even if `instance_id` changes, unless the
    # victim is dead/respawning. See src/hitlist.c: hitlist_allows().
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {expected_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 4356
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: this regression is for false-positive hits when hitlag/hitstun should remain unchanged.
    for p in (0, 1):
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == int(row["ref_t1"]["hitstun"][0, 0])
    assert int(row["seed_t"]["hitstun"][0, 1]) == int(row["ref_t1"]["hitstun"][0, 1])

    expected_action_p0 = int(row["ref_t1"]["action_id"][0, 0])
    expected_action_p1 = int(row["ref_t1"]["action_id"][0, 1])
    expected_hitlag_p0 = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitlag_p1 = int(row["ref_t1"]["hitlag"][0, 1])
    expected_hitstun_p0 = int(row["ref_t1"]["hitstun"][0, 0])
    expected_hitstun_p1 = int(row["ref_t1"]["hitstun"][0, 1])

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
        got_action_p0 = int(out["action_id"][0, 0])
        got_action_p1 = int(out["action_id"][0, 1])
        got_hitlag_p0 = int(out["hitlag"][0, 0])
        got_hitlag_p1 = int(out["hitlag"][0, 1])
        got_hitstun_p0 = int(out["hitstun"][0, 0])
        got_hitstun_p1 = int(out["hitstun"][0, 1])

        assert got_action_p0 == expected_action_p0, (
            f"record=4356 p=0 expected action_id={expected_action_p0}, got {got_action_p0}"
        )
        assert got_action_p1 == expected_action_p1, (
            f"record=4356 p=1 expected action_id={expected_action_p1}, got {got_action_p1}"
        )
        assert got_hitlag_p0 == expected_hitlag_p0, (
            f"record=4356 p=0 expected hitlag={expected_hitlag_p0}, got {got_hitlag_p0}"
        )
        assert got_hitlag_p1 == expected_hitlag_p1, (
            f"record=4356 p=1 expected hitlag={expected_hitlag_p1}, got {got_hitlag_p1}"
        )
        assert got_hitstun_p0 == expected_hitstun_p0, (
            f"record=4356 p=0 expected hitstun={expected_hitstun_p0}, got {got_hitstun_p0}"
        )
        assert got_hitstun_p1 == expected_hitstun_p1, (
            f"record=4356 p=1 expected hitstun={expected_hitstun_p1}, got {got_hitstun_p1}"
        )
    finally:
        binding.destroy(handle)

