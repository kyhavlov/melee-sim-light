from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_hitlag_hitstun_state_flags_match_treasuredbackkangaroo_record_815_p0() -> None:
    # Locks in a timing/flag-parity offender where we were diverging on:
    # - hitlag frames left,
    # - hitstun frames left,
    # - and fp+0x221A / fp+0x221C state_flags bits (isHitlag + x221A_b3 + isHitstun)
    # on a damage-hit frame.
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
    record = 815
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]
    p = 0

    expected_hitlag = int(row["ref_t1"]["hitlag"][0, p])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, p])
    expected_flags = row["ref_t1"]["state_flags"][0, p].copy()

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
        got_hitlag = int(out["hitlag"][0, p])
        got_hitstun = int(out["hitstun"][0, p])
        got_flags = out["state_flags"][0, p].copy()

        assert got_hitlag == expected_hitlag, f"record=815 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert (
            got_hitstun == expected_hitstun
        ), f"record=815 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
        assert np.array_equal(got_flags, expected_flags), (
            f"record=815 p=0 expected state_flags={expected_flags.tolist()}, got {got_flags.tolist()}"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_hitlag_hitstun_state_flags_match_attachedgoodnaturedguanaco_record_178_p1() -> None:
    # Locks in a timing/flag-parity offender where hitlag/hitstun were starting on the correct frame,
    # but the sim was not reflecting the associated fp+0x221A / fp+0x221C state_flags bits.
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
    record = 178
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]
    p = 1

    expected_hitlag = int(row["ref_t1"]["hitlag"][0, p])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, p])
    expected_flags = row["ref_t1"]["state_flags"][0, p].copy()

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
        got_hitlag = int(out["hitlag"][0, p])
        got_hitstun = int(out["hitstun"][0, p])
        got_flags = out["state_flags"][0, p].copy()

        assert got_hitlag == expected_hitlag, f"record=178 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert (
            got_hitstun == expected_hitstun
        ), f"record=178 p=1 expected hitstun={expected_hitstun}, got {got_hitstun}"
        assert np.array_equal(got_flags, expected_flags), (
            f"record=178 p=1 expected state_flags={expected_flags.tolist()}, got {got_flags.tolist()}"
        )
    finally:
        binding.destroy(handle)

