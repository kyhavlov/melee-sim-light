from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _DashIasaTurnCase:
    dataset_rel: str
    record: int
    player: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DashIasaTurnCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "PutridJoyousOryx.slpz"
            ),
            record=4828,
            player=0,
            note="PJO Dash opposite-flick Turn handoff",
        ),
        _DashIasaTurnCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.slpz"
            ),
            record=8290,
            player=1,
            note="MJA Dash opposite-flick Turn handoff",
        ),
        _DashIasaTurnCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.slpz"
            ),
            record=4230,
            player=0,
            note="BHH Dash opposite-flick Turn handoff",
        ),
    ],
)
def test_dash_iasa_turn_terminal_friction_target_pm1_lock(case: _DashIasaTurnCase) -> None:
    # Replay-real lock for Dash IASA's opposite-flick Turn path:
    # - ftCo_Dash_IASA can enter Turn via ftCo_Dash_CheckInput/ftCo_Turn_Enter_Smash.
    # - That path still falls through to Dash IASA's terminal gr_vel scalar
    #   (`gr_vel -= gr_vel * p_ftCommonData->x54 * ft_GetGroundFrictionMultiplier(fp)`).
    # - The newly entered Turn state's Phys callback then applies ordinary ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    samples = load_replay_buffers(str(dataset_path)).rows
    for rec in (case.record - 1, case.record, case.record + 1):
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[case.record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    p = case.player
    assert int(seed["action_id"][p]) == 20, case.note  # Dash
    assert int(ref["action_id"][p]) == 18, case.note  # Turn

    for rec in (case.record - 1, case.record, case.record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=rec,
            p=p,
        )


def test_dash_iasa_guardreflect_terminal_friction_bhh_lock() -> None:
    # Same Dash IASA terminal scalar, but for the non-returning powershield admission path:
    # ftCo_Dash_IASA -> ftCo_80091A4C/ftCo_80091AD8 -> ftCo_80093A50 still falls through to
    # `gr_vel -= gr_vel * p_ftCommonData->x54 * ft_GetGroundFrictionMultiplier(fp)`.
    # BHH rec241 exposed this because the Dash->GuardReflect velocity must be reduced before
    # GuardReflect_Phys applies ordinary ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80093A50}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 241
    p = 1
    samples = load_replay_buffers(str(dataset_path)).rows
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(ref["action_id"][p]) == 182  # GuardReflect

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out_row["speed_air_x_self"][p]) == pytest.approx(
        float(ref_row["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)


def test_dash_iasa_analog_guardon_terminal_friction_agn_lock() -> None:
    # Same Dash IASA terminal scalar for the analog-held GuardOn admission path:
    # ftCo_Dash_IASA -> ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0 still uses the
    # pre-entry Dash frame for the x44 early-branch predicate, then falls through to
    # `gr_vel -= gr_vel * p_ftCommonData->x54 * ft_GetGroundFrictionMultiplier(fp)`.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 3177
    p = 1
    samples = load_replay_buffers(str(dataset_path)).rows
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(seed["action_frame"][p]) == 9
    assert int(ref["action_id"][p]) == 178  # GuardOn

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out_row["speed_air_x_self"][p]) == pytest.approx(
        float(ref_row["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)


def test_run_to_guardon_does_not_use_dash_iasa_terminal_scalar_agn_lock() -> None:
    # Negative/control: Run also enters GuardOn through ftCo_80091A4C, but the Dash IASA terminal
    # scalar is not part of the Run callback. Its GuardOn frame keeps only ordinary ft_80084F3C
    # ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 111
    p = 1
    samples = load_replay_buffers(str(dataset_path)).rows
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 21  # Run
    assert int(ref["action_id"][p]) == 178  # GuardOn

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)


def _laser_owner_iids(row) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for it in row["items"]:
        if int(it["exists"]) and int(it["type"]) in (54, 55):
            out.append((int(it["type"]), int(it["owner"]), int(it["instance_id"])))
    out.sort()
    return out


def test_dash_iasa_terminal_scalar_same_frame_reflect_transfer_mja_lock() -> None:
    # Boundary lock for the same Dash IASA callback slice:
    # - the Dash -> GuardReflect admission that crosses the x44 branch boundary and reaches the
    #   terminal gr_vel scalar can install GuardReflect's ReflectDesc before laser collision,
    # - ftColl_80077464 transfers owner/xDA8 this frame, while Item_80269F14 leaves speed for the
    #   item callback lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80093A50}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 6274
    p = 1
    samples = load_replay_buffers(str(dataset_path)).rows
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(seed["action_frame"][p]) == 4
    assert int(ref["action_id"][p]) == 182  # GuardReflect

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["shield_hp"][p]) == pytest.approx(float(ref_row["shield_hp"][p]), abs=1e-6)
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=1e-6
    )
    assert _laser_owner_iids(out_row) == _laser_owner_iids(ref_row)


def test_dash_iasa_later_guardreflect_shield_hit_gat_negative_lock() -> None:
    # Negative/control: a later Dash -> GuardReflect-looking entry is not the same x44 boundary
    # source phase. The laser stays on normal shield-hit / GuardSetOff ownership rather than a broad
    # same-frame reflect-transfer suppressor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 847
    p = 0
    samples = load_replay_buffers(str(dataset_path)).rows
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(seed["action_frame"][p]) == 6
    assert int(ref["action_id"][p]) == 181  # GuardSetOff

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["shield_hp"][p]) == pytest.approx(float(ref_row["shield_hp"][p]), abs=1e-6)
    assert _laser_owner_iids(out_row) == _laser_owner_iids(ref_row)
