from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(*, dataset_rel: str, record: int, p: int) -> tuple[np.ndarray, np.ndarray]:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short: num_records={num_records} record={record}"

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
def test_downboundu_stays_downboundu_attachedgoodnaturedguanaco_record_2101_p0() -> None:
    # Seed==ref cluster regression:
    # ref=DownBoundU (183) -> out=DamageFlyLw (89) at record=2101 p0.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    record = 2101
    p = 0

    dataset_path = Path(__file__).resolve().parents[1] / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 183
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


@pytest.mark.integration
def test_downboundu_stays_downboundu_gracefulattachedturtle_record_2317_p1() -> None:
    # Seed==ref cluster regression:
    # ref=DownBoundU (183) -> out=DamageFlyLw (89) at record=2317 p1.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    record = 2317
    p = 1

    dataset_path = Path(__file__).resolve().parents[1] / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 183
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


@pytest.mark.integration
def test_damageflyhi_stays_damageflyhi_attachedgoodnaturedguanaco_record_1695_p1() -> None:
    # Seed==ref cluster regression:
    # ref=DamageFlyHi (87) -> out=DamageFlyLw (89) at record=1695 p1.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    record = 1695
    p = 1

    dataset_path = Path(__file__).resolve().parents[1] / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 87
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 35

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
