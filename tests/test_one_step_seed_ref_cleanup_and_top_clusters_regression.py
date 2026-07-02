from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _ActionCase:
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    seed_hitstun: int
    expected_action: int
    expected_anim: int


@dataclass(frozen=True)
class _ActionFrameCase:
    dataset_rel: str
    record: int
    p: int


_BASE = "replays/validation/cardinal_1.0_recent"


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/stages/final_destination.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        # Category A lock: seed==ref action_id mismatch 354->252, keep SpecialHiHoldAir (354).
        _ActionCase(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 2757, 1, 354, 354, 0, 354, 308),
        _ActionCase(f"{_BASE}/QuerulousGrandDinosaur.slpz", 5549, 1, 354, 354, 0, 354, 308),
        _ActionCase(f"{_BASE}/TreasuredBackKangaroo.slpz", 3206, 1, 354, 354, 0, 354, 308),
        # Category B lock: top unfiltered action_id cluster 38->88 should transition to DamageFall.
        _ActionCase(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 5663, 1, 88, 38, 1, 38, 29),
        _ActionCase(f"{_BASE}/QuerulousGrandDinosaur.slpz", 711, 1, 88, 38, 1, 38, 29),
        _ActionCase(f"{_BASE}/TreasuredBackKangaroo.slpz", 1751, 0, 88, 38, 1, 38, 29),
    ],
)
def test_one_step_action_id_cluster_locks(case: _ActionCase) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action)
    assert int(row["ref_t1"]["action_id"][0, p]) == int(case.ref_action)
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == int(case.seed_hitstun)
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(case.expected_action)
    assert int(out["animation_index"][p]) == int(case.expected_anim)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ActionFrameCase(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 334, 1),
        _ActionFrameCase(f"{_BASE}/GracefulAttachedTurtle.slpz", 1382, 0),
        _ActionFrameCase(f"{_BASE}/QuerulousGrandDinosaur.slpz", 2478, 1),
        _ActionFrameCase(f"{_BASE}/TreasuredBackKangaroo.slpz", 1590, 0),
    ],
)
def test_guardon_guard_action_frame_stays_minus1_with_no_submotion(case: _ActionFrameCase) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    # Seed==ref action_frame cluster: (-1 -> 0) with GuardOn -> Guard and no submotion.
    assert int(row["seed_t"]["action_frame"][0, p]) == -1
    assert int(row["ref_t1"]["action_frame"][0, p]) == -1
    assert int(row["seed_t"]["action_id"][0, p]) == 178  # GuardOn
    assert int(row["ref_t1"]["action_id"][0, p]) == 179  # Guard
    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == 0xFFFFFFFF
    assert int(out["action_frame"][p]) == -1


# Known remaining guard-family mismatch (out of scope for this slice):
# - GracefulAttachedTurtle.slpz rec=3599 p=0
# - QuerulousGrandDinosaur.slpz rec=2571 p=1
# Both are Guard snapshots (aid=179, anim=-1/frame=-1) where out transitions to GuardSetOff
# (aid=181, anim=40/frame=0) while replay ref remains Guard (179). They also appear in
# seed==ref action_id top_triples as ref->out=179->181 (count=2 suite-wide).
# This is a broader Guard IASA/release timing mismatch, not the no-submotion GuardOn->Guard
# shape issue locked above.
