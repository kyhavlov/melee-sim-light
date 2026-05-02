from __future__ import annotations

from argparse import Namespace
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.known_data_artifacts import read_mslstg01_v5


ACT_WAIT = 0x000E
ACT_WALK_MIDDLE = 0x0010
ACT_DASH = 0x0014
ACT_RUN = 0x0015
ACT_RUN_BRAKE = 0x0017
ACT_JUMP_F = 0x0019
ACT_FALL = 0x001D
ACT_ATTACK_AIR_LW = 0x0045
ACT_SQUAT_WAIT = 0x0028
ACT_LANDING = 0x002A
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_GUARD_ON = 0x00B2
ACT_DAMAGE_FALL = 0x0026
ACT_GUARD = 0x00B3
ACT_GUARD_SET_OFF = 0x00B5
ACT_GUARD_REFLECT = 0x00B6
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DOWN_BOUND_U = 0x00B7
ACT_DOWN_WAIT_U = 0x00B8
ACT_PASSIVE = 0x00C7
ACT_PASS = 0x00F4
ACT_CLIFF_JUMP_SLOW2 = 0x0105

SM_WAIT1_0 = 2
SM_JUMP_F = 16
SM_FALL = 20
SM_DAMAGE_FALL = 33
SM_PASS = 209
SM_CLIFF_JUMP_SLOW2 = 226

CHAR_FOX = 1
BUTTON_L = 0x0040
BUTTON_B = 0x0200

ACT_FX_SPECIAL_AIR_LW_START = 0x016D


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

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
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
    world_y = np.float32(float(height) * 0.80625)
    seed = _seed_base(2, ACT_WAIT, SM_WAIT1_0, -35.0, float(world_y))
    seed["stage_fod_platform_height_f32"][0, 1] = height  # platform id 1 = left
    seed["stage_fod_platform_height_valid_u8"][0, 1] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)

    out = _step_once(seed)

    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(float(world_y) + 0.0001, abs=1e-5)


def test_fountain_left_moving_platform_advances_seeded_stage_velocity() -> None:
    # grIzumi_801CC358 updates the platform JObj height before mpLib_80055E9C refreshes the line.
    # A rollout seed carrying current height + current height delta must therefore collide against
    # the next transformed world line, not the stale seed-frame line.
    height = np.float32(17.100000381469727)
    velocity = np.float32(-0.093023255)
    next_world_y = np.float32(float(np.float32(height + velocity)) * 0.80625)
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

    stage = read_mslstg01_v5(Path("data/stages/bin/grps.bin"))
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


def test_squat_down_input_enters_pass_and_skips_source_platform() -> None:
    seed = _seed_base(31, ACT_SQUAT_WAIT, 0, -40.0, 27.200000762939453)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(2)
    seed["action_frame"][0, 0] = np.int16(2)
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
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
def test_fod_grounded_contact_derives_current_platform_height_replay_real(tmp_path: Path) -> None:
    # EWT record 4509 is PassiveStandF grounded on FoD's moving left platform. The Slippi
    # `fod_platform` event stream is sparse here, but the current grounded root Y and MSLSTG01
    # platform transform expose the same grIzumi current stage state causally at seed frame t.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slp"
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
        float(row["seed_t"]["pos_y"][p]) / 0.80625,
        abs=1e-5,
    )

    out = _step_one_replay_row(ds, 4509)

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][p]), abs=2e-4)


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
    # DamageFlyTop crossing the top platform while damage KB still has upward Y. Both are
    # source-owned soft-platform rejection boundaries and must not be converted into early Landing /
    # DownBound by the replay-seeded FoD transform path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slp"
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
    out = _step_one_replay_row(ds, record)

    assert int(out["action_id"][player]) == int(row["ref_t1"]["action_id"][player]) == action_id
    assert int(out["on_ground"][player]) == int(row["ref_t1"]["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(row["ref_t1"]["ground_id"][player])
    assert float(out["pos_x"][player]) == pytest.approx(float(row["ref_t1"]["pos_x"][player]), abs=1e-6)
    assert float(out["pos_y"][player]) == pytest.approx(float(row["ref_t1"]["pos_y"][player]), abs=2e-4)


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
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slp"
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
        -0.0930233,
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
    # data/stages/bin/griz.json::platform_motion.fountain_platform
    slp = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slp"
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
