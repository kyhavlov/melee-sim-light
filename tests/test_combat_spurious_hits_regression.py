from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_spurious_body_hitstun_not_applied_treasuredbackkangaroo_record_1075_p1() -> None:
    # Locks in a suite offender where a spurious BODY hit was being applied to p=1 even though
    # the replay ref has hitlag/hitstun == 0 (seed==ref for those fields).
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
    record = 1075
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: this regression is for false-positive hits when seed==ref (t -> t+1) expects no hit.
    assert int(row["seed_t"]["hitlag"][0, 1]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(row["seed_t"]["hitstun"][0, 1]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 1]) == 0
    assert int(row["seed_t"]["action_id"][0, 1]) == int(row["ref_t1"]["action_id"][0, 1])
    assert int(row["seed_t"]["animation_index"][0, 1]) == int(row["ref_t1"]["animation_index"][0, 1])

    expected_action = int(row["ref_t1"]["action_id"][0, 1])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 1])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 1])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 1])

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 1])
        got_anim = int(out["animation_index"][0, 1])
        got_hitlag = int(out["hitlag"][0, 1])
        got_hitstun = int(out["hitstun"][0, 1])

        assert got_action == expected_action, f"record=1075 p=1 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=1075 p=1 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=1075 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=1075 p=1 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_hurtbox_state_not_overwritten_on_entry_frame_gracefulattachedturtle_record_201_p1() -> None:
    # Locks in a suite offender where we were incorrectly outputting hurtbox_state=2 when the replay
    # ref has 0 (seed==ref for hurtbox_state), typically caused by applying move-induced hit status
    # on the same frame as a post-Anim motion-state transition.
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
    record = 201
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["hurtbox_state"][0, 1]) == 0
    assert int(row["ref_t1"]["hurtbox_state"][0, 1]) == 0

    expected_hurtbox_state = int(row["ref_t1"]["hurtbox_state"][0, 1])

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["hurtbox_state"][0, 1])

        assert got == expected_hurtbox_state, (
            f"record=201 p=1 expected hurtbox_state={expected_hurtbox_state}, got {got}"
        )
    finally:
        binding.destroy(handle)

