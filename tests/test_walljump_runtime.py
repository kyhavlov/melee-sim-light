from __future__ import annotations

import numpy as np

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base
from tools.eval.validation_dtypes import COMPARE_DTYPE

ACT_DEAD_LEFT = 0x0001
ACT_REBIRTH = 0x000C
ACT_FALL = 0x001D
ACT_ATTACK_11 = 0x002C
ACT_DAMAGE_FLY_N = 0x0058
ACT_PASSIVE_WALL_JUMP = 0x00CB
SM_FALL = 20
SM_ATTACK_11 = 46
SM_DAMAGE_FLY_N = 178
SM_PASSIVE_WALL_JUMP = 203


def _walljump_phase_seed(char: str) -> np.ndarray:
    seed = _seed_base(char, grounded=False, pos_y=-25.0)
    seed["pos_x"][0, 0] = np.float32(85.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["action_frame"][0, 0] = np.int16(20)
    seed["anim_frame_f32"][0, 0] = np.float32(20.0)
    seed["walljump_input_timer"][0, 0] = np.uint8(0)
    seed["walljump_wall_side_i8"][0, 0] = np.int8(-1)
    seed["mpcoll_wall_kind_seed_u8"][0, 0] = np.uint8(2)
    seed["mpcoll_wall_id_seed_u16"][0, 0] = np.uint16(0)
    return seed


def test_can_walljump_runtime_gate_rejects_marth_but_admits_sheik() -> None:
    # ftWallJump_8008169C first gates on fp->can_walljump. The hidden timer/side seed below
    # represents a valid right-wall walljump phase with a stick-away input; Sheik can consume it,
    # Marth cannot.
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    # data/characters/{marth,sheik}.json::can_walljump
    inp = _mk_inputs(main_x=80)

    sheik = _run(_walljump_phase_seed("sheik"), [inp])[0]
    assert int(sheik["action_id"][0]) == ACT_PASSIVE_WALL_JUMP

    marth = _run(_walljump_phase_seed("marth"), [inp])[0]
    assert int(marth["action_id"][0]) != ACT_PASSIVE_WALL_JUMP


def test_walljump_seed_phase_edge_does_not_replace_live_wallhug_with_stale_tilt() -> None:
    # A replay seed can carry the hidden walljump timer/side, but ftWallJump_8008169C still needs
    # live CollData WallHug for the stale-x670 stick-edge bridge. A seed-only phase must not admit
    # a no-contact row such as low under-stage Fall/FallAerial movement.
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    seed = _walljump_phase_seed("sheik")
    seed["pos_x"][0, 0] = np.float32(-62.0)
    seed["pos_y"][0, 0] = np.float32(-42.0)
    seed["tilt_timer_x"][0, 0] = np.uint8(254)
    seed["walljump_wall_side_i8"][0, 0] = np.int8(1)
    seed["mpcoll_wall_kind_seed_u8"][0, 0] = np.uint8(0)
    seed["mpcoll_wall_id_seed_u16"][0, 0] = np.uint16(0xFFFF)

    out = _run(seed, [_mk_inputs(main_x=-80)])[0]
    assert int(out["action_id"][0]) != ACT_PASSIVE_WALL_JUMP


def _step_with_walljump_state(
    seed: np.ndarray,
    current: np.ndarray,
    *,
    previous: np.ndarray | None = None,
    steps: int = 1,
) -> tuple[np.void, tuple[int, int]]:
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(
            handle,
            seed.view(np.uint8).reshape((1, int(sizes["seed"]))),
        )
        previous_row = previous if previous is not None else _mk_inputs()
        for _ in range(steps):
            msl_binding.step_input(handle, previous_row, current)
            previous_row = current
        raw = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, raw)
        compare = raw.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        state = tuple(int(v) for v in msl_binding.debug_walljump_state(handle, 0, 0))
        return compare, state
    finally:
        msl_binding.destroy(handle)


def test_ordinary_walljump_runtime_entry_increments_and_snapshots_count() -> None:
    seed = _walljump_phase_seed("falcon")
    current = _mk_inputs(main_x=80)
    out, state = _step_with_walljump_state(seed, current)
    assert int(out["action_id"][0]) == ACT_PASSIVE_WALL_JUMP
    assert state == (1, 0)


def test_ordinary_walljump_runtime_increment_saturates() -> None:
    seed = _walljump_phase_seed("falcon")
    seed["walljump_used_count"][0, 0] = np.uint8(255)

    out, state = _step_with_walljump_state(seed, _mk_inputs(main_x=80))
    assert int(out["action_id"][0]) == ACT_PASSIVE_WALL_JUMP
    assert state == (255, 255)


