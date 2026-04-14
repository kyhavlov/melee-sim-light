from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_throwlw_thrownlw_entry_handoff_target_pm1_reduces_qgd421_position_drift() -> None:
    # Replay-real lock for the proven ThrowLw/ThrownLw entry handoff subset:
    # - ftCo_800DD398 enters ThrowLw and immediately hands the victim into ftCo_800DE3FC.
    # - ftCo_800DB368 reparents victim FtPart_XRotN under the thrower's FtPart_TransN2 before the
    #   thrown accessory callback (ftCo_800DE508) owns victim world placement.
    # - In the low-throw entry slice, the forward residual collapses onto the new attached joint
    #   while the vertical residual remains carried from the pre-entry world.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = 421
    rows = (420, 421, 422)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed = samples[target_record]["seed_t"]
    assert int(seed["action_id"][0]) == 216  # CatchWait
    assert int(seed["action_id"][1]) == 227  # CaptureWaitLw
    assert int(seed["grab_owner_port"][1]) == 0

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, 0)
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=0)
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=1)

    _, ref_target, out_target = _run_one_step_row(dataset_path, target_record, 0)
    y_err = abs(float(out_target["pos_y"][1]) - float(ref_target["pos_y"][1]))
    assert y_err <= 0.40, f"record=421 p=1 pos_y err too large: {y_err}"

    _, ref_follow, out_follow = _run_one_step_row(dataset_path, 422, 0)
    x_err = abs(float(out_follow["pos_x"][1]) - float(ref_follow["pos_x"][1]))
    y_err = abs(float(out_follow["pos_y"][1]) - float(ref_follow["pos_y"][1]))
    assert x_err <= 0.12, f"record=422 p=1 pos_x err too large: {x_err}"
    assert y_err <= 1.0, f"record=422 p=1 pos_y err too large: {y_err}"
