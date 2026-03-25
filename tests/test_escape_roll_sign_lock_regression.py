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


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_TARGET_CASES = [
    _Case("AGG:2040:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 2040, 0),
    _Case("AGG:1810:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 1810, 0),
    _Case("AGG:2041:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 2041, 0),
    _Case("GAT:597:p1", f"{_BASE}/GracefulAttachedTurtle.msl", 597, 1),
    _Case("TBK:245:p0", f"{_BASE}/TreasuredBackKangaroo.msl", 245, 0),
    _Case("TBK:748:p1", f"{_BASE}/TreasuredBackKangaroo.msl", 748, 1),
]
_CONTROL_CASES = [
    _Case("AGG:1811:p0", f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 1811, 0),
    _Case("GAT:598:p1", f"{_BASE}/GracefulAttachedTurtle.msl", 598, 1),
    _Case("TBK:749:p1", f"{_BASE}/TreasuredBackKangaroo.msl", 749, 1),
]


def _assert_roll_row_parity(*, out: np.void, ref: np.void, p: int, case_tag: str) -> None:
    # Replay-real lock for Escape roll root-motion sign ownership (ft_80085030 lane).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Phys
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
    for field in (
        "action_id",
        "action_frame",
        "on_ground",
        "facing",
        "ground_id",
        "animation_index",
        "hitlag",
        "hitstun",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), f"{case_tag} {field} parity"

    assert np.array_equal(out["state_flags"][p], ref["state_flags"][p]), f"{case_tag} state_flags parity"

    for field in ("pos_x", "pos_y", "speed_air_x_self", "speed_ground_x_self", "speed_y_self"):
        assert float(out[field][p]) == pytest.approx(float(ref[field][p]), abs=2e-6), (
            f"{case_tag} {field} parity"
        )


@pytest.mark.integration
@pytest.mark.parametrize("case", _TARGET_CASES)
def test_escape_roll_sign_target_rows_exact_t1_parity(case: _Case) -> None:
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

    # EscapeF lock context (action 233 / submotion 42) with grounded roll progression.
    assert int(row["seed_t"]["action_id"][0, p]) == 233, f"{case.tag} expected seed EscapeF"
    assert int(row["ref_t1"]["action_id"][0, p]) == 233, f"{case.tag} expected ref EscapeF"
    assert int(row["seed_t"]["animation_index"][0, p]) == 42
    assert int(row["ref_t1"]["animation_index"][0, p]) == 42
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    _assert_roll_row_parity(out=out, ref=row["ref_t1"][0], p=p, case_tag=case.tag)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CONTROL_CASES)
def test_escape_roll_sign_adjacent_controls_exact_t1_parity(case: _Case) -> None:
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

    assert int(row["seed_t"]["action_id"][0, p]) == 233, f"{case.tag} expected seed EscapeF"
    assert int(row["ref_t1"]["action_id"][0, p]) == 233, f"{case.tag} expected ref EscapeF"

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    _assert_roll_row_parity(out=out, ref=row["ref_t1"][0], p=p, case_tag=case.tag)
