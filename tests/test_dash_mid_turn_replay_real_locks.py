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
    record: int
    note: str


_CASES = (
    _Case(record=6813, note="AGN mid Dash->Turn family nearby negative control"),
    _Case(record=6814, note="AGN mid Dash->Turn family target-1 row"),
    _Case(record=6815, note="AGN mid Dash->Turn family target row"),
    _Case(record=6816, note="AGN mid Dash->Turn family target+1 row"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_dash_mid_iasa_turn_agn_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for mid Dash_IASA opposite-turn ordering parity:
    # - In the x44<x<=x4C branch, ftCo_Dash_IASA checks ftCo_Dash_CheckInput before fn_800CAF78.
    # - Opposite-facing x3C/x40 stick input therefore enters Turn instead of KneeBend.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 6815:
        assert int(seed["action_id"][p]) == 20, case.note
        assert int(seed["action_frame"][p]) == 4, case.note
        assert int(seed["animation_index"][p]) == 12, case.note
        assert int(ref["action_id"][p]) == 18, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 10, case.note
        assert int(row["input_t"][0]["p"][p]["buttons"]) == 0x0800, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == 64, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "jumps_left", "facing"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )


@pytest.mark.integration
def test_dash_mid_iasa_opposite_turn_preempts_guardreflect_replay_real_lock() -> None:
    # Replay-real lock for Dash_IASA callback ordering:
    # - Fox is in Dash with a fresh L edge and a strong opposite-facing stick flick.
    # - ftCo_Dash_IASA checks ftCo_Dash_CheckInput before the guard helper in this branch, so
    #   vanilla enters Turn rather than GuardReflect.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80091A4C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 4142
    p = 0
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(seed["action_frame"][p]) == 5
    assert int(seed["animation_index"][p]) == 12
    assert int(row["input_t"][0]["p"][p]["buttons"]) & 0x40
    assert int(row["input_t"][0]["p"][p]["main_x"]) < 0
    assert int(ref["action_id"][p]) == 18  # Turn
    assert int(ref["animation_index"][p]) == 10

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"Dash_CheckInput turn preempts guard: field={field} "
            f"expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "jumps_left", "facing"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"Dash_CheckInput turn preempts guard: stable field={field} "
            f"expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )
