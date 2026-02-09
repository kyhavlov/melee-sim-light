from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            784,
            0,
            0x00B3,  # Guard hold snapshot that previously drifted to GuardOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            115,
            1,
            0x00B6,  # GuardReflect powershield snapshot that previously dropped to GuardSetOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2275,
            0,
            0x00B6,  # GuardReflect no-submotion lane.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            5167,
            0,
            0x0019,  # Hitlist stale-latch row that previously synthesized hitlag/hitstun.
        ),
    ],
)
def test_seed_eq_guardrail_rows_stay_replay_exact(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    ref = row["ref_t1"][0]
    for field in ("action_id", "hitlag", "hitstun", "instance_id", "on_ground", "ground_id"):
        got = int(out[field][0, p])
        exp = int(ref[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert out["state_flags"][0, p].tolist() == ref["state_flags"][p].tolist(), (
        f"record={record} p={p} field=state_flags expected={ref['state_flags'][p].tolist()} "
        f"got={out['state_flags'][0, p].tolist()}"
    )
