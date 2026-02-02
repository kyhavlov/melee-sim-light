from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_record(dataset_path: Path, record: int) -> np.ndarray:
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

        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throwf_hit_while_thrownf_attached_preserves_hitstun_action_anim_record_215() -> None:
    # Locks in ThrowF damage/hitlag while the victim is still ThrownF + attached:
    # - victim remains in ThrownF (no forced Damage* entry)
    # - hitstun/misc-as remains unchanged (Slippi hitstun field stays 0)
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    record = 215
    p_victim = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 1]) == 219  # ThrowF
    assert int(row["seed_t"]["action_id"][0, 0]) == 239  # ThrownF
    assert int(row["seed_t"]["grab_owner_port"][0, 0]) == 1

    expected_action_id = int(row["ref_t1"]["action_id"][0, p_victim])
    expected_anim = int(row["ref_t1"]["animation_index"][0, p_victim])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, p_victim])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, p_victim])
    expected_percent = np.float32(row["ref_t1"]["percent"][0, p_victim])
    assert expected_action_id == 239
    assert expected_hitstun == 0

    out = _run_record(dataset_path, record)
    got_action_id = int(out["action_id"][0, p_victim])
    got_anim = int(out["animation_index"][0, p_victim])
    got_hitstun = int(out["hitstun"][0, p_victim])
    got_hitlag = int(out["hitlag"][0, p_victim])
    got_percent = np.float32(out["percent"][0, p_victim])

    assert got_action_id == expected_action_id
    assert got_anim == expected_anim
    assert got_hitstun == expected_hitstun
    assert got_hitlag == expected_hitlag
    assert got_percent.view(np.uint32) == expected_percent.view(np.uint32)


@pytest.mark.integration
def test_throwf_hit_while_thrownf_attached_preserves_hitstun_action_anim_record_1452() -> None:
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    record = 1452
    p_victim = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 1]) == 219  # ThrowF
    assert int(row["seed_t"]["action_id"][0, 0]) == 239  # ThrownF
    assert int(row["seed_t"]["grab_owner_port"][0, 0]) == 1

    expected_action_id = int(row["ref_t1"]["action_id"][0, p_victim])
    expected_anim = int(row["ref_t1"]["animation_index"][0, p_victim])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, p_victim])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, p_victim])
    expected_percent = np.float32(row["ref_t1"]["percent"][0, p_victim])
    assert expected_action_id == 239
    assert expected_hitstun == 0

    out = _run_record(dataset_path, record)
    got_action_id = int(out["action_id"][0, p_victim])
    got_anim = int(out["animation_index"][0, p_victim])
    got_hitstun = int(out["hitstun"][0, p_victim])
    got_hitlag = int(out["hitlag"][0, p_victim])
    got_percent = np.float32(out["percent"][0, p_victim])

    assert got_action_id == expected_action_id
    assert got_anim == expected_anim
    assert got_hitstun == expected_hitstun
    assert got_hitlag == expected_hitlag
    assert got_percent.view(np.uint32) == expected_percent.view(np.uint32)


@pytest.mark.integration
def test_throwf_hit_while_thrownf_attached_preserves_hitstun_action_anim_record_7768() -> None:
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    record = 7768
    p_victim = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 1]) == 219  # ThrowF
    assert int(row["seed_t"]["action_id"][0, 0]) == 239  # ThrownF
    assert int(row["seed_t"]["grab_owner_port"][0, 0]) == 1

    expected_action_id = int(row["ref_t1"]["action_id"][0, p_victim])
    expected_anim = int(row["ref_t1"]["animation_index"][0, p_victim])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, p_victim])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, p_victim])
    expected_percent = np.float32(row["ref_t1"]["percent"][0, p_victim])
    assert expected_action_id == 239
    assert expected_hitstun == 0

    out = _run_record(dataset_path, record)
    got_action_id = int(out["action_id"][0, p_victim])
    got_anim = int(out["animation_index"][0, p_victim])
    got_hitstun = int(out["hitstun"][0, p_victim])
    got_hitlag = int(out["hitlag"][0, p_victim])
    got_percent = np.float32(out["percent"][0, p_victim])

    assert got_action_id == expected_action_id
    assert got_anim == expected_anim
    assert got_hitstun == expected_hitstun
    assert got_hitlag == expected_hitlag
    assert got_percent.view(np.uint32) == expected_percent.view(np.uint32)
