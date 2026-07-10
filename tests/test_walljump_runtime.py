from __future__ import annotations

import numpy as np

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base

ACT_FALL = 0x001D
ACT_PASSIVE_WALL_JUMP = 0x00CB
SM_FALL = 20


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
