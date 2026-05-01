from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(ds, row, *, rollout: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        if rollout:
            binding.reseed_seed_rollout(handle, seed_bytes)
        else:
            binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _field_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    return np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, stride
    )


def _run_rollout(ds, start: int, stop: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start, "seed_t", seed_stride))
        for record in range(start, stop + 1):
            binding.step_input(
                handle,
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_magnify_damage_counter_applies_offscreen_percent_tick_replay_real_lock() -> None:
    # Fighter_procUpdate increments fp->dmg.x1910 while the player is in the offscreen magnifying
    # display and applies p_ftCommonData->x7B4 damage every x7AC frames below x7B0 percent.
    # HilariousVillainousGiraffe rec614 is the 60th live offscreen Falco row: the seed counter is
    # 59, so this one-step must apply the 1% magnify tick without hitlag/action changes.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 614
    p = 1
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][p]) == 366  # Falco SpecialAirLwLoop
    assert int(seed["magnify_damage_counter_x1910"][p]) == 59
    assert int(seed["state_flags"][p, 4]) & 0x80
    assert float(seed["percent"][p]) == pytest.approx(7.0)
    assert float(ref["percent"][p]) == pytest.approx(8.0)

    out = _run_one_step(ds, row)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == 0
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    rollout_out = _run_one_step(ds, row, rollout=True)
    assert float(rollout_out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    # Starting earlier in the same backfilled magnify episode must carry the seeded hidden counter
    # forward until the real tick row.
    early_out = _run_rollout(ds, 588, 614)
    assert float(early_out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_damage_counter_does_not_tick_match_flow_camera_bits() -> None:
    # The replay-visible x221F_b0 bit is also used by Rebirth/dead-flow camera-subject visibility.
    # It is not enough by itself to model ifMagnify offscreen damage.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    candidates = [
        i
        for i in range(int(samples.shape[0]))
        if int(samples[i]["seed_t"]["action_id"][1]) == 12
        and (int(samples[i]["seed_t"]["state_flags"][1, 4]) & 0x80) != 0
    ]
    if not candidates:
        pytest.skip("no Rebirth camera-bit control row in local dataset")
    record = candidates[0]
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 1
    assert int(seed["magnify_damage_counter_x1910"][p]) == 0

    out = _run_one_step(ds, row)
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_does_not_start_from_unproven_camera_rows() -> None:
    # DCC has long x221F_b0/offscreen camera runs with no source-shaped 1% magnify tick in replay.
    # The seed derivation must not turn those rows into a rollout-startable magnify episode.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 2582
    stop = 2641
    p = 1
    seed = ds.samples[start]["seed_t"]
    ref = ds.samples[stop]["ref_t1"]
    assert int(seed["state_flags"][p, 4]) & 0x80
    assert int(seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(seed["magnify_damage_counter_x1910"][p]) == 0

    out = _run_rollout(ds, start, stop)
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)
