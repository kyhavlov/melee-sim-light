from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tools.eval.dataset import read_dataset

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
class _PulseStaleLatchCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    throw_action: int
    thrower_animf: float
    victim_action: int
    expected_throw_pulse_consumed: int
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
        _PulseStaleLatchCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3091,
            thrower_port=0,
            throw_action=221,  # ThrowHi
            thrower_animf=20.0,
            victim_action=90,  # DamageFlyTop
            expected_throw_pulse_consumed=1,
            note="ThrowHi pulse20 carried stale-context lock",
        ),
        _PulseStaleLatchCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=8291,
            thrower_port=1,
            throw_action=220,  # ThrowB
            thrower_animf=15.000000953674316,
            victim_action=88,  # DamageFlyN
            expected_throw_pulse_consumed=1,
            note="ThrowB pulse15 ongoing-hitstun stale-context lock",
        ),
    ],
)
def test_throw_blaster_pulse_stale_latch_target_pm1_both_players(case: _PulseStaleLatchCase) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[case.target_record]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(target["seed_t"]["action_id"][thrower]) == int(case.throw_action), case.note
    assert int(target["seed_t"]["action_id"][victim]) == int(case.victim_action), case.note
    assert abs(float(target["seed_t"]["anim_frame_f32"][thrower]) - float(case.thrower_animf)) <= 1e-6, case.note
    assert int(target["seed_t"]["throw_pulse_consumed"][thrower]) == int(
        case.expected_throw_pulse_consumed
    ), case.note

    # Strict replay-real lock coverage for target-1 / target / target+1 on both players.
    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)
