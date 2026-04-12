from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    attacker_port: int
    defender_port: int
    seed_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1430,
            attacker_port=0,
            defender_port=1,
            seed_action=348,
            note="grounded side-B shield contact keeps GuardOn -> GuardSetOff article flow",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6516,
            attacker_port=0,
            defender_port=1,
            seed_action=351,
            note="airborne side-B body hit uses ghostEffectPos[1] article position on hit frame",
        ),
    ],
)
def test_illusion_contact_rows_match_replay_real(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    a = case.attacker_port
    d = case.defender_port

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        seed_t, out_t, ref_t = _step_one_row(dataset_path, record)

        if record == case.target_record:
            assert int(seed_t["action_id"][a]) == case.seed_action, case.note

        for p in (a, d):
            for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id"):
                assert int(out_t[field][p]) == int(ref_t[field][p]), f"{case.note}: record={record} p={p} field={field}"
            assert float(out_t["percent"][p]) == pytest.approx(float(ref_t["percent"][p]), abs=1e-6), (
                f"{case.note}: record={record} p={p} field=percent"
            )
            assert float(out_t["shield_hp"][p]) == pytest.approx(float(ref_t["shield_hp"][p]), abs=1e-6), (
                f"{case.note}: record={record} p={p} field=shield_hp"
            )

        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), (
                f"{case.note}: record={record} slot=0 field={field}"
            )
        if int(ref_t["items"][0]["exists"]):
            for field in ("pos_x", "pos_y"):
                assert float(out_t["items"][0][field]) == pytest.approx(float(ref_t["items"][0][field]), abs=1e-6), (
                    f"{case.note}: record={record} slot=0 field={field}"
                )
