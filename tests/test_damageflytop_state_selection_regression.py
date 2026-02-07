from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(row: np.ndarray, num_players: int) -> tuple[np.ndarray, np.ndarray]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=num_players)
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

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = row["ref_t1"].reshape(-1)[0].copy()
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            791,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            1036,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            130,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            2435,
            0,
        ),
    ],
)
def test_damageflytop_selection_regression(dataset_rel: str, record: int, p: int) -> None:
    # Locks ref=DamageFlyTop (90) rows that previously regressed to DamageFlyN (88).
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert record > 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])} record={record}"

    row = samples[record : record + 1]
    prev_row = samples[record - 1]

    # Teacher-forced continuity proxy with available schema: prior ref_t1 should match this seed_t.
    assert int(prev_row["ref_t1"]["action_id"][p]) == int(row["seed_t"]["action_id"][0, p])

    # Replay-real cluster preconditions for this lock.
    assert int(row["ref_t1"]["action_id"][0, p]) == 90
    assert int(row["ref_t1"]["hitlag"][0, p]) > 0
    assert int(row["ref_t1"]["hitstun"][0, p]) > 0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0

    out, ref = _run_one_step(row=row, num_players=int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
