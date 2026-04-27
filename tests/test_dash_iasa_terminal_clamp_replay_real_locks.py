from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


_QGD = (
    "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
    "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
)


@dataclass(frozen=True)
class _Case:
    record: int
    port: int
    note: str


_CASES = (
    _Case(
        record=8609,
        port=0,
        note="QGD Dash frame-5 root-motion exit clamp before Dash_IASA terminal scalar",
    ),
    _Case(
        record=4512,
        port=1,
        note="QGD Dash anim-end Wait->Turn control stays exact",
    ),
    _Case(
        record=5968,
        port=0,
        note="QGD late Dash->Turn terminal-scalar control stays exact",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}_p{c.port}")
def test_dash_iasa_terminal_clamp_replay_real_locks(case: _Case) -> None:
    # Replay-real lock for Fighter_ChangeMotionState's Dash root-motion exit clamp:
    # - Dash carries root-motion flags in the previous motion snapshot.
    # - Entering a non-root destination such as Turn clamps `fp->gr_vel` to
    #   `co_attrs.dash_run_terminal_velocity` before Dash_IASA resumes and applies its terminal
    #   velocity scalar.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / _QGD
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_QGD}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, (
        f"dataset too short for lock row: record={case.record}"
    )

    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = case.port
    if case.record == 8609:
        assert int(seed["action_id"][p]) == 20, case.note
        assert int(seed["action_frame"][p]) == 5, case.note
        assert int(seed["animation_index"][p]) == 12, case.note
        assert int(seed["facing"][p]) == 0, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == 83, case.note
        assert int(ref["action_id"][p]) == 18, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 10, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    for field in ("action_id", "action_frame", "animation_index", "facing", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )
    for field in ("pos_x", "speed_ground_x_self", "speed_air_x_self"):
        assert float(out_row[field][p]) == pytest.approx(float(ref_row[field][p]), abs=1e-6), (
            f"{case.note}: field={field} expected={float(ref_row[field][p]):.8f} "
            f"got={float(out_row[field][p]):.8f}"
        )
