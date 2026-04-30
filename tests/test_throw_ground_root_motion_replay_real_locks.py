from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


PRH = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "PositiveRevolvingHyena.msl"
)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "owner", "action_id", "expected_dx"),
    [
        (PRH, 6855, 0, 219, None),  # ThrowF
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "TubbyCurlyHerring.msl",
            5084,
            1,
            220,
            0.0,
        ),  # ThrowB early no-root-motion control
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "TubbyCurlyHerring.msl",
            252,
            1,
            221,
            0.0,
        ),  # ThrowHi early no-root-motion control
        (PRH, 545, 1, 222, -0.3000001907348633),  # ThrowLw TransN root-motion pulse
    ],
)
def test_grounded_throw_phys_uses_throw_direction_root_motion_owner(
    dataset_rel: str, record: int, owner: int, action_id: int, expected_dx: float | None
) -> None:
    # PRH ThrowF weighted-animation window from the aggregate disruptive packet:
    # - Grounded ThrowF Phys uses ft_80085004 -> ft_80085030.
    # - ft_80085030 reads the live AObj TransN offset, not the floored integer SSANIM01 tail.
    # - The attached ThrownF victim has empty Phys/Coll and must not receive the throw-owner ground
    #   velocity writer.
    #
    # Source refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
    #     ftCo_ThrowF_Phys,ftCo_ThrowB_Phys,ftCo_ThrowHi_Phys,ftCo_ThrowLw_Phys}
    # - refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
    # - refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / dataset_rel
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(ds_path, record, owner)
    assert int(seed["action_id"][owner]) == action_id
    assert int(out["action_id"][owner]) == int(ref["action_id"][owner]), record
    assert int(out["action_frame"][owner]) == int(ref["action_frame"][owner]), record
    assert float(out["pos_x"][owner]) == pytest.approx(float(ref["pos_x"][owner]), abs=4e-6), record
    assert float(out["speed_ground_x_self"][owner]) == pytest.approx(
        float(ref["speed_ground_x_self"][owner]), abs=1e-6
    ), record
    assert float(out["speed_air_x_self"][owner]) == pytest.approx(
        float(ref["speed_air_x_self"][owner]), abs=1e-6
    ), record
    if expected_dx is not None:
        assert float(ref["pos_x"][owner]) - float(seed["pos_x"][owner]) == pytest.approx(
            expected_dx, abs=4e-6
        ), record


@pytest.mark.integration
def test_throwf_grounded_phys_does_not_write_attached_victim_velocity() -> None:
    # Negative boundary: the attached ThrownF victim is still in the same contact episode, but
    # ThrownF Phys/Coll are empty. Its stale seed velocities must not be replaced by the thrower's
    # root-motion velocity.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / PRH
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {PRH}")

    owner = 0
    victim = 1
    for rec in range(6855, 6861):
        seed, ref, out = _run_one_step_row(ds_path, rec, owner)
        assert int(seed["action_id"][owner]) == 219  # ThrowF
        assert int(seed["action_id"][victim]) == 239  # ThrownF
        assert int(out["action_id"][owner]) == int(ref["action_id"][owner]), rec
        assert int(out["action_frame"][owner]) == int(ref["action_frame"][owner]), rec

        assert int(out["action_id"][victim]) == int(ref["action_id"][victim]), rec
        assert float(out["speed_ground_x_self"][victim]) == pytest.approx(0.0, abs=1e-6), rec
        assert float(out["speed_air_x_self"][victim]) == pytest.approx(0.0, abs=1e-6), rec
