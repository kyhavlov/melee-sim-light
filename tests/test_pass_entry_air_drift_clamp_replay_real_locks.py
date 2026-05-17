from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_PASS = 0x00F4
ACT_GUARD_REFLECT = 0x00B6


def _run_one_step(
    seed: np.ndarray, prev_input: np.ndarray, input_t: np.ndarray, num_players: int
) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = seed.reshape(1).view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = prev_input.reshape(1).view("u1").reshape(1, input_stride).copy()
    input_bytes = input_t.reshape(1).view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def test_guardreflect_platform_pass_entry_clamps_overmax_air_drift_mgs_460() -> None:
    # GuardReflect -> Pass on FoD calls the shared soft-platform pass entry path:
    # ftCo_8009A228 runs ftCommon_8007D5D4, clamps self_vel.x through ftCommon_ClampAirDrift,
    # writes pass y velocity, enters Pass, then Pass_Phys applies one common air-drift step.
    #
    # Without the clamp, this row starts from -0.83169 instead of Falco's -0.83 air-drift cap,
    # leaving a small horizontal drift that later cascades into an FoD platform edge rollout break.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A228
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_ClampAirDrift}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[460]
    p = 1
    assert int(row["seed_t"]["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(row["ref_t1"]["action_id"][p]) == ACT_PASS
    assert float(row["seed_t"]["speed_air_x_self"][p]) < -0.83

    out = _run_one_step(row["seed_t"].copy(), row["prev_input_t"].copy(), row["input_t"].copy(), int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == ACT_PASS
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(row["ref_t1"]["speed_air_x_self"][p]), abs=1e-7)
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)


def test_platform_pass_entry_does_not_clamp_inrange_air_drift_mgs_460_negative() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[460]
    p = 1
    seed = row["seed_t"].copy()
    seed["speed_air_x_self"][p] = np.float32(-0.7)
    seed["speed_ground_x_self"][p] = np.float32(-0.7)

    out = _run_one_step(seed, row["prev_input_t"].copy(), row["input_t"].copy(), int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == ACT_PASS
    assert float(out["speed_air_x_self"][p]) == pytest.approx(-0.68, abs=1e-6)
