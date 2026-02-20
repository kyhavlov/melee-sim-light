from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

ROOT = Path(__file__).resolve().parents[1]
DATASET_REL = (
    "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
)
LOCK_FIELDS = ("action_id", "action_frame", "animation_index", "instance_id", "hitlag", "hitstun")


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        root / DATASET_REL,
        root / "data/common/ft_common_data.json",
        root / "data/characters/fox.json",
        root / "data/characters/falco.json",
    ]
    missing = [str(path.relative_to(root)) for path in required if not path.exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step(binding, ds, row):
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "player", "seed_action", "ref_action"),
    [
        # Fixed row: C-stick edge no longer spuriously enters AttackAirF/B.
        (3349, 0, 27, 27),
        # Adjacent context control row: local follow-up transition shape remains replay-real.
        (3350, 0, 27, 67),
        # Fixed row: same ownership bug in a second cluster.
        (8293, 0, 25, 25),
    ],
)
def test_attackair_cstick_deadzone_rows_match_replay_real_t1(
    record: int, player: int, seed_action: int, ref_action: int
) -> None:
    binding = pytest.importorskip("msl_binding")
    _skip_if_required_artifacts_missing(ROOT)

    dataset_path = ROOT / DATASET_REL
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for regression check: num_records={samples.shape[0]}"

    row = samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, player]) == seed_action
    assert int(row["ref_t1"]["action_id"][0, player]) == ref_action
    assert int(row["seed_t"]["hitlag"][0, player]) == 0
    assert int(row["seed_t"]["hitstun"][0, player]) == 0
    assert int(row["ref_t1"]["hitlag"][0, player]) == 0
    assert int(row["ref_t1"]["hitstun"][0, player]) == 0

    out = _run_one_step(binding, ds, row)
    for field in LOCK_FIELDS:
        got = int(out[field][0, player])
        expected = int(row["ref_t1"][field][0, player])
        assert got == expected, f"record={record} p={player} expected {field}={expected}, got {got}"
