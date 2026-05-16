from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    attacker_p: int
    victim_p: int
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
            target_record=454,
            attacker_p=0,
            victim_p=1,
            note="ThrowLw release frame into DamageAir3 keeps combo continuation",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=4105,
            attacker_p=0,
            victim_p=1,
            note="ThrowLw release frame into DamageAir2 keeps combo continuation",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=8122,
            attacker_p=0,
            victim_p=1,
            note="ThrowLw release frame into later DamageAir3 keeps combo continuation",
        ),
    ],
)
def test_throwlw_release_combo_continuation_target_pm1_both_players_strict_lock(case: _Case) -> None:
    # Replay-real lock for ThrowLw release-frame item-domain combo continuation:
    # - Live Throw Anim detaches/damages the victim before later item BODY contacts.
    # - Seed/reseed compatibility latches can still expose a pending released victim to the same-frame
    #   blaster hit; it should remain in the throw-laser move-id domain and continue combo_count
    #   through ftColl_8007646C -> ftColl_800763C0 instead of restarting at 1.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_8007646C}
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
    ref = samples[target]["ref_t1"]
    attacker = int(case.attacker_p)
    victim = int(case.victim_p)

    assert int(seed["action_id"][attacker]) == 222, case.note  # ThrowLw
    assert int(seed["action_id"][victim]) == 242, case.note  # ThrownLw
    assert int(seed["grab_owner_port"][victim]) == attacker, case.note
    assert int(seed["combo_count"][attacker]) == 1, case.note
    assert int(seed["last_attack_landed"][attacker]) == 56, case.note
    assert int(seed["combo_victim_port"][attacker]) == 0xFF, case.note
    assert int(ref["combo_count"][attacker]) == 2, case.note
    assert int(ref["action_id"][victim]) in (85, 86), case.note  # DamageAir2 / DamageAir3

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, attacker)
        assert int(out_row["combo_count"][attacker]) == int(ref_row["combo_count"][attacker]), (
            f"{case.note}: record={rec} field=combo_count "
            f"expected={int(ref_row['combo_count'][attacker])} got={int(out_row['combo_count'][attacker])}"
        )
        assert int(out_row["last_attack_landed"][attacker]) == int(
            ref_row["last_attack_landed"][attacker]
        ), (
            f"{case.note}: record={rec} field=last_attack_landed "
            f"expected={int(ref_row['last_attack_landed'][attacker])} "
            f"got={int(out_row['last_attack_landed'][attacker])}"
        )
