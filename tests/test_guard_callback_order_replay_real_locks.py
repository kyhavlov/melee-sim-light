from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


ACT_GUARD = 179
ACT_GUARD_OFF = 180
ACT_GUARD_SET_OFF = 181
ACT_GUARD_REFLECT = 182


@pytest.mark.integration
def test_guardsetoff_anim_enter_guard_then_same_frame_guardoff_dcc_replay_lock() -> None:
    # GuardSetOff_Anim runs in Fighter_8006A360 before Fighter_procUpdate input dispatch. When the
    # GuardDamage anim finishes into Guard, destination Guard_IASA can immediately latch release and
    # enter GuardOff.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_Guard_IASA,inlineC0}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 4241
    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(seed["guard_release_latched_xc"][p]) == 0

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_OFF


@pytest.mark.integration
def test_guardsetoff_same_frame_release_does_not_exit_before_anim_finishes() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 4238
    p = 1
    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_SET_OFF


@pytest.mark.integration
def test_guardreflect_terminal_snapshot_enters_guard_before_release_gat_replay_lock() -> None:
    # Terminal no-submotion GuardReflect snapshots with expired reflect timers expose the
    # GuardReflect_Anim -> GuardOn_Anim -> Guard handoff before the following GuardOff frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800928CC}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 4834
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["guard_reflect_timer_x14"][p]) == 0
    assert int(seed["guard_reflect_timer_x18"][p]) == 0
    assert int(seed["guard_x10"][p]) == 1

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD
    assert int(out_t1["action_id"][p]) == ACT_GUARD


@pytest.mark.integration
def test_guardreflect_terminal_snapshot_gate_does_not_mask_active_timer_contact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 9479
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(seed["guard_reflect_timer_x14"][p]) == 1
    assert int(seed["guard_reflect_timer_x18"][p]) == 3

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(out_t1["action_id"][p]) != ACT_GUARD
