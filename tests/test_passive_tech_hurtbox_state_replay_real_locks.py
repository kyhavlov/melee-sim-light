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
class _PassiveTechHurtboxCase:
    dataset_rel: str
    port: int
    negative_control: int
    target_minus_1: int
    target: int
    target_plus_1: int
    target_action_id: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _PassiveTechHurtboxCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            port=1,
            negative_control=263,
            target_minus_1=264,
            target=265,
            target_plus_1=266,
            target_action_id=199,
            note="GAT DamageFlyTop -> Passive target family",
        ),
        _PassiveTechHurtboxCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            port=0,
            negative_control=3262,
            target_minus_1=3263,
            target=3264,
            target_plus_1=3265,
            target_action_id=201,
            note="AGN DamageFlyTop -> PassiveStandB target family",
        ),
    ],
)
def test_passive_tech_entry_hurtbox_state_target_family(case: _PassiveTechHurtboxCase) -> None:
    # Replay-real lock for DamageFly landing -> Passive / PassiveStand hurtbox-state ownership:
    # - ftCo_80090184 resolves the tech callback on grounded DamageFly contact.
    # - Passive / PassiveStand entry uses Fighter_ChangeMotionState but does not immediately run
    #   ftAnim_8006EBA4, while the replay-visible t+1 frame 0 already carries the new state's
    #   hurt-status table for these tech entries.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_8009872C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
    # data/scripts/{fox,falco}.bin (MSLFTSC1 set_hit_status timelines)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = int(case.port)
    for record in (case.negative_control, case.target_minus_1, case.target, case.target_plus_1):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    target = samples[case.target : case.target + 1]
    target_seed = target["seed_t"][0]
    target_ref = target["ref_t1"][0]
    assert int(target_seed["action_id"][p]) == 90, case.note  # DamageFlyTop
    assert int(target_seed["action_frame"][p]) >= 36, case.note
    assert int(target_seed["hurtbox_state"][p]) == 0, case.note
    assert int(target_ref["action_id"][p]) == int(case.target_action_id), case.note
    assert int(target_ref["action_frame"][p]) == 0, case.note
    assert int(target_ref["animation_index"][p]) == int(case.target_action_id), case.note
    assert int(target_ref["hurtbox_state"][p]) == 2, case.note

    locked_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
        "state_flags",
    )

    for record in (case.negative_control, case.target_minus_1, case.target, case.target_plus_1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in locked_fields:
            got = out_row[field][p]
            want = ref_row[field][p]
            if field == "state_flags":
                assert list(map(int, got)) == list(map(int, want)), (
                    f"record={record} p={p} field={field} expected={list(map(int, want))} "
                    f"got={list(map(int, got))}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p={p} field={field} expected={int(want)} got={int(got)}"
                )
