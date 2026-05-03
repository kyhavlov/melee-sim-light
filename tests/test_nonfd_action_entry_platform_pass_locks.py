from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _ActionCase:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    ref_action: int
    note: str


_BASE = Path("datasets/aggregate_recent/replays/validation")


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
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            404,
            1,
            344,
            344,
            "SpecialAirNStart locked ECB bottom keeps early platform crossing airborne",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5575,
            1,
            69,
            69,
            "AttackAirLw locked ECB bottom keeps early platform crossing airborne",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5495,
            0,
            39,
            39,
            "Squat pass countdown arm frame does not enter Pass early",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5496,
            0,
            39,
            244,
            "Squat pass countdown enters Pass after the source x470 delay",
        ),
    ],
)
def test_nonfd_action_entry_platform_pass_replay_real_locks(case: _ActionCase) -> None:
    # Source owners:
    # - AttackAir_Coll and Fox/Falco SpecialAirN*_Coll load a locked ECB through mpColl while
    #   CollData_X130_Locked is active after a ground-to-air handoff.
    # - Squat_IASA arms mv.co.squat.x4 through ftCo_80099F9C, returns, then only decrements the
    #   hidden countdown on later frames before calling ftCo_8009A228.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / case.dataset_rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / case.dataset_rel}")

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= int(case.record):
        pytest.skip(f"dataset too short for record {case.record}: {path}")

    row = ds.samples[case.record : case.record + 1]
    p = int(case.player)
    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action), case.note
    assert int(row["ref_t1"]["action_id"][0, p]) == int(case.ref_action), case.note
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0

    out = _step_one_record(row, int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p]), case.note
