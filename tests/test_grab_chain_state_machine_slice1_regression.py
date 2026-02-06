from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_BASE_REL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
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
        ref = row["ref_t1"].reshape(-1)[0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "attacker", "victim"),
    [
        ("AttachedGoodNaturedGuanaco.msl", 203, 1, 0),
        ("GracefulAttachedTurtle.msl", 218, 0, 1),
        ("QuerulousGrandDinosaur.msl", 4063, 0, 1),
        ("TreasuredBackKangaroo.msl", 5069, 1, 0),
    ],
)
def test_catch_connect_enters_catchpull_and_capturepulled(
    dataset_name: str,
    record: int,
    attacker: int,
    victim: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay preconditions for this lock (grab connect frame).
    assert int(row["seed_t"]["action_id"][0, attacker]) == 212
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 213
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == 0xFF

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "attacker", "victim", "expected_throw", "expected_thrown"),
    [
        ("AttachedGoodNaturedGuanaco.msl", 568, 0, 1, 221, 241),
        ("GracefulAttachedTurtle.msl", 221, 0, 1, 221, 241),
        ("QuerulousGrandDinosaur.msl", 421, 0, 1, 222, 242),
        ("TreasuredBackKangaroo.msl", 438, 0, 1, 221, 241),
    ],
)
def test_catchwait_throw_iasa_enters_throw_and_thrown(
    dataset_name: str,
    record: int,
    attacker: int,
    victim: int,
    expected_throw: int,
    expected_thrown: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay preconditions for this lock (CatchWait throw-select frame).
    assert int(row["seed_t"]["action_id"][0, attacker]) == 216
    assert int(row["seed_t"]["action_id"][0, victim]) == 227
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row["seed_t"]["grab_owner_port"][0, attacker]) == 0xFF
    assert int(row["ref_t1"]["action_id"][0, attacker]) == expected_throw
    assert int(row["ref_t1"]["action_id"][0, victim]) == expected_thrown
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "expected_action_id"),
    [
        ("AttachedGoodNaturedGuanaco.msl", 568, 0, 221),
        ("GracefulAttachedTurtle.msl", 448, 0, 221),
        ("GracefulAttachedTurtle.msl", 2507, 1, 220),
    ],
)
def test_catchwait_throw_entry_preserves_action_frame_parity(
    dataset_name: str,
    record: int,
    p: int,
    expected_action_id: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay preconditions for this lock (seed CatchWait, throw entry on next frame).
    assert int(row["seed_t"]["action_id"][0, p]) == 216
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_id"][0, p]) == expected_action_id
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][0, p]) == int(ref["action_frame"][p])
