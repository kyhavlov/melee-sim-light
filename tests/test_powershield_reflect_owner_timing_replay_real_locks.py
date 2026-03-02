from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

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
class _Case:
    dataset_rel: str
    target_record: int
    spawn_id: int
    item_type: int
    note: str


def _find_item_slot_by_key(items, *, spawn_id: int, item_type: int) -> int:
    for i in range(len(items)):
        it = items[i]
        if int(it["exists"]) != 1:
            continue
        if int(it["spawn_id"]) != int(spawn_id):
            continue
        if int(it["type"]) != int(item_type):
            continue
        return i
    return -1


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
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=4828,
            spawn_id=125,
            item_type=55,
            note="powershield owner timing A",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=6207,
            spawn_id=149,
            item_type=55,
            note="powershield owner timing B",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=7448,
            spawn_id=142,
            item_type=55,
            note="powershield owner timing C",
        ),
    ],
)
def test_powershield_reflect_owner_timing_target_pm1_both_players(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)

    seed_t, out_t, ref_t = _step_one_row(dataset_path, case.target_record)
    slot_seed = _find_item_slot_by_key(seed_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_out = _find_item_slot_by_key(out_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_ref = _find_item_slot_by_key(ref_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert slot_seed >= 0 and slot_out >= 0 and slot_ref >= 0, f"{case.note}: target row item key missing"

    owner_seed = int(seed_t["items"][slot_seed]["owner"])
    owner_out = int(out_t["items"][slot_out]["owner"])
    owner_ref = int(ref_t["items"][slot_ref]["owner"])
    assert owner_out == owner_seed, f"{case.note}: target owner expected seed={owner_seed}, got {owner_out}"
    assert owner_out != owner_ref, f"{case.note}: target owner expected != ref ({owner_ref})"

    assert int(out_t["items"][slot_out]["misc2"]) == 255, f"{case.note}: expected pending-owner marker"
    assert int(out_t["items"][slot_out]["misc3"]) == 1, f"{case.note}: expected pending owner port marker"
