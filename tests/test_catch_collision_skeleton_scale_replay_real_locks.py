from __future__ import annotations

from pathlib import Path

import numpy as np
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
_STAGE_FINAL_DESTINATION = 32


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
def test_catch_wall_obstruction_blocks_wall_separated_grabbable_capsule() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    catcher = 0
    victim = 1
    fd_right_wall_x = np.float32(85.5656967163086)

    def place_near_fd_right_wall(seed, *, y: float, cross_wall: bool) -> None:
        # Source-shaped synthetic lock for ft_80084CE4:
        # - FD's right wall includes a fighter-solid wall segment at x=85.5656967 from y=0 to -10.5.
        # - The low pair has the same catch/hurtcap overlap as the high pair, but the segment
        #   between the fighters' ECB midpoints intersects that wall and must reject CatchPull.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        # refs/melee/src/melee/ft/ft_081B.c::ft_80084CE4
        # refs/melee/src/melee/mp/mplib.c::{mpCheckLeftWall,mpCheckRightWall}
        # data/stages/bin/grnla.bin::MSLSTG01 segment i=9
        seed["stage_id"][0] = np.uint32(_STAGE_FINAL_DESTINATION)
        if cross_wall:
            x0 = fd_right_wall_x - np.float32(0.5)
            x1 = fd_right_wall_x + np.float32(0.5)
        else:
            x0 = fd_right_wall_x + np.float32(0.5)
            x1 = fd_right_wall_x + np.float32(1.5)
        seed["pos_x"][0, catcher] = x0
        seed["pos_x"][0, victim] = x1
        seed["pos_y"][0, catcher] = np.float32(y)
        seed["pos_y"][0, victim] = np.float32(y)
        seed["ground_id"][0, catcher] = np.uint16(0xFFFF)
        seed["ground_id"][0, victim] = np.uint16(0xFFFF)
        seed["on_ground"][0, catcher] = np.uint8(0)
        seed["on_ground"][0, victim] = np.uint8(0)

    seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=-8.0, cross_wall=True),
    )
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_id"][victim]) == _ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][victim]) == _ACT_DAMAGE_FLY_TOP

    _seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=5.0, cross_wall=True),
    )
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI

    _seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=-8.0, cross_wall=False),
    )
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
