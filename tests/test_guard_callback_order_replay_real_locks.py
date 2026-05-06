from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


ACT_GUARD = 179
ACT_GUARD_ON = 178
ACT_GUARD_OFF = 180
ACT_GUARD_SET_OFF = 181
ACT_GUARD_REFLECT = 182
ACT_KNEE_BEND = 24
ACT_ATTACK_HI3 = 56
ACT_ATTACK_LW4 = 64
ACT_CATCH = 212
ACT_FX_SPECIAL_HI_LANDING = 357
ACT_FX_SPECIAL_LW_END = 363


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


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            5588,
            1,
            ACT_FX_SPECIAL_HI_LANDING,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            6201,
            1,
            ACT_FX_SPECIAL_HI_LANDING,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            10583,
            1,
            ACT_FX_SPECIAL_LW_END,
        ),
    ],
)
def test_spacie_special_end_destination_wait_admits_guard_before_locomotion(
    rel_path: str, record: int, p: int, seed_action: int
) -> None:
    # Fox/Falco grounded special end Anim callbacks can enter Wait before this frame's input
    # callback dispatch. The destination Wait_IASA then reaches ftCo_80091A4C before jump/dash/
    # turn/walk, so held shield must be able to enter GuardOn on that same source frame.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwEnd_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == seed_action

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) == ACT_GUARD_ON


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            2487,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            8576,
            1,
        ),
    ],
)
def test_attacklw4_wait_iasa_admits_guard_without_locomotion_input(
    rel_path: str, record: int, p: int
) -> None:
    # AttackLw4_IASA delegates directly to ftCo_Wait_IASA when the command script has set
    # fp->allow_interrupt. Guard entry is owned by that destination IASA ordering even when no
    # jump/dash/walk input is present on the row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_ATTACK_LW4

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_ON
    assert int(out_t1["action_id"][p]) == ACT_GUARD_ON


@pytest.mark.integration
def test_attacklw4_wait_iasa_catch_precedes_attack_restart_ldw_replay_lock() -> None:
    # The same AttackLw4 -> Wait_IASA delegation checks Catch before guard/locomotion and before
    # any later grounded attack restart. LDW:1150 is a Z+shield row that source routes to Catch,
    # not a side-tilt restart from the held stick.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/battlefield_recent/LoyalDishonestWren.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1150
    p = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_ATTACK_LW4

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_CATCH
    assert int(out_t1["action_id"][p]) == ACT_CATCH


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "p", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            1670,
            1,
            ACT_ATTACK_LW4,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            9127,
            0,
            ACT_ATTACK_HI3,
        ),
    ],
)
def test_grounded_attack_wait_iasa_tap_jump_reaches_kneebend(
    rel_path: str, record: int, p: int, seed_action: int
) -> None:
    # Grounded AttackHi3/AttackLw4 IASA delegates to ftCo_Wait_IASA once allow_interrupt is live.
    # Wait_IASA checks jump input after catch/guard and before dash/squat/turn/walk, so a pure
    # tap-jump row reaches KneeBend instead of staying in the attack until another locomotion
    # predicate happens to enable the tail.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == seed_action
    assert int(ref["action_id"][p]) == ACT_KNEE_BEND

    _, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(out_t1["action_id"][p]) == int(ref_t1["action_id"][p]) == ACT_KNEE_BEND
    assert int(out_t1["action_frame"][p]) == int(ref_t1["action_frame"][p]) == 0
