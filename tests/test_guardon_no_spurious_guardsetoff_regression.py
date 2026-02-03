from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_tbk_guardon_no_spurious_guardsetoff_records_4144_4145_p0() -> None:
    # Locks in the remaining TBK seed==ref action_id mismatch cluster:
    # ref stays GuardOn (178) but the sim previously entered GuardSetOff (181) due to a
    # false-positive Falco laser->shield hit.
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

    records = [4144, 4145]
    p = 0
    for r in records:
        assert int(samples.shape[0]) > r, f"dataset too short for regression check: record={r}"

    rows = samples[records]

    for i, record in enumerate(records):
        seed_action = int(rows["seed_t"]["action_id"][i, p])
        ref_action = int(rows["ref_t1"]["action_id"][i, p])
        assert seed_action == 178, f"record={record} p={p} expected seed_t.action_id=178, got {seed_action}"
        assert ref_action == 178, f"record={record} p={p} expected ref_t1.action_id=178, got {ref_action}"

        ref_hitlag = int(rows["ref_t1"]["hitlag"][i, p])
        ref_hitstun = int(rows["ref_t1"]["hitstun"][i, p])
        assert ref_hitlag == 0, f"record={record} p={p} expected ref_t1.hitlag=0, got {ref_hitlag}"
        assert ref_hitstun == 0, f"record={record} p={p} expected ref_t1.hitstun=0, got {ref_hitstun}"

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=len(records), num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((len(records), seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((len(records), input_stride), dtype=np.uint8)
        input_bytes = np.empty((len(records), input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((len(records), compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(rows["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            len(records), seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(rows["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            len(records), input_stride
        )
        input_bytes[:] = np.frombuffer(rows["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            len(records), input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        for i, record in enumerate(records):
            got_action = int(out["action_id"][i, p])
            got_hitlag = int(out["hitlag"][i, p])
            got_hitstun = int(out["hitstun"][i, p])

            expected_action = int(rows["ref_t1"]["action_id"][i, p])
            expected_hitlag = int(rows["ref_t1"]["hitlag"][i, p])
            expected_hitstun = int(rows["ref_t1"]["hitstun"][i, p])

            assert got_action == expected_action, (
                f"record={record} p={p} expected action_id={expected_action}, got {got_action}"
            )
            assert got_hitlag == expected_hitlag, (
                f"record={record} p={p} expected hitlag={expected_hitlag}, got {got_hitlag}"
            )
            assert got_hitstun == expected_hitstun, (
                f"record={record} p={p} expected hitstun={expected_hitstun}, got {got_hitstun}"
            )

            got_shield_hp_u32 = np.asarray(out["shield_hp"][i, p], dtype=np.float32).view(np.uint32)
            ref_shield_hp_u32 = np.asarray(rows["ref_t1"]["shield_hp"][i, p], dtype=np.float32).view(np.uint32)
            assert int(got_shield_hp_u32) == int(ref_shield_hp_u32), (
                f"record={record} p={p} expected shield_hp f32 bits=0x{int(ref_shield_hp_u32):08x}, "
                f"got 0x{int(got_shield_hp_u32):08x}"
            )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_tbk_laser_shield_hit_still_happens_record_1248_p0() -> None:
    # Negative regression: ensure we did not "turn off" real laser->shield hits while fixing
    # spurious GuardSetOff transitions.
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

    # A TBK record where ref_t1 enters GuardSetOff with hitlag and the Falco laser despawns.
    record = 1248
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for regression check: record={record}"

    row = samples[record : record + 1]

    # Preconditions: ref shows a shield-hit outcome with lasers despawning.
    assert int(row["ref_t1"]["action_id"][0, p]) == 181
    assert int(row["ref_t1"]["hitlag"][0, p]) > 0
    assert float(row["ref_t1"]["shield_hp"][0, p]) < float(row["seed_t"]["shield_hp"][0, p])
    seed_laser_iids = {
        int(it["instance_id"])
        for it in row["seed_t"]["items"][0]
        if int(it["exists"]) and int(it["type"]) in (54, 55)
    }
    ref_laser_iids = {
        int(it["instance_id"])
        for it in row["ref_t1"]["items"][0]
        if int(it["exists"]) and int(it["type"]) in (54, 55)
    }
    assert seed_laser_iids and not ref_laser_iids, f"expected lasers to despawn on shield hit: {seed_laser_iids=}"

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

        expected_action = int(row["ref_t1"]["action_id"][0, p])
        expected_hitlag = int(row["ref_t1"]["hitlag"][0, p])
        expected_hitstun = int(row["ref_t1"]["hitstun"][0, p])
        expected_items = row["ref_t1"]["items"][0]

        got_action = int(out["action_id"][0, p])
        got_hitlag = int(out["hitlag"][0, p])
        got_hitstun = int(out["hitstun"][0, p])

        assert got_action == expected_action, f"record={record} p={p} expected action_id={expected_action}, got {got_action}"
        assert got_hitlag == expected_hitlag, f"record={record} p={p} expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, (
            f"record={record} p={p} expected hitstun={expected_hitstun}, got {got_hitstun}"
        )

        seed_shield_hp = float(row["seed_t"]["shield_hp"][0, p])
        ref_shield_hp = float(row["ref_t1"]["shield_hp"][0, p])
        got_shield_hp = float(out["shield_hp"][0, p])
        assert got_shield_hp < seed_shield_hp, (
            f"record={record} p={p} expected shield_hp to drop on shield hit, seed={seed_shield_hp}, got={got_shield_hp}"
        )
        assert abs(got_shield_hp - ref_shield_hp) <= 1e-3, (
            f"record={record} p={p} expected shield_hp~={ref_shield_hp}, got {got_shield_hp}"
        )

        # Laser despawn behavior matches ref (stable item ordering by instance_id is part of dataset contract).
        got_items = out["items"][0]
        assert np.array_equal(got_items["exists"], expected_items["exists"]), f"record={record} items.exists mismatch"
        assert np.array_equal(got_items["type"], expected_items["type"]), f"record={record} items.type mismatch"
        assert np.array_equal(got_items["owner"], expected_items["owner"]), f"record={record} items.owner mismatch"
        assert np.array_equal(
            got_items["instance_id"], expected_items["instance_id"]
        ), f"record={record} items.instance_id mismatch"
    finally:
        binding.destroy(handle)
