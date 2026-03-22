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
    record: int
    attacker_p: int
    victim_p: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        record=3761,
        attacker_p=0,
        victim_p=1,
        note="hitstun-backed combo victim reseed fallback",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        record=7518,
        attacker_p=1,
        victim_p=0,
        note="combo-timer-backed combo victim reseed fallback",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}:rec{c.record}:p{c.attacker_p}")
def test_combo_victim_reseed_fallback_rows_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for the fallback x2094 reseed bridge:
    # - ftColl_800763C0 continuation compares the stored victim pointer and current attack id.
    # - ftColl_800764DC / ftCo_8008F744 keep that pointer live while the victim remains in active
    #   combo context (victim hitstun or victim combo timer).
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_800764DC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, f"dataset too short for record={case.record}"

    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    attacker_p = int(case.attacker_p)
    victim_p = int(case.victim_p)

    assert int(seed["combo_count"][attacker_p]) > 0, case.note
    assert int(seed["combo_victim_port"][attacker_p]) == 0xFF, case.note
    assert int(seed["last_hit_by"][victim_p]) == attacker_p, case.note

    if attacker_p == 0:
        assert int(seed["hitstun"][victim_p]) > 0, case.note
        assert int(seed["combo_timer_x2098"][victim_p]) == 0, case.note
        assert int(ref["combo_count"][attacker_p]) == 0, case.note
    else:
        assert int(seed["hitstun"][victim_p]) == 0, case.note
        assert int(seed["combo_timer_x2098"][victim_p]) > 0, case.note
        assert int(ref["combo_count"][attacker_p]) == int(seed["combo_count"][attacker_p]) + 1, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, attacker_p)
    for field in ("combo_count", "last_attack_landed", "last_hit_by"):
        assert int(out_row[field][attacker_p]) == int(ref_row[field][attacker_p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][attacker_p])} "
            f"got={int(out_row[field][attacker_p])}"
        )
