from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.replay_buffers_loader import load_replay_buffers

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


_STRICT_FIELDS = (
    "action_id",
    "action_frame",
    "animation_index",
    "hitlag",
    "hitstun",
    "instance_id",
)


@dataclass(frozen=True)
class _FastfallLockCase:
    dataset_rel: str
    target_record: int
    owner_port: int
    seed_action: int
    ref_action: int
    seed_state_flags_1: int
    ref_state_flags_1: int
    seed_hitlag: int
    seed_fall_fast_hitlag_exit_owner: int
    note: str


def _assert_strict_transition_fields_match_ref_all_players(*, out_row, ref_row, record: int) -> None:
    num_players = int(ref_row["num_players"])
    for p in range(num_players):
        for field in _STRICT_FIELDS:
            got = int(out_row[field][p])
            exp = int(ref_row[field][p])
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        got_sf = out_row["state_flags"][p].tolist()
        exp_sf = ref_row["state_flags"][p].tolist()
        assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _FastfallLockCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=3475,
            owner_port=0,
            seed_action=65,  # AttackAirN
            ref_action=65,  # AttackAirN
            seed_state_flags_1=32,  # isHitlag only
            ref_state_flags_1=8,  # isFastFalling after hitlag release
            seed_hitlag=1,
            seed_fall_fast_hitlag_exit_owner=1,
            note="AttackAir post-hitlag fastfall bridge lock",
        ),
        _FastfallLockCase(
            dataset_rel="replays/validation/marth/InternalPowerlessWallaby.slpz",
            target_record=483,
            owner_port=1,
            seed_action=65,  # AttackAirN
            ref_action=65,  # AttackAirN
            seed_state_flags_1=32,  # isHitlag only
            ref_state_flags_1=0,  # no hidden fastfall after neutral-input hitlag exit
            seed_hitlag=1,
            seed_fall_fast_hitlag_exit_owner=0,
            note="AttackAir neutral-input hitlag exit does not inherit hitlag-frozen fastfall",
        ),
        _FastfallLockCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=3087,
            owner_port=0,
            seed_action=29,  # JumpF
            ref_action=344,  # SpecialAirNStart
            seed_state_flags_1=8,  # stale fastfall bit before SpecialAirN entry
            ref_state_flags_1=0,  # cleared by ChangeMotionState (no KeepFastFall)
            seed_hitlag=0,
            seed_fall_fast_hitlag_exit_owner=0,
            note="SpecialAirN entry fastfall clear lock",
        ),
        _FastfallLockCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=8486,
            owner_port=0,
            seed_action=358,  # SpecialHiFall
            ref_action=357,  # SpecialHiLanding
            seed_state_flags_1=8,  # stale fastfall bit at fall->landing handoff
            ref_state_flags_1=0,  # cleared on landing entry ChangeMotionState
            seed_hitlag=0,
            seed_fall_fast_hitlag_exit_owner=0,
            note="SpecialHiLanding entry fastfall clear lock",
        ),
    ],
)
def test_fastfall_special_entry_target_pm1_both_players(case: _FastfallLockCase) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    owner = int(case.owner_port)

    assert int(target["seed_t"]["action_id"][owner]) == int(case.seed_action), case.note
    assert int(target["ref_t1"]["action_id"][owner]) == int(case.ref_action), case.note
    assert int(target["seed_t"]["state_flags"][owner][1]) == int(case.seed_state_flags_1), case.note
    assert int(target["ref_t1"]["state_flags"][owner][1]) == int(case.ref_state_flags_1), case.note
    assert int(target["seed_t"]["hitlag"][owner]) == int(case.seed_hitlag), case.note
    assert int(target["seed_t"]["fall_fast_hitlag_exit_owner"][owner]) == int(
        case.seed_fall_fast_hitlag_exit_owner
    ), case.note

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)
