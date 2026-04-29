from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


MSL_ACT_LANDING_AIR_LW = 74
MSL_ACT_GUARD_REFLECT = 182
MSL_ACT_ESCAPE_N = 235
MSL_BUTTON_R = 0x0020
MSL_BUTTON_Y = 0x0400


def _pjo_dataset(root: Path) -> Path:
    return root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"


@pytest.mark.integration
def test_landingair_terminal_wait_iasa_spotdodge_beats_guard_entry_replay_real_lock() -> None:
    # Replay-real lock for LandingAirLw anim-end -> Wait -> Wait_IASA same-frame ordering.
    # Wait_IASA calls ftCo_80099794 before ftCo_80091A4C, so held L/R plus the down-stick gate
    # enters EscapeN before guard/powershield entry can consume the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099794
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _pjo_dataset(root)
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    record = 622
    p = 0
    row = read_dataset(str(dataset_path)).samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    inp = row["input_t"]

    assert int(seed["action_id"][p]) == MSL_ACT_LANDING_AIR_LW
    assert int(seed["action_frame"][p]) == 28
    assert int(inp["p"]["buttons"][p]) & MSL_BUTTON_R
    assert int(inp["p"]["buttons"][p]) & MSL_BUTTON_Y
    assert int(inp["p"]["main_y"][p]) < 0
    assert int(ref["action_id"][p]) == MSL_ACT_ESCAPE_N

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_landingair_terminal_wait_iasa_spotdodge_requires_fresh_down_tilt_gate() -> None:
    # Boundary lock: the Wait_IASA pre-guard helper is not a broad "held shield beats guard" path.
    # When the same replay-real row has a stale y-tilt timer, ftCo_80099794 fails and guard entry
    # remains eligible.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _pjo_dataset(root)
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    def stale_y_tilt(seed_t):
        seed_t["tilt_timer_y"][0, 0] = 10

    _seed, _ref_row, out_row = _run_one_step_row(dataset_path, 622, 0, seed_mutator=stale_y_tilt)
    assert int(out_row["action_id"][0]) == MSL_ACT_GUARD_REFLECT
