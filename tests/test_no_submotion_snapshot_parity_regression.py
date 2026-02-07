from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


def _skip_if_missing_dataset(root: Path, rel: str) -> Path:
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    return dataset_path


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 3599, 0),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 2571, 1),
    ],
)
def test_no_submotion_guard_hold_stays_guard(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = _skip_if_missing_dataset(root, case.dataset_rel)

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(case.record), f"dataset too short: num_records={int(samples.shape[0])}"

    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["action_frame"][0, p]) == -1
    assert int(row["seed_t"]["action_id"][0, p]) == 179
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    assert int(row["ref_t1"]["action_id"][0, p]) == 179
    assert int(row["ref_t1"]["action_frame"][0, p]) == -1
    assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["state_flags"][p, 1]) == int(row["ref_t1"]["state_flags"][0, p, 1])


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 3600, 0),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 2572, 1),
        _Case(f"{_BASE}/TreasuredBackKangaroo.msl", 5559, 0),
    ],
)
def test_no_submotion_guard_snapshot_enters_guardsetoff(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = _skip_if_missing_dataset(root, case.dataset_rel)

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(case.record), f"dataset too short: num_records={int(samples.shape[0])}"

    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["action_frame"][0, p]) == -1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    assert int(row["ref_t1"]["action_id"][0, p]) == 181
    assert int(row["ref_t1"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["animation_index"][0, p]) == 40
    assert int(row["ref_t1"]["hitlag"][0, p]) > 0

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["state_flags"][p, 1]) == int(row["ref_t1"]["state_flags"][0, p, 1])
