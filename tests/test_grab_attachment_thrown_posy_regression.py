from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    try:
        binding = importlib.import_module("msl_binding")
    except ModuleNotFoundError:
        pytest.skip("missing local artifact: msl_binding extension")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        ref = row["ref_t1"].reshape(-1)[0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "victim", "pos_x_max_err", "pos_y_max_err"),
        [
            # Frame-6 regression rows where the prior repeated spike was centered.
            ("AttachedGoodNaturedGuanaco.slpz", 5972, 0, 0.7, 0.05),
            ("TreasuredBackKangaroo.slpz", 6823, 1, 0.7, 0.05),
            # Keep original frame-7 coverage as a broad "no return of spike" guard.
            ("AttachedGoodNaturedGuanaco.slpz", 5973, 0, 3.0, 0.6),
            ("TreasuredBackKangaroo.slpz", 6824, 1, 3.0, 0.6),
        ],
)
def test_thrownhi_attachment_matches_ref_position_tight(
    dataset_name: str,
    record: int,
    victim: int,
    pos_x_max_err: float,
    pos_y_max_err: float,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"replays/validation/cardinal_1.0_recent/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    # Replay-real preconditions for this regression slice.
    assert int(row["seed_t"]["action_id"][0, victim]) == 241
    assert int(row["ref_t1"]["action_id"][0, victim]) == 241
    # `ref_t1` compare rows do not carry grab_owner_port in the current schema; lock attachment
    # using the seeded per-player owner link for this frame.
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) != 0xFF
    # These two records have zero hitlag/hitstun for both active players.
    for p in (0, 1):
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out, ref = _run_record(dataset_path, record)

    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])

    got_pos_x = np.float32(out["pos_x"][0, victim])
    ref_pos_x = np.float32(ref["pos_x"][victim])
    got_pos_y = np.float32(out["pos_y"][0, victim])
    ref_pos_y = np.float32(ref["pos_y"][victim])

    # Regression guard for the prior ThrownHi attachment spikes:
    # - Frame-6 offenders in this slice had a repeatable ~9.38 abs_err on pos_y before the
    #   thrown-anchor proxy correction.
    # - Keep frame-6 checks tight and frame-7 checks broad enough to guard against regressions.
    assert float(np.abs(got_pos_x - ref_pos_x)) < pos_x_max_err
    assert float(np.abs(got_pos_y - ref_pos_y)) < pos_y_max_err
