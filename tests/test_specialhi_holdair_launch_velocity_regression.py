from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row_with_rollout_at_record(
    dataset_path: Path, record: int, p: int, *, window_before: int = 24
) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for record={record}"

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    one_step_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
        out_one = out_view[0].copy()
    finally:
        binding.destroy(one_step_handle)

    start = max(0, int(record) - int(window_before))
    rollout_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
        binding.reseed_seed(rollout_handle, seed_bytes)
        for j in range(start, int(record) + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            if j == int(record):
                binding.write_compare(rollout_handle, out_compare_bytes)
        out_roll = out_view[0].copy()
    finally:
        binding.destroy(rollout_handle)

    seed = samples["seed_t"][record]
    ref = samples["ref_t1"][record]
    return seed, out_one, ref, out_roll


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "baseline_abs_pos_y"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            9368,
            0,
            4.308922,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            2905,
            0,
            4.204998,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            7079,
            0,
            3.231899,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            635,
            1,
            2.944418,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            8924,
            1,
            2.944418,
        ),
    ],
)
def test_specialhi_holdair_launch_rows_clear_hold_velocity_and_improve_pos_y_error(
    dataset_rel: str, record: int, p: int, baseline_abs_pos_y: float
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    # Decomp ownership:
    # - HoldAir anim end enters launch (`ftFx_SpecialAirHi_Enter`) in-air.
    # - Launch enter overwrites `fp->self_vel.{x,y}` (does not preserve HoldAir drift velocity).
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter
    # }
    assert int(seed["action_id"][p]) == 354  # ftFx_MS_SpecialHiHoldAir
    assert int(ref["action_id"][p]) == 356  # ftFx_MS_SpecialAirHi
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 356
    assert abs(float(out["speed_y_self"][p])) <= 1e-6

    # Runtime-dominant locks: one-step@t and rollout@t agree for these rows.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert abs(float(out_roll["pos_x"][p]) - float(out["pos_x"][p])) <= 1e-4
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4

    abs_pos_y_err = abs(float(out["pos_y"][p]) - float(ref["pos_y"][p]))
    assert abs_pos_y_err <= (float(baseline_abs_pos_y) - 0.25)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            9367,
            0,
            354,  # HoldAir one frame before launch
            354,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            7246,
            1,
            354,  # HoldAir -> AirHi context lane (rollout-sensitive)
            356,
        ),
    ],
)
def test_specialhi_holdair_launch_context_controls_remain_replay_real(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert np.isfinite(float(out["pos_x"][p]))
    assert np.isfinite(float(out["pos_y"][p]))
    assert np.isfinite(float(ref["pos_x"][p]))
    assert np.isfinite(float(ref["pos_y"][p]))
