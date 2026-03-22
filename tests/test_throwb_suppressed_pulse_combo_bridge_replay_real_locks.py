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
    thrower_p: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=9960,
            thrower_p=1,
            note="ThrowB pulse15 suppressed-pulse combo bookkeeping bridge",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=9962,
            thrower_p=1,
            note="ThrowB pulse17 suppressed-pulse combo bookkeeping bridge",
        ),
    ],
)
def test_throwb_suppressed_pulse_combo_bridge_target_pm1(case: _Case) -> None:
    # Replay-real lock for the kept ThrowB bookkeeping bridge:
    # - Throw-side projectile pulses are one-shot script events consumed in ftFx_Throw_Anim.
    # - Confirmed throw-side item hits route combo tracking through ftColl_8007646C -> ftColl_800763C0.
    # - When the pulse is suppressed in an ongoing same-owner throw-laser hitstun context, keep the
    #   attacker-side combo bookkeeping aligned for the uniquely-owned victim.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target = int(case.target_record)
    rows = (target - 1, target, target + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed = samples[target]["seed_t"]
    thrower = int(case.thrower_p)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 220, case.note  # ThrowB
    assert int(seed["combo_count"][thrower]) > 0, case.note
    assert int(seed["throw_pulse_consumed"][thrower]) == 0, case.note
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, thrower)
        for field in ("combo_count", "last_attack_landed"):
            assert int(out_row[field][thrower]) == int(ref_row[field][thrower]), (
                f"{case.note}: record={rec} field={field} "
                f"expected={int(ref_row[field][thrower])} got={int(out_row[field][thrower])}"
            )
