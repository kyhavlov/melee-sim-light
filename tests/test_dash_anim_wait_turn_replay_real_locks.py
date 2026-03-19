from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


_CASES = (
    _Case(record=4510, note="QGD Dash anim-end turn family nearby negative control"),
    _Case(record=4511, note="QGD Dash anim-end turn family target-1 row"),
    _Case(record=4512, note="QGD Dash anim-end turn family target row"),
    _Case(record=4513, note="QGD Dash anim-end turn family target+1 row"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_dash_anim_end_wait_turn_qgd_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for Dash_Anim -> Wait destination turn ownership:
    # - ftCo_Dash_Anim enters Wait through ft_8008A2BC when the dash animation expires.
    # - Fighter proc order then dispatches the destination Wait_IASA callback in the same frame,
    #   so held opposite-stick non-flick windows can enter Turn via ftCo_Turn_CheckInput.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 4512:
        assert int(seed["action_id"][p]) == 20, case.note
        assert int(seed["action_frame"][p]) == 21, case.note
        assert int(seed["animation_index"][p]) == 12, case.note
        assert int(ref["action_id"][p]) == 18, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 10, case.note
        assert int(row["prev_input_t"][0]["p"][p]["buttons"]) == 0, case.note
        assert int(row["input_t"][0]["p"][p]["buttons"]) == 0, case.note
        assert int(row["prev_input_t"][0]["p"][p]["main_x"]) == -45, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == -45, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "facing", "jumps_left"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )
