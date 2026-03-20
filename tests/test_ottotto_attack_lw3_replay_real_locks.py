from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(record=3336, note="QGD Ottotto attack family negative control"),
        _Case(record=3337, note="QGD Ottotto attack family target-1"),
        _Case(record=3338, note="QGD Ottotto attack family target"),
        _Case(record=3339, note="QGD Ottotto attack family target+1"),
    ],
)
def test_ottotto_grounded_a_attack_rows_match_replay(case: _Case) -> None:
    # Replay-real lock for the kept Ottotto-only grounded A-attack IASA lane:
    # - ftCo_Ottotto_IASA checks grounded A-attack inputs before jump/dash/turn/walk.
    # - The target row is an Ottotto frame-3 A+forward/down input that should enter AttackS3Lw.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_CheckInput
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

    if record == 3338:
        assert int(seed["action_id"][p]) == 245, case.note  # ftCo_MS_Ottotto
        assert int(seed["animation_index"][p]) == 210, case.note  # ftCo_SM_Ottotto
        assert int(ref["action_id"][p]) == 55, case.note  # ftCo_MS_AttackS3Lw
        assert int(ref["animation_index"][p]) == 57, case.note  # ftCo_SM_AttackS3Lw
        assert int(row["input_t"]["p"]["buttons"][0, p]) == 0x0100, case.note  # A edge
        assert int(row["input_t"]["p"]["main_x"][0, p]) == -45, case.note
        assert int(row["input_t"]["p"]["main_y"][0, p]) == -53, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
    ):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"
