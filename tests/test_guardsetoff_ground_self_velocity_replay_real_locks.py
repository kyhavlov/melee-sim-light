from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_seed_ground: float
    expected_ref_ground: float
    expected_ref_air: float
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=4044,
            p=1,
            expected_seed_ground=0.945753276348114,
            expected_ref_ground=-0.5800000429153442,
            expected_ref_air=0.8657532334327698,
            note="AGN laser shield-hit GuardSetOff entry keeps prior grounded self_vel.x separate from recoil gr_vel",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            target_record=3493,
            p=0,
            expected_seed_ground=-2.1725001335144043,
            expected_ref_ground=-0.5800000429153442,
            expected_ref_air=-2.0124998092651367,
            note="TBK laser shield-hit GuardSetOff entry keeps prior grounded self_vel.x separate from recoil gr_vel",
        ),
    ],
)
def test_guardsetoff_entry_replay_real_splits_ground_recoil_and_self_velocity(case: _Case) -> None:
    # Replay-real lock for grounded GuardSetOff entry after item shield-hit:
    # - ftColl_80076CBC / the item shield-hit path feeds ftCo_80092F2C's GuardSetOff recoil lane
    #   (`gr_vel`, sign via `specialn_facing_dir`),
    # - but Fighter_procUpdate's grounded self_vel.x does not collapse onto that recoil lane on the
    #   same hitlag entry row, so replay-visible `speed_ground_x_self` and `speed_air_x_self`
    #   legitimately diverge there.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.target_record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    p = case.p

    assert int(seed["action_id"][p]) != 181, case.note
    assert int(ref["action_id"][p]) == 181, case.note

    assert float(seed["speed_ground_x_self"][p]) == pytest.approx(case.expected_seed_ground), case.note
    assert float(seed["speed_air_x_self"][p]) == pytest.approx(case.expected_seed_ground), case.note

    assert float(ref["speed_ground_x_self"][p]) == pytest.approx(case.expected_ref_ground), case.note
    assert float(ref["speed_air_x_self"][p]) == pytest.approx(case.expected_ref_air), case.note
    assert float(ref["speed_ground_x_self"][p]) != pytest.approx(float(ref["speed_air_x_self"][p])), case.note
