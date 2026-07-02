from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    negative_record: int
    target_record: int
    p: int
    ref_action_id: int
    ref_animation_index: int
    note: str


_CASES = (
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
        negative_record=6114,
        target_record=6116,
        p=1,
        ref_action_id=67,
        ref_animation_index=70,
        note="QGD DamageFall c-stick AttackAirB family",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        negative_record=4645,
        target_record=4647,
        p=0,
        ref_action_id=65,
        ref_animation_index=68,
        note="TBK DamageFall A-button AttackAirN family",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}")
def test_damagefall_attackair_target_pm1_with_negative_control(case: _Case) -> None:
    # Replay-real lock for DamageFall aerial attack interrupt parity:
    # - ftCo_DamageFall_IASA consults the common airborne attack input path.
    # - ftCo_AttackAir_CheckInput accepts both A-edge and C-stick edge sources.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p
    for rec in (
        case.negative_record,
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
    ):
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed_t = target["seed_t"]
    ref_t1 = target["ref_t1"]
    assert int(seed_t["action_id"][p]) == 38, case.note  # DamageFall
    assert int(ref_t1["action_id"][p]) == int(case.ref_action_id), case.note
    assert int(ref_t1["animation_index"][p]) == int(case.ref_animation_index), case.note

    for rec in (
        case.negative_record,
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
    ):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "instance_id",
            "on_ground",
            "jumps_left",
            "facing",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: record={rec} field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
