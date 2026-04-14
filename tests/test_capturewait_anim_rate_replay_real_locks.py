from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_rollout_window(dataset_path: Path, start_record: int, end_record: int) -> dict[int, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > end_record, "dataset too short for rollout lock window"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    outs: dict[int, np.ndarray] = {}
    try:
        binding.reseed_seed(handle, seed_bytes)
        for rec in range(start_record, end_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            outs[rec] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)
    return outs


@pytest.mark.integration
def test_capturewait_anim_rate_runtime_rollout_matches_qgd_window() -> None:
    # Replay-real lock for the CaptureWaitLw mash-rate runtime owner:
    # - ftCo_CaptureWaitHi/Lw_Anim writes ftAnim_SetAnimRate(x3B4) after the anim advance when
    #   ftCommon_GrabMash succeeds and the internal x2344 hold timer is idle, then keeps the boost
    #   alive for x3B0 frames before restoring rate 1.
    # - The sim already had a reseed continuity bridge for one-step; this lock covers the missing
    #   live rollout owner on the QGD low-throw chain before the later ThrownLw handoff rows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    target_rows = (415, 416, 417, 418, 419, 420)
    for rec in target_rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for record={rec}"

    seed = samples[415]["seed_t"]
    assert int(seed["action_id"][0]) == 216  # CatchWait
    assert int(seed["action_id"][1]) == 227  # CaptureWaitLw
    assert int(seed["grab_owner_port"][1]) == 0
    assert float(seed["frame_speed_mul_f32"][1]) == pytest.approx(1.0)

    # One-step rows around the first live rate jump should stay replay-exact.
    for rec in (415, 416, 417):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, 0)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)

    # Unreseeded rollout from the earlier owner frame must keep the CaptureWaitLw timebase exact.
    outs = _run_rollout_window(dataset_path, 355, 420)
    for rec in target_rows:
        ref_row = samples[rec]["ref_t1"]
        out_row = outs[rec]
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)