def test_walltech_runtime_entry_keeps_count_and_uses_zero_exponent() -> None:
    seed = _seed_base("fox", grounded=False, pos_y=-41.08169)
    seed["char_id"][0, 0] = np.uint8(22)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_N)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_N)
    seed["seed_prev_action_frame"][0, 0] = np.int16(11)
    seed["action_frame"][0, 0] = np.int16(12)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FLY_N)
    seed["anim_frame_f32"][0, 0] = np.float32(12.0)
    seed["pos_x"][0, 0] = np.float32(69.689896)
    seed["speed_y_self"][0, 0] = np.float32(-1.8699998)
    seed["speed_x_attack"][0, 0] = np.float32(-1.858745)
    seed["speed_y_attack"][0, 0] = np.float32(0.3549526)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["hitstun"][0, 0] = np.uint16(21)
    seed["colanim_hit_status_x198c"][0, 0] = np.uint8(1)
    seed["colanim_timer_x1994"][0, 0] = np.uint16(80)
    seed["x67E"][0, 0] = np.uint8(76)
    seed["x680"][0, 0] = np.uint8(18)
    seed["x684"][0, 0] = np.uint8(119)
    seed["state_flags"][0, 0, 3] = np.uint8(0x02)
    seed["walljump_used_count"][0, 0] = np.uint8(7)
    seed["passivewall_vel_y_exponent"][0, 0] = np.uint8(6)
    inp = _mk_inputs(main_x=-27, main_y=62, buttons=0x0020)

    out, state = _step_with_walljump_state(seed, inp, previous=inp)
    assert int(out["action_id"][0]) == ACT_PASSIVE_WALL_JUMP
    assert state == (7, 0)


def test_runtime_landing_reset_survives_same_frame_processhit_without_jumps_edge() -> None:
    def make_seed(pos_y: float) -> np.ndarray:
        seed = _seed_base("falcon", grounded=False, pos_y=pos_y)
        seed["action_id"][0, 0] = np.uint16(ACT_FALL)
        seed["animation_index"][0, 0] = np.uint32(SM_FALL)
        seed["speed_y_self"][0, 0] = np.float32(-3.0)
        seed["jumps_left"][0, 0] = np.uint8(1)
        seed["walljump_used_count"][0, 0] = np.uint8(7)
        seed["pos_x"][0, :2] = np.float32(0.0)
        seed["action_id"][0, 1] = np.uint16(ACT_ATTACK_11)
        seed["animation_index"][0, 1] = np.uint32(SM_ATTACK_11)
        seed["action_frame"][0, 1] = np.int16(1)
        seed["anim_frame_f32"][0, 1] = np.float32(1.0)
        return seed

    landed_seed = make_seed(0.5)
    midair_seed = make_seed(1.0)
    current = _mk_inputs()
    landed_out, landed_state = _step_with_walljump_state(landed_seed, current)
    midair_out, midair_state = _step_with_walljump_state(midair_seed, current)

    # Fox Attack11 creates its fighter hitboxes on frame 2. The lower Falcon first converts to
    # ground during Coll, resets x1969, then the same frame's ProcessHit returns him airborne and
    # writes x1968 back to one. The higher Falcon is hit without landing.
    # data/moves/fox.json::moves.ftCo_SM_Attack11.events
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
    assert int(landed_out["on_ground"][0]) == int(midair_out["on_ground"][0]) == 0
    assert int(landed_out["jumps_left"][0]) == int(midair_out["jumps_left"][0]) == 1
    assert float(landed_out["percent"][0]) == float(midair_out["percent"][0]) == 4.0
    assert landed_state[0] == 0
    assert midair_state[0] == 7


def test_runtime_grounding_resets_walljump_count() -> None:
    seed = _seed_base("falcon", grounded=False, pos_y=1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["speed_y_self"][0, 0] = np.float32(-3.0)
    seed["walljump_used_count"][0, 0] = np.uint8(7)

    out, state = _step_with_walljump_state(seed, _mk_inputs(), steps=4)
    assert int(out["on_ground"][0]) == 1
    assert state[0] == 0


def test_runtime_rebirth_resets_walljump_count() -> None:
    seed = _seed_base("falcon", grounded=False, pos_y=-200.0)
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_LEFT)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["match_flow_timer"][0, 0] = np.uint8(1)
    seed["walljump_used_count"][0, 0] = np.uint8(7)

    out, state = _step_with_walljump_state(seed, _mk_inputs())
    assert int(out["action_id"][0]) == ACT_REBIRTH
    assert state[0] == 0


def _passivewall_launch_seed(*, used_count: int, exponent: int) -> np.ndarray:
    seed = _seed_base("falcon", grounded=False, pos_y=100.0)
    seed["action_id"][0, 0] = np.uint16(ACT_PASSIVE_WALL_JUMP)
    seed["animation_index"][0, 0] = np.uint32(SM_PASSIVE_WALL_JUMP)
    seed["passivewall_timer"][0, 0] = np.uint8(1)
    seed["walljump_used_count"][0, 0] = np.uint8(used_count)
    seed["passivewall_vel_y_exponent"][0, 0] = np.uint8(exponent)
    return seed


def test_consecutive_walljump_launch_uses_exact_source_vertical_decay() -> None:
    first = _run(_passivewall_launch_seed(used_count=1, exponent=0), [_mk_inputs()])[0]
    second = _run(_passivewall_launch_seed(used_count=2, exponent=1), [_mk_inputs()])[0]

    # ftCo_PassiveWall_Anim launches Falcon at 3.1 * powf(0.9750000238, exponent), then the same
    # frame's common-air Phys callback applies Falcon's 0.13 gravity. These exact f32 locks are the
    # vanilla first and second consecutive walljump post-frame velocities.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Anim
    # data/common/ft_common_data.json::passive_wall_vel_y_base
    assert float(first["speed_y_self"][0]) == float(np.float32(2.9699997901916504))
    assert float(second["speed_y_self"][0]) == float(np.float32(2.8924999237060547))
