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
    dataset_rel: str
    target_record: int
    port: int
    note: str


_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=552,
        port=1,
        note="AGN first steady GuardSetOff row clears stale x221C_b2 in the gx10==5 full-lightshield lane",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=2279,
        port=0,
        note="GAT first steady GuardSetOff row clears stale x221C_b2 in the gx10==5 full-lightshield lane",
    ),
)

_B1_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=2393,
        port=0,
        note="validation AGN first steady GuardSetOff row clears stale x221C_b1 on the gx10==7 full-lightshield lane",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=4897,
        port=0,
        note="validation GAT first steady GuardSetOff row clears stale x221C_b1 on the gx10==7 full-lightshield lane",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=2393,
        port=0,
        note="AGN first steady GuardSetOff row clears stale x221C_b1 on the gx10==7 full-lightshield lane",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=4897,
        port=0,
        note="GAT first steady GuardSetOff row clears stale x221C_b1 on the gx10==7 full-lightshield lane",
    ),
)

_ACTIVE_TIMER_B1_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=6315,
        port=0,
        note="validation GAT GuardReflect->GuardSetOff hitlag row clears x221C_b1 once x14 has expired while x18 remains active",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=6315,
        port=0,
        note="GAT GuardReflect->GuardSetOff hitlag row clears x221C_b1 once x14 has expired while x18 remains active",
    ),
)

_ACTIVE_TIMER_B1_NEGATIVE_CONTROLS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=2390,
        port=0,
        note="validation AGN active-timer control keeps x221C_b1 while x14 remains live",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=2390,
        port=0,
        note="AGN active-timer control keeps x221C_b1 while x14 remains live",
    ),
)

_NEGATIVE_CONTROLS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=6318,
        port=0,
        note="GAT gx10==6 control keeps x221C_b2",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=9482,
        port=0,
        note="GAT higher-tilt gx10==6 control keeps x221C_b2",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz"
        ),
        target_record=2326,
        port=0,
        note="TBK gx10==6 control keeps x221C_b2",
    ),
)

_SECOND_STEADY_B2_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=6319,
        port=0,
        note="GAT second steady GuardSetOff self-loop clears stale x221C_b2 once both reflect timers are gone",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=9483,
        port=0,
        note="GAT mirrored second steady GuardSetOff self-loop clears stale x221C_b2 once both reflect timers are gone",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz"
        ),
        target_record=2327,
        port=0,
        note="TBK second steady GuardSetOff self-loop clears stale x221C_b2 once both reflect timers are gone",
    ),
)

_SECOND_STEADY_INIT_MINUS_ONE_B2_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.slpz"
        ),
        target_record=1226,
        port=1,
        note="EWT FoD second steady GuardSetOff self-loop clears stale x221C_b2 at gx10==init-1",
    ),
)

_CARRY_SNAPSHOT_B2_TARGETS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=2395,
        port=0,
        note="AGN GuardSetOff carry snapshot clears stale x221C_b2 after the prior steady callback pass",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=4899,
        port=0,
        note="GAT GuardSetOff carry snapshot clears stale x221C_b2 after the prior steady callback pass",
    ),
)

_CARRY_SNAPSHOT_NEGATIVE_CONTROLS = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=4049,
        port=1,
        note="AGN later carry control keeps x221C_b2 on the next countdown value",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=852,
        port=0,
        note="GAT later carry control keeps x221C_b2 on the next countdown value",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz"
        ),
        target_record=3498,
        port=0,
        note="TBK later carry control keeps x221C_b2 on the next countdown value",
    ),
)



