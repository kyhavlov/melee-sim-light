from __future__ import annotations

import importlib
import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            1397,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            201,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            10011,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            7481,
            1,
        ),
    ],
)
def test_landing_does_not_spuriously_enter_specialn_start(dataset_rel: str, record: int, p: int) -> None:
    # Regression lock for the seed==ref Landing (42) -> SpecialN Start (341) cluster:
    # the replay stays in Landing at t+1, but the sim used to enter blaster immediately.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Preconditions (replay-real): seed==ref Landing at t+1 and no hitlag/hitstun at t+1.
    seed_frame = int(row["seed_t"]["frame_id"][0])
    ref_frame = int(row["ref_t1"]["frame_id"][0])
    seed_action = int(row["seed_t"]["action_id"][0, p])
    ref_action = int(row["ref_t1"]["action_id"][0, p])
    assert seed_action == 42
    assert ref_action == 42
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    # Input sanity: B is pressed on this frame (pressed edge), but Landing is still not interruptible.
    # Source of truth for button bits: src/buttons.h::MSL_BUTTON_B.
    btn_b = 0x0200
    prev_btn = int(row["prev_input_t"]["p"]["buttons"][0, p])
    cur_btn = int(row["input_t"]["p"]["buttons"][0, p])
    pressed = cur_btn & (~prev_btn) & 0xFFFF
    assert (pressed & btn_b) != 0
    assert (prev_btn & btn_b) == 0
    assert (cur_btn & btn_b) != 0

    # Ensure we're actually in the landing-lag window (i.e., the decomp Landing IASA early-return
    # branch), so the test can’t pass accidentally if the record drifts.
    seed_action_frame = int(row["seed_t"]["action_frame"][0, p])
    cid = int(row["seed_t"]["char_id"][0, p])
    if cid == 1:
        char = "fox"
    elif cid == 22:
        char = "falco"
    else:
        raise AssertionError(f"unexpected char_id for this suite regression: {cid} (record={record} p={p})")
    landing_lag_frames = int(json.loads((root / f"data/characters/{char}.json").read_text())["landing_lag_frames"])
    assert seed_action_frame < landing_lag_frames, (
        f"expected seed_t.action_frame<{landing_lag_frames} in Landing lag window, "
        f"got {seed_action_frame} (dataset={dataset_rel} record={record} p={p} seed_frame={seed_frame} ref_frame={ref_frame})"
    )

    expected_action = ref_action
    expected_anim = int(row["ref_t1"]["animation_index"][0, p])
    expected_af = int(row["ref_t1"]["action_frame"][0, p])

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))

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
    got_anim = int(out["animation_index"][0, p])
    got_af = int(out["action_frame"][0, p])

    assert got_action == expected_action, f"record={record} p={p} expected action_id={expected_action}, got {got_action}"
    assert got_anim == expected_anim, f"record={record} p={p} expected animation_index={expected_anim}, got {got_anim}"
    assert got_af == expected_af, f"record={record} p={p} expected action_frame={expected_af}, got {got_af}"
