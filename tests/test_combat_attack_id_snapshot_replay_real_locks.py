from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
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

        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            5088,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            6012,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            815,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            2259,
            1,
        ),
    ],
)
def test_same_frame_damage_does_not_reset_last_attack_landed(dataset_rel: str, record: int, p: int) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for regression check: num_records={int(samples.shape[0])}"

    row = samples[record : record + 1]

    # Replay-real preconditions for the decomp-ordering lock:
    # - the attacker is already carrying a concrete landed attack id at seed,
    # - the sim previously rewrote t+1 to FtMoveId_Default (1) after same-frame damage-state entry.
    assert int(row["seed_t"]["last_attack_landed"][0, p]) != 0
    assert int(row["ref_t1"]["last_attack_landed"][0, p]) != 1

    out = _one_step_out_compare(ds=ds, row=row)
    got_last_attack = int(out["last_attack_landed"][0, p])
    exp_last_attack = int(row["ref_t1"]["last_attack_landed"][0, p])
    assert got_last_attack == exp_last_attack, (
        f"record={record} p={p} expected last_attack_landed={exp_last_attack}, got {got_last_attack}"
    )

    got_combo = int(out["combo_count"][0, p])
    exp_combo = int(row["ref_t1"]["combo_count"][0, p])
    assert got_combo == exp_combo, f"record={record} p={p} expected combo_count={exp_combo}, got {got_combo}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            5355,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            1758,
            1,
        ),
    ],
)
def test_item_domain_last_attack_does_not_inherit_body_default_id_fallback(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for regression check: num_records={int(samples.shape[0])}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]

    # Replay-real guard for non-BODY bookkeeping:
    # - fighter attack_id is default (1),
    # - attacker still carries a prior concrete last_attack_landed,
    # - an owned item is present this frame,
    # - ref_t1 advances last_attack_landed in the item attack-id domain.
    assert int(seed["attack_id"][p]) == 1
    assert int(seed["last_attack_landed"][p]) != 0
    assert any(int(item["exists"]) and int(item["owner"]) == p for item in seed["items"])
    assert int(row["ref_t1"]["last_attack_landed"][0, p]) != int(seed["last_attack_landed"][p])

    out = _one_step_out_compare(ds=ds, row=row)
    got_last_attack = int(out["last_attack_landed"][0, p])
    exp_last_attack = int(row["ref_t1"]["last_attack_landed"][0, p])
    assert got_last_attack == exp_last_attack, (
        f"record={record} p={p} expected item-domain last_attack_landed={exp_last_attack}, "
        f"got {got_last_attack}"
    )
