from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(dataset_path: Path, record: int):
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = row["ref_t1"][0].copy()
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damage_on_every_hitlag_sdi_target_pm1_rows_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (313, 314, 315)
    p = 0
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for record={rec}"

    target = samples[314 : 315]
    assert int(target["seed_t"]["action_id"][0, p]) == 79
    assert int(target["seed_t"]["hitlag"][0, p]) == 5
    assert int(target["ref_t1"]["hitlag"][0, p]) == 4
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(-17.902629852294922, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(-23.902629852294922, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damage_on_every_hitlag_sdi_explicit_negative_control_row_3097_stays_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3097 : 3098]
    p = 1
    assert int(row["seed_t"]["action_id"][0, p]) == 86
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert float(row["seed_t"]["pos_x"][0, p]) == pytest.approx(float(row["ref_t1"]["pos_x"][0, p]), abs=1e-6)

    out, ref = _run_one_step(dataset_path, 3097)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflylw_hitlag_sdi_target_pm1_and_negative_control_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (3374, 3375, 3376)
    neg = 3373
    p = 1
    for rec in (*rows, neg):
        assert int(samples.shape[0]) > rec, f"dataset too short for record={rec}"

    target = samples[3375 : 3376]
    assert int(target["seed_t"]["action_id"][0, p]) == 89
    assert int(target["seed_t"]["hitlag"][0, p]) == 6
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(-81.57569122314453, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(-87.57569122314453, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    out_neg, ref_neg = _run_one_step(dataset_path, neg)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p])
    assert int(out_neg["hitlag"][p]) == int(ref_neg["hitlag"][p])
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_downdamaged_hitlag_sdi_target_pm1_and_negative_control_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (6049, 6050, 6051)
    neg = 6048
    p = 1
    for rec in (*rows, neg):
        assert int(samples.shape[0]) > rec, f"dataset too short for record={rec}"

    target = samples[6050 : 6051]
    assert int(target["seed_t"]["action_id"][0, p]) == 193
    assert int(target["seed_t"]["hitlag"][0, p]) == 2
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(17.695518493652344, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(23.695518493652344, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        if rec != 6051:
            assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    out_neg, ref_neg = _run_one_step(dataset_path, neg)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p])
    assert int(out_neg["hitlag"][p]) == int(ref_neg["hitlag"][p])
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)
