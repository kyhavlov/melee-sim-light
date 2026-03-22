from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    attacker_p: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=438,
            attacker_p=0,
            note="ThrowLw attached victim bookkeeping carry row 438",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=4089,
            attacker_p=0,
            note="ThrowLw attached victim bookkeeping carry row 4089",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=8106,
            attacker_p=0,
            note="ThrowLw attached victim bookkeeping carry row 8106",
        ),
    ],
)
def test_throwlw_attached_item_bookkeeping_target_pm1(case: _Case) -> None:
    # Replay-real lock for attached-victim item bookkeeping in combat_apply_item_hit():
    # - Throw-side item hits on an attached victim still update attacker-side item-domain stale/combo
    #   bookkeeping through ftColl_8007646C -> ftColl_800763C0 even when victim state stays Thrown*.
    # refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed = samples[target_record]["seed_t"]
    attacker = int(case.attacker_p)
    victim = 1 - attacker
    assert int(seed["action_id"][attacker]) == 222, case.note  # ThrowLw
    assert int(seed["grab_owner_port"][victim]) == attacker, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, attacker)
        assert int(out_row["last_attack_landed"][attacker]) == int(
            ref_row["last_attack_landed"][attacker]
        ), (
            f"{case.note}: record={rec} field=last_attack_landed "
            f"expected={int(ref_row['last_attack_landed'][attacker])} "
            f"got={int(out_row['last_attack_landed'][attacker])}"
        )
