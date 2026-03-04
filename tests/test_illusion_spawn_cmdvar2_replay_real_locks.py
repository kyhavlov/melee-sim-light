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
class _IllusionSpawnCase:
    dataset_rel: str
    target_record: int
    owner_port: int
    owner_action: int
    owner_msid: int
    owner_animf: float
    slot: int
    item_type: int
    item_state: int
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
        _IllusionSpawnCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1429,
            owner_port=0,
            owner_action=348,  # ftFx_MS_SpecialS
            owner_msid=302,  # side_ground.main
            owner_animf=1.0,
            slot=0,
            item_type=57,  # Falco Phantasm
            item_state=0,  # ground spawn state from ftLib_800865CC(owner)->ground_or_air
            note="ground side-special cmd_var2 spawn pulse lock",
        ),
        _IllusionSpawnCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=545,
            owner_port=1,
            owner_action=351,  # ftFx_MS_SpecialAirS
            owner_msid=305,  # side_air.main
            owner_animf=1.0,
            slot=0,
            item_type=56,  # Fox Illusion
            item_state=1,  # air spawn state from ftLib_800865CC(owner)->ground_or_air
            note="air side-special cmd_var2 spawn pulse lock",
        ),
        _IllusionSpawnCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=9278,
            owner_port=0,
            owner_action=351,  # ftFx_MS_SpecialAirS (transitions to End at t+1)
            owner_msid=305,  # side_air.main
            owner_animf=1.0,
            slot=0,
            item_type=57,  # Falco Phantasm
            item_state=1,  # air spawn state from ftLib_800865CC(owner)->ground_or_air
            note="air side-special end-entry pulse bridge lock (QGD)",
        ),
        _IllusionSpawnCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6701,
            owner_port=1,
            owner_action=351,  # ftFx_MS_SpecialAirS (transitions to End at t+1)
            owner_msid=305,  # side_air.main
            owner_animf=1.0,
            slot=0,
            item_type=57,  # Falco Phantasm
            item_state=1,  # air spawn state from ftLib_800865CC(owner)->ground_or_air
            note="air side-special end-entry pulse bridge lock (TBK)",
        ),
    ],
)
def test_illusion_spawn_cmdvar2_target_pm1_both_players(case: _IllusionSpawnCase) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[case.target_record]
    owner = int(case.owner_port)
    slot = int(case.slot)

    assert int(target["seed_t"]["action_id"][owner]) == int(case.owner_action), case.note
    assert int(target["seed_t"]["animation_index"][owner]) == int(case.owner_msid), case.note
    assert abs(float(target["seed_t"]["anim_frame_f32"][owner]) - float(case.owner_animf)) <= 1e-6, case.note

    assert int(target["seed_t"]["items"][slot]["exists"]) == 0, case.note
    assert int(target["ref_t1"]["items"][slot]["exists"]) == 1, case.note
    assert int(target["ref_t1"]["items"][slot]["type"]) == int(case.item_type), case.note
    assert int(target["ref_t1"]["items"][slot]["state"]) == int(case.item_state), case.note
    assert int(target["ref_t1"]["items"][slot]["owner"]) == int(case.owner_port), case.note

    # Strict replay-real lock coverage for target-1 / target / target+1 on both players.
    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)

    _, out_target, ref_target = _step_one_row(dataset_path, case.target_record)
    for fld in ("exists", "type", "state", "owner", "instance_id"):
        got = int(out_target["items"][slot][fld])
        exp = int(ref_target["items"][slot][fld])
        assert got == exp, f"{case.note}: slot={slot} field={fld} expected={exp} got={got}"
