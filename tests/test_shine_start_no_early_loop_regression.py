from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
@pytest.mark.parametrize(
    ("expected_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            1232,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            4591,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            5298,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            5619,
            1,
        ),
    ],
)
def test_shine_start_no_early_loop_regression(expected_rel: str, record: int, p: int) -> None:
    # Regression lock: some seed==ref snapshots remain in SpecialLwStart (action_id=360) at t+1,
    # but the sim incorrectly enters SpecialLwLoop (361) at t+1 due to Shine Start/Loop timing.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Preconditions: seed==ref on action_id (Start persists); clean hitlag/hitstun cases only.
    assert int(row["seed_t"]["action_id"][0, p]) == 360
    assert int(row["ref_t1"]["action_id"][0, p]) == 360
    # These records are the *last hitlag frame* of Shine Start: hitlag decrements from 1 -> 0 at
    # proc prio 0 (Fighter_8006A1BC), so the animation tick runs this frame (Fighter_8006A360 gate).
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    # Decomp: reflector motion states are entered with anim_speed=1.0f via Fighter_ChangeMotionState.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    assert float(row["seed_t"]["frame_speed_mul_f32"][0, p]) == pytest.approx(1.0)

    expected_action = int(row["ref_t1"]["action_id"][0, p])
    expected_action_frame = int(row["ref_t1"]["action_frame"][0, p])
    expected_anim = int(row["ref_t1"]["animation_index"][0, p])

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
        got_action = int(out["action_id"][0, p])
        got_action_frame = int(out["action_frame"][0, p])
        got_anim = int(out["animation_index"][0, p])

        assert got_action == expected_action, (
            f"record={record} p={p} expected action_id={expected_action}, got {got_action}"
        )
        assert got_action_frame == expected_action_frame, (
            f"record={record} p={p} expected action_frame={expected_action_frame}, got {got_action_frame}"
        )
        assert got_anim == expected_anim, (
            f"record={record} p={p} expected animation_index={expected_anim}, got {got_anim}"
        )
    finally:
        binding.destroy(handle)
