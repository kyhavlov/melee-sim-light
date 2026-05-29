from __future__ import annotations

import json
from argparse import Namespace
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.modelplay.sim_env import build_match_config_array
from tools.modelplay.state_adapter import STAGE_DEBUG_DTYPE
from tools.slippi.known_data_artifacts import (
    STAGE_PLATFORM_MOTION_KIND_FOD,
    STAGE_METADATA_BIN_BY_STAGE_ID,
    STAGE_OBJECT_SUPPORT_KIND_YOSHI_SHYGUY,
    read_mslstg01_v7,
    stage_metadata_path_for_stage_id,
)
from tools.slippi.seed_history import load_shield_tilt_table_meta
from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype


ACT_WAIT = 0x000E
ACT_WALK_MIDDLE = 0x0010
ACT_TURN = 0x0012
ACT_DASH = 0x0014
ACT_RUN = 0x0015
ACT_RUN_BRAKE = 0x0017
ACT_KNEE_BEND = 0x0018
ACT_JUMP_F = 0x0019
ACT_JUMP_B = 0x001A
ACT_JUMP_AERIAL_F = 0x001B
ACT_JUMP_AERIAL_B = 0x001C
ACT_FALL = 0x001D
ACT_FALL_SPECIAL = 0x0023
ACT_ATTACK_AIR_F = 0x0042
ACT_ATTACK_AIR_LW = 0x0045
ACT_SQUAT = 0x0027
ACT_SQUAT_WAIT = 0x0028
ACT_LANDING = 0x002A
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_LANDING_AIR_N = 0x0046
ACT_GUARD_ON = 0x00B2
ACT_DAMAGE_FALL = 0x0026
ACT_GUARD = 0x00B3
ACT_GUARD_SET_OFF = 0x00B5
ACT_GUARD_REFLECT = 0x00B6
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_N_2 = 0x004F
ACT_DAMAGE_HI_2 = 0x004C
ACT_DAMAGE_AIR_2 = 0x0055
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_ESCAPE_AIR = 0x00EC
ACT_DOWN_BOUND_U = 0x00B7
ACT_DOWN_BOUND_D = 0x00BF
ACT_DOWN_WAIT_U = 0x00B8
ACT_DOWN_FOWARD_U = 0x00BC
ACT_PASSIVE = 0x00C7
ACT_PASSIVE_STAND_F = 0x00C8
ACT_PASS = 0x00F4
ACT_OTTOTTO = 0x00F5
ACT_OTTOTTO_WAIT = 0x00F6
ACT_CLIFF_JUMP_SLOW2 = 0x0105
ACT_CLIFF_CATCH = 0x00FC
ACT_CLIFF_WAIT = 0x00FD
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_B = 0x0043
ACT_ATTACK_AIR_HI = 0x0044
ACT_LANDING_AIR_B = 0x0048
ACT_ATTACK_DASH = 0x0032
ACT_ATTACK_S4_S = 0x003C
ACT_ATTACK_HI4 = 0x003F
ACT_FX_SPECIAL_AIR_HI = 0x0164
ACT_FX_SPECIAL_AIR_S_END = 0x0160
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_N_LOOP = 0x0156
ACT_FX_SPECIAL_N_END = 0x0157
ACT_FX_SPECIAL_HI_LANDING = 0x0165
ACT_FX_SPECIAL_HI_FALL = 0x0166
ACT_FX_SPECIAL_HI_BOUND = 0x0167
ACT_FX_SPECIAL_LW_START = 0x0168
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_ITEM_PARASOL_FALL = 0x0091
ACT_CATCH = 0x00D4
ACT_THROW_F = 0x00DB
ACT_CAPTURE_DAMAGE_HI = 0x00E1
ACT_FX_SPECIAL_AIR_N_START = 0x0158

SM_WAIT1_0 = 2
SM_TURN = 10
SM_OTTOTTO = 210
SM_KNEE_BEND = 15
SM_JUMP_F = 16
SM_FALL = 20
SM_DAMAGE_FALL = 33
SM_PASS = 209
SM_CLIFF_JUMP_SLOW2 = 226
SM_CLIFF_WAIT = 217
SM_ATTACK_DASH = 52
SM_ATTACK_S4 = 62
SM_ATTACK_HI4 = 66
SM_FX_SPECIAL_N_START = 301
SM_FX_SPECIAL_N_LOOP = 302
SM_FX_SPECIAL_N_END = 303
SM_ESCAPE_AIR = 44
SM_DOWN_BOUND_U = 183
SM_FX_SPECIAL_HI = 309
SM_OTTOTTO_WAIT = 211
SM_DOWN_FOWARD_U = 188
SM_PASSIVE_STAND_F = 200
SM_ATTACK_AIR_N = 68
SM_ATTACK_AIR_F = 69
SM_ATTACK_AIR_B = 70
SM_ATTACK_AIR_HI = 71
SM_DAMAGE_N_2 = 169
SM_DAMAGE_HI_2 = 166
SM_DAMAGE_AIR_2 = 174
SM_DAMAGE_FLY_TOP = 180

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_POKEMON = 3
STAGE_FOD = 2
STAGE_YOSHI = 8
STAGE_BATTLEFIELD = 31
BUTTON_L = 0x0040
BUTTON_A = 0x0100
BUTTON_B = 0x0200
BUTTON_Y = 0x0800
BUTTON_R = 0x0020

ACT_FX_SPECIAL_AIR_LW_START = 0x016D
ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E
ACT_FX_SPECIAL_HI_HOLD = 0x0161
ACT_FX_SPECIAL_HI_HOLD_AIR = 0x0162
ACT_FX_SPECIAL_HI = 0x0163

COLLIDE_LEFT_WALL_MASK = 0x0000003F
COLLIDE_RIGHT_WALL_MASK = 0x00000FC0
COLLIDE_CEILING_MASK = 0x00006000

def _seed_specialairhi_downward(*, x: float, y: float, prev_y: float, ground_id: int) -> np.ndarray:
    seed = _seed_base(STAGE_BATTLEFIELD, ACT_FX_SPECIAL_AIR_HI, SM_FX_SPECIAL_HI, x, y)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(prev_y)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-3.8)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["action_frame"][0, 0] = np.int16(8)
    seed["anim_frame_f32"][0, 0] = np.float32(8.0)
    seed["ground_id"][0, 0] = np.uint16(ground_id)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["specialhi_rotate_model_valid_u8"][0, 0] = np.uint8(1)
    seed["specialhi_rotate_model_f32"][0, 0] = np.float32(-np.pi / 2.0)
    return seed
COLLIDE_FLOOR_MASK = 0x00018000


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 0, 0.0, 1.125),  # FoD source-local platform; runtime transforms live lines.
        (31, 2, -40.0, 27.200000762939453),  # Battlefield left platform
        (8, 4, 0.0, 42.0),  # Yoshi's Story top platform
        (28, 2, 0.0, 51.42530059814453),  # Dream Land top platform
        (3, 35, -40.0, 25.0),  # frozen Pokemon Stadium left platform
        (3, 36, 40.0, 25.0),  # frozen Pokemon Stadium right platform
    ],
)
def test_static_platform_lines_are_debug_visible_but_not_in_filtered_graph(
    stage_id: int, line_id: int, x: float, y: float
) -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seg = msl_binding.stage_floor_segment(stage_id, line_id)
        assert seg is not None
        assert int(seg["is_platform"]) == 1
        assert float(seg["x0"]) <= x <= float(seg["x1"])
        assert float(seg["y0"]) == pytest.approx(y)

        # The filtered graph remains a hard-floor-only view; runtime platform collision uses the
        # full graph plus Pass/floor-skip gating.
        assert msl_binding.stage_fighter_floor_segment(stage_id, line_id) is None
    finally:
        msl_binding.destroy(handle)


def test_yoshi_turn_ground_to_air_edge_requires_live_ecb_side_crossing_negative() -> None:
    # Turn_Coll's ft_80083F88/mpColl_8004B108 floor-loss owner may leave the floor when the live
    # CollData ECB side reaches a floor endpoint on the first Turn callback row. The owner is not a
    # generic "Turn near Yoshi" fallback: an inboard Turn row whose current ECB side is still inside
    # the sloped floor remains grounded.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    x = 50.0
    y = -3.5 * ((x - 39.20000076293945) / (56.0 - 39.20000076293945))
    seed = _seed_base(STAGE_YOSHI, ACT_TURN, SM_TURN, x, y)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.0)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_TURN
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 6


def _floor_y_at(seg, x: float) -> float:
    dx = float(seg.x1) - float(seg.x0)
    if dx == 0.0:
        return max(float(seg.y0), float(seg.y1))
    t = (x - float(seg.x0)) / dx
    return float(seg.y0) + (float(seg.y1) - float(seg.y0)) * t


def _floor_normal(seg) -> tuple[float, float]:
    dx = float(seg.x1) - float(seg.x0)
    dy = float(seg.y1) - float(seg.y0)
    nx = -dy
    ny = dx
    length = (nx * nx + ny * ny) ** 0.5
    return nx / length, ny / length


def _input_bytes() -> np.ndarray:
    import msl_binding

    return np.zeros((1, int(msl_binding.sizes()["input"])), dtype=np.uint8)


def _seed_base(stage_id: int, action_id: int, submotion_id: int, x: float, y: float) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(submotion_id)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["floor_skip_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["floor_skip_segment_valid_u8"][0, :] = np.uint8(0)
    seed["cliff_ledge_floor_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["anim_frame_f32"][0, :2] = seed["action_frame"][0, :2].astype(np.float32)
    return seed


def _step_once(seed: np.ndarray, prev_input: np.ndarray | None = None, input_t: np.ndarray | None = None):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    if prev_input is None:
        prev_input = _input_bytes()
    if input_t is None:
        input_t = _input_bytes()
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1
    )
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _step_once_rollout(
    seed: np.ndarray, prev_input: np.ndarray | None = None, input_t: np.ndarray | None = None
):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    if prev_input is None:
        prev_input = _input_bytes()
    if input_t is None:
        input_t = _input_bytes()
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1
    )
    try:
        msl_binding.reseed_seed_rollout(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _step_once_with_contacts(
    seed: np.ndarray, prev_input: np.ndarray | None = None, input_t: np.ndarray | None = None
):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    if prev_input is None:
        prev_input = _input_bytes()
    if input_t is None:
        input_t = _input_bytes()
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    contacts = np.zeros((1, contacts_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1
    )
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        msl_binding.debug_write_collision_contacts(handle, contacts)
        return (
            out.view(COMPARE_DTYPE).reshape((1,))[0],
            contacts.view(_collision_contacts_dtype()).reshape((1,))[0],
        )
    finally:
        msl_binding.destroy(handle)


def _step_once_with_contacts_and_colldata(
    seed: np.ndarray, prev_input: np.ndarray | None = None, input_t: np.ndarray | None = None
):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    colldata_stride = int(sizes["colldata_ecb"])
    if prev_input is None:
        prev_input = _input_bytes()
    if input_t is None:
        input_t = _input_bytes()
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    contacts = np.zeros((1, contacts_stride), dtype=np.uint8)
    colldata = np.zeros((1, colldata_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        msl_binding.debug_write_collision_contacts(handle, contacts)
        msl_binding.debug_write_colldata_ecb(handle, colldata)
        return (
            out.view(COMPARE_DTYPE).reshape((1,))[0],
            contacts.view(_collision_contacts_dtype()).reshape((1,))[0],
            colldata.view(_colldata_ecb_dtype()).reshape((1,))[0],
        )
    finally:
        msl_binding.destroy(handle)


def _collision_contacts_dtype() -> np.dtype:
    return np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_WAIT, SM_WAIT1_0),
        (ACT_WALK_MIDDLE, SM_WAIT1_0),
        (ACT_DASH, SM_WAIT1_0),
        (ACT_TURN, SM_TURN),
    ],
)
def test_phase3_common_grounded_entry_and_sustained_actions_use_ordered_retry(
    action_id: int, submotion_id: int
) -> None:
    # Phase 3 routes common grounded callbacks through the ordered mpColl substrate. This
    # disconnected Battlefield setup requires the 4A908 floor retry after persisted-floor projection
    # misses; mpCollEnd then publishes the accepted main-floor result.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC,ft_80083F88}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor,mpCollEnd}
    seed = _seed_base(STAGE_BATTLEFIELD, action_id, submotion_id, 30.0, -6.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(30.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert int(colldata["floor_result_valid"][0]) == 1
    assert int(colldata["floor_result_segment_id"][0]) == 1
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=2e-5)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_KNEE_BEND, SM_KNEE_BEND),
        (ACT_SQUAT, SM_WAIT1_0),
        (ACT_SQUAT_WAIT, SM_WAIT1_0),
        (ACT_TURN, SM_TURN),
    ],
)
def test_phase3_common_grounded_b108_floor_loss_enters_fall(
    action_id: int, submotion_id: int
) -> None:
    # These common callbacks reach ft_80082708/mpColl_8004B108. When the current floor is no longer
    # under the ECB side span, the source callback leaves ground instead of preserving a stale floor.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_800844EC,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    seed = _seed_base(STAGE_BATTLEFIELD, action_id, submotion_id, -70.0, 27.2001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(27.2001)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 2


@pytest.mark.parametrize("action_id", [ACT_LANDING, ACT_LANDING_FALL_SPECIAL])
def test_phase3_landing_common_owner_projects_sustained_slope(action_id: int) -> None:
    # Landing and LandingFallSpecial share ftCo_Landing_Coll -> ft_80084280. After the entry-frame
    # handoff, sustained grounded projection follows the live slope through the common owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    stage = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    slope = next(seg for seg in stage.segments if int(seg.line_id) == 6)
    x = 47.0
    seed = _seed_base(STAGE_YOSHI, action_id, SM_WAIT1_0, x, _floor_y_at(slope, x) + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.5)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 6
    assert float(out["pos_y"][0]) == pytest.approx(_floor_y_at(slope, float(out["pos_x"][0])) + 0.0001, abs=2e-5)


@pytest.mark.parametrize(
    ("action_id", "submotion_id", "expected_action"),
    [
        (ACT_FALL, SM_FALL, ACT_LANDING),
        (ACT_FALL_SPECIAL, SM_FALL, ACT_LANDING_FALL_SPECIAL),
        (ACT_JUMP_AERIAL_F, SM_FALL, ACT_LANDING),
        (ACT_CLIFF_JUMP_SLOW2, SM_CLIFF_JUMP_SLOW2, ACT_LANDING),
    ],
)
def test_phase3_common_airborne_owners_publish_hard_floor(
    action_id: int, submotion_id: int, expected_action: int
) -> None:
    # Common airborne owners use the Phase 2 floor substrate and publish the accepted hard-floor
    # result through mpCollEnd. FallSpecial keeps the source LandingFallSpecial action boundary.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083090,ft_800831CC,ft_800835B0,ft_80082B1C}
    seed = _seed_base(STAGE_BATTLEFIELD, action_id, submotion_id, 0.0, -2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(6.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][0]) == expected_action
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert int(colldata["floor_result_valid"][0]) == 1
    assert int(colldata["floor_result_segment_id"][0]) == 1


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_FALL, SM_FALL),
        (ACT_FALL_SPECIAL, SM_FALL),
        (ACT_JUMP_AERIAL_F, SM_FALL),
    ],
)
def test_phase3_common_airborne_platform_pass_writes_floor_skip(
    action_id: int, submotion_id: int
) -> None:
    # Down-held common airborne callbacks pass ftCo_80096CC8 to platform floor checks. On
    # source-trusted transformed FoD platforms, rejection writes CollData.floor_skip instead of
    # landing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(STAGE_FOD, action_id, submotion_id, -35.0, float(world_y) - 2.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 14.0)
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed, _input_bytes(), input_t)

    assert int(out["on_ground"][0]) == 0
    assert int(colldata["floor_skip_valid"][0]) == 1
    assert int(colldata["floor_skip_segment_id"][0]) == 0


def test_phase3_common_airborne_joint_skip_and_only_filter_floor_publication() -> None:
    # The common airborne owner forwards CollData joint filters into the ordered floor query.
    # joint_id_skip rejects the owning joint, joint_id_only admits only that joint, and a wrong
    # only-filter leaves the floor result empty.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpCheckFloor}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    colldata_stride = int(sizes["colldata_ecb"])
    compare_stride = int(sizes["compare"])
    floor_seg = msl_binding.stage_floor_segment(STAGE_POKEMON, 34)
    assert floor_seg is not None
    joint_id = int(floor_seg["joint_id"])

    seed = _seed_base(STAGE_POKEMON, ACT_FALL, SM_FALL, 0.0, -2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_y_self"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(5.0)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    def run_with_filters(skip: int, only: int) -> tuple[int, int, int]:
        handle = msl_binding.init(batch_size=1, num_players=2)
        try:
            colldata = np.zeros((1, colldata_stride), dtype=np.uint8)
            compare = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
            msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, skip, only)
            msl_binding.step_input(handle, inp, inp)
            msl_binding.debug_write_colldata_ecb(handle, colldata)
            msl_binding.write_compare(handle, compare)
            snap = colldata.view(_colldata_ecb_dtype()).reshape((1,))[0]
            row = compare.view(COMPARE_DTYPE).reshape((1,))[0]
            return (
                int(snap["floor_result_valid"][0]),
                int(snap["floor_result_segment_id"][0]),
                int(row["on_ground"][0]),
            )
        finally:
            msl_binding.destroy(handle)

    assert run_with_filters(-1, -1) == (1, 34, 1)
    assert run_with_filters(joint_id, -1) == (0, 0xFFFF, 0)
    assert run_with_filters(-1, joint_id) == (1, 34, 1)
    assert run_with_filters(-1, joint_id + 1) == (0, 0xFFFF, 0)


def test_phase3_common_grounded_wall_ceiling_and_combined_contacts_are_ordered() -> None:
    # Common grounded Wait_Coll routes through the Phase 2 ordered substrate for wall, ceiling,
    # floor+wall, and floor+ceiling cases.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
    wall_seed = _seed_base(STAGE_BATTLEFIELD, ACT_WAIT, SM_WAIT1_0, 90.0, -6.0)
    wall_seed["on_ground"][0, 0] = np.uint8(1)
    wall_seed["ground_id"][0, 0] = np.uint16(3)
    wall_seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    wall_seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    wall_seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    wall_out, wall_contacts = _step_once_with_contacts(wall_seed)
    assert int(wall_contacts["wall_kind"][0]) == 2
    assert int(wall_contacts["wall_id"][0]) != 0xFFFF
    assert int(wall_out["on_ground"][0]) == 1
    assert int(wall_out["ground_id"][0]) == 1

    ceil_seed = _seed_base(STAGE_FOD, ACT_WAIT, SM_WAIT1_0, -40.0, -175.0)
    ceil_seed["on_ground"][0, 0] = np.uint8(1)
    ceil_seed["ground_id"][0, 0] = np.uint16(5)
    ceil_seed["speed_y_self"][0, 0] = np.float32(-80.0)

    ceil_out, ceil_contacts, colldata = _step_once_with_contacts_and_colldata(ceil_seed)
    flags = int(ceil_contacts["coll_env_flags"][0])
    assert flags & COLLIDE_CEILING_MASK
    assert flags & COLLIDE_FLOOR_MASK
    assert int(ceil_contacts["ceiling_id"][0]) == 8
    assert int(ceil_out["on_ground"][0]) == 1
    assert int(ceil_out["ground_id"][0]) == 5
    assert int(colldata["squeeze_restore_valid"][0]) == 1


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_ATTACK_AIR_N, 68),
        (ACT_ESCAPE_AIR, SM_ESCAPE_AIR),
        (ACT_DAMAGE_FALL, SM_DAMAGE_FALL),
        (ACT_DAMAGE_FLY_TOP, 180),
        (ACT_ITEM_PARASOL_FALL, 0),
        (ACT_CATCH, 0),
        (ACT_THROW_F, 0),
        (ACT_CAPTURE_DAMAGE_HI, 0),
        (ACT_FX_SPECIAL_AIR_N_START, SM_FX_SPECIAL_N_START),
        (ACT_FX_SPECIAL_AIR_LW_START, 0),
    ],
)
def test_phase3_excluded_owners_do_not_use_common_air_platform_skip(
    action_id: int, submotion_id: int
) -> None:
    # Negative runtime boundary for Phase 3: later-owner families may have their own collision
    # behavior, but they must not enter the common-air ftCo_80096CC8 floor-skip route.
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(STAGE_FOD, action_id, submotion_id, -35.0, float(world_y) - 2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 14.0)
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)

    _out, _contacts, colldata = _step_once_with_contacts_and_colldata(
        seed, _input_bytes(), input_t
    )

    assert int(colldata["floor_skip_valid"][0]) == 0
    assert int(colldata["floor_skip_segment_id"][0]) == 0xFFFF


@pytest.mark.parametrize(
    ("action_id", "submotion_id", "expected_action"),
    [
        (ACT_ATTACK_AIR_N, SM_ATTACK_AIR_N, ACT_LANDING_AIR_N),
        (ACT_ESCAPE_AIR, SM_ESCAPE_AIR, ACT_LANDING_FALL_SPECIAL),
        (ACT_DAMAGE_FALL, SM_DAMAGE_FALL, ACT_DOWN_BOUND_U),
    ],
)
def test_phase4_later_owners_forward_joint_filters_to_ordered_floor(
    action_id: int, submotion_id: int, expected_action: int
) -> None:
    # Phase 4 AttackAir/EscapeAir/Damage callbacks route through the Phase 2 ordered floor
    # producer, including CollData joint filters. The same crossing lands with no filter or a
    # matching joint-only filter, and stays airborne when the owning joint is skipped or excluded.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpCheckFloor}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    floor_seg = msl_binding.stage_floor_segment(STAGE_POKEMON, 34)
    assert floor_seg is not None
    joint_id = int(floor_seg["joint_id"])

    seed = _seed_base(STAGE_POKEMON, action_id, submotion_id, 0.0, -2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(12.0)
    if action_id == ACT_DAMAGE_FALL:
        seed["hitstun"][0, 0] = np.uint8(10)

    inp = np.zeros((1, input_stride), dtype=np.uint8)

    def run(skip: int, only: int) -> tuple[int, int, int, int]:
        handle = msl_binding.init(
            batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1
        )
        try:
            compare = np.zeros((1, compare_stride), dtype=np.uint8)
            colldata = np.zeros((1, colldata_stride), dtype=np.uint8)
            msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
            msl_binding.debug_set_mpcoll_joint_filters(handle, 0, 0, skip, only)
            msl_binding.step_input(handle, inp, inp)
            msl_binding.write_compare(handle, compare)
            msl_binding.debug_write_colldata_ecb(handle, colldata)
            row = compare.view(COMPARE_DTYPE).reshape((1,))[0]
            snap = colldata.view(_colldata_ecb_dtype()).reshape((1,))[0]
            return (
                int(row["action_id"][0]),
                int(row["on_ground"][0]),
                int(snap["floor_result_valid"][0]),
                int(snap["floor_result_segment_id"][0]),
            )
        finally:
            msl_binding.destroy(handle)

    assert run(-1, -1) == (expected_action, 1, 1, 34)
    assert run(joint_id, -1) == (action_id, 0, 0, 0xFFFF)
    assert run(-1, joint_id) == (expected_action, 1, 1, 34)
    assert run(-1, joint_id + 1) == (action_id, 0, 0, 0xFFFF)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_ATTACK_AIR_N, SM_ATTACK_AIR_N),
        (ACT_ESCAPE_AIR, SM_ESCAPE_AIR),
        (ACT_DAMAGE_FALL, SM_DAMAGE_FALL),
        (ACT_DAMAGE_FLY_TOP, SM_DAMAGE_FLY_TOP),
    ],
)
def test_phase4_later_owners_reject_no_crossing_floor_sweeps(
    action_id: int, submotion_id: int
) -> None:
    # Source floor producers require an actual bottom/root crossing for these callbacks. A
    # non-crossing row must not synthesize a Phase 4 landing from action family alone.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
    seed = _seed_base(STAGE_POKEMON, action_id, submotion_id, 0.0, 20.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_y_self"][0, 0] = np.float32(-0.5)
    seed["speed_y_attack"][0, 0] = np.float32(-0.5)
    seed["hitstun"][0, 0] = np.uint8(10 if action_id == ACT_DAMAGE_FLY_TOP else 0)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(21.0)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][0]) == action_id
    assert int(out["on_ground"][0]) == 0
    assert int(colldata["floor_result_valid"][0]) == 0
    assert int(colldata["floor_result_segment_id"][0]) == 0xFFFF


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_ATTACK_AIR_N, SM_ATTACK_AIR_N),
        (ACT_ESCAPE_AIR, SM_ESCAPE_AIR),
        (ACT_DAMAGE_FLY_TOP, SM_DAMAGE_FLY_TOP),
    ],
)
def test_phase4_later_owners_publish_wall_and_ceiling_contacts(
    action_id: int, submotion_id: int
) -> None:
    # AttackAir/EscapeAir use ft_80082C74 -> ft_80081D0C -> mpColl_800471F8; DamageFly uses
    # ft_80081DD4. Both enter the shared airborne wall/ceiling envelope through the Phase 4 owner
    # gate, so static wall and ceiling contacts publish through the same CollData env surface.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C,ft_80081DD4}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
    wall_seed = _seed_base(STAGE_YOSHI, action_id, submotion_id, 49.0, -13.0)
    wall_seed["on_ground"][0, 0] = np.uint8(0)
    wall_seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    wall_seed["speed_air_x_self"][0, 0] = np.float32(5.0)
    wall_seed["speed_x_attack"][0, 0] = np.float32(5.0)
    wall_seed["action_frame"][0, 0] = np.int16(4)
    wall_seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    wall_seed["hitstun"][0, 0] = np.uint8(10 if action_id == ACT_DAMAGE_FLY_TOP else 0)

    _wall_out, wall_contacts = _step_once_with_contacts(wall_seed)

    assert int(wall_contacts["wall_kind"][0]) == 2
    assert int(wall_contacts["wall_id"][0]) == 17
    assert int(wall_contacts["coll_env_flags"][0]) & COLLIDE_RIGHT_WALL_MASK

    ceiling_seed = _seed_base(STAGE_BATTLEFIELD, action_id, submotion_id, 0.0, -44.0)
    ceiling_seed["on_ground"][0, 0] = np.uint8(0)
    ceiling_seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    ceiling_seed["speed_y_self"][0, 0] = np.float32(10.0)
    ceiling_seed["speed_y_attack"][0, 0] = np.float32(10.0)
    ceiling_seed["action_frame"][0, 0] = np.int16(4)
    ceiling_seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    ceiling_seed["hitstun"][0, 0] = np.uint8(10 if action_id == ACT_DAMAGE_FLY_TOP else 0)

    _ceil_out, ceiling_contacts = _step_once_with_contacts(ceiling_seed)

    assert int(ceiling_contacts["ceiling_id"][0]) == 8
    assert int(ceiling_contacts["coll_env_flags"][0]) & COLLIDE_CEILING_MASK