@pytest.mark.integration
@pytest.mark.parametrize("case", _TARGETS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_guardsetoff_first_steady_gx10_5_rows_clear_b2(case: _Case) -> None:
    # Narrow replay-real lock for the surviving GuardSetOff x221C_b2 lane:
    # - first steady post-hitlag GuardSetOff row,
    # - full-lightshield seed, x18 already expired, and
    # - guard.x10 advanced into the 5-frame steady window.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.port
    target = samples[case.target_record]
    seed = target["seed_t"]
    assert int(seed["action_id"][p]) == 181, case.note
    assert int(seed["action_frame"][p]) == 0, case.note
    assert int(seed["hitlag"][p]) == 1, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_setoff_hitlag_damage_min"][p]) == 1, case.note
    assert int(seed["guard_x10"][p]) == 5, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
        assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize("case", _B1_TARGETS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_guardsetoff_first_steady_gx10_7_rows_clear_b1_only(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 181, case.note
    assert int(seed["action_frame"][p]) == 0, case.note
    assert int(seed["hitlag"][p]) == 1, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_x10"][p]) == 7, case.note
    assert int(seed["state_flags"][p, 3]) == 96, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]) == 32, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case", _ACTIVE_TIMER_B1_TARGETS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}"
)
def test_guardsetoff_active_timer_handoff_rows_clear_b1_only(case: _Case) -> None:
    # Replay-real lock for the active-timer GuardReflect -> GuardSetOff handoff:
    # - the destination GuardSetOff row is already in hitlag,
    # - ftCo_80093BC0 ties x221C_b1 to x14 and x221C_b2 to x18,
    # - once x14 has expired on the destination row while x18 remains active, only x221C_b1 should
    #   clear.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 182, case.note
    assert int(seed["seed_prev_action_id"][p]) == 182, case.note
    assert int(seed["action_frame"][p]) == -1, case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 1, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 3, case.note
    assert int(seed["state_flags"][p, 3]) == 96, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
        assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    _ACTIVE_TIMER_B1_NEGATIVE_CONTROLS,
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}",
)
def test_guardsetoff_active_timer_controls_keep_b1_while_x14_is_live(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 182, case.note
    assert int(seed["action_frame"][p]) == -1, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 2, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 4, case.note
    assert int(seed["state_flags"][p, 3]) == 112, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]) == 96, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case", _NEGATIVE_CONTROLS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}"
)
def test_guardsetoff_first_steady_gx10_6_controls_keep_b2(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 181, case.note
    assert int(seed["action_frame"][p]) == 0, case.note
    assert int(seed["hitlag"][p]) == 1, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_setoff_hitlag_damage_min"][p]) == 1, case.note
    assert int(seed["guard_x10"][p]) == 6, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]) == 32, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case", _SECOND_STEADY_B2_TARGETS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}"
)
def test_guardsetoff_second_steady_rows_clear_b2_once_x18_owner_is_gone(case: _Case) -> None:
    # Replay-real lock for the principled late GuardSetOff x221C_b2 lane:
    # - GuardSetOff entry seeds mv.co.guard.x10 from ftCommonData,
    # - steady rows are then owned by GuardSetOff_Anim / ftCo_80093BC0, and
    # - on the second steady tick after the frozen af0 snapshot, stale powershield-active carry
    #   should be gone once both GuardReflect timers have expired.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80092F2C,ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_800925A4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # data/common/ft_common_data.json: guard_x10_init_frames
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_id"][p]) == 181, case.note
    assert int(seed["action_frame"][p]) > 0, case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_x10"][p]) == 6, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
        assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    _SECOND_STEADY_INIT_MINUS_ONE_B2_TARGETS,
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}",
)
def test_guardsetoff_second_steady_init_minus_one_row_clears_b2_after_post_hitlag_owner(
    case: _Case,
) -> None:
    # Boundary complement to the gx10<init-1 locks:
    # - the first non-hitlag GuardSetOff row can keep x221C_b2 via the explicit post-hitlag owner,
    # - the following same-action self-loop has no post-hitlag owner and both GuardReflect timers
    #   expired, so ftCo_GuardSetOff_Anim / ftCo_80093BC0 clears the stale powershield bit even
    #   while guard.x10 is still init-1.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_80092F2C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_frame"][p]) > 0, case.note
    assert int(seed["action_frame"][p]) > 0, case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["guard_setoff_hitlag_exit_phase_u8"][p]) == 0, case.note
    assert int(seed["guard_setoff_post_hitlag_owner_u8"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_x10"][p]) == 7, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
    assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
    assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case", _CARRY_SNAPSHOT_B2_TARGETS, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}"
)
def test_guardsetoff_carry_snapshot_rows_clear_b2_once_x18_owner_is_gone(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["seed_prev_action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_frame"][p]) > 0, case.note
    assert int(seed["action_frame"][p]) > 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_x10"][p]) == 7, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
        assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    _CARRY_SNAPSHOT_NEGATIVE_CONTROLS,
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}",
)
def test_guardsetoff_carry_snapshot_controls_keep_b2(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = ds.rows[case.target_record]
    seed = target["seed_t"]
    p = case.port
    assert int(seed["seed_prev_action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_frame"][p]) > 0, case.note
    assert int(seed["action_frame"][p]) > 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][p]) == 0, case.note
    assert int(seed["guard_x10"][p]) == 8, case.note
    assert int(seed["state_flags"][p, 3]) == 32, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]) == 32, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case,expected_flag3",
    [
        (
            _Case(
                dataset_rel=(
                    "replays/validation/cardinal_1.0_recent/"
                    "AttachedGoodNaturedGuanaco.slpz"
                ),
                target_record=2395,
                port=0,
                note="repeated GuardSetOff shield-hit entry clears stale x221C_b2 when no timer owner is live",
            ),
            0,
        ),
        (
            _Case(
                dataset_rel=(
                    "replays/validation/cardinal_1.0_recent/"
                    "GracefulAttachedTurtle.slpz"
                ),
                target_record=4899,
                port=0,
                note="GuardSetOff->Guard carry snapshot clears stale x221C_b2 after countdown has advanced",
            ),
            0,
        ),
        (
            _Case(
                dataset_rel=(
                    "replays/validation/cardinal_1.0_recent/"
                    "AttachedGoodNaturedGuanaco.slpz"
                ),
                target_record=4049,
                port=1,
                note="GuardSetOff->Guard init-tick carry snapshot keeps x221C_b2",
            ),
            32,
        ),
        (
            _Case(
                dataset_rel=(
                    "replays/validation/cardinal_1.0_recent/"
                    "GracefulAttachedTurtle.slpz"
                ),
                target_record=852,
                port=0,
                note="mirrored GuardSetOff->Guard init-tick carry snapshot keeps x221C_b2",
            ),
            32,
        ),
    ],
    ids=lambda v: v.note if isinstance(v, _Case) else f"flag{v}",
)
def test_guardsetoff_validation_carry_rows_match_b2_owner_boundary(
    case: _Case, expected_flag3: int
) -> None:
    # Validation-suite lock for the primary rollout regression:
    # - in-place/re-entered GuardSetOff rows are owned by GuardSetOff_Anim / ftCo_80093BC0,
    # - destination Guard carry rows are owned by ftCo_800928CC after GuardSetOff_Anim, and
    # - stale x221C_b2 must clear only when no GuardReflect timer owner remains for that phase.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_800928CC,ftCo_80092F2C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, case.port)
    assert int(ref_row["state_flags"][case.port, 3]) == expected_flag3, case.note
    assert int(out_row["action_id"][case.port]) == int(ref_row["action_id"][case.port]), case.note
    assert int(out_row["action_frame"][case.port]) == int(ref_row["action_frame"][case.port]), case.note
    assert int(out_row["state_flags"][case.port, 3]) == expected_flag3, case.note
