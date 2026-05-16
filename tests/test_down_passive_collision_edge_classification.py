from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_DOWN_WAIT_U = 0x00B8
ACT_DOWN_STAND_U = 0x00BA
ACT_DOWN_ATTACK_U = 0x00BB
ACT_DOWN_FOWARD_U = 0x00BC
ACT_DOWN_BACK_U = 0x00BD
ACT_PASSIVE_STAND_F = 0x00C8
ACT_PASSIVE_STAND_B = 0x00C9
ACT_FALL = 0x001D
ACT_WAIT = 0x000E

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_DOWN_WAIT_U = 184
SM_DOWN_STAND_U = 186
SM_DOWN_ATTACK_U = 187
SM_DOWN_FOWARD_U = 188
SM_DOWN_BACK_U = 189
SM_PASSIVE_STAND_F = 200
SM_PASSIVE_STAND_B = 201
SM_WAIT1_0 = 2

CHAR_FOX = 1
STAGE_FD = 32

# FD runtime floor graph (MSLSTG01 simplified) has segment_i=1 from x=-75.0 to x=75.0,
# connected to segment_i=2 from x=75.0 to x=85.57. To test edge-snap vs allow-ground-to-air
# past the end of the entire connected chain, place the fighter past x=85.57.
# At this position DD90 fails (no next line), so edge-snap behavior is exercised.
_FD_MAIN_FLOOR_EDGE = 75.0
_FD_MAIN_FLOOR_SEGMENT_ID = 1
# Position past the end of the rightmost connected floor segment.
_PAST_FLOOR_EDGE_X = 86.0


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _require_local_artifacts_or_skip() -> None:
    stage_path = Path("data/stages/final_destination.json")
    fox_pose_path = Path("data/anims/fox.bin")
    if not stage_path.exists() or not fox_pose_path.exists():
        pytest.skip("requires local data/ artifacts (stage + anims); run tools/extraction to generate")


def _seed_ground_edge_base(action_id: int, submotion_id: int, x: float) -> np.ndarray:
    """Seed a fighter just past the right edge of FD main stage, grounded."""
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # facing right
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_x"][0, 1] = np.float32(20.0)  # P2 away from edge
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(_FD_MAIN_FLOOR_SEGMENT_ID)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["downwait_timer"][0, :2] = np.int16(220)

    # P1: target action
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(submotion_id)

    # P2: stable grounded idle
    seed["action_id"][0, 1] = np.uint16(0x000E)  # Wait
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(2)  # SM_WAIT1_0
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(_FD_MAIN_FLOOR_SEGMENT_ID)
    return seed


def _seed_downwait_overlap_nudge_at_right_edge(*, downwait_x: float) -> np.ndarray:
    seed = _seed_ground_edge_base(ACT_WAIT, SM_WAIT1_0, 82.7)
    # Put both fighters on FD's rightmost floor line. P0 is to P1's left, so common xF8 player
    # nudge pushes P1 right by p_ftCommonData->x450 before DownWait_Coll's ft_80083F88 floor test.
    seed["ground_id"][0, :2] = np.uint16(2)
    seed["pos_x"][0, 0] = np.float32(downwait_x - 2.8)
    seed["pos_x"][0, 1] = np.float32(downwait_x)
    seed["pos_z"][0, 0] = np.float32(-0.8)
    seed["pos_z"][0, 1] = np.float32(0.8)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)
    seed["facing_dir1"][0, 0] = np.int8(1)
    seed["facing_dir1"][0, 1] = np.int8(-1)
    seed["action_id"][0, 1] = np.uint16(ACT_DOWN_WAIT_U)
    seed["action_frame"][0, 1] = np.int16(15)
    seed["animation_index"][0, 1] = np.uint32(SM_DOWN_WAIT_U)
    return seed


