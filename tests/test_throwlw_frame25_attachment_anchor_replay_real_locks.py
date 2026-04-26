from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _rollout_window(dataset_path: Path, start_record: int, length: int) -> list[tuple[np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) >= start_record + length

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(
            samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, seed_stride).copy()
        binding.reseed_seed(handle, seed_bytes)

        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        out: list[tuple[np.void, np.void]] = []
        for rec in range(start_record, start_record + length):
            prev_input_bytes = np.frombuffer(
                samples[rec : rec + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride).copy()
            input_bytes = np.frombuffer(
                samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride).copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out_row = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref_row = samples[rec]["ref_t1"].copy()
            out.append((out_row, ref_row))
        return out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throwlw_frame25_slowest_rate_attachment_anchor_positive_prh() -> None:
    # Replay-real positive for the slowest supported weight-dependent ThrowLw frame-25 attached
    # callback phase. The old capture-anchor-only rollout stayed too low by >1.0 by record 5635 and
    # cascaded into an early blastzone death.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/debug/fd_mixed_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_window(dataset_path, start_record=5628, length=8)
    out, ref = rows[7]
    victim = 0

    assert int(out["action_id"][victim]) == 242  # ThrownLw
    assert int(ref["action_id"][victim]) == 242
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim])
    assert abs(float(out["pos_x"][victim]) - float(ref["pos_x"][victim])) <= 0.01
    assert abs(float(out["pos_y"][victim]) - float(ref["pos_y"][victim])) <= 0.30


@pytest.mark.integration
def test_throwlw_frame25_faster_victim_rate_keeps_existing_attachment_owner_qgd() -> None:
    # Replay-real negative for the faster Fox-victim ThrowLw rate: QGD must not take the slowest-rate
    # frame-25 vertical TransN2/x1A70 handoff used by PRH.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_window(dataset_path, start_record=8108, length=3)
    out, ref = rows[2]
    victim = 1

    assert int(out["action_id"][victim]) == 242  # ThrownLw
    assert int(ref["action_id"][victim]) == 242
    assert abs(float(out["pos_x"][victim]) - float(ref["pos_x"][victim])) <= 0.01
    # QGD already has a known faster-rate vertical residual in the existing attached-world owner;
    # keep the negative tied to the replay reference instead of a one-sided sentinel.
    assert abs(float(out["pos_y"][victim]) - float(ref["pos_y"][victim])) <= 0.50
