from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player"),
    [
        (
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            4050,
            1,
        ),
        (
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            9314,
            1,
        ),
        (
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            3082,
            0,
        ),
    ],
)
def test_rebirthwait_does_not_spuriously_enter_fall(dataset_rel: str, record: int, player: int) -> None:
    # Regression lock for the dominant seed==ref action_id mismatch cluster:
    # ref action_id = RebirthWait (0x000D), out action_id = Fall (0x001D).
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed assertions (record t state).
    assert int(row["seed_t"]["action_id"][0, player]) == 0x000D
    assert int(row["ref_t1"]["action_id"][0, player]) == 0x000D

    # Preconditions: keep combat timers neutral (0) so the action_id assertion can't be
    # accidentally satisfied via combat-timer gating divergence.
    assert int(row["seed_t"]["hitlag"][0, player]) == 0
    assert int(row["ref_t1"]["hitlag"][0, player]) == 0
    assert int(row["seed_t"]["hitstun"][0, player]) == 0
    assert int(row["ref_t1"]["hitstun"][0, player]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, player])
    expected_on_ground = int(row["ref_t1"]["on_ground"][0, player])
    expected_ground_id = int(row["ref_t1"]["ground_id"][0, player])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, player])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, player])
    assert expected_hitlag == 0
    assert expected_hitstun == 0

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
        got_action = int(out["action_id"][0, player])
        got_on_ground = int(out["on_ground"][0, player])
        got_ground_id = int(out["ground_id"][0, player])
        got_hitlag = int(out["hitlag"][0, player])
        got_hitstun = int(out["hitstun"][0, player])

        assert (
            got_action == expected_action
        ), f"dataset={dataset_rel} record={record} p={player} expected action_id={expected_action}, got {got_action}"
        assert (
            got_on_ground == expected_on_ground
        ), f"dataset={dataset_rel} record={record} p={player} expected on_ground={expected_on_ground}, got {got_on_ground}"
        assert (
            got_ground_id == expected_ground_id
        ), f"dataset={dataset_rel} record={record} p={player} expected ground_id={expected_ground_id}, got {got_ground_id}"
        assert (
            got_hitlag == expected_hitlag
        ), f"dataset={dataset_rel} record={record} p={player} expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert (
            got_hitstun == expected_hitstun
        ), f"dataset={dataset_rel} record={record} p={player} expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)

