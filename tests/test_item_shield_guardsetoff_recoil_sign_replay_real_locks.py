from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    expected_shield_hp: float
    expected_ground: float
    expected_air: float
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=4044,
            p=1,
            expected_shield_hp=58.599998474121094,
            expected_ground=-0.5800000429153442,
            expected_air=0.8657532334327698,
            note="AGN locomotion-origin laser shield-hit uses item contact pos.x for GuardSetOff recoil sign",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=3493,
            p=0,
            expected_shield_hp=58.599998474121094,
            expected_ground=-0.5800000429153442,
            expected_air=-2.0124998092651367,
            note="TBK laser shield-hit sign must use the contact-time item x after the shot advances onto the defender",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=5280,
            p=0,
            expected_shield_hp=58.599998474121094,
            expected_ground=-0.34800004959106445,
            expected_air=-1.0355385541915894,
            note="tilted-shield counterexample still resolves the same item-pos sign owner while preserving carried self_vel.x",
        ),
    ],
)
def test_item_shield_guardsetoff_entry_recoil_sign_replay_real_locks(case: _Case) -> None:
    # Item->shield GuardSetOff entry owner:
    # - ftColl_80077688 writes defender x19AC from defender cur_pos.x versus the item's contact-time
    #   world pos.x,
    # - ftCo_80092F2C consumes that sign into grounded GuardSetOff recoil gr_vel,
    # - the same frozen entry row still keeps grounded self_vel.x separate.
    # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80077688 (0x800778F4..0x80077918)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.p)
    p = case.p

    assert int(ref_row["action_id"][p]) == 181, case.note
    assert int(out_row["action_id"][p]) == 181, case.note
    assert int(ref_row["hitlag"][p]) == 3, case.note
    assert int(out_row["hitlag"][p]) == 3, case.note
    assert float(ref_row["shield_hp"][p]) == pytest.approx(case.expected_shield_hp), case.note
    assert float(out_row["shield_hp"][p]) == pytest.approx(case.expected_shield_hp), case.note

    assert float(ref_row["speed_ground_x_self"][p]) == pytest.approx(case.expected_ground), case.note
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(case.expected_ground), case.note
    assert float(ref_row["speed_air_x_self"][p]) == pytest.approx(case.expected_air), case.note
    assert float(out_row["speed_air_x_self"][p]) == pytest.approx(case.expected_air), case.note
    assert float(out_row["speed_ground_x_self"][p]) != pytest.approx(
        float(out_row["speed_air_x_self"][p])
    ), case.note
