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
    record: int
    note: str


_CASES = (
    _Case(record=6382, note="QGD AttackHi3->GuardOn family nearby negative control"),
    _Case(record=6383, note="QGD AttackHi3->GuardOn family target-1 row"),
    _Case(record=6384, note="QGD AttackHi3->GuardOn family target row"),
    _Case(record=6385, note="QGD AttackHi3->GuardOn family target+1 row"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_attackhi3_allow_interrupt_held_l_enters_guardon_qgd_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for AttackHi3 IASA -> GuardOn ordering:
    # - ftCo_AttackHi3_IASA gates on allow_interrupt, then delegates into ftCo_Wait_IASA.
    # - ftCo_Wait_IASA checks guard entry (ftCo_80091A4C) before the locomotion subset.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 6384:
        assert int(seed["action_id"][p]) == 56, case.note
        assert int(seed["action_frame"][p]) == 22, case.note
        assert int(seed["animation_index"][p]) == 58, case.note
        assert int(ref["action_id"][p]) == 178, case.note
        assert int(ref["animation_index"][p]) == 0xFFFFFFFF, case.note
        assert int(ref["on_ground"][p]) == 1, case.note
        assert int(ref["facing"][p]) == 0, case.note
        assert int(ref["jumps_left"][p]) == 2, case.note
        assert int(row["prev_input_t"][0]["p"][p]["buttons"]) == 64, case.note
        assert int(row["input_t"][0]["p"][p]["buttons"]) == 64, case.note
        assert int(row["prev_input_t"][0]["p"][p]["l"]) == 255, case.note
        assert int(row["input_t"][0]["p"][p]["l"]) == 255, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    for i in range(5):
        assert int(out_row["state_flags"][p][i]) == int(ref_row["state_flags"][p][i]), (
            f"{case.note}: state_flags[{i}] expected={int(ref_row['state_flags'][p][i])} "
            f"got={int(out_row['state_flags'][p][i])}"
        )

    for field in ("on_ground", "facing", "jumps_left"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    if record == 6384:
        assert int(out_row["on_ground"][p]) == 1, case.note
        assert int(out_row["facing"][p]) == 0, case.note
        assert int(out_row["jumps_left"][p]) == 2, case.note
