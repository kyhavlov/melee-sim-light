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
class _ThrowBContextCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    thrower_animf: float
    victim_action: int
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
        _ThrowBContextCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=9958,
            thrower_port=1,
            thrower_animf=11.999999046325684,
            victim_action=88,  # DamageFlyN
            note="ThrowB hitstun stale-context lock (early pulse window)",
        ),
        _ThrowBContextCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=9968,
            thrower_port=1,
            thrower_animf=25.333335876464844,
            victim_action=86,  # DamageFlyTop
            note="ThrowB hitstun stale-context lock (late pulse window)",
        ),
    ],
)
def test_throwb_stale_context_target_pm1_both_players(case: _ThrowBContextCase) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[case.target_record]
    thrower = int(case.thrower_port)
    victim = 1 - thrower

    # ThrowB stale-context preconditions from src/items.c:
    # - thrower in ThrowB
    # - victim already in hitstun from this thrower's laser shot kind
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    assert int(target["seed_t"]["action_id"][thrower]) == 220, case.note  # ThrowB
    assert int(target["seed_t"]["action_id"][victim]) == int(case.victim_action), case.note
    assert abs(float(target["seed_t"]["anim_frame_f32"][thrower]) - float(case.thrower_animf)) <= 1e-6, case.note

    thrower_char = int(target["seed_t"]["char_id"][thrower])
    shot_kind = 54 if thrower_char == 1 else 55
    assert int(target["seed_t"]["hitstun"][victim]) > 0, case.note
    assert int(target["seed_t"]["last_attack_landed"][victim]) == shot_kind, case.note

    # Strict replay-real lock coverage for target-1 / target / target+1 on both players.
    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)
