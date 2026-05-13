from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


MSL_ACT_SQUAT = 39
MSL_ACT_LANDING_AIR_LW = 74
MSL_ACT_GUARD_REFLECT = 182
MSL_ACT_ESCAPE_N = 235
MSL_BUTTON_Z = 0x0010
MSL_BUTTON_L = 0x0040
MSL_BUTTON_R = 0x0020
MSL_BUTTON_Y = 0x0400


def _pjo_dataset(root: Path) -> Path:
    return root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"


def _gat_doubles_dataset(root: Path) -> Path:
    return root / "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"


def _run_one_step_row_with_input_mutator(ds_path: Path, record: int, input_mutator) -> np.void:
    ds = read_dataset(str(ds_path))
    row = ds.samples[record : record + 1]
    seed_t = row["seed_t"].copy()
    prev_input_t = row["prev_input_t"].copy()
    input_t = row["input_t"].copy()
    input_mutator(prev_input_t, input_t)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(prev_input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_view[0].copy()
    finally:
        binding.destroy(handle)


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


@pytest.mark.integration
def test_escapen_terminal_wait_iasa_analog_l_spotdodge_beats_squat_replay_real_lock() -> None:
    # EscapeN_Anim can enter Wait before the same frame's input callback dispatch. The destination
    # Wait_IASA then runs ftCo_80099794 before GuardOn and before the locomotion Squat tail.
    # This doubles row holds analog L and down-stick without a digital L bit; Melee's synthesized
    # held_inputs still includes HSD_PAD_LR, so vanilla re-enters EscapeN instead of crouching.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_EscapeN_Anim,ftCo_80099794}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/fighter.c:1868-1890
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _gat_doubles_dataset(root)
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    record = 1765
    p = 0
    row = read_dataset(str(dataset_path)).samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    inp = row["input_t"]

    assert int(seed["action_id"][p]) == MSL_ACT_ESCAPE_N
    assert int(seed["action_frame"][p]) == 22
    assert int(inp["p"]["buttons"][p]) & (MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z) == 0
    assert int(inp["p"]["l"][p]) > 0
    assert int(inp["p"]["main_y"][p]) < 0
    assert int(ref["action_id"][p]) == MSL_ACT_ESCAPE_N
    assert int(ref["action_frame"][p]) == 1

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)

    def clear_analog_lr(_prev_input_t: np.ndarray, input_t: np.ndarray) -> None:
        input_t["p"]["buttons"][0, p] &= np.uint16(0xFFFF ^ (MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z))
        input_t["p"]["l"][0, p] = 0
        input_t["p"]["r"][0, p] = 0

    no_lr_out = _run_one_step_row_with_input_mutator(dataset_path, record, clear_analog_lr)
    assert int(no_lr_out["action_id"][p]) == MSL_ACT_SQUAT
