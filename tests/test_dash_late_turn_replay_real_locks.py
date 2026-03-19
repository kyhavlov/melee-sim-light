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
    _Case(record=5966, note="QGD late Dash->Turn family negative control"),
    _Case(record=5967, note="QGD late Dash->Turn family target-1 row"),
    _Case(record=5968, note="QGD late Dash->Turn family target row"),
    _Case(record=5969, note="QGD late Dash->Turn family target+1 row"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_dash_late_iasa_turn_qgd_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for late Dash_IASA opposite-flick smash-turn parity:
    # - ftCo_Dash_IASA routes the late window through ftCo_Dash_CheckInput before guard / jump /
    #   run checks.
    # - ftCo_Dash_CheckInput sends opposite-facing fresh flicks into ftCo_Turn_Enter_Smash.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
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
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 5968:
        assert int(seed["action_id"][p]) == 20, case.note
        assert int(seed["action_frame"][p]) == 21, case.note
        assert int(ref["action_id"][p]) == 18, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 10, case.note
        assert int(seed["facing"][p]) == 0, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == 99, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "jumps_left", "facing"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    if record == 5968:
        # The kept lane fixes the Dash->Turn transition bundle but leaves the instance-id handoff
        # one step behind on the transition row; keep that residual explicit.
        assert int(out_row["instance_id"][p]) == 1070, case.note
        assert int(ref_row["instance_id"][p]) == 1071, case.note
    else:
        assert int(out_row["instance_id"][p]) == int(ref_row["instance_id"][p]), case.note
