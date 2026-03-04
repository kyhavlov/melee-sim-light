from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _BlasterGunDamageExitCase:
    dataset_rel: str
    target_record: int
    owner_port: int
    note: str


def _count_owner_gun_items(row, owner_port: int) -> int:
    count = 0
    for item in row["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) != owner_port:
            continue
        if int(item["type"]) not in (74, 75):
            continue
        count += 1
    return count


def _is_damage_action(action_id: int) -> bool:
    return action_id in {
        0x26,  # DamageFall
        0x4B,
        0x4C,
        0x4D,
        0x4E,
        0x4F,
        0x50,
        0x51,
        0x52,
        0x53,
        0x54,
        0x55,
        0x56,
        0x57,
        0x58,
        0x59,
        0x5A,
        0x5B,
    }


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2132,
            owner_port=1,
            note="GAT blaster gun stale carry on SpecialAirNLoop->DamageFlyTop transition",
        ),
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=9611,
            owner_port=1,
            note="GAT blaster gun stale carry on SpecialAirNStart->DamageAir2 transition",
        ),
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=4412,
            owner_port=1,
            note="TBK blaster gun stale carry on SpecialAirNLoop->DamageAir2 transition",
        ),
    ],
)
def test_blaster_gun_damage_exit_target_pm1_both_players_strict_lock(
    case: _BlasterGunDamageExitCase,
) -> None:
    # Replay-real target+/-1 strict lock for post-combat blaster-gun stale-carry clear in
    # src/items.c::items_update_post_combat.
    #
    # Decomp refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
    # - refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    owner = int(case.owner_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # Lane preconditions:
    # - seed row still has owner-attached blaster gun item;
    # - ref row has no owner blaster gun item at t+1;
    # - owner transitions into a Damage* action on t+1.
    assert _count_owner_gun_items(seed, owner) >= 1, case.note
    assert _count_owner_gun_items(ref, owner) == 0, case.note
    assert _is_damage_action(int(ref["action_id"][owner])), case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