@pytest.mark.integration
def test_specialairhi_platform_contact_writes_floor_skip_and_stays_airborne() -> None:
    # Vanilla reference: /home/kyle/Slippi/2026-05/Game_20260506T201935.slp, P1 Battlefield
    # frames 29-32. Downward Firefox touches the left platform, stays in SpecialAirHi/airborne, and
    # then continues below it because ftFox_SpecialHi_IsBound calls ftCo_8009A134 on platforms.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    import msl_binding

    seed = _seed_specialairhi_downward(x=-38.8, y=36.0, prev_y=40.0, ground_id=2)
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    input_stride = int(sizes["input"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        rows = []
        for _ in range(5):
            msl_binding.step_input(handle, inp, inp)
            msl_binding.write_compare(handle, out)
            rows.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
    finally:
        msl_binding.destroy(handle)

    assert int(rows[3]["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(rows[3]["on_ground"][0]) == 0
    assert int(rows[3]["ground_id"][0]) == 2
    assert float(rows[3]["pos_y"][0]) == pytest.approx(27.2001, abs=1e-3)

    assert int(rows[4]["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(rows[4]["on_ground"][0]) == 0
    assert int(rows[4]["ground_id"][0]) == 2
    assert float(rows[4]["pos_y"][0]) < 25.0


@pytest.mark.integration
def test_specialairhi_shallow_hard_floor_contact_keeps_floor_correction_airborne() -> None:
    # Source split: ftFx_SpecialAirHi_Coll calls ft_CheckGroundAndLedge -> mpColl_800473CC.
    # A shallow floor.normal/self_vel angle does not enter SpecialHiBound, but mpColl's
    # stay-airborne floor path still applies the floor correction and leaves floor env flags for
    # the callback's facing/rotateModel update.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800473CC,mpColl_80044628_Floor,mpColl_80044948_Floor}
    import msl_binding

    seed = _seed_specialairhi_downward(x=-38.8, y=-8.0, prev_y=0.0, ground_id=1)
    seed["speed_air_x_self"][0, 0] = np.float32(-10.0)
    seed["speed_y_self"][0, 0] = np.float32(-0.1)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["facing"][0, 0] = np.uint8(1)

    got, contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(got["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(got["on_ground"][0]) == 0
    assert int(got["ground_id"][0]) == 1
    assert int(got["facing"][0]) == 0
    assert int(contacts["coll_env_flags"][0]) & COLLIDE_FLOOR_MASK
    assert int(colldata["floor_result_segment_id"][0]) == 1
    assert float(colldata["floor_result_normal_x"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(colldata["floor_result_normal_y"][0]) == pytest.approx(1.0, abs=1e-6)
    assert float(got["pos_y"][0]) == pytest.approx(0.0001, abs=1e-5)


@pytest.mark.integration
def test_specialairhi_hard_floor_contact_still_enters_bound() -> None:
    # Negative boundary: ftCo_8009A134 only returns true on platforms. Hard-floor contact remains a
    # SpecialHiBound owner, but the floor hit is owned by the live XRotN-rotated JObj ECB bottom,
    # not the root or static SSANIM ECB. This synthetic row therefore stays airborne until that
    # bottom crosses the floor, then enters Bound on the first source-owned hard-floor contact.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
    import msl_binding

    seed = _seed_specialairhi_downward(x=-38.8, y=0.7, prev_y=2.9, ground_id=1)
    seed["speed_y_self"][0, 0] = np.float32(-2.2)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(20.0)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    input_stride = int(sizes["input"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        rows = []
        for _ in range(3):
            msl_binding.step_input(handle, inp, inp)
            msl_binding.write_compare(handle, out)
            rows.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
    finally:
        msl_binding.destroy(handle)

    assert int(rows[0]["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(rows[0]["on_ground"][0]) == 0
    assert int(rows[1]["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert int(rows[1]["on_ground"][0]) == 0
    assert int(rows[2]["action_id"][0]) == ACT_FX_SPECIAL_HI_BOUND


@pytest.mark.integration
def test_escapeair_pokemon_ledge_floor_handoff_matches_vanilla_probe() -> None:
    # Vanilla reference: a Dolphin-orchestrated ledgedash probe from
    # replays/validation/pokemon_stadium_recent/ThisVioletRaccoon.slpz lands on frozen Pokemon's
    # right ledge floor one frame after EscapeAir starts:
    #
    #   reports/triage/legal_stage_modelplay_misc/vanilla_pokemon_ledgedash
    #   P2 raw frame 1720: EscapeAir x=86.210 y=-4.389 vx=-2.392 vy=-1.435
    #   P2 raw frame 1721: LandingFallSpecial x=84.057 y=0.000
    #
    # Source owner: EscapeAir_Coll calls ft_80082C74 -> mpColl_800471F8, which loads the live
    # EscapeAir ECB with flags=6. That path does not force desired_ecb.bottom.y to zero while
    # CollData_X130_Locked is active, so a downward ledgedash can resolve the ledge floor instead of
    # passing through the stage.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 86.210, -4.389)
    seed["char_id"][0, :2] = np.uint8(CHAR_FALCO)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(88.603)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-2.954)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.392)
    seed["speed_y_self"][0, 0] = np.float32(-1.435)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(53)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(4)
    seed["facing"][0, 0] = np.uint8(0)
    seed["jumps_left"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 54
    assert float(out["pos_x"][0]) == pytest.approx(84.057, abs=1e-3)
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-6)


@pytest.mark.integration
def test_escapeair_pokemon_carried_ledge_floor_synthetic_boundary() -> None:
    # Synthetic boundary for the vanilla-probed source owner above. This proves that a seeded
    # EscapeAir row carrying the right ledge floor can publish the adjacent right ledge floor through
    # `ftCo_EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8`.
    #
    # This lock complements the live cliff-floor root-crossing test below: replay one-step rows can
    # seed the same EscapeAir floor owner directly, while modelplay/free-run traces materialize it
    # through the x2064 cliff-floor lifetime.
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 84.924, -3.474)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(86.700)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-1.699)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.598)
    seed["speed_y_self"][0, 0] = np.float32(-1.598)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(53)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["facing"][0, 0] = np.uint8(0)
    seed["jumps_left"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 54
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-6)


def test_escapeair_pokemon_cliff_ledge_floor_owner_uses_root_crossing() -> None:
    # Manual set12 PS ledgedash shape: a live ledge-release x2064 owner carries Pokemon's right
    # ledge floor through JumpAerial -> EscapeAir. The callback root crosses from above the ledge
    # floor into the generated ledge span before the raw ECB bottom reaches y=0, and source
    # `mpColl_80044838_Floor` may publish the stored ledge floor from that root crossing.
    #
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 84.924278, -3.474371)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(54)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ledge_cooldown"][0, 0] = np.uint8(22)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(54)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(88.672646)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(0.274002)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.598)
    seed["speed_y_self"][0, 0] = np.float32(-1.598)
    seed["facing"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 54
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-6)


def test_escapeair_pokemon_cliff_ledge_floor_root_crossing_requires_vertical_crossing() -> None:
    # The hidden cliff-floor owner is not a broad ledge magnet: if the root was already below the
    # generated ledge floor, preserve airborne EscapeAir.
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 84.924278, -3.474371)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(54)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ledge_cooldown"][0, 0] = np.uint8(22)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(54)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(86.7)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-1.698826)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.598)
    seed["speed_y_self"][0, 0] = np.float32(-1.598)
    seed["facing"][0, 0] = np.uint8(0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0


def test_escapeair_pokemon_cliff_ledge_floor_root_crossing_requires_frame_start_below_floor() -> None:
    # SweatyThisMallard regression guard: Pokemon ledge-release EscapeAir can carry the live cliff
    # floor owner while root is still above the flat ledge floor. Vanilla does not publish
    # LandingFallSpecial from the late root-crossing path until the callback frame starts below the
    # floor and then crosses/deepens through the source cliff floor.
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 75.044533, 0.170612)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(9)
    seed["anim_frame_f32"][0, 0] = np.float32(9.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(54)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(8)
    seed["ecb_lock_timer"][0, 0] = np.uint8(0)
    seed["ledge_cooldown"][0, 0] = np.uint8(10)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(54)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(75.952118)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(0.957185)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.786573)
    seed["speed_y_self"][0, 0] = np.float32(-0.786573)
    seed["facing"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0


def test_escapeair_pokemon_jump_entry_bottom_sweep_lands_on_terminal_ledge_floor() -> None:
    # pokemon_ledgedash_clip_are_you_fucking_kidding.msltrace.json reaches this source shape from
    # match-start rollout: JumpB enters EscapeAir from the right ledge, the first two EscapeAir rows
    # stay airborne, and frame 3 has a real ECB-bottom sweep through Pokemon's terminal ledge floor.
    # Source EscapeAir_Coll still owns that bottom hit through ft_80082C74/mpColl_800471F8; this is
    # not a stale root projection or ledge magnet.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 84.435631, -3.776449)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(54)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_B)
    seed["seed_prev_action_frame"][0, 0] = np.int16(3)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ledge_cooldown"][0, 0] = np.uint8(18)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(54)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(86.377617)
    # floor_sweep_prev_pos_* is the callback-entry previous root used by source `mpCollPrev`.
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(0.097050)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.748)
    seed["speed_y_self"][0, 0] = np.float32(-1.433)
    seed["facing"][0, 0] = np.uint8(0)
    seed["jumps_left"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 54
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-6)


def test_escapeair_pokemon_jump_entry_terminal_ledge_requires_bottom_crossing() -> None:
    # Boundary for the source owner above: fresh JumpF/B provenance alone is not enough. If the
    # current callback ECB bottom remains above the terminal ledge floor, the locked EscapeAir ledge
    # suppression remains in force.
    seed = _seed_base(STAGE_POKEMON, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 84.435631, -2.2)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(54)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_B)
    seed["seed_prev_action_frame"][0, 0] = np.int16(3)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ledge_cooldown"][0, 0] = np.uint8(18)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(54)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(86.377617)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-0.7)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.748)
    seed["speed_y_self"][0, 0] = np.float32(-0.2)
    seed["facing"][0, 0] = np.uint8(0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0


def test_ottotto_neutral_b_enters_grounded_blaster_from_source_iasa() -> None:
    # ftCo_Ottotto_IASA reaches the same grounded B-special dispatchers as Wait:
    # SpecialS -> ftCo_800D6824 -> ftCo_800D68C0 before attack/guard/jump/dash/turn/walk.
    # This proves teeter can consume a neutral-B edge into grounded Blaster through the source
    # IASA chain, rather than requiring a Teeter-specific special-case.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6824,ftCo_800D68C0}
    seed = _seed_base(STAGE_YOSHI, ACT_OTTOTTO, SM_OTTOTTO, 56.0, -3.5)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["facing"][0, 0] = np.uint8(0)

    cur = _input_bytes()
    view = cur.view(INPUT_DTYPE).reshape((1,))
    view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)

    out = _step_once(seed, input_t=cur)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_N_START
    assert int(out["on_ground"][0]) == 1


def test_ottotto_down_b_enters_grounded_reflector_from_source_iasa() -> None:
    # Negative/priority companion for the neutral-B teeter lock: the same `ftCo_800D68C0` source
    # chain admits grounded Reflector from B+down, but the helper still uses the normal side-special
    # precedence and character support gates.
    seed = _seed_base(STAGE_YOSHI, ACT_OTTOTTO, SM_OTTOTTO, 56.0, -3.5)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["facing"][0, 0] = np.uint8(0)

    cur = _input_bytes()
    view = cur.view(INPUT_DTYPE).reshape((1,))
    view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    view["p"]["main_y"][0, 0] = np.int8(-100)

    out = _step_once(seed, input_t=cur)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_LW_START
    assert int(out["on_ground"][0]) == 1


def test_yoshi_reflector_platform_pass_keeps_floor_skip_through_air_loop() -> None:
    # Manual set12 Shine-on-Randall repro: grounded Reflector Start consumes
    # `ftFx_SpecialLwStart_Pass -> ftCo_8009A184`, which writes CollData.floor_skip for the
    # platform being passed and creates the aerial reflect hit. The skip must survive the same
    # platform-pass Shine episode long enough for sustained SpecialAirLwLoop_Coll to reject the
    # platform; otherwise Fox snaps back onto Randall as soon as the ECB bottom crosses it.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A184
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    import msl_binding

    sizes = msl_binding.sizes()
    compare_stride = int(sizes["compare"])
    input_stride = int(sizes["input"])
    config = build_match_config_array(
        num_players=2,
        char_ids=(CHAR_FOX, CHAR_FALCO),
        team_ids=(0, 1),
        facing=(1, 0),
        stage_id=STAGE_YOSHI,
        frame_id=0,
        random_seed=4,
    )
    neutral = np.zeros((1, input_stride), dtype=np.uint8)
    down = np.zeros((1,), dtype=INPUT_DTYPE)
    down["p"][0, 0]["main_y"] = np.int8(-80)
    down_b = down.copy()
    down_b["p"][0, 0]["buttons"] = np.uint16(BUTTON_B)
    down_b_bytes = down_b.view(np.uint8).reshape((1, input_stride))
    down_bytes = down.view(np.uint8).reshape((1, input_stride))
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = neutral
    row = None

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame in range(138):
            if frame == 126:
                cur_input = down_bytes
            elif 127 <= frame <= 136:
                cur_input = down_b_bytes
            else:
                cur_input = neutral
            msl_binding.step_input(handle, prev_input, cur_input)
            msl_binding.write_compare(handle, out_bytes)
            row = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            prev_input = cur_input.copy()
    finally:
        msl_binding.destroy(handle)

    assert row is not None
    assert int(row["action_id"][0]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(row["on_ground"][0]) == 0
    assert int(row["ground_id"][0]) == 1
    assert float(row["pos_y"][0]) < 18.0


def _run_cliff_wait_ledgedash(
    *,
    stage_id: int,
    char_id: int,
    x: float,
    y: float,
    facing: int,
    ground_id: int,
    percent: float,
    action_frame: int,
    inputs: list[tuple[int, int, int, int]],
) -> list[np.void]:
    seed = _seed_base(stage_id, ACT_CLIFF_WAIT, SM_CLIFF_WAIT, x, y)
    seed["char_id"][0, :2] = np.uint8(char_id)
    seed["facing"][0, 0] = np.uint8(facing)
    seed["jumps_left"][0, 0] = np.uint8(1)
    seed["action_frame"][0, 0] = np.int16(action_frame)
    seed["anim_frame_f32"][0, 0] = np.float32(action_frame)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(ground_id)
    seed["percent"][0, 0] = np.float32(percent)
    # These synthetic ledge-dash fixtures start after CliffWait has already seen a neutral
    # stick/c-stick IASA frame, so the source mv.co.cliff.x8 option latch is live before the
    # first held-down ledge option input.
    seed["cliff_option_stick_latch_x8"][0, 0] = np.uint8(1)

    def set_input(arr: np.ndarray, buttons: int, main_x: int, main_y: int, r: int = 0) -> None:
        view = arr.view(INPUT_DTYPE).reshape((1,))
        view["p"]["buttons"][0, 0] = np.uint16(buttons)
        view["p"]["main_x"][0, 0] = np.int8(main_x)
        view["p"]["main_y"][0, 0] = np.int8(main_y)
        view["p"]["r"][0, 0] = np.uint8(r)

    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = _input_bytes()
    input_t = _input_bytes()

    rows = []
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        previous = (0, 0, 0, 0)
        for current in inputs:
            set_input(prev_input, *previous)
            set_input(input_t, *current)
            msl_binding.step_input(handle, prev_input, input_t)
            msl_binding.write_compare(handle, out)
            rows.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            previous = current
    finally:
        msl_binding.destroy(handle)
    return rows


def _run_direct_fall_cliff_exit_ledgedash(
    *,
    stage_id: int,
    char_id: int,
    x: float,
    y: float,
    facing: int,
    ground_id: int,
    cliff_floor_id: int,
    ledge_cooldown: int,
    inputs: list[tuple[int, int, int, int]],
) -> list[np.void]:
    seed = _seed_base(stage_id, ACT_FALL, SM_FALL, x, y)
    seed["char_id"][0, :2] = np.uint8(char_id)
    seed["facing"][0, 0] = np.uint8(facing)
    seed["jumps_left"][0, 0] = np.uint8(1)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(ground_id)
    seed["ledge_cooldown"][0, 0] = np.uint8(ledge_cooldown)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(cliff_floor_id)

    def set_input(arr: np.ndarray, buttons: int, main_x: int, main_y: int, r: int = 0) -> None:
        view = arr.view(INPUT_DTYPE).reshape((1,))
        view["p"]["buttons"][0, 0] = np.uint16(buttons)
        view["p"]["main_x"][0, 0] = np.int8(main_x)
        view["p"]["main_y"][0, 0] = np.int8(main_y)
        view["p"]["r"][0, 0] = np.uint8(r)

    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = _input_bytes()
    input_t = _input_bytes()

    rows = []
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        previous = (0, 0, 0, 0)
        for current in inputs:
            set_input(prev_input, *previous)
            set_input(input_t, *current)
            msl_binding.step_input(handle, prev_input, input_t)
            msl_binding.write_compare(handle, out)
            rows.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
            previous = current
    finally:
        msl_binding.destroy(handle)
    return rows


def _first_static_ledge_floor_fixture(*, sloped: bool):
    for stage_id in sorted(STAGE_METADATA_BIN_BY_STAGE_ID):
        stage_path = stage_metadata_path_for_stage_id(stage_id, Path("data"))
        assert stage_path is not None
        stage = read_mslstg01_v7(stage_path)
        stale_floor = next(
            (
                seg
                for seg in stage.segments
                if int(seg.kind_id) == 0
                and bool(seg.fighter_solid)
                and (int(seg.flags) & 2) == 0
            ),
            None,
        )
        if stale_floor is None:
            continue
        for seg in stage.segments:
            if (
                int(seg.kind_id) == 0
                and bool(seg.fighter_solid)
                and (int(seg.flags) & 2) != 0
                and ((float(seg.y0) != float(seg.y1)) == sloped)
            ):
                return stage_id, seg, stale_floor
    raise AssertionError("missing static ledge floor fixture")


def _metadata_sloped_cliff_floor_prefix_fixture() -> tuple[int, int, int, float, float, int, int]:
    stage_id, ledge_floor, stale_floor = _first_static_ledge_floor_fixture(sloped=True)
    min_x = min(float(ledge_floor.x0), float(ledge_floor.x1))
    max_x = max(float(ledge_floor.x0), float(ledge_floor.x1))
    min_y = min(float(ledge_floor.y0), float(ledge_floor.y1))
    midpoint_x = (min_x + max_x) * 0.5
    left_ledge = midpoint_x < 0.0
    facing = 1 if left_ledge else 0
    outside_x = min_x - 1.92 if left_ledge else max_x + 1.92
    start_y = min_y - 14.4
    return (
        stage_id,
        int(ledge_floor.line_id),
        int(stale_floor.line_id),
        outside_x,
        start_y,
        facing,
        1 if left_ledge else -1,
    )


def _run_metadata_sloped_cliff_floor_prefix(
    *, cliff_floor_id: int | None = None, ledge_cooldown: int = 20
) -> tuple[list[np.void], int, int]:
    (
        stage_id,
        generated_sloped_ledge_floor_id,
        stale_floor_id,
        x,
        y,
        facing,
        inward_sign,
    ) = _metadata_sloped_cliff_floor_prefix_fixture()
    carried_floor = generated_sloped_ledge_floor_id if cliff_floor_id is None else cliff_floor_id
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=stage_id,
        char_id=CHAR_FOX,
        x=x,
        y=y,
        facing=facing,
        ground_id=stale_floor_id,
        cliff_floor_id=carried_floor,
        ledge_cooldown=ledge_cooldown,
        inputs=[
            (BUTTON_Y, 96 * inward_sign, -40, 0),
            (BUTTON_Y, 96 * inward_sign, -40, 0),
            (BUTTON_Y, 96 * inward_sign, -40, 0),
            (BUTTON_Y, 96 * inward_sign, -40, 0),
            (BUTTON_Y | BUTTON_R, 80 * inward_sign, -48, 255),
            (BUTTON_Y | BUTTON_R, 80 * inward_sign, -48, 255),
            (BUTTON_Y | BUTTON_R, 80 * inward_sign, -48, 255),
        ],
    )
    return rows, generated_sloped_ledge_floor_id, stale_floor_id


def _seed_cliff_owned_floor_handoff(
    *, sloped: bool, offspan: bool = False, no_crossing: bool = False, current_floor: bool = False
) -> np.ndarray:
    stage_id, ledge_floor, stale_floor = _first_static_ledge_floor_fixture(sloped=sloped)
    min_x = min(float(ledge_floor.x0), float(ledge_floor.x1))
    max_x = max(float(ledge_floor.x0), float(ledge_floor.x1))
    x = (min_x + max_x) * 0.5
    if offspan:
        x = max_x + 4.0
    dx = float(ledge_floor.x1) - float(ledge_floor.x0)
    t = 0.0 if abs(dx) < 1e-6 else (x - float(ledge_floor.x0)) / dx
    line_y = float(ledge_floor.y0) + ((float(ledge_floor.y1) - float(ledge_floor.y0)) * t)

    seed = _seed_base(stage_id, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, x, line_y - 3.474371)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)
    seed["ledge_cooldown"][0, 0] = np.uint8(22)
    seed["cliff_ledge_floor_segment_id_u16"][0, 0] = np.uint16(int(ledge_floor.line_id))
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(line_y + (-1.0 if no_crossing else 1.0))
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.598)
    seed["facing"][0, 0] = np.uint8(0)
    if current_floor:
        seed["ground_id"][0, 0] = np.uint16(int(ledge_floor.line_id))
    return seed


@pytest.mark.integration
def test_cliff_owned_sloped_ledge_floor_prefix_lands_from_jumpaerial_escapeair() -> None:
    # Compact source-state prefix for the carried cliff-floor owner: start before the terminal row
    # in Fall with live cliff floor/cooldown state, then route through JumpAerial -> EscapeAir. The
    # generated sloped ledge floor comes from MSLSTG01 metadata and is admitted only when the current
    # EscapeAir callback's bottom/root floor producer reaches that carried floor.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    rows, carried_floor, _stale_floor = _run_metadata_sloped_cliff_floor_prefix()

    assert any(int(row["action_id"][0]) in (ACT_JUMP_AERIAL_F, ACT_JUMP_AERIAL_B) for row in rows)
    assert any(int(row["action_id"][0]) == ACT_ESCAPE_AIR for row in rows[:-1])
    landed = rows[-1]
    assert int(landed["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(landed["on_ground"][0]) == 1
    assert int(landed["ground_id"][0]) == carried_floor


@pytest.mark.integration
@pytest.mark.parametrize(
    ("cliff_floor_delta", "ledge_cooldown"),
    [
        (1, 20),
        (0, 0),
    ],
)
def test_cliff_owned_sloped_ledge_floor_prefix_rejects_wrong_or_expired_owner(
    cliff_floor_delta: int, ledge_cooldown: int
) -> None:
    _rows, carried_floor, _stale_floor = _run_metadata_sloped_cliff_floor_prefix()
    rows, _expected_floor, _stale_floor = _run_metadata_sloped_cliff_floor_prefix(
        cliff_floor_id=carried_floor + cliff_floor_delta,
        ledge_cooldown=ledge_cooldown,
    )

    final = rows[-1]
    assert int(final["action_id"][0]) != ACT_LANDING_FALL_SPECIAL
    assert int(final["on_ground"][0]) == 0


@pytest.mark.parametrize("sloped", [False, True])
def test_cliff_owned_floor_handoff_accepts_static_ledge_floor_source_sweep(sloped: bool) -> None:
    seed = _seed_cliff_owned_floor_handoff(sloped=sloped)
    carried_floor = int(seed["cliff_ledge_floor_segment_id_u16"][0, 0])
    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == carried_floor


@pytest.mark.parametrize("sloped", [False, True])
def test_cliff_owned_floor_handoff_accepts_current_carried_ledge_floor(
    sloped: bool,
) -> None:
    seed = _seed_cliff_owned_floor_handoff(sloped=sloped, current_floor=True)
    carried_floor = int(seed["cliff_ledge_floor_segment_id_u16"][0, 0])
    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == carried_floor


@pytest.mark.parametrize("sloped", [False, True])
def test_cliff_owned_floor_handoff_rejects_fresh_entry_current_floor_provenance(
    sloped: bool,
) -> None:
    seed = _seed_cliff_owned_floor_handoff(sloped=sloped, current_floor=True)
    carried_floor = int(seed["cliff_ledge_floor_segment_id_u16"][0, 0])
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(3)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == carried_floor


@pytest.mark.parametrize(
    ("offspan", "no_crossing"),
    [
        (True, False),
        (False, True),
    ],
)
def test_cliff_owned_floor_handoff_rejects_missing_source_floor_producer(
    offspan: bool, no_crossing: bool
) -> None:
    seed = _seed_cliff_owned_floor_handoff(sloped=True, offspan=offspan, no_crossing=no_crossing)
    carried_floor = int(seed["cliff_ledge_floor_segment_id_u16"][0, 0])
    out = _step_once_rollout(
        seed
    )

    assert int(out["ground_id"][0]) != carried_floor
    if no_crossing:
        assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
        assert int(out["on_ground"][0]) == 0


@pytest.mark.parametrize(
    ("cliff_floor_id", "ledge_cooldown"),
    [
        (2, 20),
        (6, 0),
        (3, 20),
        (0xFFFF, 20),
    ],
)
def test_cliff_owned_floor_handoff_rejects_wrong_expired_or_nonledge_owner(
    cliff_floor_id: int, ledge_cooldown: int
) -> None:
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=57.91999816894531,
        y=-17.9,
        facing=0,
        ground_id=3,
        cliff_floor_id=cliff_floor_id,
        ledge_cooldown=ledge_cooldown,
        inputs=[
            (0, 0, -40, 0),
            (0, 0, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
        ],
    )

    final = rows[-1]
    assert int(final["action_id"][0]) != ACT_LANDING_FALL_SPECIAL
    assert int(final["on_ground"][0]) == 0


@pytest.mark.integration
def test_battlefield_ledge_drop_jump_airdodge_uses_source_cliff_floor_owner() -> None:
    # Manual modelplay repro:
    # reports/modelplay/20260506_231300_full_matrix_legal_stages_rl_doubles_v27_7000/
    # env_005_battlefield_falco_vs_falco/trace.json, P2 frames 799-806.
    #
    # Source owner:
    # - Cliff state stores `mv.co.cliff.ledge_id`, chosen from Collide_*LedgeGrab in
    #   ftCliffCommon_80081370.
    # - CliffWait drop enters Fall through ftCo_8009AAFC while that ledge floor identity is still the
    #   source cliff/CollData owner, even though Slippi's visible lastGroundId can be stale.
    # - Fall -> JumpAerial -> EscapeAir then reaches ftCo_EscapeAir_Coll -> ft_80082C74 ->
    #   mpColl_800471F8 and projects against that same floor owner.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    rows = _run_cliff_wait_ledgedash(
        stage_id=STAGE_BATTLEFIELD,
        char_id=CHAR_FALCO,
        x=70.5999984741211,
        y=-16.5,
        facing=0,
        ground_id=0,
        percent=59.84,
        action_frame=6,
        inputs=[
            (0, 0, -64, 0),
            (BUTTON_Y, 0, -40, 0),
            (BUTTON_Y, -96, -40, 0),
            (BUTTON_Y, -96, -40, 0),
            (BUTTON_Y, -96, -40, 0),
            (BUTTON_Y | BUTTON_R, -80, -48, 255),
            (BUTTON_Y | BUTTON_R, -80, -48, 255),
        ],
    )

    assert int(rows[5]["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(rows[5]["on_ground"][0]) == 0
    # The retained current owner keeps this shallow ledge-floor EscapeAir handoff airborne until the
    # loaded EscapeAir bottom actually owns the floor contact. Do not broaden the synthetic
    # Cliff/CollData lane back into an immediate ledge-floor root snap.
    assert int(rows[6]["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(rows[6]["on_ground"][0]) == 0


@pytest.mark.integration
def test_direct_fall_reseed_carries_seeded_cliff_floor_through_jump_and_escapeair() -> None:
    # Direct one-step seeds can start after CliffWait has already released into Fall. The hidden
    # floor owner must come from MslSeed and carry through Fall -> JumpAerial -> EscapeAir while
    # the source bottom/root floor producer accepts the carried ledge floor.
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=-57.91999816894531,
        y=-17.9,
        facing=1,
        ground_id=0,
        cliff_floor_id=2,
        ledge_cooldown=20,
        inputs=[
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
        ],
    )

    before = rows[-2]
    landed = rows[-1]
    assert int(before["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(before["on_ground"][0]) == 0
    assert int(landed["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(landed["on_ground"][0]) == 1
    assert int(landed["ground_id"][0]) == 2


@pytest.mark.integration
def test_restored_cliff_floor_without_live_source_producer_stays_airborne() -> None:
    # Source owner:
    # - CliffCatch/CliffWait store `mv.co.cliff.ledge_id`, and x2064 ledge cooldown proves that the
    #   cliff-owned floor lane is still live.
    # - That restored lane is provenance, not publication. EscapeAir_Coll may publish the carried
    #   floor only after the current callback's mpColl floor producer accepts the candidate.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=57.91999816894531,
        y=-17.9,
        facing=0,
        ground_id=3,
        cliff_floor_id=6,
        ledge_cooldown=20,
        inputs=[
            (0, 0, -40, 0),
            (0, 0, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
        ],
    )

    before = rows[11]
    landed = rows[12]
    assert int(before["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(before["on_ground"][0]) == 0
    assert int(before["ground_id"][0]) == 3
    assert int(landed["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(landed["on_ground"][0]) == 0


@pytest.mark.parametrize(
    ("cliff_floor_id", "ledge_cooldown", "expected_action", "expected_grounded"),
    [
        (0xFFFF, 20, ACT_ESCAPE_AIR, 0),
        (6, 0, ACT_CLIFF_WAIT, 0),
        (2, 20, ACT_ESCAPE_AIR, 0),
        (6, 20, ACT_ESCAPE_AIR, 0),
    ],
)
@pytest.mark.integration
def test_restored_cliff_floor_requires_live_source_producer(
    cliff_floor_id: int, ledge_cooldown: int, expected_action: int, expected_grounded: int
) -> None:
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=57.91999816894531,
        y=-17.9,
        facing=0,
        ground_id=3,
        cliff_floor_id=cliff_floor_id,
        ledge_cooldown=ledge_cooldown,
        inputs=[
            (0, 0, -40, 0),
            (0, 0, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y, -40, -40, 0),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
            (BUTTON_Y | BUTTON_R, -40, -40, 255),
        ],
    )

    final = rows[12]
    assert int(final["action_id"][0]) == expected_action
    assert int(final["on_ground"][0]) == expected_grounded


@pytest.mark.parametrize(
    ("cliff_floor_id", "ledge_cooldown", "expected_action", "expected_grounded"),
    [
        (0xFFFF, 20, ACT_ESCAPE_AIR, 0),  # direct later reseed without the hidden lane
        (2, 0, ACT_ESCAPE_AIR, 0),  # cooldown expired
        (6, 20, ACT_ESCAPE_AIR, 0),  # wrong-side reconstructed owner
        (2, 20, ACT_LANDING_FALL_SPECIAL, 1),  # same-side carried ledge floor owns handoff
    ],
)
@pytest.mark.integration
def test_direct_fall_reseed_cliff_floor_owner_negative_boundaries(
    cliff_floor_id: int, ledge_cooldown: int, expected_action: int, expected_grounded: int
) -> None:
    rows = _run_direct_fall_cliff_exit_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=-57.91999816894531,
        y=-17.9,
        facing=1,
        ground_id=0,
        cliff_floor_id=cliff_floor_id,
        ledge_cooldown=ledge_cooldown,
        inputs=[
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
        ],
    )

    final = rows[-1]
    assert int(final["on_ground"][0]) == expected_grounded
    assert int(final["action_id"][0]) == expected_action


@pytest.mark.parametrize(
    ("stage_id", "char_id", "x", "y", "facing", "ground_id", "percent", "action_frame", "inputs", "land_step", "expected_ground"),
    [
        (
            STAGE_BATTLEFIELD,
            CHAR_FALCO,
            -70.5999984741211,
            -16.5,
            1,
            0,
            120.11998748779297,
            6,
            [
                (0, 0, -72, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y | BUTTON_R, 80, -48, 255),
                (BUTTON_Y | BUTTON_R, 80, -48, 255),
            ],
            7,
            0,
        ),
        (
            STAGE_YOSHI,
            CHAR_FOX,
            -57.91999816894531,
            -17.899999618530273,
            1,
            0,
            95.97999572753906,
            4,
            [
                (0, 0, -72, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y, 96, -40, 0),
                (BUTTON_Y | BUTTON_R, 80, -48, 255),
                (BUTTON_Y | BUTTON_R, 80, -48, 255),
                (BUTTON_Y | BUTTON_R, 80, -48, 255),
            ],
            8,
            2,
        ),
        (
            STAGE_YOSHI,
            CHAR_FALCO,
            58.20000076293945,
            -20.0,
            0,
            0,
            26.400001525878906,
            2,
            [
                (0, 0, -96, 0),
                (BUTTON_Y, -88, -88, 0),
                (BUTTON_Y, -88, -88, 0),
                (BUTTON_Y, -88, -88, 0),
                (BUTTON_Y, -88, -88, 0),
                (BUTTON_Y, -88, -88, 0),
                (BUTTON_Y | BUTTON_R, -88, -88, 255),
                (BUTTON_Y | BUTTON_R, -96, -80, 255),
                (BUTTON_Y | BUTTON_R, -104, -64, 255),
            ],
            9,
            6,
        ),
    ],
)
@pytest.mark.integration
def test_cliff_floor_owner_covers_same_side_and_yoshi_stale_floor(
    stage_id: int,
    char_id: int,
    x: float,
    y: float,
    facing: int,
    ground_id: int,
    percent: float,
    action_frame: int,
    inputs: list[tuple[int, int, int, int]],
    land_step: int,
    expected_ground: int,
) -> None:
    # Manual modelplay repros from the 20260506 legal-stage matrix:
    # - Battlefield left ledge: env_004_battlefield_fox_vs_falco P2 frames 2229-2236.
    # - Yoshi left ledge: env_010_yoshi_fox_vs_falco P1 frames 7500-7508.
    # - Yoshi right ledge: env_011_yoshi_falco_vs_falco P1 frames 5379-5388.
    #
    # These lock the retained shallow Cliff/CollData boundary: visible floor.index can still name the
    # ledge/stale stage-object floor, but the first shallow EscapeAir pass remains airborne until the
    # source bottom-sweep owner reaches a real floor handoff.
    rows = _run_cliff_wait_ledgedash(
        stage_id=stage_id,
        char_id=char_id,
        x=x,
        y=y,
        facing=facing,
        ground_id=ground_id,
        percent=percent,
        action_frame=action_frame,
        inputs=inputs,
    )

    before = rows[land_step - 2]
    landed = rows[land_step - 1]
    assert int(before["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(before["on_ground"][0]) == 0
    assert expected_ground in (0, 2, 6)
    if stage_id == STAGE_YOSHI:
        assert int(landed["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
        assert int(landed["on_ground"][0]) == 1
        assert int(landed["ground_id"][0]) == expected_ground
    else:
        assert int(landed["action_id"][0]) == ACT_ESCAPE_AIR
        assert int(landed["on_ground"][0]) == 0


def test_non_cliff_escapeair_near_yoshi_ledge_does_not_reconstruct_cliff_floor_owner() -> None:
    # Negative control: a direct later EscapeAir reseed near the low Yoshi ledge does not have the
    # hidden Cliff floor seed lane, so the sim must not infer a cliff ledge floor from position,
    # ledge-cooldown alone, or future outcome.
    seed = _seed_base(STAGE_YOSHI, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, -53.5, -4.2)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["ledge_cooldown"][0, 0] = np.uint8(20)
    seed["speed_air_x_self"][0, 0] = np.float32(2.15)
    seed["speed_y_self"][0, 0] = np.float32(-1.29)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0


def test_native_cliff_ledge_floor_seed_lane_carries_and_clears_by_source_lifetime() -> None:
    import msl_binding

    actions = np.array(
        [
            ACT_CLIFF_WAIT,
            ACT_FALL,
            ACT_JUMP_AERIAL_F,
            ACT_ESCAPE_AIR,
            ACT_ESCAPE_AIR,
            ACT_ESCAPE_AIR,
            ACT_WAIT,
            ACT_ESCAPE_AIR,
        ],
        dtype=np.uint16,
    )
    facing = np.array([1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    on_ground = np.array([0, 0, 0, 0, 1, 0, 0, 0], dtype=np.uint8)
    ledge_cooldown = np.array([0, 20, 19, 18, 17, 0, 20, 20], dtype=np.uint8)

    out = msl_binding.derive_cliff_ledge_floor_segment_id(
        actions,
        facing,
        on_ground,
        ledge_cooldown,
        2,
        6,
    )

    assert out.tolist() == [2, 2, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF]

    wrong_side = msl_binding.derive_cliff_ledge_floor_segment_id(
        np.array([ACT_CLIFF_WAIT, ACT_FALL], dtype=np.uint16),
        np.array([0, 0], dtype=np.uint8),
        np.array([0, 0], dtype=np.uint8),
        np.array([0, 20], dtype=np.uint8),
        2,
        6,
    )
    assert wrong_side.tolist() == [6, 6]


def test_cliff_ledge_floor_owner_reseed_uses_facing_not_position() -> None:
    # Negative control for wrong-side teacher-forced CliffWait reseeds. The hidden cliff owner may
    # be reconstructed from Cliff action + facing + generated stage ledge table only; a fighter
    # seeded at the left ledge position but facing left is treated as owning the right ledge.
    rows = _run_cliff_wait_ledgedash(
        stage_id=STAGE_YOSHI,
        char_id=CHAR_FOX,
        x=-57.91999816894531,
        y=-17.9,
        facing=0,
        ground_id=0,
        percent=95.97999572753906,
        action_frame=4,
        inputs=[
            (0, 0, -72, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y, 96, -40, 0),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
            (BUTTON_Y | BUTTON_R, 80, -48, 255),
        ],
    )

    final = rows[-1]
    assert int(final["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(final["on_ground"][0]) == 0
    assert int(final["ground_id"][0]) != 2


def test_yoshi_downhill_slope_grounded_projection_tracks_floor_height() -> None:
    # Manual live-viewer bug: walking/running downhill on Yoshi could keep the fighter stranded at the
    # previous higher Y. Source `mpLib_8004DD90_Floor` returns signed correction for current
    # floor.index, so grounded slope persistence must apply the downward correction too.
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    stage = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    seg = next(seg for seg in stage.segments if int(seg.line_id) == 6)
    x0 = 45.0
    seed = _seed_base(8, ACT_WALK_MIDDLE, SM_WAIT1_0, x0, _floor_y_at(seg, x0) + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["speed_ground_x_self"][0, 0] = np.float32(2.0)

    out = _step_once(seed)
    x1 = float(out["pos_x"][0])

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 6
    assert float(out["pos_y"][0]) == pytest.approx(_floor_y_at(seg, x1) + 0.0001, abs=1e-5)


@pytest.mark.integration
def test_yoshi_landingfallspecial_sustained_slope_projection_replay_real() -> None:
    # CNM 5277 p1 is sustained LandingFallSpecial crossing from Yoshi's flat main floor to the
    # connected right slope. Source Landing_Coll still calls
    # ft_80084280 -> mpColl_8004B4B0 -> mpLib_8004DD90_Floor after the landing entry handoff, so
    # the grounded root must follow the signed slope projection instead of preserving the flat Y.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor}
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    root = Path(__file__).resolve().parents[1]
    path = root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    p = 1
    row = ds.samples[5277]
    assert int(row["seed_t"]["stage_id"]) == STAGE_YOSHI
    assert int(row["seed_t"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["seed_t"]["action_frame"][p]) == 18
    assert int(row["seed_t"]["ground_id"][p]) == 3
    assert int(row["ref_t1"]["ground_id"][p]) == 6

    out = _step_one_replay_row(ds, 5277)
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 6
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-5)

    rollout = _rollout_replay_to_record(ds, 5255, 5277)
    assert int(rollout["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(rollout["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(rollout["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 6
    assert float(rollout["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-5)


def test_fod_stage_lip_slope_grounded_projection_tracks_floor_height() -> None:
    # FoD side lips are admitted sloped GrIz floor segments. Grounded persistence should follow
    # the current line's `mpLib_8004DD90_Floor` projection instead of carrying the previous root
    # height across the raised lip.
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    seg = next(seg for seg in stage.segments if int(seg.line_id) == 6)
    x0 = 51.5
    seed = _seed_base(2, ACT_WALK_MIDDLE, SM_WAIT1_0, x0, _floor_y_at(seg, x0) + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["speed_ground_x_self"][0, 0] = np.float32(0.5)

    out = _step_once(seed)
    x1 = float(out["pos_x"][0])

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 6
    assert float(out["pos_y"][0]) == pytest.approx(_floor_y_at(seg, x1) + 0.0001, abs=1e-5)


def test_fod_stage_lip_slope_to_flat_handoff_drops_to_current_floor_height() -> None:
    # The right FoD lip slopes down into the main floor. Source DD90 traversal remaps across the
    # connected endpoint and applies the signed Y correction for the resulting floor, so walking
    # down from the raised lip must not preserve the lip height over line 5.
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    slope = next(seg for seg in stage.segments if int(seg.line_id) == 6)
    flat = next(seg for seg in stage.segments if int(seg.line_id) == 5)
    x0 = 51.45
    seed = _seed_base(2, ACT_RUN, SM_WAIT1_0, x0, _floor_y_at(slope, x0) + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.8)

    out = _step_once(seed)
    x1 = float(out["pos_x"][0])

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 5
    assert float(out["pos_y"][0]) == pytest.approx(_floor_y_at(flat, x1) + 0.0001, abs=1e-5)
    # Source ordering: Phys projects gr_vel through the starting floor normal, then Coll remaps the
    # floor id/height after movement. The frame's self_vel therefore remains slope-tangent even
    # though CollData.floor.index publishes the connected flat floor.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    nx, ny = _floor_normal(slope)
    assert float(out["speed_air_x_self"][0]) == pytest.approx(
        ny * float(out["speed_ground_x_self"][0]), abs=1e-4
    )
    assert float(out["speed_y_self"][0]) == pytest.approx(
        -nx * float(out["speed_ground_x_self"][0]), abs=1e-4
    )


def test_fod_slope_corner_order_keeps_floor_owner_without_wall_or_ceiling() -> None:
    # Grounded mpColl_8004ACE4 checks walls and ceiling before floor. At the FoD lip corner, those
    # prepasses must not steal ownership from the source DD90 slope-to-flat floor traversal.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    slope = next(seg for seg in stage.segments if int(seg.line_id) == 6)
    flat = next(seg for seg in stage.segments if int(seg.line_id) == 5)
    x0 = 51.45
    seed = _seed_base(2, ACT_RUN, SM_WAIT1_0, x0, _floor_y_at(slope, x0) + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.8)

    out, contacts = _step_once_with_contacts(seed)
    x1 = float(out["pos_x"][0])

    assert int(contacts["wall_kind"][0]) == 0
    assert int(contacts["ceiling_id"][0]) == 0xFFFF
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 5
    assert float(out["pos_y"][0]) == pytest.approx(_floor_y_at(flat, x1) + 0.0001, abs=1e-5)


def test_battlefield_mpcoll_4a908_side_midpoint_retry_finds_disconnected_floor() -> None:
    # Source mpColl_8004A908_Floor performs a second retry from previous ECB side-midpoint Y to
    # current ECB bottom Y after ordinary persisted-floor projection fails. This catches a
    # disconnected floor that the previous-bottom sweep misses, and only accepts floors that are not
    # connected to the persisted CollData.floor.index.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, 30.0, -6.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)  # persisted BF top platform, disconnected from main
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(30.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(30.0, abs=1e-6)
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-5)


def test_battlefield_mpcoll_4a908_connected_floor_retry_is_rejected() -> None:
    # mpColl_8004A908_Floor rejects hits on floors connected to the persisted CollData.floor.index.
    # Moving off BF's main floor toward the connected right edge should stay on the source floor
    # chain/endpoint path instead of switching to the connected edge segment through the retry.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, 80.0, -6.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(30.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(60.0, abs=1e-5)
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-5)


def test_battlefield_mpcoll_4a908_floor_skip_rejects_platform_retry() -> None:
    # The 4A908 retry uses the same CollData.floor_skip gate as mpCheckFloorRemap. Without a skip,
    # the disconnected left platform can be recovered; with floor_skip set to that platform, the
    # retry must leave the fighter airborne.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpUpdateFloorSkip}
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, -40.0, 25.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(26.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 2

    seed["floor_skip_segment_id_u16"][0, 0] = np.uint16(2)
    seed["floor_skip_segment_valid_u8"][0, 0] = np.uint8(1)
    out_skip = _step_once(seed)
    assert int(out_skip["on_ground"][0]) == 0
    assert int(out_skip["ground_id"][0]) == 3


def test_yoshi_mpcoll_4a908_fighter_solid_rejects_raw_center_line() -> None:
    # Yoshi's center raw line is visible in debug data but not fighter-solid in the current legal
    # stage policy. The 4A908 retry must use the generated fighter_solid metadata and not admit it.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    import msl_binding

    raw = msl_binding.stage_floor_segment(8, 0)
    assert raw is not None
    assert int(raw["fighter_solid"]) == 0

    seed = _seed_base(8, ACT_WAIT, SM_WAIT1_0, 0.0, 4.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 5


def test_yoshi_stage_object_support_preserves_current_raw_non_solid_floor() -> None:
    # Yoshi Shy Guys are stage-owned item objects. Source CollData can carry the current support
    # floor id even though the raw platform line is not selectable as static fighter-solid terrain.
    # refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    # refs/melee/src/melee/it/items/itheiho.c
    stage = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    support = next(seg for seg in stage.segments if int(seg.line_id) == 0)
    assert support.stage_object_support_kind == STAGE_OBJECT_SUPPORT_KIND_YOSHI_SHYGUY

    seed = _seed_base(8, ACT_WAIT, SM_WAIT1_0, -80.0, -13.649894)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-79.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-13.649894)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert int(out["jumps_left"][0]) == 2


def test_yoshi_stage_object_support_does_not_admit_new_raw_contact() -> None:
    # The Shy Guy owner preserves current CollData support only. A new airborne sweep crossing the
    # raw support line must still reject it as static terrain.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    seed = _seed_base(8, ACT_FALL, SM_FALL, -80.0, -12.5)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_y_self"][0, 0] = np.float32(-2.0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-80.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF


def test_yoshi_raw_non_solid_floor_without_stage_object_support_still_falls() -> None:
    # Negative lock for the same raw non-solid line: without the Shy Guy stage-object owner, the
    # static mpColl graph must not keep the fighter grounded on debug-only platform metadata.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    seed = _seed_base(8, ACT_WAIT, SM_WAIT1_0, -80.0, -13.649894)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-79.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-13.649894)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0


def test_unrelated_raw_non_solid_platform_is_not_stage_object_support() -> None:
    # Frozen Stadium keeps transformation platforms debug-visible but not fighter-solid. Even with
    # the Yoshi stage-object seed lane set, generated MSLSTG01 support-kind metadata must prevent
    # unrelated raw platform lines from being preserved as current support.
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    stage = read_mslstg01_v7(Path("data/stages/bin/grps.bin"))
    raw = next(seg for seg in stage.segments if int(seg.line_id) == 11)
    assert int(raw.flags) & 1
    assert raw.fighter_solid is False
    assert raw.stage_object_support_kind == 0

    seed = _seed_base(3, ACT_WAIT, SM_WAIT1_0, 12.0, 39.003)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(11)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(12.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["stage_yoshi_shyguy_valid_u8"][0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 11


def test_battlefield_mpcoll_4a908_platform_endpoint_snap_after_disconnected_retry() -> None:
    # Source runs mpColl_80044838_Floor immediately after an accepted 4A908 retry. Keep hard-floor
    # endpoint snap scoped to that accepted disconnected-floor path; this platform endpoint case
    # proves the follow-up still runs when the wall prepass does not preempt the floor result.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpColl_80044838_Floor}
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, -60.0, 25.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(26.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 2
    assert float(out["pos_x"][0]) == pytest.approx(-57.60000228881836, abs=1e-5)
    assert float(out["pos_y"][0]) == pytest.approx(27.200000762939453, abs=1e-5)


def test_battlefield_grounded_wall_prepass_runs_before_4a908_hard_floor_endpoint() -> None:
    # Source grounded inline2 order checks walls before floor/4A908. This corner would previously
    # snap the accepted hard floor to BF's main-floor endpoint; source order first resolves the
    # right wall, so the later floor projection consumes the clamped X.
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_8004ACE4,mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall,
    #   mpColl_8004A908_Floor,mpColl_80044838_Floor}
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, 90.0, -6.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(3)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-5.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)

    out, contacts = _step_once_with_contacts(seed)

    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) != 0xFFFF
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(60.0, abs=1e-5)
    assert float(out["pos_y"][0]) == pytest.approx(0.0, abs=1e-5)


def test_fd_cardinal_grounded_center_does_not_broaden_wall_or_ceiling_prepass() -> None:
    # FD/cardinal regression lock: enabling UCF cardinal handling must not let the new grounded
    # wall/ceiling prepass synthesize wall or ceiling contacts on an ordinary hard-floor frame.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
    seed = _seed_base(32, ACT_WAIT, SM_WAIT1_0, 0.0, 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1)
    prev_input = _input_bytes()
    input_t = _input_bytes()
    prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
    input_view = input_t.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    prev_view["p"]["main_y"][0, 0] = np.int8(127)
    input_view["p"]["main_x"][0, 0] = np.int8(0)
    input_view["p"]["main_y"][0, 0] = np.int8(127)

    out, contacts = _step_once_with_contacts(seed, prev_input, input_t)

    assert int(contacts["wall_kind"][0]) == 0
    assert int(contacts["ceiling_id"][0]) == 0xFFFF
    assert int(contacts["coll_env_flags"][0]) & (COLLIDE_LEFT_WALL_MASK | COLLIDE_RIGHT_WALL_MASK) == 0
    assert int(contacts["coll_env_flags"][0]) & COLLIDE_CEILING_MASK == 0
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["pos_y"][0]) == pytest.approx(0.0001, abs=1e-6)


def test_fod_grounded_horizontal_squeeze_records_both_wall_sides_between_walls() -> None:
    # FoD's lower wall shaft can produce same-frame left and right wall contact during the grounded
    # inline2 prepass. Source mpColl_8004ACE4 records both sides, then runs
    # mpCollSqueezeHorizontal before ceiling/floor resolution.
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_8004ACE4,mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall,
    #   mpColl_80049778_LeftWall,mpColl_80049EAC_LeftWall,mpCollSqueezeHorizontal}
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, 10.0, -200.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["speed_ground_x_self"][0, 0] = np.float32(-120.0)

    out, contacts, colldata = _step_once_with_contacts_and_colldata(seed)
    flags = int(contacts["coll_env_flags"][0])

    assert flags & COLLIDE_LEFT_WALL_MASK
    assert flags & COLLIDE_RIGHT_WALL_MASK
    assert flags & COLLIDE_CEILING_MASK == 0
    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) == 12
    assert int(out["on_ground"][0]) == 0
    assert int(colldata["squeeze_restore_valid"][0]) == 1
    assert float(colldata["squeeze_restore_left_rel_x"][0]) == pytest.approx(-2.0)
    assert float(colldata["squeeze_restore_right_rel_x"][0]) == pytest.approx(2.0)
    assert float(colldata["current_left_rel_x"][0]) != pytest.approx(
        float(colldata["squeeze_restore_left_rel_x"][0])
    )
    assert float(out["pos_x"][0]) == pytest.approx(-49.663360595703125, abs=1e-5)
    assert float(out["pos_y"][0]) == pytest.approx(-200.0, abs=1e-5)


def test_fod_grounded_floor_ceiling_retry_runs_vertical_squeeze_owner() -> None:
    # Source mpColl_8004ACE4 preserves ceiling/floor squeeze state across the floor pass. This
    # forced FoD lower-stage setup hits a ceiling in the grounded prepass, resolves the carried
    # floor, and keeps the final grounded root on the floor after the ceiling retry/vertical
    # squeeze owner runs.
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_8004ACE4,mpColl_80044AD8_Ceiling,mpColl_8004AB80,mpCollSqueezeVertical}
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, -40.0, -175.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["speed_y_self"][0, 0] = np.float32(-80.0)

    out, contacts, colldata = _step_once_with_contacts_and_colldata(seed)
    flags = int(contacts["coll_env_flags"][0])

    assert flags & COLLIDE_CEILING_MASK
    assert flags & COLLIDE_FLOOR_MASK
    assert int(contacts["wall_kind"][0]) == 0
    assert int(contacts["ceiling_id"][0]) == 8
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 5
    assert int(colldata["floor_skip_valid"][0]) == 0
    assert int(colldata["floor_skip_segment_id"][0]) == 0xFFFF
    assert int(colldata["squeeze_restore_valid"][0]) == 1
    assert float(colldata["squeeze_restore_top_rel_y"][0]) > 0.0
    assert float(colldata["current_top_rel_y"][0]) < 0.0
    assert float(out["pos_y"][0]) == pytest.approx(0.0028839111328125, abs=1e-5)


def test_fod_grounded_vertical_squeeze_restore_consumes_x64_ecb() -> None:
    # Source mpCollInterpolateECB consumes x34_flags.b6 on the next interpolation: prev_ecb first
    # receives the squeezed ECB, current ECB restores from x64_ecb, and b6 clears unless another
    # squeeze is produced.
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCollSqueezeVertical}
    import msl_binding

    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, -40.0, -175.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["speed_y_self"][0, 0] = np.float32(-80.0)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_t = _input_bytes()
    colldata = np.zeros((1, int(sizes["colldata_ecb"])), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        snaps = []
        for _ in range(3):
            msl_binding.step_input(handle, input_t, input_t)
            msl_binding.debug_write_colldata_ecb(handle, colldata)
            snaps.append(colldata.view(_colldata_ecb_dtype()).reshape((1,))[0].copy())
    finally:
        msl_binding.destroy(handle)

    assert int(snaps[0]["squeeze_restore_valid"][0]) == 1
    assert float(snaps[0]["prev_top_rel_y"][0]) < 0.0
    assert float(snaps[0]["squeeze_restore_top_rel_y"][0]) > 0.0
    assert int(snaps[2]["squeeze_restore_valid"][0]) == 0
    assert float(snaps[2]["current_top_rel_y"][0]) > 0.0


def test_yoshi_center_raw_platform_is_debug_visible_but_not_fighter_solid() -> None:
    # The tiny raised center line is raw GrSt collision metadata, but not admitted as current legal
    # Yoshi fighter-solid terrain. Keep debug visibility while rejecting fighter collision.
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        raw = msl_binding.stage_floor_segment(8, 0)
        assert raw is not None
        assert int(raw["is_platform"]) == 1
        assert float(raw["y0"]) == pytest.approx(5.25)
        assert msl_binding.stage_fighter_floor_segment(8, 0) is None
        randall = msl_binding.stage_floor_segment(8, 1000)
        assert randall is not None
        assert int(randall["is_platform"]) == 1
    finally:
        msl_binding.destroy(handle)


def test_yoshi_randall_stage_debug_reports_runtime_position_for_viewer() -> None:
    # Live viewer/modelplay render Randall from this debug stage-state path, so the visual platform
    # center must be the same generated GrSt platform transform used by collision.
    import msl_binding

    sizes = msl_binding.sizes()
    stage_out = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
    config = build_match_config_array(stage_id=8, random_seed=0, char_ids=(CHAR_FOX, CHAR_FOX))
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        msl_binding.debug_write_stage_state(handle, stage_out)
        stage = stage_out.view(STAGE_DEBUG_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(stage["randall_exists"]) == 1
    assert np.isfinite(float(stage["randall_x"]))
    assert float(stage["randall_y"]) == pytest.approx(-33.248901, abs=1e-5)


def _moving_surface_packet_after_seed(seed: np.ndarray, segment_i: int) -> dict[str, object] | None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed_rollout(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        return msl_binding.debug_stage_moving_floor_surface(handle, 0, segment_i)
    finally:
        msl_binding.destroy(handle)


def test_fod_moving_surface_packet_reports_current_height_velocity_and_hidden_visibility() -> None:
    # Phase-6 moving floors use one stage-collision runtime surface packet: grIzumi-owned height,
    # visibility, source bits, and motion delta are exposed together before fighter mpColl consumes
    # the transformed floor result.
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    motion = next(m for m in stage.platform_motions if m.kind_id == STAGE_PLATFORM_MOTION_KIND_FOD)

    height = np.float32(17.100000381469727)
    velocity = np.float32(-0.1)
    seed = _seed_base(STAGE_FOD, ACT_WAIT, SM_WAIT1_0, -35.0, 1.125 + float(height) * 0.75)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(4)

    live = _moving_surface_packet_after_seed(seed, 0)
    assert live is not None
    assert int(live["segment_i"]) == 0
    assert int(live["platform_transform_kind"]) == 1
    assert int(live["platform_transform_id"]) == 1
    assert int(live["valid"]) == 1
    assert int(live["active"]) == 1
    assert int(live["visible"]) == 1
    assert int(live["current_owned"]) == 1
    assert int(live["source_bits"]) == 4
    assert float(live["y0"]) == pytest.approx(1.125 + float(height) * 0.75, abs=1e-6)
    assert float(live["velocity_y"]) == pytest.approx(float(velocity) * 0.75, abs=1e-6)
    assert float(live["normal_x"]) == pytest.approx(0.0, abs=1e-7)
    assert float(live["normal_y"]) == pytest.approx(1.0, abs=1e-7)

    hidden_seed = seed.copy()
    hidden_seed["stage_fod_platform_height_f32"][0, 1] = np.float32(motion.hidden_target_height)
    hidden_seed["stage_fod_platform_velocity_f32"][0, 1] = np.float32(0.0)
    hidden_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(0)
    hidden_seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(1)

    hidden = _moving_surface_packet_after_seed(hidden_seed, 0)
    assert hidden is not None
    assert int(hidden["valid"]) == 1
    assert int(hidden["current_owned"]) == 1
    assert int(hidden["source_trusted"]) == 1
    assert int(hidden["active"]) == 0
    assert int(hidden["visible"]) == 0
    assert int(hidden["reached_hidden_this_step"]) == 0
    assert float(hidden["y0"]) == pytest.approx(float(motion.hidden_target_height) * 0.75, abs=1e-6)
    assert float(hidden["velocity_y"]) == pytest.approx(0.0, abs=1e-7)


def test_yoshi_randall_moving_surface_packet_uses_generated_path_and_velocity() -> None:
    # Randall's packet is sourced from the generated GrStory/Ground_801C2FE0 path samples, including
    # the same one-frame platform delta consumed by grounded support/carry callbacks.
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
    # refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
    stage = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    path = {(int(rec.line_id), int(rec.frame)): rec for rec in stage.platform_paths}
    frame_id = 477
    rec = path[(1000, frame_id)]
    next_rec = path[(1000, frame_id + 1)]

    seed = _seed_base(STAGE_YOSHI, ACT_WAIT, SM_WAIT1_0, 95.0, float(rec.y) + 0.0001)
    seed["frame_id"][0] = np.int32(frame_id)

    surface = _moving_surface_packet_after_seed(seed, 1000)
    assert surface is not None
    assert int(surface["segment_i"]) == 1000
    assert int(surface["platform_transform_kind"]) == 3
    assert int(surface["active"]) == 1
    assert int(surface["visible"]) == 1
    assert int(surface["current_owned"]) == 1
    assert int(surface["source_trusted"]) == 1
    assert int(surface["source_frame"]) == frame_id
    assert float(surface["x0"]) == pytest.approx(float(rec.x0), abs=1e-6)
    assert float(surface["x1"]) == pytest.approx(float(rec.x1), abs=1e-6)
    assert float(surface["y0"]) == pytest.approx(float(rec.y), abs=1e-6)
    assert float(surface["velocity_x"]) == pytest.approx(float(next_rec.x0 - rec.x0), abs=1e-6)
    assert float(surface["velocity_y"]) == pytest.approx(float(next_rec.y - rec.y), abs=1e-6)


def test_fod_live_platform_scheduler_moves_without_replay_seed() -> None:
    # Live-viewer/new-match FoD has no Slippi current-height seed lanes. Runtime should initialize
    # grIzumi scheduler state from generated stage data and advance the C-owned platform height.
    import msl_binding

    default_h = np.float32(28.0)
    sizes = msl_binding.sizes()
    inp = _input_bytes()
    stage_dtype = STAGE_DEBUG_DTYPE
    stage_out = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
    config = build_match_config_array(stage_id=2, random_seed=0, char_ids=(CHAR_FOX, CHAR_FOX))
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for _ in range(1400):
            msl_binding.step_input(handle, inp, inp)
        msl_binding.debug_write_stage_state(handle, stage_out)
        stage = stage_out.view(stage_dtype).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(stage["fod_platform_height_valid"][0]) == 1
    assert float(stage["fod_platform_height"][0]) < float(default_h) - 1.0


def test_fod_match_start_rollout_reseed_enables_platform_scheduler() -> None:
    # A rollout seeded from frame -123 is a real match-start owner, not a mid-replay teacher-forced
    # reseed. grIzumi initializes side-platform JObjs from extracted data and then runs
    # grIzumi_801CC358 from the frame_pre_random_seed stream, matching init_match ownership.
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    inp = _input_bytes()
    stage_dtype = STAGE_DEBUG_DTYPE
    stage_out = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, 0.0, 0.0)
    seed["frame_id"][0] = np.int32(-123)
    seed["stage_fod_platform_height_f32"][0] = np.array([28.0, 20.0], dtype=np.float32)
    seed["stage_fod_platform_height_valid_u8"][0] = np.array([0, 0], dtype=np.uint8)
    seed["stage_fod_platform_height_source_u8"][0] = np.array([0, 0], dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed_rollout(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        for _ in range(1400):
            msl_binding.step_input(handle, inp, inp)
        msl_binding.debug_write_stage_state(handle, stage_out)
        stage = stage_out.view(stage_dtype).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(stage["fod_platform_height_valid"][0]) == 1
    assert float(stage["fod_platform_height"][0]) < 27.0
    assert int(stage["fod_platform_height_valid"][1]) == 1
    assert float(stage["fod_platform_height"][1]) != pytest.approx(20.0)


def test_fod_mid_reseed_current_named_pose_resumes_scheduler_only_with_source() -> None:
    # Mid-replay rollout reseeds normally do not reconstruct grIzumi's hidden phase/timer. A
    # current source bit at a generated stationary pose proves the JObj/mpLib owner for that frame,
    # so runtime may resume the grIzumi scheduler from that source pose. A valid sparse height with
    # no source bit remains a stale replay carry and must not begin moving on its own.
    #
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    inp = _input_bytes()
    stage_dtype = STAGE_DEBUG_DTYPE

    def run_seed(seed: np.ndarray, *, steps: int = 1400) -> np.void:
        stage_out = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
        handle = msl_binding.init(
            batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1
        )
        try:
            msl_binding.reseed_seed_rollout(handle, seed.view(np.uint8).reshape((1, seed_stride)))
            for _ in range(steps):
                msl_binding.step_input(handle, inp, inp)
            msl_binding.debug_write_stage_state(handle, stage_out)
            return stage_out.view(stage_dtype).reshape((1,))[0].copy()
        finally:
            msl_binding.destroy(handle)

    sourced = _seed_base(2, ACT_WAIT, SM_WAIT1_0, 0.0, 0.0)
    sourced["frame_id"][0] = np.int32(900)
    sourced["stage_fod_platform_height_f32"][0] = np.array([28.0, 20.0], dtype=np.float32)
    sourced["stage_fod_platform_height_valid_u8"][0] = np.array([1, 1], dtype=np.uint8)
    sourced["stage_fod_platform_velocity_valid_u8"][0] = np.array([0, 0], dtype=np.uint8)
    sourced["stage_fod_platform_height_source_u8"][0] = np.array([4, 0], dtype=np.uint8)

    stale = sourced.copy()
    stale["stage_fod_platform_height_source_u8"][0] = np.array([0, 0], dtype=np.uint8)

    sourced_stage = run_seed(sourced)
    stale_stage = run_seed(stale)

    assert int(sourced_stage["fod_platform_height_valid"][0]) == 1
    assert float(sourced_stage["fod_platform_height"][0]) < 27.0
    assert int(sourced_stage["fod_platform_height_source"][0]) == 0
    assert int(stale_stage["fod_platform_height_valid"][0]) == 1
    assert float(stale_stage["fod_platform_height"][0]) == pytest.approx(28.0)
    assert int(stale_stage["fod_platform_height_source"][0]) == 0


@pytest.mark.integration
def test_fod_grizumi_min_visible_stop_wait_boundary_matches_second_hidden_descent_replay_real() -> None:
    # MGS's left FoD platform covers the grIzumi lower-visible-stop boundary from match-start
    # rollout: the first visible descent must still start at rec720, and after reaching min-visible
    # height the next wait phase must choose the rec1728 HSD_Randf sample, not the previous frame's
    # sample, before hidden descent appears at rec1729.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    slp_path = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    import msl_binding

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
    )
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    stage_stride = int(sizes["stage_state"])
    stage_dtype = STAGE_DEBUG_DTYPE
    stage_out = np.zeros((1, stage_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed_rollout(
            handle,
            np.frombuffer(ds.samples[0]["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride),
        )
        observed: dict[int, float] = {}
        for record in range(1730):
            row = ds.samples[record]
            msl_binding.step_input(
                handle,
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
            )
            if record in (720, 1728, 1729):
                msl_binding.debug_write_stage_state(handle, stage_out)
                stage = stage_out.view(stage_dtype).reshape((1,))[0]
                observed[record] = float(stage["fod_platform_height"][1])
    finally:
        msl_binding.destroy(handle)

    assert observed[720] == pytest.approx(
        float(ds.samples[720]["seed_t"]["stage_fod_platform_height_f32"][1]), abs=1e-6
    )
    assert observed[1728] == pytest.approx(15.0, abs=1e-6)
    assert observed[1729] == pytest.approx(
        float(ds.samples[1729]["seed_t"]["stage_fod_platform_height_f32"][1]), abs=1e-6
    )


@pytest.mark.integration
def test_fod_platform_scheduler_rand_range_matches_grizumi_replay_real() -> None:
    # EWT rec2523 lands AttackAirB on FoD's moving left platform only if the live grIzumi scheduler
    # has reached the same platform height as vanilla. grIzumi calls rand_range(max,min), whose
    # HSD_Randi(max-min) upper bound is exclusive; an inclusive wait span leaves the platform too
    # low and the row stays airborne.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/gr/inlines.h::rand_range
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    import msl_binding

    ds = read_dataset(str(path))
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    stage_stride = int(sizes["stage_state"])
    stage_dtype = STAGE_DEBUG_DTYPE
    out_buf = np.zeros((1, compare_stride), dtype=np.uint8)
    stage_out = np.zeros((1, stage_stride), dtype=np.uint8)
    p = 0
    record = 2523

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed_rollout(
            handle,
            np.frombuffer(ds.samples[0]["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride),
        )
        for cur_record in range(0, record):
            row = ds.samples[cur_record]
            msl_binding.step_input(
                handle,
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
            )
        msl_binding.debug_write_stage_state(handle, stage_out)
        pre_stage = stage_out.view(stage_dtype).reshape((1,))[0].copy()

        row = ds.samples[record]
        msl_binding.step_input(
            handle,
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, input_stride),
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, input_stride),
        )
        msl_binding.write_compare(handle, out_buf)
    finally:
        msl_binding.destroy(handle)

    out = out_buf.view(COMPARE_DTYPE).reshape((1,))[0]
    ref = ds.samples[record]["ref_t1"]
    assert int(pre_stage["fod_platform_height_valid"][1]) == 1
    assert float(pre_stage["fod_platform_height"][1]) == pytest.approx(25.0, abs=1e-5)
    assert int(ds.samples[record]["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING_AIR_B
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_fod_live_platform_stage_debug_reports_runtime_height_for_live() -> None:
    # Live viewer/modelplay render FoD platforms from this debug stage-state path, so the visual
    # platform height must be the same grIzumi runtime owner value used by collision.
    import msl_binding

    default_h = np.float32(28.0)
    stage_dtype = STAGE_DEBUG_DTYPE
    sizes = msl_binding.sizes()
    inp = _input_bytes()
    stage_out = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
    config = build_match_config_array(stage_id=2, random_seed=0, char_ids=(CHAR_FOX, CHAR_FOX))
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for _ in range(1400):
            msl_binding.step_input(handle, inp, inp)
        msl_binding.debug_write_stage_state(handle, stage_out)
        stage = stage_out.view(stage_dtype).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(stage["fod_platform_height_valid"][0]) == 1
    assert float(stage["fod_platform_height"][0]) < float(default_h)


@pytest.mark.integration
def test_fod_sustained_landing_does_not_snap_to_live_scheduler_platform() -> None:
    # EWT rec838 is sustained Landing on the hard floor while FoD's live right-platform scheduler is
    # valid and moving. grIzumi keeps the platform line current, but Landing_Coll's fallback floor
    # retry must still require an actual sweep/contact with that floor; this must not become generic
    # "scheduler platform wins" behavior.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 838
    row = ds.samples[record]
    out = _rollout_replay_to_record(ds, 0, record)
    p = 1

    assert int(row["seed_t"]["action_id"][p]) == ACT_LANDING
    assert int(row["seed_t"]["ground_id"][p]) == 5
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 5
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)


def test_fod_landing_4a908_retry_can_replace_still_valid_main_floor_with_rising_platform() -> None:
    # EWT rec1538: Landing_Coll still has the main hard floor in CollData, but FoD's left platform
    # rises through the fighter. Source mpColl_8004ACE4 runs mpColl_8004A908_Floor after the
    # ordinary current-floor pass, so the disconnected transformed platform can replace the still
    # valid main floor without requiring a transient replay source bit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    seed = _seed_base(2, ACT_LANDING, 35, -28.025644, 0.00287533)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.29125)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.29125)
    seed["speed_y_self"][0, 0] = np.float32(-3.4)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(2)
    seed["stage_fod_platform_height_f32"][0] = np.array([35.000134, 0.64986664], dtype=np.float32)
    seed["stage_fod_platform_height_valid_u8"][0] = np.array([1, 1], dtype=np.uint8)
    seed["stage_fod_platform_velocity_f32"][0] = np.array([0.0, 0.0], dtype=np.float32)
    seed["stage_fod_platform_velocity_valid_u8"][0] = np.array([1, 0], dtype=np.uint8)
    seed["stage_fod_platform_height_source_u8"][0] = np.array([0, 0], dtype=np.uint8)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(1.6125, abs=1e-4)


def test_fod_landing_4a908_retry_does_not_snap_to_distant_platform() -> None:
    # Negative boundary for the same source retry: rec838-shaped Landing starts on the main hard
    # floor while a different FoD platform is live but not intersected by the 4A908 side-midpoint
    # sweep. The retry must not become generic "scheduler platform wins" behavior.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
    seed = _seed_base(2, ACT_LANDING, 35, 27.201181, 0.00287533)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.3855)
    seed["speed_air_x_self"][0, 0] = np.float32(-0.3855)
    seed["speed_y_self"][0, 0] = np.float32(-3.5)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["seed_prev_action_frame"][0, 0] = np.int16(1)
    seed["stage_fod_platform_height_f32"][0] = np.array([35.0, 0.59996], dtype=np.float32)
    seed["stage_fod_platform_height_valid_u8"][0] = np.array([1, 1], dtype=np.uint8)
    seed["stage_fod_platform_velocity_f32"][0] = np.array([0.0, -0.1], dtype=np.float32)
    seed["stage_fod_platform_velocity_valid_u8"][0] = np.array([0, 1], dtype=np.uint8)
    seed["stage_fod_platform_height_source_u8"][0] = np.array([0, 0], dtype=np.uint8)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 5
    assert float(out["pos_y"][0]) == pytest.approx(0.00287533, abs=1e-6)


def test_yoshi_randall_dynamic_platform_world_line_admits_collision() -> None:
    # Randall is a generated stage-object floor record. This seed is at the right-side horizontal
    # pass; static artifact endpoints are on the left, so remaining grounded proves runtime used
    # the stage-object transform.
    seed = _seed_base(8, ACT_WAIT, SM_WAIT1_0, 90.0, -13.64989 + 0.0001)
    seed["frame_id"][0] = np.int32(700)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1000)
    seed["facing"][0, 0] = np.uint8(1)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_OTTOTTO
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(-13.64989 + 0.0001, abs=1e-5)


def test_platform_edge_wait_enters_teeter_and_teeter_shield_enters_guard() -> None:
    # Common ft_80084280 admits Ottotto on platform edges. ftCo_Ottotto_IASA then calls
    # ftCo_80091A4C, so pressing shield during teeter enters GuardOn.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, 58.1, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["facing"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_OTTOTTO
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4

    teeter = _seed_base(31, ACT_OTTOTTO, SM_OTTOTTO, 57.600002, 27.2000007629 + 0.0001)
    teeter["on_ground"][0, 0] = np.uint8(1)
    teeter["ground_id"][0, 0] = np.uint16(4)
    teeter["facing"][0, 0] = np.uint8(1)
    teeter["shield_hp"][0, 0] = np.float32(60.0)
    prev_shield = _input_bytes()
    prev_shield.view(INPUT_DTYPE).reshape((1,))["p"]["l"][0, 0] = np.uint8(38)
    shield = _input_bytes()
    shield_v = shield.view(INPUT_DTYPE).reshape((1,))
    shield_v["p"]["l"][0, 0] = np.uint8(255)
    shield_v["p"]["main_y"][0, 0] = np.int8(-103)
    out2 = _step_once(teeter, prev_input=prev_shield, input_t=shield)
    assert int(out2["action_id"][0]) == ACT_GUARD_ON


def test_teeter_wait_can_enter_smash_attack() -> None:
    # ftCo_OttottoWait_IASA shares the grounded A-attack owner with Ottotto. Manual play exposed
    # that teeter-wait could shield after the prior fix but still failed to enter smash attacks.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_OttottoWait_IASA
    seed = _seed_base(31, ACT_OTTOTTO_WAIT, SM_OTTOTTO_WAIT, 57.600002, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["facing"][0, 0] = np.uint8(1)
    attack = _input_bytes()
    attack_v = attack.view(INPUT_DTYPE).reshape((1,))
    attack_v["p"]["buttons"][0, 0] = np.uint16(BUTTON_A)
    attack_v["p"]["main_x"][0, 0] = np.int8(127)

    out = _step_once(seed, input_t=attack)

    assert int(out["action_id"][0]) == ACT_ATTACK_S4_S


def test_shield_drop_immediate_aerial_keeps_platform_floor_skip() -> None:
    # Shield-drop writes CollData.floor_skip via mpUpdateFloorSkip. An immediate aerial should not
    # broadly re-ground on the same platform; delayed/non-platform contacts remain separately owned.
    import msl_binding

    seed = _seed_base(31, ACT_GUARD, 0xFFFFFFFF, 30.0, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["shield_hp"][0, 0] = np.float32(60.0)

    sizes = msl_binding.sizes()
    neutral = _input_bytes()
    drop = _input_bytes()
    aerial = _input_bytes()
    drop_v = drop.view(INPUT_DTYPE).reshape((1,))
    drop_v["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    # Pass-only down input: below the platform-pass threshold but above spotdodge.
    drop_v["p"]["main_y"][0, 0] = np.int8(-55)
    aerial.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(BUTTON_A)
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, neutral, drop)
        msl_binding.step_input(handle, drop, aerial)
        msl_binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(row["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(row["on_ground"][0]) == 0
    assert int(row["ground_id"][0]) == 4


def test_shield_tilt_on_platform_updates_bubble_and_still_allows_shield_drop() -> None:
    # Shield tilt is data-table owned and should update Guard bubble pose without blocking the
    # Guard platform-drop path.
    import msl_binding

    seed = _seed_base(31, ACT_WAIT, SM_WAIT1_0, -80.0, 0.0001)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["pos_x"][0, 1] = np.float32(30.0)
    seed["pos_y"][0, 1] = np.float32(27.2000007629 + 0.0001)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(4)
    seed["shield_hp"][0, 1] = np.float32(60.0)
    neutral_tilt, _frame_max = load_shield_tilt_table_meta()[CHAR_FOX]
    seed["guard_tilt_x8"][0, 1] = np.uint16(neutral_tilt)
    seed["guard_tilt_x4"][0, 1] = np.float32(0.0)

    sizes = msl_binding.sizes()
    neutral = _input_bytes()
    tilt = _input_bytes()
    drop = _input_bytes()
    neutral.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
    tilt_v = tilt.view(INPUT_DTYPE).reshape((1,))
    tilt_v["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
    tilt_v["p"]["main_x"][0, 1] = np.int8(50)
    drop_v = drop.view(INPUT_DTYPE).reshape((1,))
    drop_v["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
    # Pass-only down input: below the platform-pass threshold but above spotdodge.
    drop_v["p"]["main_y"][0, 1] = np.int8(-55)
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, neutral, neutral)
        b0 = msl_binding.debug_shield_bubbles_world(handle, 0)
        msl_binding.step_input(handle, neutral, tilt)
        b1 = msl_binding.debug_shield_bubbles_world(handle, 0)
        bubble_delta = (
            abs(float(b1[1, 0]) - float(b0[1, 0]))
            + abs(float(b1[1, 1]) - float(b0[1, 1]))
            + abs(float(b1[1, 2]) - float(b0[1, 2]))
        )
        assert bubble_delta > 1.0e-5

        msl_binding.step_input(handle, tilt, drop)
        msl_binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(row["action_id"][1]) == ACT_PASS
    assert int(row["on_ground"][1]) == 0
    assert int(row["ground_id"][1]) == 4


def test_shield_drop_floor_skip_clears_after_another_floor_owns_collision() -> None:
    # CollData.floor_skip blocks only the source platform. Once the fighter lands on another floor,
    # mpClearFloorSkip-style ownership clears the skipped platform id.
    import msl_binding

    seed = _seed_base(31, ACT_GUARD, 0xFFFFFFFF, 30.0, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["shield_hp"][0, 0] = np.float32(60.0)

    sizes = msl_binding.sizes()
    neutral = _input_bytes()
    drop = _input_bytes()
    drop_v = drop.view(INPUT_DTYPE).reshape((1,))
    drop_v["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    # Pass-only down input: below the platform-pass threshold but above spotdodge.
    drop_v["p"]["main_y"][0, 0] = np.int8(-55)
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = neutral
        cur = drop
        row = None
        for _ in range(60):
            msl_binding.step_input(handle, prev, cur)
            msl_binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(row["on_ground"][0]) == 1 and int(row["ground_id"][0]) != 4:
                break
            prev = cur
            cur = neutral
    finally:
        msl_binding.destroy(handle)

    assert row is not None
    assert int(row["on_ground"][0]) == 1
    assert int(row["ground_id"][0]) != 4


def test_shield_drop_jump_released_down_can_reland_same_platform() -> None:
    # mpClearFloorSkip is called by Fighter_ChangeMotionState. The shield-drop frame consumes the
    # platform skip, but after Pass hands off to JumpAerial/Fall and down is released, the same
    # platform must be admissible again.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpClearFloorSkip}
    import msl_binding

    seed = _seed_base(31, ACT_GUARD, 0xFFFFFFFF, 30.0, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["shield_hp"][0, 0] = np.float32(60.0)

    sizes = msl_binding.sizes()
    neutral = _input_bytes()
    drop = _input_bytes()
    jump = _input_bytes()
    drop_v = drop.view(INPUT_DTYPE).reshape((1,))
    drop_v["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    # Pass-only down input: below the platform-pass threshold but above spotdodge.
    drop_v["p"]["main_y"][0, 0] = np.int8(-55)
    jump.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(BUTTON_Y)
    out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, neutral, drop)
        msl_binding.step_input(handle, drop, jump)
        prev = jump
        cur = neutral
        row = None
        for _ in range(90):
            msl_binding.step_input(handle, prev, cur)
            msl_binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if int(row["on_ground"][0]) == 1:
                break
            prev = cur
            cur = neutral
    finally:
        msl_binding.destroy(handle)

    assert row is not None
    assert int(row["on_ground"][0]) == 1
    assert int(row["ground_id"][0]) == 4


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_ATTACK_DASH, SM_ATTACK_DASH),
        (ACT_ATTACK_S4_S, SM_ATTACK_S4),
        (ACT_ATTACK_HI4, SM_ATTACK_HI4),
    ],
)
def test_grounded_root_actions_snap_to_platform_edge_without_impossible_floor(
    action_id: int, submotion_id: int
) -> None:
    # Rooted grounded attacks use the same platform-edge/floor-persistence owner as locomotion.
    # At the BF side-platform edge they should clamp to the live platform edge, not preserve a
    # stale off-platform X/Y or snap to another impossible floor.
    seed = _seed_base(31, action_id, submotion_id, 57.9, 27.2000007629 + 0.0001)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["facing"][0, 0] = np.uint8(1)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4
    assert float(out["pos_x"][0]) == pytest.approx(57.600002, abs=1e-5)
    assert float(out["pos_y"][0]) == pytest.approx(27.2000007629, abs=1e-5)


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 2, 0.0, 42.75),
        (31, 2, -40.0, 27.200000762939453),
        (8, 4, 0.0, 42.0),
        (28, 2, 0.0, 51.42530059814453),
        (3, 35, -40.0, 25.0),
        (3, 36, 40.0, 25.0),
    ],
)
def test_grounded_fighter_stays_on_static_platform(stage_id: int, line_id: int, x: float, y: float) -> None:
    seed = _seed_base(stage_id, ACT_WAIT, SM_WAIT1_0, x, y)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(line_id)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == line_id
    assert float(out["pos_y"][0]) == pytest.approx(y + 0.0001, abs=1e-5)


@pytest.mark.parametrize(
    "action_id",
    [
        ACT_WAIT,
        ACT_WALK_MIDDLE,
        ACT_DASH,
        ACT_RUN,
        ACT_RUN_BRAKE,
        ACT_SQUAT_WAIT,
        ACT_GUARD_ON,
        ACT_GUARD,
        ACT_GUARD_SET_OFF,
        ACT_GUARD_REFLECT,
        ACT_DOWN_BOUND_U,
        ACT_DOWN_WAIT_U,
        ACT_PASSIVE,
        ACT_LANDING_FALL_SPECIAL,
    ],
)
def test_battlefield_platform_persistence_covers_grounded_action_families(action_id: int) -> None:
    seed = _seed_base(31, action_id, 0, -40.0, 27.200000762939453)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 2


def test_fountain_left_moving_platform_uses_seeded_stage_height() -> None:
    # FoD platform state is owned by grIzumi_801CC358 and exposed by Slippi fod_platform events.
    # Runtime must transform the stable source line id instead of grounding on the source-local
    # MSLSTG01 y=1.125 platform row.
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, -35.0, float(world_y))
    seed["stage_fod_platform_height_f32"][0, 1] = height  # platform id 1 = left
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(world_y), abs=1.0e-4)


def test_fountain_left_moving_platform_advances_seeded_stage_velocity() -> None:
    # grIzumi_801CC358 updates the platform JObj height before mpLib_80055E9C refreshes the line.
    # A rollout seed carrying current height + current height delta must therefore collide against
    # the next transformed world line, not the stale seed-frame line.
    height = np.float32(17.100000381469727)
    velocity = np.float32(-0.093023255)
    next_world_y = np.float32(1.125 + float(np.float32(height + velocity)) * 0.75)
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, -35.0, float(next_world_y))
    seed["stage_fod_platform_height_f32"][0, 1] = height  # platform id 1 = left
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(next_world_y) + 0.0001, abs=1e-5)


def test_fod_kneebend_follows_descending_height_platform_via_80083f88_coll() -> None:
    # KneeBend_Coll uses ft_80083F88 -> ft_80082708 -> mpColl_8004B108. While CollData.floor.index
    # stays attached to a grIzumi height-platform, that source path writes the signed DD90 floor
    # correction back to fp->cur_pos just like other grounded callbacks. This locks the FoD/MGS
    # JumpB startup case where KneeBend must follow the descending side platform before takeoff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    height = np.float32(15.400115966796875)
    velocity = np.float32(-0.1)
    current_world_y = np.float32(1.125 + float(height) * 0.75)
    next_world_y = np.float32(1.125 + float(np.float32(height + velocity)) * 0.75)
    seed = _seed_base(2, ACT_KNEE_BEND, SM_KNEE_BEND, -35.0, float(current_world_y))
    seed["stage_fod_platform_height_f32"][0, 1] = height  # platform id 1 = left
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_KNEE_BEND
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(next_world_y) + 0.0001, abs=1e-5)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_FX_SPECIAL_N_LOOP, SM_FX_SPECIAL_N_LOOP),
        (ACT_FX_SPECIAL_N_END, SM_FX_SPECIAL_N_END),
    ],
)
def test_fod_grounded_blaster_follows_descending_height_platform_via_80083f88_coll(
    action_id: int, submotion_id: int
) -> None:
    # Grounded Fox/Falco SpecialN collision callbacks call ft_80083F88, which writes CollData.cur_pos
    # back through mpColl_8004B108. Like KneeBend, an already-grounded blaster state on a moving
    # grIzumi height-platform must consume the signed DD90 floor correction instead of freezing at
    # the previous platform pose.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNStart_Coll,ftFx_SpecialNLoop_Coll,ftFx_SpecialNEnd_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    height = np.float32(14.030420303344727)
    velocity = np.float32(-0.1)
    current_world_y = np.float32(1.125 + float(height) * 0.75)
    next_world_y = np.float32(1.125 + float(np.float32(height + velocity)) * 0.75)
    seed = _seed_base(2, action_id, submotion_id, -35.982688903808594, float(current_world_y))
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == action_id
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(next_world_y) + 0.0001, abs=1e-5)

    static_seed = seed.copy()
    static_seed["stage_fod_platform_velocity_f32"][0, 1] = np.float32(0.0)
    static_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    static_out = _step_once(static_seed)
    assert float(static_out["pos_y"][0]) == pytest.approx(float(current_world_y) + 0.0001, abs=1e-5)


def test_grounded_blaster_floor_loss_enters_fall_via_80083f88_coll() -> None:
    # Manual set12 laser-on-Randall repro: grounded blaster was left airborne in SpecialN after the
    # platform/floor was lost. Grounded SpecialN_Coll calls ft_80083F88; when ft_80082708 reports
    # no floor, vanilla enters Fall rather than preserving the grounded blaster action in air.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    seed = _seed_base(STAGE_YOSHI, ACT_FX_SPECIAL_N_LOOP, SM_FX_SPECIAL_N_LOOP, 60.0, 42.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 4


def test_downbound_does_not_inherit_grounded_blaster_floor_loss_owner_on_valid_floor() -> None:
    # The FT80083F88 generated class includes other callback families such as downed/passive
    # states. The grounded-blaster floor-loss repair is narrowed by MotionState move_id so those
    # families do not inherit SpecialN's grounded platform-carry/floor-loss source owner while a
    # valid floor remains current.
    seed = _seed_base(STAGE_YOSHI, ACT_DOWN_BOUND_U, SM_DOWN_BOUND_U, 0.0, 42.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4


def test_fod_landing_rider_inherits_height_platform_motion_delta() -> None:
    # Landing_Coll uses ft_80084280 and keeps CollData.floor.index attached to the current floor.
    # For FoD side platforms, that current floor is the grIzumi JObj-refreshed height-transform
    # MapLine, so the grounded rider inherits velocity * MSLSTG01.height_coeff before projection.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
    height = np.float32(17.100000381469727)
    velocity = np.float32(-0.1)
    current_world_y = np.float32(1.125 + float(height) * 0.75)
    next_world_y = np.float32(1.125 + float(np.float32(height + velocity)) * 0.75)

    moving_seed = _seed_base(2, ACT_LANDING, SM_WAIT1_0, -35.0, float(current_world_y) + 0.0001)
    moving_seed["stage_fod_platform_height_f32"][0, 1] = height
    moving_seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    moving_seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    moving_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    moving_seed["on_ground"][0, 0] = np.uint8(1)
    moving_seed["ground_id"][0, 0] = np.uint16(0)

    static_seed = moving_seed.copy()
    static_seed["stage_fod_platform_velocity_f32"][0, 1] = np.float32(0.0)
    static_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)

    moving_out = _step_once(moving_seed)
    static_out = _step_once(static_seed)

    assert int(moving_out["on_ground"][0]) == 1
    assert int(moving_out["ground_id"][0]) == 0
    assert float(moving_out["pos_y"][0]) == pytest.approx(float(next_world_y) + 0.0001, abs=1e-5)
    assert float(static_out["pos_y"][0]) == pytest.approx(float(current_world_y) + 0.0001, abs=1e-5)


def test_kneebend_static_platform_does_not_gain_generic_downward_snap() -> None:
    # The 80083F88 admission above is specifically for generated moving height platforms. Static
    # platform rows keep the existing grounded anti-snap guard rather than using KneeBend as a
    # generic downward root clamp.
    seed = _seed_base(31, ACT_KNEE_BEND, SM_KNEE_BEND, -40.0, 27.700000762939453)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 2
    assert float(out["pos_y"][0]) == pytest.approx(27.700000762939453, abs=1e-6)


def test_downbound_does_not_inherit_kneebend_height_platform_carry() -> None:
    # DownBound also sits in the generated ft_80083F88 callback class, but its downed collision
    # owner is separate from KneeBend's grounded jump-start owner. Keep the moving FoD y-correction
    # out of DownBound so shield-hit knockdown rows do not drift with the platform during the
    # downed callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    height = np.float32(15.400115966796875)
    velocity = np.float32(-0.1)
    current_world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_DOWN_BOUND_U, SM_KNEE_BEND, -35.0, float(current_world_y))
    for s in (seed,):
        s["stage_fod_platform_height_f32"][0, 1] = height
        s["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
        s["on_ground"][0, 0] = np.uint8(1)
        s["ground_id"][0, 0] = np.uint16(0)
    moving_seed = seed.copy()
    moving_seed["stage_fod_platform_velocity_f32"][0, 1] = velocity
    moving_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)
    static_seed = seed.copy()
    static_seed["stage_fod_platform_velocity_f32"][0, 1] = np.float32(0.0)
    static_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(1)

    moving_out = _step_once(moving_seed)
    static_out = _step_once(static_seed)

    assert int(moving_out["action_id"][0]) == ACT_DOWN_BOUND_U
    assert int(moving_out["ground_id"][0]) == 0
    assert float(moving_out["pos_y"][0]) == pytest.approx(float(static_out["pos_y"][0]), abs=1e-6)


def test_fod_kneebend_projects_down_generated_slope_during_rollout() -> None:
    # Replay-real positive for the same KneeBend_Coll floor-persistence owner on a generated FoD
    # hard-floor slope. In EWT:9753, jump-squat root motion moves left across floor segment 6; source
    # mpLib_8004DD90_Floor returns a signed downward correction, keeping the grounded root attached
    # to the slope before the later JumpB/AttackAirB sequence.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9753:9754]
    out = _step_once(row["seed_t"].copy(), row["prev_input_t"].view("u1").reshape(1, -1).copy(),
                     row["input_t"].view("u1").reshape(1, -1).copy())
    ref = row["ref_t1"][0]

    assert int(row["seed_t"]["action_id"][0, 0]) == ACT_KNEE_BEND
    assert int(row["seed_t"]["ground_id"][0, 0]) == 6
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_KNEE_BEND
    assert int(out["on_ground"][0]) == int(ref["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == int(ref["ground_id"][0]) == 6
    assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_fod_kneebend_to_landingfallspecial_projects_connected_slope_floor() -> None:
    # EWT:9755 is the next callback in the same source family as the KneeBend slope projection
    # above. `KneeBend_Coll -> ft_80083F88 -> ft_80082708 -> mpColl_8004B108` reaches the connected
    # flat floor through `mpLib_8004DD90_Floor`, then publishes LandingFallSpecial on that returned
    # floor. The previous-action continuity keeps this limited to the jump-squat handoff; unrelated
    # first-frame LandingFallSpecial entries keep the ordinary landing entry guard.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{ftCo_Landing_Coll,ftCo_LandingFallSpecial_Enter}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_80084280}
    # refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9755:9756]
    seed = row["seed_t"].copy()
    out = _step_once(seed, row["prev_input_t"].view("u1").reshape(1, -1).copy(),
                     row["input_t"].view("u1").reshape(1, -1).copy())
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][0, 0]) == ACT_KNEE_BEND
    assert int(seed["seed_prev_action_id"][0, 0]) == ACT_KNEE_BEND
    assert int(seed["ground_id"][0, 0]) == 6
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0
    assert int(out["on_ground"][0]) == int(ref["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == int(ref["ground_id"][0]) == 5
    assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)

    stale_prev_seed = row["seed_t"].copy()
    stale_prev_seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_WAIT)
    stale_prev_out = _step_once(stale_prev_seed, row["prev_input_t"].view("u1").reshape(1, -1).copy(),
                                row["input_t"].view("u1").reshape(1, -1).copy())

    assert int(stale_prev_out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(stale_prev_out["ground_id"][0]) == 5
    assert float(stale_prev_out["pos_y"][0]) > float(ref["pos_y"][0]) + 0.1


def test_yoshi_randall_carries_grounded_rider_with_platform_motion() -> None:
    # Randall is a stage-object-owned transformed floor. Grounded riders keep CollData.floor.index
    # and inherit the platform transform delta before projection, rather than standing at stale X
    # until the platform slides out from under them.
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
    frame_id = 477
    x0 = 101.235443 - 11.9
    y = -13.64989
    seed = _seed_base(8, ACT_WAIT, SM_WAIT1_0, x0 + 6.0, y + 0.0001)
    seed["frame_id"][0] = np.int32(frame_id)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1000)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_x"][0]) < float(seed["pos_x"][0, 0]) - 0.3
    assert float(out["pos_y"][0]) == pytest.approx(-13.64979, abs=1e-5)


def test_yoshi_randall_carries_landing_rider_with_platform_motion() -> None:
    # Manual set9 repro: Fox landed on Randall and froze in X throughout Landing, then only moved
    # again once Landing ended. Landing and LandingAir use ftCo_Landing_Coll -> ft_80084280, so an
    # already-grounded rider keeps CollData.floor.index and inherits Randall's stage-object motion.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Coll
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
    frame_id = 477
    seed = _seed_base(8, ACT_LANDING, SM_WAIT1_0, 95.8551, -13.64989 + 0.0001)
    seed["frame_id"][0] = np.int32(frame_id)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1000)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_x"][0]) < float(seed["pos_x"][0, 0]) - 0.3
    assert float(out["pos_y"][0]) == pytest.approx(-13.64979, abs=1e-5)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_FX_SPECIAL_N_LOOP, SM_FX_SPECIAL_N_LOOP),
        (ACT_FX_SPECIAL_N_END, SM_FX_SPECIAL_N_END),
    ],
)
def test_yoshi_randall_carries_grounded_blaster_via_80083f88_coll(
    action_id: int, submotion_id: int
) -> None:
    # Grounded Fox/Falco Blaster callbacks use ft_80083F88 -> ft_80082708 -> mpColl_8004B108.
    # On Randall, that source path preserves the current path-transformed stage-object floor and
    # inherits the stage object's motion just like other grounded floor-persistence callbacks.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNStart_Coll,ftFx_SpecialNLoop_Coll,ftFx_SpecialNEnd_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
    frame_id = 477
    seed = _seed_base(8, action_id, submotion_id, 95.8551, -13.64989 + 0.0001)
    seed["frame_id"][0] = np.int32(frame_id)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1000)
    seed["action_frame"][0, 0] = np.int16(6)
    seed["anim_frame_f32"][0, 0] = np.float32(6.0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == action_id
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_x"][0]) < float(seed["pos_x"][0, 0]) - 0.3
    assert float(out["pos_y"][0]) == pytest.approx(-13.64979, abs=1e-5)


def test_yoshi_randall_grounded_blaster_carry_accepts_raw_replay_ground_id() -> None:
    # Slippi/replay rows can report raw Yoshi line 0 while the fighter is visibly supported by the
    # generated Randall path line. Runtime uses the generated MSLSTG01 Randall line for CollData
    # support while preserving the replay-visible raw ground id in the public compare row.
    # Replay probe: /home/kyle/Slippi/2026-05/Game_20260522T051848.slp.
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
    # data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
    seed = _seed_base(8, ACT_FX_SPECIAL_N_LOOP, SM_FX_SPECIAL_N_LOOP, 101.62235, -17.32131)
    seed["frame_id"][0] = np.int32(463)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)

    out = _step_once_rollout(seed)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_N_LOOP
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_x"][0]) == pytest.approx(float(seed["pos_x"][0, 0]), abs=1e-6)
    assert float(out["pos_y"][0]) > float(seed["pos_y"][0, 0]) + 0.3
    assert float(out["pos_y"][0]) == pytest.approx(-16.96656, abs=1e-5)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_DOWN_FOWARD_U, SM_DOWN_FOWARD_U),
        (ACT_PASSIVE_STAND_F, SM_PASSIVE_STAND_F),
    ],
)
def test_yoshi_randall_carries_downed_ft80084104_rider(action_id: int, submotion_id: int) -> None:
    # These downed/passive-family callbacks use ft_80084104, the same floor-persistence owner as
    # grounded attacks. They should stay attached to Randall's generated floor and inherit its
    # stage-object carry rather than waiting for a later non-downed action to start moving.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084104
    frame_id = 477
    seed = _seed_base(8, action_id, submotion_id, 95.8551, -13.64989 + 0.0001)
    seed["frame_id"][0] = np.int32(frame_id)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(1000)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == action_id
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_x"][0]) > float(seed["pos_x"][0, 0]) + 1.0


def test_fod_transformed_platform_edge_snap_uses_world_height_for_rooted_actions() -> None:
    # Grounded rooted callbacks near FoD platform edges must snap to the transformed world line,
    # not the source-local MSLSTG01 y=1.125 row. This covers the manual fsmash/dash-attack/roll
    # "teleport to ground until the move ends" failure mode.
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_ATTACK_S4_S, SM_ATTACK_S4, -20.5, float(world_y) + 0.0001)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(world_y), abs=1e-4)


@pytest.mark.parametrize(
    ("action_id", "submotion_id"),
    [
        (ACT_ATTACK_AIR_N, 68),
        (ACT_DAMAGE_FLY_TOP, 180),
    ],
)
def test_fod_source_owned_airborne_aerial_and_tumble_can_land_on_transformed_platform_with_down_input(
    action_id: int, submotion_id: int
) -> None:
    # AttackAir_Coll and DamageFly_Coll do not pass ftCo_80096CC8, so held down cannot reject a
    # current source-owned FoD platform floor publication. Unsourced AttackAir pass-through rows are
    # covered separately by the transformed-platform floor-skip owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, action_id, submotion_id, -35.0, float(world_y) - 2.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(4)
    if action_id == ACT_ATTACK_AIR_N:
        seed["action_frame"][0, 0] = np.int16(4)
        seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["speed_y_attack"][0, 0] = np.float32(-5.0 if action_id == ACT_DAMAGE_FLY_TOP else -1.0)
    seed["hitstun"][0, 0] = np.uint8(10 if action_id == ACT_DAMAGE_FLY_TOP else 0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 14.0)
    prev_input = _input_bytes()
    prev_input.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)
    out = _step_once(seed, prev_input, input_t)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(world_y) + 0.0001, abs=1e-4)


def test_fod_damagefly_lands_on_downward_sweep_even_with_positive_kb_lane() -> None:
    # DamageFly_Coll routes through ft_80081DD4 and mpColl's floor sweep. The collision predicate is
    # the live CollData bottom movement; a still-positive split KB lane is not a source reason to
    # reject an actually descending platform crossing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor}
    height = np.float32(20.576549530029297)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_DAMAGE_FLY_TOP, 180, 30.5, float(world_y) - 6.0)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["stage_fod_platform_height_f32"][0, 0] = height  # platform id 0 = right
    seed["stage_fod_platform_height_valid_u8"][0, 0] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-3.1)
    seed["speed_y_attack"][0, 0] = np.float32(1.5)
    seed["hitstun"][0, 0] = np.uint8(15)
    seed["action_frame"][0, 0] = np.int16(46)
    seed["anim_frame_f32"][0, 0] = np.float32(46.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(30.5)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 7.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1
    assert int(out["action_id"][0]) == ACT_DOWN_BOUND_D
    assert float(out["pos_y"][0]) == pytest.approx(float(world_y) + 0.0001, abs=1e-4)


def test_fod_escapeair_downward_airdodge_can_waveland_on_transformed_platform() -> None:
    # EscapeAir_Coll uses ft_80082C74 and does not pass ftCo_80096CC8, so a downward airdodge can
    # waveland on FoD transformed platforms instead of inheriting the common-air held-down
    # pass-through gate. Floor skip remains the separate shield-drop/pass owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, -35.0, float(world_y) + 2.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 14.0)
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)
    out = _step_once(seed, _input_bytes(), input_t)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0


@pytest.mark.integration
def test_fod_jumpaerial_escapeair_entry_can_waveland_on_static_center_platform_replay_real() -> None:
    # EWT record 5925 enters EscapeAir from JumpAerialF during IASA, then the same
    # EscapeAir_Coll callback lands on FoD's generated static-y center platform. Source
    # `ft_80081D0C` writes CollData.last_pos from the frame-start JumpAerial root before
    # `mpColl_800471F8`; the pre-entry ECB has also advanced through Fighter_8006A360 before IASA.
    # Do not require a stale replay ecb_lock lane for this no-lock, same-frame platform waveland.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,mpColl_80044628_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 5925
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == 2

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


def test_fod_jumpaerial_escapeair_static_platform_sweep_requires_bottom_crossing() -> None:
    # Negative boundary for the no-lock JumpAerial -> EscapeAir entry owner: the source
    # `mpColl_80044628_Floor` floor hit still requires the callback-visible ECB bottom to cross the
    # platform. A frame-start JumpAerial seed lane alone must not snap to FoD's static-y platform.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
    seed = _seed_base(2, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, -6.0, 36.0)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(10)
    seed["speed_air_x_self"][0, 0] = np.float32(2.1)
    seed["speed_y_self"][0, 0] = np.float32(-1.8)
    seed["ground_id"][0, 0] = np.uint16(5)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-8.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(30.0)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 5


def test_fod_jumpaerial_escapeair_entry_preserves_pre_entry_ecb_for_right_ledge_floor() -> None:
    # Modelplay trace lock: reports/modelplay/20260521_fod_trained_vs_base/
    # trace_001_trained_p1_seed101/trace.json frames 8400..8404. JumpAerialF IASA enters
    # EscapeAir near FoD's right side wall; source EscapeAir_Coll calls ft_80082C74 ->
    # mpColl_800471F8 after mpCollPrev has copied the pre-entry JumpAerial ECB. The floor sweep is
    # from that pre-entry ECB bottom to the current EscapeAir bottom, so it must publish the
    # generated right ledge instead of letting the fighter pass under the stage.
    # data/stages/bin/griz.bin::MSLSTG01 line 7 right ledge
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,mpCheckFloor}
    seed = _seed_base(2, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 61.62493896484375, -2.6133792400360107)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.04)
    seed["speed_y_self"][0, 0] = np.float32(-1.9011576)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(2)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(63.6669235)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(-0.7122216)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 7
    assert float(out["pos_y"][0]) == pytest.approx(0.623875, abs=1e-5)


def test_fod_jumpaerial_escapeair_right_ledge_entry_requires_early_source_phase() -> None:
    # Frame-boundary negative for the pre-entry ECB owner above: later JumpAerial source frames do
    # not use the fresh entry handoff even if the old trace-visible bottom path is near FoD's ledge.
    seed = _seed_base(2, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 61.62493896484375, 3.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.04)
    seed["speed_y_self"][0, 0] = np.float32(-0.2)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(4)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(63.6669235)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(3.2)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 6


def test_fod_jumpaerial_escapeair_right_ledge_entry_requires_bottom_sweep_crossing() -> None:
    # Bottom-sweep negative for the pre-entry ECB owner above: keep the source/admitted frame window
    # live, but place both pre-entry and current ECB bottoms above the generated right ledge floor.
    # Preserving a JumpAerial ECB is not enough to synthesize LandingFallSpecial without the actual
    # `mpCheckFloor` segment crossing used by EscapeAir_Coll.
    seed = _seed_base(2, ACT_ESCAPE_AIR, SM_ESCAPE_AIR, 61.62493896484375, 10.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(6)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.04)
    seed["speed_y_self"][0, 0] = np.float32(-0.2)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_JUMP_AERIAL_F)
    seed["seed_prev_action_frame"][0, 0] = np.int16(2)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(63.6669235)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(10.2)

    out = _step_once(seed)

    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 6


def test_fod_common_air_down_input_rejects_transformed_soft_platform_callback() -> None:
    # Ordinary Fall still passes ftCo_80096CC8, including on transformed FoD platform lines. Held
    # down should pass through here, while AttackAir/EscapeAir/Damage owners above remain admitted.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    height = np.float32(19.899999618530273)
    world_y = np.float32(1.125 + float(height) * 0.75)
    seed = _seed_base(2, ACT_FALL, SM_FALL, -35.0, float(world_y) - 2.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = height
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(float(world_y) + 14.0)
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)

    out = _step_once(seed, _input_bytes(), input_t)

    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF


def test_fod_negative_reconstructed_platform_height_stays_below_stage_until_visibility_lane() -> None:
    # Negative FoD heights in no-event replay caches are the existing reconstructed below-stage
    # sentinel. They should not become a low active platform near the main floor unless/until the
    # separate grIzumi JObj hidden/visible phase is modeled explicitly.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::{mpLib_80055E9C,mpJointHide}
    seed = _seed_base(2, ACT_FALL, SM_FALL, -35.0, 2.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["stage_fod_platform_height_f32"][0, 1] = np.float32(-1.0)
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["speed_y_self"][0, 0] = np.float32(-4.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-35.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(2.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF
    assert float(out["pos_y"][0]) < 0.0


def test_guard_on_yoshi_platform_stays_grounded() -> None:
    seed = _seed_base(8, ACT_GUARD, 0, 0.0, 42.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4
    assert int(out["action_id"][0]) != ACT_FALL


@pytest.mark.parametrize(
    ("stage_id", "line_id", "x", "y"),
    [
        (2, 2, 0.0, 42.75),
        (31, 2, -40.0, 27.200000762939453),
        (8, 4, 0.0, 42.0),
        (28, 2, 0.0, 51.42530059814453),
        (3, 35, -40.0, 25.0),
        (3, 36, 40.0, 25.0),
    ],
)
def test_damagefall_lands_on_static_platform(stage_id: int, line_id: int, x: float, y: float) -> None:
    seed = _seed_base(stage_id, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, x, y + 8.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(y + 14.0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == line_id


def test_common_air_down_input_rejects_soft_platform_callback() -> None:
    # ftCo_80096CC8 is passed to common-air floor collision through ft_80083090_inline.
    # The callback rejects platform floors while stick Y is held at/below p_ftCommonData->x25C, so
    # the same Battlefield platform sweep stays airborne for Fall but still admits DamageFall, whose
    # collision owner does not pass the soft-platform callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_8008370C}
    input_t = _input_bytes()
    input_t.view(INPUT_DTYPE).reshape((1,))["p"]["main_y"][0, 0] = np.int8(-95)

    fall = _seed_base(31, ACT_FALL, SM_FALL, -40.0, 35.0)
    fall["speed_y_self"][0, 0] = np.float32(-10.0)
    fall["ground_id"][0, 0] = np.uint16(0xFFFF)
    fall["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    fall["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    fall["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)
    fall_out = _step_once(fall, _input_bytes(), input_t)

    damage = _seed_base(31, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, -40.0, 35.0)
    damage["speed_y_self"][0, 0] = np.float32(-10.0)
    damage["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    damage["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    damage["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)
    damage_out = _step_once(damage, _input_bytes(), input_t)

    assert int(fall_out["on_ground"][0]) == 0
    assert int(fall_out["ground_id"][0]) == 0xFFFF
    assert int(damage_out["on_ground"][0]) == 1
    assert int(damage_out["ground_id"][0]) == 2


def test_jump_up_through_platform_does_not_ground_from_below() -> None:
    seed = _seed_base(31, ACT_FALL, SM_FALL, -40.0, 25.0)
    seed["speed_y_self"][0, 0] = np.float32(3.0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 0xFFFF


def test_frozen_pokemon_stadium_ignores_transformation_platform_line() -> None:
    # The GrPs collision artifact contains transformation-object platforms as source data, but the
    # current runtime domain is frozen Stadium. The source frozen toggle makes Stadium take the
    # no-transformation branch; line 11 remains debug-visible but is not fighter-solid.
    #
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # refs/melee/src/melee/gr/grpstadium.c::{grStadium_OnInit,grStadium_801D10F0}
    seed = _seed_base(3, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, 40.0, 34.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)

    out = _step_once(seed)

    assert int(out["ground_id"][0]) != 11


def test_frozen_pokemon_stadium_rejects_inactive_raw_wall_for_fighter_collision() -> None:
    # MSLSTG01 keeps raw transformation wall records visible, but the generated frozen/current
    # domain marks them non-fighter-solid so runtime wall collision ignores them.
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # refs/melee/src/melee/gr/grpstadium.c::{grStadium_OnInit,grStadium_801D10F0}
    import msl_binding

    stage = read_mslstg01_v7(Path("data/stages/bin/grps.bin"))
    wall = next(seg for seg in stage.segments if int(seg.line_id) == 81)
    assert int(wall.kind_id) == 2  # right_wall
    assert wall.fighter_solid is False
    # Current frozen-domain PS has no inactive ceiling records. Ceiling collision is still covered
    # by the same generated fighter_solid metadata path when such records exist.
    assert all(seg.fighter_solid for seg in stage.segments if int(seg.kind_id) == 1)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    contacts_stride = int(sizes["collision_contacts"])
    contacts_dtype = _collision_contacts_dtype()
    assert int(contacts_dtype.itemsize) == contacts_stride

    char_id = CHAR_FOX
    msid_wait = SM_WAIT1_0
    af = 0
    min_x, _max_x, _min_y, max_y = msl_binding.ecb_extents_rel(char_id, msid_wait, af)
    bottom_y = float(msl_binding.ecb_bottom_rel_y(char_id, msid_wait, af))
    side_y = 0.5 * (float(max_y) + bottom_y)
    wall_y_mid = 0.5 * (float(wall.y0) + float(wall.y1))
    wall_x = float(wall.x0)

    seed = _seed_base(3, ACT_WAIT, msid_wait, wall_x + 0.05 - float(min_x), wall_y_mid - side_y)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["speed_air_x_self"][0, 0] = np.float32(-2.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        contacts = np.zeros((1, contacts_stride), dtype=np.uint8)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.debug_write_collision_contacts(handle, contacts)
        c0 = contacts.view(contacts_dtype).reshape((1,))[0]
        assert int(c0["wall_id"][0]) != 81
    finally:
        msl_binding.destroy(handle)


def test_frozen_pokemon_stadium_inactive_transform_policy_covers_floor_wall_ceiling() -> None:
    # Generated MSLSTG01 fighter_solid policy is shared by floor, wall, and ceiling queries.
    # Frozen Stadium currently exposes inactive transform floors and walls, and no inactive
    # transform ceilings; if ceiling records are added later, this lock should be extended to step
    # through one instead of adding a stage-id allowlist in runtime code.
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # data/stages/bin/grps.bin::MSLSTG01 flags
    stage = read_mslstg01_v7(Path("data/stages/bin/grps.bin"))
    inactive_floors = [seg for seg in stage.segments if int(seg.kind_id) == 0 and not seg.fighter_solid]
    inactive_ceilings = [seg for seg in stage.segments if int(seg.kind_id) == 1 and not seg.fighter_solid]
    inactive_right_walls = [seg for seg in stage.segments if int(seg.kind_id) == 2 and not seg.fighter_solid]
    inactive_left_walls = [seg for seg in stage.segments if int(seg.kind_id) == 3 and not seg.fighter_solid]

    assert inactive_floors
    assert inactive_right_walls
    assert inactive_left_walls
    assert inactive_ceilings == []

    seed = _seed_base(3, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, 40.0, 34.0)
    seed["speed_y_self"][0, 0] = np.float32(-10.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(40.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)

    out, contacts = _step_once_with_contacts(seed)

    assert int(out["ground_id"][0]) not in {int(seg.line_id) for seg in inactive_floors}
    assert int(contacts["wall_id"][0]) not in {
        int(seg.line_id) for seg in inactive_right_walls + inactive_left_walls
    }
    assert int(contacts["ceiling_id"][0]) == 0xFFFF


def test_squat_down_input_enters_pass_and_skips_source_platform() -> None:
    seed = _seed_base(31, ACT_SQUAT, 0, -40.0, 27.200000762939453)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["action_frame"][0, 0] = np.int16(3)
    seed["anim_frame_f32"][0, 0] = np.float32(3.0)
    seed["tilt_timer_y"][0, 0] = np.uint8(1)

    prev_input = _input_bytes()
    input_t = _input_bytes()
    prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
    cur_view = input_t.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_y"][0, 0] = np.int8(-80)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_input, input_t)

    assert int(out["action_id"][0]) == ACT_PASS
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 2

    seed2 = _seed_base(31, ACT_PASS, SM_PASS, -40.0, 27.0)
    seed2["seed_prev_action_id"][0, 0] = np.uint16(ACT_PASS)
    seed2["ground_id"][0, 0] = np.uint16(2)
    seed2["speed_y_self"][0, 0] = np.float32(-0.5)
    seed2["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed2["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed2["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(28.0)

    out2 = _step_once(seed2)

    assert int(out2["action_id"][0]) == ACT_PASS
    assert int(out2["on_ground"][0]) == 0

    seed3 = _seed_base(31, ACT_DAMAGE_FALL, SM_DAMAGE_FALL, -40.0, 35.0)
    seed3["seed_prev_action_id"][0, 0] = np.uint16(ACT_FALL)
    seed3["ground_id"][0, 0] = np.uint16(2)
    seed3["speed_y_self"][0, 0] = np.float32(-10.0)
    seed3["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed3["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(-40.0)
    seed3["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(40.0)

    out3 = _step_once(seed3)

    assert int(out3["action_id"][0]) != ACT_PASS
    assert int(out3["on_ground"][0]) == 1
    assert int(out3["ground_id"][0]) == 2


def test_pass_iasa_can_enter_aerial_shine() -> None:
    seed = _seed_base(31, ACT_PASS, SM_PASS, -40.0, 26.5)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["speed_y_self"][0, 0] = np.float32(-0.67)

    input_t = _input_bytes()
    cur_view = input_t.view(INPUT_DTYPE).reshape((1,))
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_once(seed, _input_bytes(), input_t)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_LW_START


@pytest.mark.integration
def test_fod_jump_to_aerial_shine_start_rejects_stale_platform_sweep_replay_real() -> None:
    # EWT record 10580 enters aerial Shine from JumpF before Fighter_procMap. Source
    # `ftFx_SpecialAirLwStart_Coll -> ft_80081D0C` rewrites CollData.last_pos from the current
    # fighter root before `mpColl_800471F8`; the prior JumpF bottom sweep cannot by itself publish
    # FoD's static center platform when the callback-current bottom is still below that platform.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialAirLw_Enter,ftFx_SpecialAirLwStart_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 10580
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]

    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["speed_y_self"][p]) == pytest.approx(0.0, abs=1e-6)

    # Boundary control: this is specifically the JumpF/B -> SpecialAirLwStart callback-lifetime
    # owner. A non-Jump predecessor is not rejected by this guard.
    seed = np.array([row["seed_t"]], dtype=SEED_DTYPE)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_FALL)
    out2 = _step_once(
        seed,
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, -1),
        np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, -1),
    )

    assert int(out2["action_id"][p]) == ACT_FX_SPECIAL_LW_START
    assert int(out2["on_ground"][p]) == 1
    assert int(out2["ground_id"][p]) == 2


@pytest.mark.integration
def test_guard_drop_through_replay_real_enters_pass_on_frozen_ps_platform() -> None:
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[90]
    p = 1

    assert int(row["seed_t"]["stage_id"]) == 3
    assert int(row["seed_t"]["action_id"][p]) == 178  # GuardOn
    assert int(row["seed_t"]["ground_id"][p]) == 36
    assert int(row["ref_t1"]["action_id"][p]) == ACT_PASS

    out = _step_one_replay_row(ds, 90)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == ACT_PASS
    assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 36
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_yoshi_stage_object_support_replay_real_keeps_wait_on_raw_non_solid_floor() -> None:
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[7082]
    p = 1

    assert int(row["seed_t"]["stage_id"]) == 8
    assert int(row["seed_t"]["stage_yoshi_shyguy_valid_u8"]) == 1
    assert int(row["seed_t"]["action_id"][p]) == ACT_WAIT
    assert int(row["seed_t"]["ground_id"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_WAIT
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _step_one_replay_row(ds, 7082)

    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(row["ref_t1"][field][p]), field
    assert int(out["jumps_left"][p]) == int(row["ref_t1"]["jumps_left"][p])
    # The runtime owner carries Randall through the generated path-transformed floor while public
    # replay output keeps raw ground id 0. The residual is one floor-bias-scale projection delta; the
    # lock here is for action/grounded/public-ground-id preservation, not f32-exact Randall display Y.
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_frozen_ps_platform_dash_ignores_inactive_side_wall_replay_real() -> None:
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[6258]
    p = 1

    assert int(row["seed_t"]["stage_id"]) == 3
    assert int(row["seed_t"]["ground_id"][p]) == 35
    assert int(row["seed_t"]["action_id"][p]) == ACT_DASH
    assert float(row["ref_t1"]["pos_x"][p]) == pytest.approx(
        float(row["seed_t"]["pos_x"][p]) + float(row["ref_t1"]["speed_ground_x_self"][p]),
        abs=1e-6,
    )

    out = _step_one_replay_row(ds, 6258)

    assert int(out["ground_id"][p]) == 35
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_yoshi_cliffjump_down_input_passes_through_platform_rollout_replay_real() -> None:
    # Disruptive packet F08b_body_contact_geometry_residual, PhysicalElectricCapybara record 6575:
    # CliffJumpSlow2 holds stick down while crossing Yoshi's left platform. Source passes
    # ftCo_80096CC8 into mpColl_80044628_Floor, so the platform is rejected and the rollout remains
    # in CliffJumpSlow2 instead of entering Landing.
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    out = _rollout_replay_to_record(ds, 6575, 6577)
    ref = ds.samples[6577]["ref_t1"]
    p = 1

    assert int(ref["action_id"][p]) == ACT_CLIFF_JUMP_SLOW2
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 3
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_yoshi_escapeair_locked_desired_bottom_waits_then_lands_rollout_replay_real() -> None:
    # PhysicalElectricCapybara records 146 -> 149 expose the free-running counterpart of the
    # direct locked-ECB rows in test_escapeair_locked_ecb_bottom_replay_real_locks.py. EscapeAir_Coll
    # preserves CollData_X130_Locked desired_ecb.bottom through mpColl_800471F8; the first shallow
    # platform root snap stays airborne, then the later desired-bottom sweep owns LandingFallSpecial.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    p = 1

    for airborne_record in (146, 147, 148):
        airborne = _rollout_replay_to_record(ds, 0, airborne_record)
        ref_airborne = ds.samples[airborne_record]["ref_t1"]
        assert int(airborne["action_id"][p]) == int(ref_airborne["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(airborne["action_frame"][p]) == int(ref_airborne["action_frame"][p])
        assert int(airborne["on_ground"][p]) == int(ref_airborne["on_ground"][p]) == 0
        assert int(airborne["ground_id"][p]) == int(ref_airborne["ground_id"][p]) == 3
        assert float(airborne["pos_y"][p]) == pytest.approx(float(ref_airborne["pos_y"][p]), abs=1e-6)

    landed = _rollout_replay_to_record(ds, 0, 149)
    ref_landed = ds.samples[149]["ref_t1"]
    assert int(landed["action_id"][p]) == int(ref_landed["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(landed["action_frame"][p]) == int(ref_landed["action_frame"][p]) == 0
    assert int(landed["on_ground"][p]) == int(ref_landed["on_ground"][p]) == 1
    assert int(landed["ground_id"][p]) == int(ref_landed["ground_id"][p]) == 1
    assert float(landed["pos_y"][p]) == pytest.approx(float(ref_landed["pos_y"][p]), abs=1e-5)


@pytest.mark.integration
def test_fod_downbound_platform_rows_do_not_borrow_platform_carry_snap_replay_real() -> None:
    # EWT record 5135 is DownBoundU already grounded on the transformed FoD left platform. The
    # stable grounded platform-carry correction must not leak into knockdown floor-contact
    # callbacks; those callbacks own their projection/transition separately.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    out = _step_one_replay_row(ds, 5135)
    ref = ds.samples[5135]["ref_t1"]
    p = 1

    assert int(ds.samples[5135]["seed_t"]["stage_id"]) == 2
    assert int(ds.samples[5135]["seed_t"]["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_downbound_airborne_refreshes_source_trusted_height_platform_floor_replay_real() -> None:
    # MGS record 1885 is airborne DownBoundD crossing the FoD left height platform. Vanilla's
    # DownBound_Coll calls ft_80082708 -> mpColl_8004B108 and can refresh CollData.floor.index and
    # cur_pos from the source-trusted height-platform line while still returning GA_Air to the
    # callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    slp_path = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
    )
    out = _step_one_replay_row(ds, 1885)
    row = ds.samples[1885]
    ref = row["ref_t1"]
    p = 1

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) != 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_downbound_airborne_height_platform_refresh_requires_current_platform_owner_replay_real() -> None:
    # The same DownBound_Coll refresh is allowed only when the current FoD height platform is
    # source/current-owned for this seed/current frame. Clearing both source and velocity ownership
    # leaves the airborne row on its stale floor index instead of inventing platform authority from
    # proximity.
    slp_path = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
    )
    row = ds.samples[1885]
    p = 1
    import msl_binding

    input_stride = int(msl_binding.sizes()["input"])
    seed = np.array(row["seed_t"], dtype=SEED_DTYPE).reshape((1,))
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(0)
    seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(0)
    prev_input = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_t = (
        np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )

    out = _step_once(seed, prev_input, input_t)

    assert int(row["seed_t"]["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(row["seed_t"]["ground_id"][p]) == 5
    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][1]) == 1
    assert int(out["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == 5
    assert float(out["pos_y"][p]) != pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-4)


@pytest.mark.integration
def test_fod_downbound_airborne_follows_height_platform_until_hidden_replay_real() -> None:
    # MGS records 1886-1888 cover the continuation after DownBoundD has refreshed CollData.floor to
    # FoD's left side-platform. DownBound_Coll uses the allow-ground-to-air path, so the fighter
    # remains airborne while the live grIzumi platform carries cur_pos downward; once grIzumi sends
    # the platform to the generated hidden target, mpColl refreshes the floor back to the exposed
    # hard floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    slp_path = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
    )
    p = 1
    for record, expected_ground_id, pos_abs in ((1886, 0, 2.0e-5), (1887, 0, 2.0e-5), (1888, 5, 1.0e-6)):
        out = _step_one_replay_row(ds, record)
        row = ds.samples[record]
        ref = row["ref_t1"]

        assert int(row["seed_t"]["stage_id"]) == 2
        assert int(row["seed_t"]["action_id"][p]) == ACT_DOWN_BOUND_D
        assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_D
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
        assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == expected_ground_id
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=pos_abs)


@pytest.mark.integration
def test_fod_jumpf_down_input_passes_through_platform_rollout_replay_real() -> None:
    # Disruptive packet F08d_damage_timer_scalar_residual, ElatedWearyTermite record 10573:
    # the visible first diff was a false platform Landing while JumpF held stick down through the
    # moving FoD platform. The runtime should keep using the transformed platform line for geometry
    # but reject it through ftCo_80096CC8 for this callback.
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    out = _rollout_replay_to_record(ds, 10573, 10579)
    ref = ds.samples[10579]["ref_t1"]
    p = 1

    assert int(ref["action_id"][p]) == ACT_JUMP_F
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 5
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_jumpf_already_below_transformed_platform_stays_airborne_replay_real() -> None:
    # MGS record 3232 is a JumpF continuation under FoD's left transformed platform. The loaded ECB
    # bottom overlaps the platform, but both callback root endpoints are already below it, so
    # mpColl_80044948_Floor's root-projection path has no above->below soft-platform crossing to
    # publish as Landing.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044948_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 3232
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_attackair_shallow_transformed_platform_ecb_crossing_stays_airborne_replay_real() -> None:
    # MGS record 3172 is a sustained Falco AttackAirLw continuation under FoD's left moving
    # platform. The transformed platform intersects the live ECB bottom, but both callback root
    # endpoints are already below it and the penetration is shallow; AttackAir_Coll should not
    # publish LandingAirLw until the ft_80082C74/mpColl_800471F8 floor owner has a stable handoff.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 3172
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_attackair_deep_transformed_platform_crossing_still_lands_replay_real() -> None:
    # Negative boundary for the shallow AttackAir transformed-platform guard: PTE record 957 is
    # still a sustained AttackAirB row on FoD's right moving platform, but the source callback
    # publishes the normal Landing floor handoff.
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 957
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    # The negative boundary is the callback-owned Landing handoff; the float compare can differ by
    # the floor-y bias used when projecting against the transformed FoD platform line.
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


def test_fd_damageair_to_attackairhi_entry_bottom_sweep_lands_hard_floor() -> None:
    # Damage_IASA / Fall_IASA can enter AttackAir before Fighter_procMap. The first entered
    # AttackAir_Coll pass still consumes CollData's pre-entry current ECB as `prev_ecb`, then loads
    # the AttackAir desired ECB before mpColl_80044628_Floor. This locks the hard-floor owner that
    # prevents first-frame AttackAirHi entries from falling through FD after DamageAir.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{ftCo_AttackAir_Enter,ftCo_AttackAir_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800471F8,mpColl_80044628_Floor}
    seed = _seed_base(32, ACT_ATTACK_AIR_HI, SM_ATTACK_AIR_HI, 22.546633, -6.824717)
    p = 0
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0xFFFF)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DAMAGE_AIR_2)
    seed["seed_prev_action_frame"][0, p] = np.int16(18)
    seed["speed_y_self"][0, p] = np.float32(-2.514217)
    seed["speed_air_x_self"][0, p] = np.float32(0.295344)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(22.251289)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-4.348161)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(0.0001, abs=1e-7)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(colldata["floor_result_segment_id"][p]) == 1

    no_cross = seed.copy()
    no_cross["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-6.0)
    no_cross_out, _contacts2, no_cross_colldata = _step_once_with_contacts_and_colldata(no_cross)

    assert int(no_cross_out["action_id"][p]) == ACT_ATTACK_AIR_HI
    assert int(no_cross_out["on_ground"][p]) == 0
    assert int(no_cross_out["ground_id"][p]) == 0xFFFF
    assert int(no_cross_colldata["floor_result_valid"][p]) == 0


@pytest.mark.parametrize(
    ("attack_action", "attack_motion"),
    [
        (ACT_ATTACK_AIR_F, SM_ATTACK_AIR_F),
        (ACT_ATTACK_AIR_B, SM_ATTACK_AIR_B),
        (ACT_ATTACK_AIR_HI, SM_ATTACK_AIR_HI),
    ],
)
def test_fd_damagefly_to_attackair_entry_bottom_sweep_lands_hard_floor(
    attack_action: int, attack_motion: int
) -> None:
    # DamageFly_IASA delegates through DamageFall_IASA and can enter AttackAir before
    # Fighter_procMap. The entered AttackAir_Coll still consumes the callback-local CollData
    # current ECB promoted by mpCollInterpolateECB, so first-frame AttackAir* entries from
    # DamageFlyTop must publish the hard floor when that source bottom sweep crosses it.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800471F8,mpColl_80044628_Floor}
    seed = _seed_base(32, attack_action, attack_motion, -28.25, -7.37)
    p = 0
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0xFFFF)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["seed_prev_action_frame"][0, p] = np.int16(34)
    seed["speed_y_self"][0, p] = np.float32(-1.85)
    seed["speed_air_x_self"][0, p] = np.float32(-0.08)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(-28.18)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-4.0)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(0.0001, abs=1e-7)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(colldata["floor_result_segment_id"][p]) == 1

    no_cross = seed.copy()
    no_cross["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-6.0)
    no_cross_out, _contacts2, no_cross_colldata = _step_once_with_contacts_and_colldata(no_cross)

    assert int(no_cross_out["on_ground"][p]) == 0
    assert int(no_cross_out["ground_id"][p]) == 0xFFFF
    assert int(no_cross_colldata["floor_result_valid"][p]) == 0


def test_fd_active_damage_hitlag_without_current_floor_projects_hard_floor_airborne() -> None:
    # Active Damage hitlag can move the root below a hard floor while CollData has no current
    # floor.index. Source `ftCo_Damage_OnEveryHitlag` still routes through
    # ft_80081DD4/mpColl_800477E0 and materializes FloorPush|FloorHug through
    # mpColl_80044948_Floor without publishing grounded state.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    seed = _seed_base(32, ACT_DAMAGE_AIR_2, SM_DAMAGE_AIR_2, 9.424597, -7.469892)
    p = 0
    seed["char_id"][0, p] = np.uint8(CHAR_FOX)
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0xFFFF)
    seed["action_frame"][0, p] = np.int16(1)
    seed["anim_frame_f32"][0, p] = np.float32(1.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_ATTACK_AIR_B)
    seed["seed_prev_action_frame"][0, p] = np.int16(29)
    seed["hitlag"][0, p] = np.int16(4)
    seed["hitstun"][0, p] = np.int16(15)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(9.821096)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(1.930108)

    out, _contacts, _colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(0.0001, abs=1e-6)

    no_hitlag = seed.copy()
    no_hitlag["hitlag"][0, p] = np.int16(0)
    no_hitlag_out, _contacts2, _colldata2 = _step_once_with_contacts_and_colldata(no_hitlag)

    assert int(no_hitlag_out["on_ground"][p]) == 1
    assert int(no_hitlag_out["ground_id"][p]) == 1


@pytest.mark.parametrize(
    ("damage_action", "damage_motion"),
    [
        (ACT_DAMAGE_AIR_2, SM_DAMAGE_AIR_2),
        (ACT_DAMAGE_N_2, SM_DAMAGE_N_2),
        (ACT_DAMAGE_HI_2, SM_DAMAGE_HI_2),
    ],
)
def test_fd_damage_post_hitlag_carried_hard_floor_projects_airborne(
    damage_action: int, damage_motion: int
) -> None:
    # Sustained post-hitlag Damage rows still route through
    # ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800477E0. When CollData.floor.index already names
    # the ordinary hard floor and the root is below it, mpColl_80044948_Floor projects the root but
    # keeps the fighter airborne while hitstun remains active.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    seed = _seed_base(32, damage_action, damage_motion, 49.0, -16.0)
    p = 0
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(1)
    seed["action_frame"][0, p] = np.int16(12)
    seed["anim_frame_f32"][0, p] = np.float32(12.0)
    seed["hitstun"][0, p] = np.int16(5)
    seed["seed_prev_action_id"][0, p] = np.uint16(damage_action)
    seed["seed_prev_action_frame"][0, p] = np.int16(11)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(49.0)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-14.0)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][p]) == damage_action
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(0.0001, abs=1e-7)
    assert int(colldata["floor_result_segment_id"][p]) == 1

    no_carried_floor = seed.copy()
    no_carried_floor["ground_id"][0, p] = np.uint16(0xFFFF)
    no_carried_out, _contacts2, no_carried_colldata = _step_once_with_contacts_and_colldata(
        no_carried_floor
    )

    assert int(no_carried_out["action_id"][p]) == damage_action
    assert int(no_carried_out["on_ground"][p]) == 0
    assert int(no_carried_out["ground_id"][p]) == 0xFFFF
    assert float(no_carried_out["pos_y"][p]) < -16.0
    assert int(no_carried_colldata["floor_result_valid"][p]) == 0

    # Visible/restored CollData.floor.index alone is not source authority. With ground_id still
    # naming FD hard floor but no source-owned mpCollPrev packet, this must stay below-floor and
    # airborne instead of projecting.
    restored_only = seed.copy()
    restored_only["ground_id"][0, p] = np.uint16(1)
    restored_only["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(0)
    restored_only_out, _contacts3, restored_only_colldata = _step_once_with_contacts_and_colldata(
        restored_only
    )

    assert int(restored_only_out["action_id"][p]) == damage_action
    assert int(restored_only_out["on_ground"][p]) == 0
    assert int(restored_only_out["ground_id"][p]) == 1
    assert float(restored_only_out["pos_y"][p]) < -16.0
    assert int(restored_only_colldata["floor_result_valid"][p]) == 0


def test_fd_attackair_carried_connected_hard_floor_root_projection_lands() -> None:
    # AttackAir_Coll's ft_80082C74/mpColl_800471F8 floor owner can accept a connected hard-floor
    # candidate even when the carried CollData.floor.index is the adjacent ledge-top segment. The
    # source-owned proof is the generated MSLSTG01 segment graph plus mpColl_80044838_Floor root
    # projection, not an FD coordinate band.
    #
    # data/stages/bin/grnla.bin::MSLSTG01 segment links/fighter_solid flags
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    seed = _seed_base(32, ACT_ATTACK_AIR_B, SM_ATTACK_AIR_B, 49.184, -16.62)
    p = 0
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(2)
    seed["action_frame"][0, p] = np.int16(4)
    seed["anim_frame_f32"][0, p] = np.float32(4.0)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_ATTACK_AIR_B)
    seed["seed_prev_action_frame"][0, p] = np.int16(3)
    seed["speed_y_self"][0, p] = np.float32(-2.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(49.25)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(-14.0)

    out, _contacts, colldata = _step_once_with_contacts_and_colldata(seed)

    assert int(out["action_id"][p]) == ACT_LANDING_AIR_B
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(0.0001, abs=1e-7)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(colldata["floor_result_mode"][p]) == 2
    assert int(colldata["floor_result_segment_id"][p]) == 1

    # AttackAirB's first create_hitbox command is frame 4 in MSLFTSC1, and the source callback can
    # observe that entry floor packet through the following map-callback tick. By action_frame=6, a
    # carried hard-floor id plus mpCollPrev root packet is stale unless the normal floor producer
    # accepts a fresh crossing.
    sustained_stale = seed.copy()
    sustained_stale["action_frame"][0, p] = np.int16(6)
    sustained_stale["anim_frame_f32"][0, p] = np.float32(6.0)
    sustained_stale_out, _contacts_sustained, sustained_colldata = (
        _step_once_with_contacts_and_colldata(sustained_stale)
    )

    assert int(sustained_stale_out["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(sustained_stale_out["on_ground"][p]) == 0
    assert int(sustained_stale_out["ground_id"][p]) == 2
    assert int(sustained_colldata["floor_result_valid"][p]) == 0

    no_carried_floor = seed.copy()
    no_carried_floor["ground_id"][0, p] = np.uint16(0xFFFF)
    no_carried_out, _contacts2, no_carried_colldata = _step_once_with_contacts_and_colldata(
        no_carried_floor
    )

    assert int(no_carried_out["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(no_carried_out["on_ground"][p]) == 0
    assert int(no_carried_out["ground_id"][p]) == 0xFFFF
    assert int(no_carried_colldata["floor_result_valid"][p]) == 0

    # Stale/reseeded carried floor state is not enough: without the source-owned mpCollPrev
    # previous-root packet, the carried hard floor must not publish.
    restored_only = seed.copy()
    restored_only["action_frame"][0, p] = np.int16(6)
    restored_only["anim_frame_f32"][0, p] = np.float32(6.0)
    restored_only["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(0)
    restored_only_out, _contacts3, restored_only_colldata = _step_once_with_contacts_and_colldata(
        restored_only
    )

    assert int(restored_only_out["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(restored_only_out["on_ground"][p]) == 0
    assert int(restored_only_out["ground_id"][p]) == 2
    assert int(restored_only_colldata["floor_result_valid"][p]) == 0


@pytest.mark.integration
def test_fod_jumpf_transformed_platform_crossing_still_lands_replay_real() -> None:
    # Negative boundary for the root-below-platform guard: EWT record 9375 descends through FoD's
    # right transformed platform from a callback previous root above the platform, so the normal
    # Jump_Coll landing handoff remains live.
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 9375
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_fod_damageflytop_static_center_platform_enters_downbound_replay_real() -> None:
    # EWT record 3668 crosses FoD's static center top-platform line while in DamageFlyTop. The
    # moving-platform ECB-only suppression must stay limited to MSLSTG01 height transforms; the
    # static-y center platform is still the normal ft_80081DD4 -> ftCo_80090184 floor-contact owner.
    #
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80046904,mpCheckFloor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 3668
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["ref_t1"]["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(row["ref_t1"]["ground_id"][p]) == 2

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 2
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_grounded_contact_derives_current_platform_height_replay_real(tmp_path: Path) -> None:
    # EWT record 4509 is PassiveStandF grounded on FoD's moving left platform. The Slippi
    # `fod_platform` event stream is sparse here, but the current grounded root Y and MSLSTG01
    # platform transform expose the same grIzumi current stage state causally at seed frame t.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ElatedWearyTermite.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    row = ds.samples[4509]
    p = 0

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == 200  # PassiveStandF
    assert int(row["seed_t"]["ground_id"][p]) == 0
    assert int(row["seed_t"]["stage_fod_platform_height_valid_u8"][1]) == 1
    assert float(row["seed_t"]["stage_fod_platform_height_f32"][1]) == pytest.approx(
        (float(row["seed_t"]["pos_y"][p]) - 1.125) / 0.75,
        abs=1e-5,
    )

    out = _step_one_replay_row(ds, 4509)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=2e-4)


def _decode_modelplay_input_stream(
    stream: list[list[object]], fields: list[str], frame_count: int
) -> list[list[object]]:
    current: list[object] = [0] * len(fields)
    decoded: list[list[object]] = []
    row_index = 0
    for frame in range(frame_count):
        while row_index < len(stream) and int(stream[row_index][1]) <= frame:
            op = int(stream[row_index][0])
            row_frame = int(stream[row_index][1])
            payload = stream[row_index][2]
            if row_frame != frame:
                break
            if op == 0:
                current = list(payload)
            else:
                current = current.copy()
                for field_index, value in payload:
                    current[int(field_index)] = value
            row_index += 1
        decoded.append(current.copy())
    return decoded


def _write_modelplay_input(
    input_view: np.ndarray,
    streams: list[list[list[object]]],
    field_lookup: dict[str, int],
    frame: int,
) -> None:
    for player, decoded in enumerate(streams):
        values = decoded[frame] if frame >= 0 else [0] * len(field_lookup)
        input_view["p"][0, player]["buttons"] = np.uint16(int(values[field_lookup["buttons"]]))
        input_view["p"][0, player]["main_x"] = np.int8(
            np.clip(np.rint(float(values[field_lookup["mainX"]]) * 80.0), -80, 80)
        )
        input_view["p"][0, player]["main_y"] = np.int8(
            np.clip(np.rint(float(values[field_lookup["mainY"]]) * 80.0), -80, 80)
        )
        input_view["p"][0, player]["c_x"] = np.int8(
            np.clip(np.rint(float(values[field_lookup["cX"]]) * 80.0), -80, 80)
        )
        input_view["p"][0, player]["c_y"] = np.int8(
            np.clip(np.rint(float(values[field_lookup["cY"]]) * 80.0), -80, 80)
        )
        input_view["p"][0, player]["l"] = np.uint8(
            np.clip(np.rint(float(values[field_lookup["l"]]) * 255.0), 0, 255)
        )
        input_view["p"][0, player]["r"] = np.uint8(
            np.clip(np.rint(float(values[field_lookup["r"]]) * 255.0), 0, 255)
        )


def test_modelplay_trace111_sideb_fallspecial_fd_floor_clip_repro_lands() -> None:
    # This compact modelplay prefix reproduces the trace_seed111 Side-B -> FallSpecial FD floor
    # crossing from match start. The old local FallSpecial terminal-cardinal same-floor suppression
    # kept the fighter airborne forever once the carried CollData.floor.index matched FD's main
    # floor. Source has no such hard-floor rejection: FallSpecial_Coll calls ft_80083090 with
    # ftCo_80096CC8, which accepts every non-platform line and then enters LandingFallSpecial when
    # ftCo_80096D28 runs.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80083090
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    import msl_binding

    fixture_path = (
        Path(__file__).resolve().parents[1]
        / "tests/fixtures/modelplay/trace111_sideb_fd_floor_clip_prefix.json"
    )
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    fields = list(fixture["input_fields"])
    assert fields == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    field_lookup = {name: idx for idx, name in enumerate(fields)}
    landing_frame = int(fixture["assertions"]["landing_frame"])
    before_floor_frame = int(fixture["assertions"]["before_floor_frame"])
    player = int(fixture["assertions"]["player"])
    frame_count = landing_frame + 1
    streams = [
        _decode_modelplay_input_stream(stream, fields, frame_count)
        for stream in fixture["input_rows"]
    ]

    players = sorted(fixture["players"], key=lambda p: int(p["port"]))
    config = build_match_config_array(
        num_players=len(players),
        char_ids=tuple(int(p["charId"]) for p in players),
        team_ids=tuple(int(p["teamId"]) for p in players),
        facing=(1, 0),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["random_seed"]),
    )
    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    cur_input = np.zeros((1, input_stride), dtype=np.uint8)
    prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
    cur_view = cur_input.view(INPUT_DTYPE).reshape((1,))
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    rows: dict[int, np.void] = {}

    handle = msl_binding.init(
        batch_size=1,
        num_players=len(players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame in range(landing_frame):
            _write_modelplay_input(prev_view, streams, field_lookup, frame - 1)
            _write_modelplay_input(cur_view, streams, field_lookup, frame)
            msl_binding.step_input(handle, prev_input, cur_input)
            msl_binding.write_compare(handle, out_bytes)
            if frame + 1 in {before_floor_frame, landing_frame}:
                rows[frame + 1] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    before = rows[before_floor_frame]
    assert int(before["action_id"][player]) == ACT_FALL_SPECIAL
    assert int(before["on_ground"][player]) == 0
    assert float(before["pos_y"][player]) == pytest.approx(0.7258, abs=1e-3)

    landed = rows[landing_frame]
    assert int(landed["action_id"][player]) == int(fixture["assertions"]["landing_action"])
    assert int(landed["on_ground"][player]) == 1
    assert int(landed["ground_id"][player]) == int(fixture["assertions"]["ground_id"])
    assert float(landed["pos_y"][player]) == pytest.approx(
        float(fixture["assertions"]["landing_y"]), abs=2e-4
    )


@pytest.mark.integration
def test_manual_stage_clip_traces_roll_out_to_collision_resolution_from_match_start() -> None:
    # Manual viewer repros:
    # - pokemon_ledgedash_clip_are_you_fucking_kidding.msltrace.json
    # - fox_up_b_through_fod.msltrace.json
    # - fox_up_B_through_fod_2.msltrace.json
    #
    # These traces all exercise the same source class: an airborne fighter reaches the outside of a
    # legal-stage shell or ledge from a fast movement state and must resolve through source mpColl
    # floor/wall/ceiling ordering instead of passing into solid stage geometry.
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80046904,mpColl_80044628_Floor,mpColl_80044E10_RightWall,
    #   mpColl_800454A4_RightWall,mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_CheckGroundAndLedge}
    import msl_binding

    fixture_path = (
        Path(__file__).resolve().parents[1]
        / "tests/fixtures/modelplay/manual_stage_clip_trace_rollout_inputs.json"
    )
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for trace in fixture["traces"]:
        fields = list(trace["input_fields"])
        assert fields == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
        field_lookup = {name: idx for idx, name in enumerate(fields)}
        assertion = trace["assertion"]
        assertions = [assertion, *trace.get("extra_assertions", [])]
        target_frame = max(int(assertion["frame"]) for assertion in assertions)
        player = int(assertion["player"])
        streams = [
            _decode_modelplay_input_stream(stream, fields, target_frame + 1)
            for stream in trace["input_rows"]
        ]
        players = sorted(trace["players"], key=lambda p: int(p["port"]))
        config = build_match_config_array(
            num_players=len(players),
            char_ids=tuple(int(p["charId"]) for p in players),
            team_ids=tuple(int(p["teamId"]) for p in players),
            facing=(1, 0),
            stage_id=int(trace["stage_id"]),
            frame_id=0,
            random_seed=int(trace["random_seed"]),
        )

        prev_input = np.zeros((1, input_stride), dtype=np.uint8)
        cur_input = np.zeros((1, input_stride), dtype=np.uint8)
        prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
        cur_view = cur_input.view(INPUT_DTYPE).reshape((1,))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        handle = msl_binding.init(
            batch_size=1,
            num_players=len(players),
            ucf_enabled=1,
            ucf_cardinals_1_0_enabled=1,
        )
        try:
            msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
            pending_assertions = {int(a["frame"]): a for a in assertions}
            for frame in range(1, target_frame + 1):
                _write_modelplay_input(prev_view, streams, field_lookup, frame - 1)
                _write_modelplay_input(cur_view, streams, field_lookup, frame)
                msl_binding.step_input(handle, prev_input, cur_input)
                assertion_now = pending_assertions.get(frame)
                if assertion_now is not None:
                    msl_binding.write_compare(handle, out_bytes)
                    row = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
                    p_now = int(assertion_now["player"])
                    assert int(row["action_id"][p_now]) == int(assertion_now["action_id"]), trace["name"]
                    assert int(row["on_ground"][p_now]) == int(assertion_now["on_ground"]), trace["name"]
                    assert int(row["ground_id"][p_now]) == int(assertion_now["ground_id"]), trace["name"]
                    assert float(row["pos_y"][p_now]) >= float(assertion_now["min_y"]), trace["name"]
                    if "min_x" in assertion_now:
                        assert float(row["pos_x"][p_now]) >= float(assertion_now["min_x"]), trace["name"]
                    if "max_x" in assertion_now:
                        assert float(row["pos_x"][p_now]) <= float(assertion_now["max_x"]), trace["name"]
            msl_binding.write_compare(handle, out_bytes)
        finally:
            msl_binding.destroy(handle)

        row = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(row["action_id"][player]) == int(assertion["action_id"]), trace["name"]
        assert int(row["on_ground"][player]) == int(assertion["on_ground"]), trace["name"]
        assert int(row["ground_id"][player]) == int(assertion["ground_id"]), trace["name"]
        assert float(row["pos_y"][player]) >= float(assertion["min_y"]), trace["name"]


@pytest.mark.integration
def test_fod_same_step_landing_contact_derives_hidden_platform_height_replay_real(
    tmp_path: Path,
) -> None:
    # EWT record 10353 lands out of Fox/Falco aerial Side-B on FoD's left moving platform. The
    # sparse Slippi platform event stream has not exposed the current left-platform height yet, but
    # the same source collision step has already updated grIzumi/mpLib before
    # ftFx_SpecialAirSEnd_Coll calls ft_CheckGroundAndLedge. The post-frame grounded root exposes
    # that hidden transformed-line height for teacher-forced one-step seeds.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ElatedWearyTermite.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    row = ds.samples[10353]
    p = 0

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_S_END
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == 0
    assert int(row["seed_t"]["stage_fod_platform_height_valid_u8"][1]) == 1
    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][1]) == 0

    out = _step_one_replay_row(ds, 10353)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][p]) == 0
    assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-5)

    prev_row = ds.samples[10352]
    prev_out = _step_one_replay_row(ds, 10352)
    assert int(prev_out["action_id"][p]) == int(prev_row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_S_END
    assert int(prev_out["on_ground"][p]) == int(prev_row["ref_t1"]["on_ground"][p]) == 0

    right_row = ds.samples[10984]
    p = 1
    assert int(right_row["seed_t"]["action_id"][p]) == ACT_FX_SPECIAL_HI_FALL
    assert int(right_row["seed_t"]["on_ground"][p]) == 0
    assert int(right_row["seed_t"]["stage_fod_platform_height_source_u8"][0]) == 4
    assert int(right_row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_HI_LANDING
    assert int(right_row["ref_t1"]["ground_id"][p]) == 1

    right_out = _step_one_replay_row(ds, 10984)

    assert int(right_out["action_id"][p]) == int(right_row["ref_t1"]["action_id"][p])
    assert int(right_out["action_frame"][p]) == int(right_row["ref_t1"]["action_frame"][p])
    assert int(right_out["on_ground"][p]) == int(right_row["ref_t1"]["on_ground"][p]) == 1
    assert int(right_out["ground_id"][p]) == int(right_row["ref_t1"]["ground_id"][p]) == 1
    assert float(right_out["pos_x"][p]) == pytest.approx(
        float(right_row["ref_t1"]["pos_x"][p]), abs=1e-6
    )
    # SpecialHiFall's same-step FoD platform contact consumes `mpLib_8004DD90_Floor`'s source
    # floor-bias publication. The replay float is one bias unit lower, but the action/ground owner
    # and source current-height lane are the package lock here.
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    assert float(right_out["pos_y"][p]) == pytest.approx(
        float(right_row["ref_t1"]["pos_y"][p]) + 1.0e-4, abs=1e-5
    )

    right_prev_row = ds.samples[10983]
    right_prev_out = _step_one_replay_row(ds, 10983)
    assert int(right_prev_row["seed_t"]["stage_fod_platform_height_source_u8"][0]) == 0
    assert int(right_prev_out["action_id"][p]) == int(right_prev_row["ref_t1"]["action_id"][p])
    assert int(right_prev_out["on_ground"][p]) == int(right_prev_row["ref_t1"]["on_ground"][p]) == 0


def test_fod_attackair_height_platform_no_current_source_stays_pending_replay_real() -> None:
    # PTE record 3256 is AttackAirB above FoD's right transformed side platform. The sparse replay
    # seed carries a valid named platform height, but no direct/ground/same-step contact bit and no
    # live scheduler/velocity owner for the current callback. Vanilla keeps the first-hitbox-phase
    # AttackAir row airborne and publishes LandingAirB on the following callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 3256
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["seed_t"]["action_frame"][p]) == 6
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0]) == 0
    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][0]) == 1
    assert float(row["seed_t"]["stage_fod_platform_velocity_f32"][0]) == pytest.approx(0.0)
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)

    seed = np.array([row["seed_t"]], dtype=row["seed_t"].dtype)
    seed["stage_fod_platform_height_source_u8"][0, 0] = np.uint8(4)
    prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    sourced_out = _step_once(
        seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )

    assert int(sourced_out["action_id"][p]) == ACT_LANDING_AIR_B
    assert int(sourced_out["on_ground"][p]) == 1
    assert int(sourced_out["ground_id"][p]) == 1


def test_fod_attackair_same_step_height_platform_source_lands_despite_down_input_replay_real() -> None:
    # EWT record 9157 is AttackAirB crossing FoD's left height platform with down held. The seed
    # carries MSLSTG01 same-step contact provenance for that platform, so AttackAir_Coll's
    # ft_80082C74 -> mpColl_800471F8 floor result is current source ownership, not the
    # ftCo_80096CC8 soft-platform pass callback. Clearing that source bit keeps the down-held
    # in-span contact airborne, which locks the boundary away from a stage-wide AttackAir fallback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 9157
    p = 0
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) == 4
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-5)

    seed = np.array([row["seed_t"]], dtype=row["seed_t"].dtype)
    seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(0)
    prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    unsourced_out = _step_once(
        seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )

    assert int(unsourced_out["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(unsourced_out["on_ground"][p]) == 0


def test_fod_landing_live_height_platform_reprojects_after_hard_floor_entry_replay_real() -> None:
    # EWT rec1537 enters basic Landing on FoD's flat floor while the left height platform is rising
    # underneath the callback span. On the following Landing_Coll frame, live grIzumi velocity owns
    # the transformed platform line and mpColl_8004B4B0 can replace the carried hard floor through
    # its floor-release/retry path. Clearing both same-step source and live velocity keeps the
    # carried hard floor, so this is not a generic Landing platform snap.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B4B0
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    import msl_binding

    ds = read_dataset(str(path))
    p = 0
    row1537 = ds.samples[1537]
    row1538 = ds.samples[1538]
    assert int(row1537["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row1537["ref_t1"]["ground_id"][p]) == 5
    assert int(row1538["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row1538["ref_t1"]["ground_id"][p]) == 0

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed = np.array([row1537["seed_t"]], dtype=row1537["seed_t"].dtype)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        for row in (row1537, row1538):
            prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
            input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
            msl_binding.step_input(
                handle,
                prev_input.reshape(1, input_stride),
                input_t.reshape(1, input_stride),
            )
        msl_binding.write_compare(handle, out)
        rollout_out = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(rollout_out["action_id"][p]) == ACT_LANDING
    assert int(rollout_out["on_ground"][p]) == 1
    assert int(rollout_out["ground_id"][p]) == 0
    assert float(rollout_out["pos_y"][p]) == pytest.approx(
        float(row1538["ref_t1"]["pos_y"][p]), abs=1e-6
    )

    no_source_seed = np.array([row1538["seed_t"]], dtype=row1538["seed_t"].dtype)
    no_source_seed["stage_fod_platform_height_source_u8"][0, 1] = np.uint8(0)
    no_source_seed["stage_fod_platform_velocity_valid_u8"][0, 1] = np.uint8(0)
    prev_input = np.frombuffer(row1538["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row1538["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    no_source_out = _step_once(
        no_source_seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )
    assert int(no_source_out["action_id"][p]) == ACT_LANDING
    assert int(no_source_out["ground_id"][p]) == 5
    assert float(no_source_out["pos_y"][p]) == pytest.approx(
        float(row1538["seed_t"]["pos_y"][p]), abs=1e-6
    )


def test_fod_attackair_hard_floor_slope_lands_despite_unrelated_platform_skip_replay_real() -> None:
    # EWT record 9739 is AttackAirN descending from FoD's main floor onto the connected right
    # hard-floor slope while CollData.floor_skip still names the center soft platform. Source
    # AttackAir_Coll goes through ft_80082C74 -> mpColl_800471F8: floor_skip can reject that
    # selected soft platform, but it must not suppress a hard-floor mpCheckFloor hit or the
    # following mpLib_8004DD90_Floor seam traversal.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 floor segment links/fighter_solid flags
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 9739
    p = 0
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 1
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(row["ref_t1"]["ground_id"][p]) == 6

    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 6
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)

    seed = np.array([row["seed_t"]], dtype=row["seed_t"].dtype)
    seed["pos_x"][0, p] = np.float32(40.0)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(40.0)
    prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    no_current_span_out = _step_once(
        seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )

    assert int(no_current_span_out["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(no_current_span_out["on_ground"][p]) == 0


def test_fod_attackair_hard_slope_root_projection_requires_bottom_sweep_replay_real() -> None:
    # PTE record 1701 is the same FoD right hard-floor slope family as the EWT positive above, but
    # one callback frame earlier: the AttackAir root crosses below the generated slope while the
    # live ECB bottom is still above it. Source `mpColl_800471F8` reaches the final
    # `mpColl_80044838_Floor` snap only after `mpColl_80044628_Floor` accepts an ECB-bottom floor
    # sweep, so this first root-only projection must remain airborne. Nudging the same row downward
    # proves the guard is the bottom-sweep precondition, not a record/action-frame veto.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 1701
    p = 0
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 1
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == 5
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=1e-6)

    seed = np.array([row["seed_t"]], dtype=row["seed_t"].dtype)
    seed["pos_y"][0, p] = np.float32(float(seed["pos_y"][0, p]) - 0.05)
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(
        float(seed["floor_sweep_prev_pos_y_f32"][0, p]) - 0.05
    )
    prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    bottom_owned_out = _step_once(
        seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )

    assert int(bottom_owned_out["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(bottom_owned_out["on_ground"][p]) == 1
    assert int(bottom_owned_out["ground_id"][p]) == 6


@pytest.mark.integration
def test_fod_specialhi_holdair_platform_contact_enters_ground_hold_replay_real() -> None:
    # PTE record 4522 is Fox/Falco SpecialHiHoldAir crossing FoD's transformed left platform during
    # the charge/hold callback. Vanilla's collision callback takes the
    # ftFx_SpecialHiHoldAir_Coll -> ftFx_SpecialHiHoldAir_AirToGround path, not the later launch or
    # fall-special owners, so the visible state becomes grounded SpecialHiHold while preserving the
    # current animation frame.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialHiHoldAir_AirToGround}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 4522
    p = 0
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]

    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )

    prev_row = ds.samples[record - 1]
    prev_out = _step_one_replay_row(ds, record - 1)
    assert int(prev_row["seed_t"]["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(prev_row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(prev_out["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(prev_out["on_ground"][p]) == int(prev_row["ref_t1"]["on_ground"][p]) == 0


@pytest.mark.integration
def test_fod_specialhi_ground_hold_air_launch_carries_colldata_floor_replay_real() -> None:
    # PTE record 4535 is grounded SpecialHiHold on FoD's transformed left platform. The anim-end
    # path takes the aerial launch fallback because the held direction is a platform pass-through:
    # ftFx_SpecialHiHold_Anim -> ftCommon_8007D60C -> ftFx_SpecialAirHi_Enter. Source switches
    # ground_or_air to Air and installs the ECB lock, but it does not clear CollData.floor.index, so
    # the same frame remains airborne SpecialAirHi with the platform floor id still visible.
    #
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHold_Anim,ftFx_SpecialAirHi_Enter}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    record = 4535
    p = 0
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_FX_SPECIAL_HI_HOLD
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_HI
    assert int(row["ref_t1"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    seed = np.array([row["seed_t"]], dtype=row["seed_t"].dtype)
    seed["ground_id"][0, p] = np.uint16(0xFFFF)
    seed["on_ground"][0, p] = np.uint8(1)
    prev_input = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    input_t = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy()
    no_floor_out = _step_once(
        seed,
        prev_input.reshape(1, INPUT_DTYPE.itemsize),
        input_t.reshape(1, INPUT_DTYPE.itemsize),
    )

    assert int(no_floor_out["action_id"][p]) == ACT_FX_SPECIAL_HI
    assert int(no_floor_out["on_ground"][p]) == 0
    assert int(no_floor_out["ground_id"][p]) == 0xFFFF


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "player", "action_id"),
    [
        (1025, 0, ACT_ATTACK_AIR_LW),
        (1895, 1, ACT_DAMAGE_FLY_TOP),
    ],
)
def test_fod_soft_platform_airborne_regression_rows_stay_airborne(
    tmp_path: Path,
    record: int,
    player: int,
    action_id: int,
) -> None:
    # EWT 1025 is a down-held aerial crossing FoD's transformed left platform. EWT 1895 is
    # DamageFlyTop whose apparent top-platform crossing is dominated by early DamageFly ECB-bottom
    # interpolation while residual KB is still upward. Both are source-owned soft-platform rejection
    # boundaries and must not be converted into early Landing / DownBound by the replay-seeded FoD
    # transform path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ElatedWearyTermite.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    row = ds.samples[record]

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][player]) == action_id
    if action_id == ACT_ATTACK_AIR_LW:
        assert int(row["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][player]) == int(row["ref_t1"]["action_id"][player]) == action_id
    assert int(out["on_ground"][player]) == int(row["ref_t1"]["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(row["ref_t1"]["ground_id"][player])
    assert float(out["pos_x"][player]) == pytest.approx(float(row["ref_t1"]["pos_x"][player]), abs=1e-6)
    assert float(out["pos_y"][player]) == pytest.approx(float(row["ref_t1"]["pos_y"][player]), abs=2e-4)


@pytest.mark.integration
def test_fod_sustained_damagefly_reseed_uses_current_coll_root_replay_real(
    tmp_path: Path,
) -> None:
    # PTE 7492 is sustained airborne DamageFlyTop over FoD's transformed center platform. Source
    # `ft_80081DD4` starts DamageFly_Coll by copying the current fighter root into CollData.cur_pos
    # before `mpColl_800473CC`, so the replay seed's callback-visible previous sweep point is the
    # current visible root rather than the public replay row before the seed. Using that older row
    # makes the floor sweep hit the soft platform one frame early and falsely publishes DownBound.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800473CC}
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ParallelTemptingElk.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    record = 7492
    p = 1
    row = ds.samples[record]
    seed = row["seed_t"]

    assert int(seed["stage_id"]) == 2
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) > 0
    assert int(seed["floor_sweep_prev_pos_valid_u8"][p]) == 1
    assert float(seed["floor_sweep_prev_pos_x_f32"][p]) == pytest.approx(
        float(seed["pos_x"][p]), abs=1e-6
    )
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) == pytest.approx(
        float(seed["pos_y"][p]), abs=1e-6
    )

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 5
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_nonfod_damagefly_reseed_keeps_previous_row_hard_floor_sweep_replay_real(
    tmp_path: Path,
) -> None:
    # AGN 5208 is the non-FoD hard-floor boundary for the same seed lane. Here the public previous
    # row sweep is source-owned: the DamageFlyHi victim is already below FD floor height and vanilla
    # resolves the hard-floor contact into Passive. The FoD transformed-platform current-root
    # reconstruction must not broaden into ordinary hard-floor DamageFly collision.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "AttachedGoodNaturedGuanaco.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    record = 5208
    p = 0
    row = ds.samples[record]
    seed = row["seed_t"]

    assert int(seed["stage_id"]) != 2
    assert int(seed["action_id"][p]) == 0x0057  # DamageFlyHi
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) > 0
    assert int(seed["floor_sweep_prev_pos_valid_u8"][p]) == 1
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) != pytest.approx(float(seed["pos_y"][p]))

    out = _step_one_replay_row(ds, record)
    ref = row["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_PASSIVE
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_hidden_height_platform_grounded_rider_remaps_to_main_floor_replay_real() -> None:
    # PTE 10971 has FoD's right height platform at grIzumi's hidden target. The source collision
    # line is no longer the carried platform floor for grounded riders; mpLib remaps them to the
    # main floor under the root.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[10971]
    out = _step_one_replay_row(ds, 10971)
    ref = row["ref_t1"]
    seed = row["seed_t"]

    assert int(seed["stage_id"]) == 2
    assert int(seed["stage_fod_platform_height_source_u8"][0]) & 0x02
    assert float(seed["stage_fod_platform_height_f32"][0]) == pytest.approx(-1.0, abs=1.0e-3)
    for p in (0, 1):
        assert int(seed["on_ground"][p]) == 1
        assert int(seed["ground_id"][p]) == 1
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
        assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 5
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_fod_visible_height_platform_grounded_rider_stays_on_platform_replay_real() -> None:
    # One frame earlier, the same current-source platform is still visible above main floor. The
    # hidden-target remap must not convert ordinary grounded platform carry into main-floor contact.
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    row = ds.samples[10970]
    out = _step_one_replay_row(ds, 10970)
    ref = row["ref_t1"]
    seed = row["seed_t"]

    assert int(seed["stage_id"]) == 2
    assert int(seed["stage_fod_platform_height_source_u8"][0]) & 0x02
    assert float(seed["stage_fod_platform_height_f32"][0]) < 0.0
    assert float(seed["stage_fod_platform_height_f32"][0]) != pytest.approx(-1.0, abs=1.0e-3)
    for p in (0, 1):
        assert int(seed["on_ground"][p]) == 1
        assert int(seed["ground_id"][p]) == 1
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
        assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1


@pytest.mark.integration
def test_fod_prefix_platform_velocity_keeps_rollout_floor_transform_replay_real(
    tmp_path: Path,
) -> None:
    # PTE record 3318 seeds after grounded prefix contact on FoD's right moving platform. The
    # platform remains in the same grIzumi velocity phase after the fighter jumps away, so rollout
    # floor admission must keep advancing the transformed source line before collision.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ParallelTemptingElk.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    row = ds.samples[3318]
    p = 0

    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][0]) == 1
    assert float(row["seed_t"]["stage_fod_platform_velocity_f32"][0]) == pytest.approx(
        -0.1,
        abs=1e-6,
    )

    out = _rollout_replay_to_record(ds, 3318, 3338)
    ref = ds.samples[3338]["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_prefix_platform_velocity_stops_at_source_target_replay_real(tmp_path: Path) -> None:
    # PTE record 5801 is hundreds of frames after the last replay-visible grounded contact on
    # FoD's right platform. Prefix-derived velocity must stop at the GrIz source target instead of
    # extrapolating indefinitely and making the platform non-solid at the later landing frame.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_motion.fountain_platform
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ParallelTemptingElk.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    row = ds.samples[5801]
    p = 0

    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][0]) == 0
    assert float(row["seed_t"]["stage_fod_platform_height_f32"][0]) == pytest.approx(25.0)

    out = _rollout_replay_to_record(ds, 5801, 5820)
    ref = ds.samples[5820]["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_same_step_platform_contact_defers_velocity_until_next_frame_pte(tmp_path: Path) -> None:
    # PTE rec3257 lands on FoD's right height platform from a same-step reconstructed contact. The
    # current landing frame must use that current collision height without pre-advancing grIzumi,
    # then carry the next source-visible platform velocity into sustained LandingAirB/grounded
    # callbacks. Without the deferred velocity seed, rollout strands the rider at the first landing
    # height and later creates a false AttackAirN/AttackAirB overlap.
    #
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    )
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ParallelTemptingElk.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    p = 0
    row = ds.samples[3257]

    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_B
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0]) & 0x04
    assert int(row["seed_t"]["stage_fod_platform_velocity_valid_u8"][0]) == 0
    assert int(row["seed_t"]["stage_fod_platform_deferred_velocity_valid_u8"][0]) == 1
    assert float(row["seed_t"]["stage_fod_platform_deferred_velocity_f32"][0]) == pytest.approx(
        -0.1,
        abs=1e-6,
    )

    direct = _step_one_replay_row(ds, 3257)
    assert int(direct["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert float(direct["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=2e-4)

    rollout = _rollout_replay_to_record(ds, 3257, 3352)
    ref = ds.samples[3352]["ref_t1"]
    assert int(rollout["action_id"][p]) == int(ref["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(rollout["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(rollout["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert float(rollout["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-4)


@pytest.mark.integration
def test_ps_attackair_platform_endpoint_uses_mpcoll_floor_edge_snap_replay_real() -> None:
    # STM record 2478 enters AttackAirLw and lands on Pokemon Stadium right-platform's left
    # endpoint. Source mpColl first accepts the floor sweep, then mpColl_80044838_Floor snaps the
    # ECB bottom to the endpoint when DD90 projection is off-end.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    out = _rollout_replay_to_record(ds, 2478, 2486)
    ref = ds.samples[2486]["ref_t1"]
    p = 1

    assert int(ds.samples[2486]["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 36
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fd_damagefly_hard_floor_off_end_does_not_use_platform_endpoint_snap() -> None:
    # TCH record 5773 is an FD DamageFlyN row whose swept ECB bottom is near a hard-floor
    # endpoint. The retained mpColl_80044838 endpoint slice is platform-scoped; applying it to this
    # hard-floor case grounds the fighter one frame early.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    out = _step_one_replay_row(ds, 5773)
    ref = ds.samples[5773]["ref_t1"]
    p = 1

    assert int(ds.samples[5773]["seed_t"]["stage_id"]) == 32
    assert int(ds.samples[5773]["seed_t"]["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1


@pytest.mark.integration
def test_fod_hidden_return_timer_reappears_platform_for_rollout_reseed_replay_real() -> None:
    # EWT rec3270 starts a rollout while FoD's left platform is parked at grIzumi's generated
    # hidden target. The next source-visible rising height proves the hidden phase-4 countdown; when
    # that timer is seeded, the live scheduler has raised the platform by rec3762 and AttackAirB
    # lands on the platform instead of falling through stale hidden geometry.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    path = (
        Path(__file__).resolve().parents[1]
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not path.exists():
        pytest.skip(f"missing local dataset: {path}")

    ds = read_dataset(str(path))
    start = 3270
    record = 3762
    p = 0
    seed = ds.samples[start]["seed_t"]
    assert int(seed["stage_fod_platform_hidden_return_valid_u8"][0]) == 1
    assert int(seed["stage_fod_platform_hidden_return_timer_u16"][0]) > 0

    out = _rollout_replay_to_record(ds, start, record)
    ref = ds.samples[record]["ref_t1"]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)


def _step_one_replay_row(ds, record: int):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    row = ds.samples[record]
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed(
            handle,
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride),
        )
        msl_binding.step_input(
            handle,
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride),
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride),
        )
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _rollout_replay_to_record(ds, start_record: int, target_record: int):
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed_rollout(
            handle,
            np.frombuffer(ds.samples[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride),
        )
        for record in range(start_record, target_record + 1):
            row = ds.samples[record]
            msl_binding.step_input(
                handle,
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
            )
            msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)
