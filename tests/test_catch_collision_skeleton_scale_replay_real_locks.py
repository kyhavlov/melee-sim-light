from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
)
_BHH_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
)
_MAJ_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
)

_ACT_CATCH = 212
_ACT_CATCH_PULL = 213
_ACT_JUMP_F = 25
_ACT_FX_SPECIAL_LW_START = 360
_ACT_DAMAGE_FLY_TOP = 90
_ACT_CAPTURE_PULLED_HI = 223


@pytest.mark.integration
def test_catch_frame6_marginal_airborne_hurtcap_uses_collision_skeleton_scale() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    victim = 0
    catcher = 1
    seed, ref, out = _run_one_step_row(ds_path, 7851, victim)

    assert int(seed["action_id"][victim]) == _ACT_JUMP_F
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5

    # This row is a narrow miss: applying Falco's character model_scaling to the catch hitbox
    # center makes hb0 barely overlap Fox's grabbable cap12. Vanilla catch selection consumes the
    # collision-skeleton HitCapsule point through lbColl_80007ECC, so the catch must remain a miss.
    assert int(ref["action_id"][victim]) == _ACT_JUMP_F
    assert int(ref["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][victim]) == _ACT_JUMP_F
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["instance_id"][victim]) == int(ref["instance_id"][victim])
    assert int(out["instance_id"][catcher]) == int(ref["instance_id"][catcher])


@pytest.mark.integration
def test_catch_collision_skeleton_scale_still_connects_nearby_positive() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    catcher = 0
    victim = 1
    seed, ref, out = _run_one_step_row(ds_path, 921, catcher)

    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5
    assert int(ref["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(ref["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset", "record", "catcher", "defender", "defender_action"),
    [
        (_BHH_DATASET, 3377, 0, 1, _ACT_FX_SPECIAL_LW_START),
        (_MAJ_DATASET, 9428, 1, 0, _ACT_DAMAGE_FLY_TOP),
    ],
)
def test_catch_collision_skeleton_scale_does_not_extend_small_model_reach(
    dataset: str, record: int, catcher: int, defender: int, defender_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / dataset
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {dataset}")

    seed, ref, out = _run_one_step_row(ds_path, record, catcher)

    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5
    assert int(seed["action_id"][defender]) == defender_action

    # Fox's model_scaling is below 1.0. A broad inverse-scale catch correction would extend the
    # catch bubble and create false CatchPull/CapturePulledHi connects on these rows.
    assert int(ref["action_id"][catcher]) == _ACT_CATCH
    assert int(ref["action_id"][defender]) == defender_action
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][defender]) == defender_action
    assert int(out["instance_id"][catcher]) == int(ref["instance_id"][catcher])
    assert int(out["instance_id"][defender]) == int(ref["instance_id"][defender])
