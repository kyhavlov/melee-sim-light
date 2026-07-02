from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "seed_action_id"),
    [
        (781, 0x00B3),  # Guard
        (5256, 0x00B2),  # GuardOn
    ],
)
def test_guard_does_not_transition_to_guardoff_on_trigger_release(record: int, seed_action_id: int) -> None:
    # Regression lock for Cluster A triage:
    # Guard/GuardOn should not transition to GuardOff in one step on trigger release for these records.
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
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed assertions (record t state).
    assert int(row["seed_t"]["action_id"][0, 0]) == seed_action_id

    # GuardSetOff->Guard carry lane:
    # With explicit guard lockout seeding, some Guard snapshots now carry `x10==0` straight out of
    # GuardSetOff. Those rows are covered by dedicated guard ownership locks elsewhere; this test
    # focuses on the release-latch lane where lockout is still active.
    if seed_action_id == 0x00B3 and int(row["seed_t"]["guard_x10"][0, 0]) == 0:
        pytest.skip("setoff-carry guard snapshot (x10==0) is covered by separate guard ownership locks")

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    assert expected_action == seed_action_id
    assert expected_anim == int(row["seed_t"]["animation_index"][0, 0])

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
    got_action = int(out["action_id"][0, 0])
    got_anim = int(out["animation_index"][0, 0])

    assert got_action == expected_action, f"record={record} p=0 expected action_id={expected_action}, got {got_action}"
    assert (
        got_anim == expected_anim
    ), f"record={record} p=0 expected animation_index={expected_anim}, got {got_anim}"
