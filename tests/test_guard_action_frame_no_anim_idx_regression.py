from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_row_one_step_compare(ds, row: np.ndarray) -> np.ndarray:
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
def test_guardon_preserves_action_frame_minus1_when_animation_index_invalid() -> None:
    # Regression lock: AGG rec 191 p1.
    #
    # Preconditions (replay-real seed/ref):
    # - action_id == 178 (GuardOn)
    # - animation_index == 0xFFFFFFFF (no submotion)
    # - action_frame == -1
    #
    # Expected:
    # - out.animation_index stays 0xFFFFFFFF
    # - out.action_frame stays -1 (do not clamp/recompute to 0)
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 191
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 178
    assert int(row["ref_t1"]["action_id"][0, p]) == 178
    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["action_frame"][0, p]) == -1
    assert int(row["ref_t1"]["action_frame"][0, p]) == -1

    out = _run_row_one_step_compare(ds, row)
    got_anim = int(out["animation_index"][0, p])
    got_af = int(out["action_frame"][0, p])

    assert got_anim == 0xFFFFFFFF, f"record={record} p={p} expected animation_index=-1, got {got_anim}"
    assert got_af == -1, f"record={record} p={p} expected action_frame=-1, got {got_af}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            3599,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2571,
            1,
        ),
    ],
)
def test_guard_hold_no_spurious_guardsetoff_no_submotion_seed_rows(
    dataset_rel: str, record: int, p: int
) -> None:
    # Regression lock (seed==ref Guard hold):
    # - seed/ref action_id == 179 (Guard)
    # - seed/ref animation_index == 0xFFFFFFFF (no submotion)
    # - seed/ref action_frame == -1
    # - ref_t1 stays Guard with hitlag/hitstun == 0
    #
    # Expected out: remain in Guard and preserve no-submotion/action_frame.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 179
    assert int(row["ref_t1"]["action_id"][0, p]) == 179
    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["action_frame"][0, p]) == -1
    assert int(row["ref_t1"]["action_frame"][0, p]) == -1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    # Replay-real input precondition: shield is held across prev->cur with no L/R release edge.
    # L/R mask: src/buttons.h (MSL_BUTTON_L|MSL_BUTTON_R = 0x0060).
    lr_mask = 0x0060
    prev_buttons = int(row["prev_input_t"]["p"][0, p]["buttons"])
    cur_buttons = int(row["input_t"]["p"][0, p]["buttons"])
    assert (prev_buttons & lr_mask) != 0 and (cur_buttons & lr_mask) != 0
    assert ((prev_buttons ^ cur_buttons) & lr_mask) == 0

    out = _run_row_one_step_compare(ds, row)

    got_action = int(out["action_id"][0, p])
    got_anim = int(out["animation_index"][0, p])
    got_af = int(out["action_frame"][0, p])
    got_hitlag = int(out["hitlag"][0, p])
    got_hitstun = int(out["hitstun"][0, p])

    assert got_action == 179, f"record={record} p={p} expected action_id=179, got {got_action}"
    assert got_anim == 0xFFFFFFFF, f"record={record} p={p} expected animation_index=-1, got {got_anim}"
    assert got_af == -1, f"record={record} p={p} expected action_frame=-1, got {got_af}"
    assert got_hitlag == 0, f"record={record} p={p} expected hitlag=0, got {got_hitlag}"
    assert got_hitstun == 0, f"record={record} p={p} expected hitstun=0, got {got_hitstun}"