def _step_once(seed: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.reseed_seed(handle, seed_bytes)
        inp = _mk_input_bytes(1, input_stride)
        msl_binding.step_input(handle, inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("action_id", "submotion_id", "description"),
    [
        (ACT_DOWN_WAIT_U, SM_DOWN_WAIT_U, "DownWaitU"),
        (ACT_DOWN_STAND_U, SM_DOWN_STAND_U, "DownStandU"),
    ],
)
@pytest.mark.integration
def test_down_wait_stand_at_edge_becomes_airborne(action_id: int, submotion_id: int, description: str) -> None:
    """DownWait/DownStand use ft_80083F88 -> mpColl_8004B108 (allow-ground-to-air).

    When seeded past the floor edge, the fighter should lose ground contact rather
    than being snapped back to the edge.
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_DownWait_Coll,ftCo_DownStand_Coll}
    refs/melee/src/melee/ft/ft_081B.c::ft_80083F88
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    """
    _require_local_artifacts_or_skip()
    # Place fighter past the end of the connected floor chain; DD90 fails and no
    # edge-snap should occur for allow-ground-to-air callbacks.
    seed = _seed_ground_edge_base(action_id, submotion_id, _PAST_FLOOR_EDGE_X)
    out = _step_once(seed)
    assert out["on_ground"][0] == 0, f"{description}: expected airborne at floor edge, got on_ground={out['on_ground'][0]}"
    # Should have fallen off without snapping back.
    assert float(out["pos_x"][0]) == pytest.approx(_PAST_FLOOR_EDGE_X, abs=1e-3)


@pytest.mark.parametrize(
    ("action_id", "submotion_id", "description"),
    [
        (ACT_DOWN_ATTACK_U, SM_DOWN_ATTACK_U, "DownAttackU"),
        (ACT_DOWN_FOWARD_U, SM_DOWN_FOWARD_U, "DownForwardU"),
        (ACT_DOWN_BACK_U, SM_DOWN_BACK_U, "DownBackU"),
        (ACT_PASSIVE_STAND_F, SM_PASSIVE_STAND_F, "PassiveStandF"),
        (ACT_PASSIVE_STAND_B, SM_PASSIVE_STAND_B, "PassiveStandB"),
    ],
)
@pytest.mark.integration
def test_down_attack_roll_passive_stand_at_edge_stays_grounded(
    action_id: int, submotion_id: int, description: str
) -> None:
    """DownAttack/DownRoll/PassiveStand use ft_80084104 -> mpColl_8004B2DC (edge-snap).

    When seeded past the floor edge, the fighter should be snapped back to the edge
    and remain grounded.
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_DownAttack_Coll
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Coll
    refs/melee/src/melee/ft/ft_081B.c::ft_80084104
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
    """
    _require_local_artifacts_or_skip()
    # Place fighter past the end of the connected floor chain; DD90 fails and
    # edge-snap should keep the fighter grounded for rooted callbacks.
    # PassiveStandB moves backward via root motion, so we place it further out
    # to ensure it still ends up past the edge after the physics step.
    if action_id == ACT_PASSIVE_STAND_B:
        start_x = _PAST_FLOOR_EDGE_X + 2.0
    else:
        start_x = _PAST_FLOOR_EDGE_X
    seed = _seed_ground_edge_base(action_id, submotion_id, start_x)
    out = _step_once(seed)
    assert out["on_ground"][0] == 1, f"{description}: expected grounded at floor edge, got on_ground={out['on_ground'][0]}"
    # After edge-snap, pos_x should be clamped back to the persisted floor edge.
    assert float(out["pos_x"][0]) <= _FD_MAIN_FLOOR_EDGE + 1.0e-3, (
        f"{description}: expected pos_x <= {_FD_MAIN_FLOOR_EDGE} after edge snap, got {out['pos_x'][0]}"
    )


@pytest.mark.integration
def test_down_wait_floor_skip_after_dd90_failure_at_chain_edge() -> None:
    """mpColl_8004ACE4 floor-skip: after DD90 fails, sweep must skip the current floor line.

    Place DownWait on the rightmost floor segment (segment_i=2, edge at ~85.57) with a small
    forward ground velocity. After physics, the fighter crosses the segment endpoint but the
    motion segment still intersects the floor line. Without floor-skip, the sweep would re-find
    segment_i=2 and incorrectly keep the fighter grounded. With floor-skip, the fighter falls off.

    DownAttack at the same position stays grounded via edge-snap, confirming the difference is
    the sweep skip, not a missing floor intersection.
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
    """
    _require_local_artifacts_or_skip()
    # Start just inside the right edge of segment_i=2 with a small forward velocity.
    # After physics: pos_x = 85.5 + 0.25 = 85.75, which is past the 85.57 endpoint.
    start_x = 85.5
    speed = 0.25

    # DownWait: should fall off because floor-skip prevents re-contact with segment_i=2.
    seed_wait = _seed_ground_edge_base(ACT_DOWN_WAIT_U, SM_DOWN_WAIT_U, start_x)
    seed_wait["speed_ground_x_self"][0, 0] = np.float32(speed)
    seed_wait["ground_id"][0, 0] = np.uint16(2)
    seed_wait["ground_id"][0, 1] = np.uint16(2)
    out_wait = _step_once(seed_wait)
    assert out_wait["on_ground"][0] == 0, (
        f"DownWait with floor-skip should fall off at chain edge, got on_ground={out_wait['on_ground'][0]}"
    )

    # DownAttack: should stay grounded via edge-snap (same position, different callback).
    seed_atk = _seed_ground_edge_base(ACT_DOWN_ATTACK_U, SM_DOWN_ATTACK_U, start_x)
    seed_atk["speed_ground_x_self"][0, 0] = np.float32(speed)
    seed_atk["ground_id"][0, 0] = np.uint16(2)
    seed_atk["ground_id"][0, 1] = np.uint16(2)
    out_atk = _step_once(seed_atk)
    assert out_atk["on_ground"][0] == 1, (
        f"DownAttack should edge-snap at chain edge, got on_ground={out_atk['on_ground'][0]}"
    )
    assert float(out_atk["pos_x"][0]) <= 85.57 + 1.0e-3, (
        f"DownAttack edge-snap pos_x should be <= 85.57, got {out_atk['pos_x'][0]}"
    )


@pytest.mark.integration
def test_downwait_player_nudge_can_drive_ft80083f88_floor_loss_at_ledge() -> None:
    """Common player nudge feeds DownWait_Coll's allow-ground-to-air floor-loss test.

    The player-overlap helper (`ftCommon_8007E0E4`) runs before Fighter_procUpdate integration.
    When the x450 nudge pushes DownWait beyond the current floor line, `ftCo_DownWait_Coll` uses
    `ft_80083F88 -> ft_80082708 -> mpColl_8004B108` and enters Fall. This is not constrained to
    the fighter's facing direction; the floor-loss owner follows the nudge motion segment.

    refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Coll
    refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    """
    _require_local_artifacts_or_skip()
    seed = _seed_downwait_overlap_nudge_at_right_edge(downwait_x=85.5)
    out = _step_once(seed)
    assert int(out["action_id"][1]) == ACT_FALL
    assert int(out["on_ground"][1]) == 0
    assert int(out["jumps_left"][1]) == 1
    assert float(out["pos_x"][1]) == pytest.approx(85.8, abs=1e-5)


@pytest.mark.integration
def test_downwait_player_nudge_inside_floor_span_stays_grounded() -> None:
    """Boundary control: the same DownWait nudge is not a Fall shortcut away from the ledge."""
    _require_local_artifacts_or_skip()
    seed = _seed_downwait_overlap_nudge_at_right_edge(downwait_x=84.5)
    out = _step_once(seed)
    assert int(out["action_id"][1]) == ACT_DOWN_WAIT_U
    assert int(out["on_ground"][1]) == 1
    assert float(out["pos_x"][1]) == pytest.approx(84.8, abs=1e-5)
