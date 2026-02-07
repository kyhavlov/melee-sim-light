from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 5369, 1),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 2839, 0),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 753, 1),
        _Case(f"{_BASE}/TreasuredBackKangaroo.msl", 2101, 0),
    ],
)
def test_damageair_anim_end_does_not_spuriously_stay_in_damageair(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(case.record), f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    # Locked replay-real preconditions for this cluster:
    # - seed at t is DamageAir1 (84), ref at t+1 is Fall (29) with clean hitlag/hitstun.
    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["ref_t1"]["action_id"][0, p]) == 29
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])


@pytest.mark.integration
def test_damageair_anim_end_same_frame_rehit_stays_damageair1() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2610
    p = 0
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Locked replay-real preconditions:
    # - seed at t is DamageAir1 (84) on its anim-end frame.
    # - replay ref at t+1 is still DamageAir1 because a same-frame hit re-enters damage.
    # Causal note (TBK rec=2610 p0): when laser BODY overlap is evaluated as a current-point probe
    # only, this frame misses the replay-causal same-frame laser contact and sim exits 84->29. With
    # prev->cur swept laser BODY overlap enabled, the contact is present and out stays in DamageAir1.
    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["seed_t"]["action_frame"][0, p]) == 11
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 84
    assert int(row["ref_t1"]["animation_index"][0, p]) == 174
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert int(row["ref_t1"]["hitstun"][0, p]) == 9

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
