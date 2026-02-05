from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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
    ("dataset_name", "record", "attacker", "victim", "expected_thrown"),
    [
        # From --debug-float offenders: seed_t.action_id[victim]==227 (CaptureWait) -> Thrown*
        ("AttachedGoodNaturedGuanaco.msl", 4185, 1, 0, 241),
        ("QuerulousGrandDinosaur.msl", 8279, 1, 0, 240),
    ],
)
def test_capturewait_to_thrown_entry_preserves_position_bitwise(
    dataset_name: str,
    record: int,
    attacker: int,
    victim: int,
    expected_thrown: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, victim]) == 227
    assert int(row["ref_t1"]["action_id"][0, victim]) == expected_thrown
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == attacker
    for p in (0, 1):
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out, ref = _run_record(dataset_path, record)

    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])

    # Slice 2A lock: throw entry recomputes offsets to preserve world position across
    # CaptureWait* -> Thrown* motion-state entry.
    got_pos_x = np.float32(out["pos_x"][0, victim]).view(np.uint32)
    seed_pos_x = np.float32(row["seed_t"]["pos_x"][0, victim]).view(np.uint32)
    assert int(got_pos_x) == int(seed_pos_x), (
        f"{dataset_name} record={record} p={victim} pos_x did not preserve seed bits: "
        f"got=0x{int(got_pos_x):08x} seed=0x{int(seed_pos_x):08x}"
    )

    got_pos_y = np.float32(out["pos_y"][0, victim]).view(np.uint32)
    seed_pos_y = np.float32(row["seed_t"]["pos_y"][0, victim]).view(np.uint32)
    assert int(got_pos_y) == int(seed_pos_y), (
        f"{dataset_name} record={record} p={victim} pos_y did not preserve seed bits: "
        f"got=0x{int(got_pos_y):08x} seed=0x{int(seed_pos_y):08x}"
    )
