from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _EscapeEndCase:
    dataset_rel: str
    record: int
    player: int
    note: str


_BASE = Path("datasets/aggregate_recent/replays/validation")
_ACT_ESCAPE_N = 235
_ACT_GUARD_ON = 178


def _step_one_record(row: np.ndarray, num_players: int):
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(num_players))
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _EscapeEndCase(
            "aggregate_recent/MotionlessAggressiveJay.msl",
            7546,
            0,
            "EscapeN anim-end destination Wait checks GuardOn before down-held Squat",
        ),
        _EscapeEndCase(
            "dream_land_recent/ShadyDecimalStarling.msl",
            6646,
            1,
            "EscapeN destination Wait keeps shield-owned GuardOn priority on platforms too",
        ),
    ],
)
def test_escapen_anim_end_destination_wait_guard_preempts_squat(case: _EscapeEndCase) -> None:
    # Source owner:
    # - EscapeN_Anim can enter Wait before the current Fighter_procUpdate input callback finishes.
    # - Destination Wait_IASA runs ftCo_80091A4C/GuardOn before ftCo_Squat_CheckInput, so a
    #   down-held shield row must not enter Squat just because the stick also satisfies crouch.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_CheckInput
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / case.dataset_rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / case.dataset_rel}")

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= int(case.record):
        pytest.skip(f"dataset too short for record {case.record}: {path}")

    row = ds.samples[case.record : case.record + 1]
    p = int(case.player)
    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_ESCAPE_N, case.note
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_GUARD_ON, case.note

    out = _step_one_record(row, int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == _ACT_GUARD_ON, case.note
