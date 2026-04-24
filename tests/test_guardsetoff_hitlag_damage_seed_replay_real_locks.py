from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_seed_damage_min: int
    expected_ref_action_frame: int
    expected_ref_state_flags_3: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1212,
            p=1,
            expected_seed_damage_min=9,
            expected_ref_action_frame=2,
            expected_ref_state_flags_3=0,
            note="AGN row A carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1477,
            p=0,
            expected_seed_damage_min=9,
            expected_ref_action_frame=3,
            expected_ref_state_flags_3=0,
            note="AGN row B carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2393,
            p=0,
            expected_seed_damage_min=1,
            expected_ref_action_frame=6,
            expected_ref_state_flags_3=32,
            note="AGN row C still carries the GuardSetOff entry hitlag-damage lower bound through powershield-active hitlag after the x221C_b1 lane is corrected",
        ),
    ],
)
def test_guardsetoff_hitlag_damage_seed_lane_locks_blockers_and_adjacent_controls(case: _Case) -> None:
    # Foundational F02 seed-owner locks:
    # - GuardSetOff entry rate in ftCo_80092F2C reads fp->x19A4, a hidden shield-hit int damage lane.
    # - Slippi does not expose x19A4 directly, so preprocessing carries a causal lower bound from
    #   the segment's entry hitlag, while the causal frame-speed derivation reconstructs the hidden
    #   timebase owner needed for action_frame parity on the hitlag-exit row.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        assert int(samples.shape[0]) > rec, f"dataset too short: record={rec}"
        seed = samples[rec]["seed_t"]
        assert int(seed["guard_setoff_hitlag_damage_min"][p]) == case.expected_seed_damage_min, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note
    assert int(out_target["action_frame"][p]) == int(ref_target["action_frame"][p]), case.note
    assert int(ref_target["state_flags"][p, 3]) == case.expected_ref_state_flags_3, case.note
    assert int(out_target["state_flags"][p, 3]) == int(ref_target["state_flags"][p, 3]), case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
def test_guardsetoff_damage_lane_seeds_post_hitlag_attackair_rehit_suppression() -> None:
    # Replay-real aggregate lock for the GuardSetOff damage lane's fighter-hitlist bridge:
    # - ftColl_80076CBC / ftColl_80076808 latch the accepted same-group shield victim in the
    #   HitCapsule, not just while the defender's visible hitlag scalar is nonzero.
    # - A reseed on the first post-hitlag GuardSetOff row still needs that hidden victims_1 carry
    #   to prevent the active AttackAirN hitbox from immediately re-entering shield hitlag.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 10271
    defender = 0
    attacker = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][defender]) == 181  # GuardSetOff
    assert int(seed["hitlag"][defender]) == 0
    assert int(seed["guard_setoff_hitlag_damage_min"][defender]) == 9
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed["hitlag"][attacker]) == 0

    _, ref_attacker, out_attacker = _run_one_step_row(dataset_path, record, attacker)
    assert int(ref_attacker["hitlag"][attacker]) == 0
    assert int(out_attacker["hitlag"][attacker]) == 0
    assert int(out_attacker["state_flags"][attacker, 1]) == int(ref_attacker["state_flags"][attacker, 1])
