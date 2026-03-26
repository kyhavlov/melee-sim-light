from __future__ import annotations

from dataclasses import dataclass
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
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    tag: str
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    ref_facing: int


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_CAPTURE_CASES = [
    _Case("AGN:2421:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 2421, 0, 235, 226, 1),
    _Case("GAT:367:p1", f"{_BASE}/GracefulAttachedTurtle.msl", 367, 1, 178, 226, 1),
    _Case("TBK:5769:p1", f"{_BASE}/TreasuredBackKangaroo.msl", 5769, 1, 90, 223, 1),
]
_TURN_CASES = [
    _Case("AGN:3817:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 3817, 0, 18, 56, 1),
    _Case("TBK:473:p0", f"{_BASE}/TreasuredBackKangaroo.msl", 473, 0, 18, 63, 0),
    _Case("QGD:3671:p1", f"{_BASE}/QuerulousGrandDinosaur.msl", 3671, 1, 18, 53, 1),
]


def _assert_exact_t1_parity(*, out: np.void, ref: np.void, p: int, case_tag: str) -> None:
    for field in (
        "action_id",
        "action_frame",
        "facing",
        "on_ground",
        "hitlag",
        "hitstun",
        "animation_index",
        "last_hit_by",
        "last_attack_landed",
        "combo_count",
    ):
        assert int(out[field][p]) == int(ref[field][p]), f"{case_tag} {field} parity"
    assert np.array_equal(out["state_flags"][p], ref["state_flags"][p]), f"{case_tag} state_flags parity"


@pytest.mark.integration
@pytest.mark.parametrize("case", _CAPTURE_CASES)
def test_capture_pulled_entry_facing_replay_real_locks(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == case.seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == case.ref_action
    assert int(row["ref_t1"]["facing"][0, p]) == case.ref_facing

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    _assert_exact_t1_parity(out=out, ref=row["ref_t1"][0], p=p, case_tag=case.tag)


@pytest.mark.integration
@pytest.mark.parametrize("case", _TURN_CASES)
def test_turn_iasa_attack_entry_facing_replay_real_locks(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == case.seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == case.ref_action
    assert int(row["ref_t1"]["facing"][0, p]) == case.ref_facing

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    _assert_exact_t1_parity(out=out, ref=row["ref_t1"][0], p=p, case_tag=case.tag)
