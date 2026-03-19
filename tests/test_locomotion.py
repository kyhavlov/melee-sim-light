from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_X = 0x0400
BUTTON_Y = 0x0800
BUTTON_A = 0x0100

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_WALK_SLOW = 0x000F
ACT_TURN = 0x0012
ACT_DASH = 0x0014
ACT_RUN = 0x0015
ACT_KNEEBEND = 0x0018
ACT_JUMPF = 0x0019
ACT_FALL = 0x001D
ACT_SQUAT = 0x0027
ACT_ATTACK_DASH = 0x0032
ACT_ATTACK_HI3 = 0x0038
ACT_DAMAGEFALL = 0x0026
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_B = 0x0043
ACT_DAMAGE_AIR_2 = 0x0055
ACT_LANDING = 0x002A
ACT_OTTOTTO = 0x00F5

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_WALK_SLOW = 7
SM_TURN = 10
SM_DASH = 12
SM_RUN = 13
SM_KNEEBEND = 15
SM_JUMPF = 16
SM_FALL = 20
SM_SQUAT = 30
SM_ATTACK_DASH = 52
SM_ATTACK_HI3 = 58
SM_ATTACK_AIR_N = 68
SM_ATTACK_AIR_B = 70
SM_DAMAGE_AIR_2 = 175
SM_OTTOTTO = 210

# Collision env flag bits: refs/melee/src/common_structs.h, src/coll_env_flags.h
MSL_COLLIDE_EDGE = 0x00800000

CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4

INTERNALS_DTYPE = np.dtype(
    [
        ("tilt_timer_x", ("u1", (MAX_PLAYERS,))),
        ("turn_frames_to_turn", ("u1", (MAX_PLAYERS,))),
        ("turn_has_turned", ("u1", (MAX_PLAYERS,))),
        ("guard_reflect_timer_x14", ("u1", (MAX_PLAYERS,))),
        ("attack_id", ("<u2", (MAX_PLAYERS,))),
        ("attack_instance", ("<u2", (MAX_PLAYERS,))),
        ("attack_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_x2073", ("u1", (MAX_PLAYERS,))),
        ("instance_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_counter", "<u2"),
        ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
    ],
    align=False,
)

COLLISION_CONTACTS_DTYPE = np.dtype(
    [
        ("wall_kind", ("u1", (MAX_PLAYERS,))),
        ("_pad0", ("u1", (MAX_PLAYERS,))),
        ("wall_id", ("<u2", (MAX_PLAYERS,))),
        ("wall_contact_x", ("<f4", (MAX_PLAYERS,))),
        ("wall_contact_y", ("<f4", (MAX_PLAYERS,))),
        ("wall_normal_x", ("<f4", (MAX_PLAYERS,))),
        ("wall_normal_y", ("<f4", (MAX_PLAYERS,))),
        ("ceiling_id", ("<u2", (MAX_PLAYERS,))),
        ("_pad1", ("<u2", (MAX_PLAYERS,))),
        ("ceiling_contact_x", ("<f4", (MAX_PLAYERS,))),
        ("ceiling_contact_y", ("<f4", (MAX_PLAYERS,))),
        ("ceiling_normal_x", ("<f4", (MAX_PLAYERS,))),
        ("ceiling_normal_y", ("<f4", (MAX_PLAYERS,))),
        ("coll_env_flags", ("<u4", (MAX_PLAYERS,))),
        ("coll_prev_env_flags", ("<u4", (MAX_PLAYERS,))),
    ],
    align=False,
)

_ECB_LOADED = False


def _ensure_ecb_loaded() -> None:
    global _ECB_LOADED
    if _ECB_LOADED:
        return
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    msl_binding.destroy(handle)
    _ECB_LOADED = True


def _fox_ecb_bottom_rel_y(msid: int, action_frame: int) -> float:
    _ensure_ecb_loaded()
    import msl_binding

    return float(msl_binding.ecb_bottom_rel_y(CHAR_FOX, int(msid), int(action_frame)))


def _fox_attr(name: str) -> float:
    import json
    from pathlib import Path

    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox[name])


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _step_once(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _step_many(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray, n: int) -> list[np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        outs: list[np.ndarray] = []

        msl_binding.reseed_seed(handle, seed_bytes)
        for _ in range(n):
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)
            outs.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
        return outs
    finally:
        msl_binding.destroy(handle)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    return seed


def _step_once_with_internals(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    internals_stride = int(sizes["internals"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert internals_stride == INTERNALS_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        out_int = np.zeros((1, internals_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
        msl_binding.debug_write_internals(handle, out_int)

        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        int0 = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
        return cmp0, int0
    finally:
        msl_binding.destroy(handle)


def _step_once_with_collision_contacts(
    seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert contacts_stride == COLLISION_CONTACTS_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
        msl_binding.debug_write_collision_contacts(handle, out_contacts)

        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        contacts0 = out_contacts.view(COLLISION_CONTACTS_DTYPE).reshape((1,))[0].copy()
        return cmp0, contacts0
    finally:
        msl_binding.destroy(handle)


def test_dash_iasa_opposite_flick_enters_turn_without_same_frame_flip() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_DASH)
    seed["facing"][0, 0] = np.uint8(1)  # right

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Opposite-facing fresh flick (prev neutral -> cur full left).
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(-80)

    out0, out1 = _step_many(seed, prev_inp, inp, 2)
    assert int(out0["action_id"][0]) == ACT_TURN
    # No same-frame flip on Dash->Turn entry.
    assert int(out0["facing"][0]) == 1


def test_dash_late_iasa_opposite_flick_enters_turn_without_same_frame_flip() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    dash_iasa_x4c = float(_common_attr("dash_iasa_x4c"))
    assert dash_iasa_x4c > 0.0

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(int(dash_iasa_x4c) + 1)
    seed["anim_frame_f32"][0, 0] = np.float32(dash_iasa_x4c + 1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DASH)
    seed["facing"][0, 0] = np.uint8(0)  # left
    seed["dash_x4"][0, 0] = np.uint8(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Late Dash IASA still calls ftCo_Dash_CheckInput, so a fresh opposite-facing flick enters Turn.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(100)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_TURN
    # Turn flips later in ftCo_Turn_Anim_Inner; entry frame still keeps original facing.
    assert int(out0["facing"][0]) == 0


def test_dash_anim_end_wait_entry_held_opposite_stick_enters_turn() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    dash_iasa_x4c = float(_common_attr("dash_iasa_x4c"))
    turn_threshold = float(_common_attr("turn_stick_x_threshold"))
    dash_flick_abs = float(_common_attr("dash_flick_abs"))
    tilt_max = int(_common_attr("dash_flick_tilt_max_frames"))
    assert dash_iasa_x4c > 0.0
    assert turn_threshold < 0.0

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(int(dash_iasa_x4c) + 1)
    seed["anim_frame_f32"][0, 0] = np.float32(dash_iasa_x4c + 1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DASH)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["dash_x4"][0, 0] = np.uint8(0)
    seed["tilt_timer_x"][0, 0] = np.uint8(tilt_max + 4)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    held_turn = int(-80 * max(dash_flick_abs - 0.2, -turn_threshold + 0.1))
    prev_view["p"]["main_x"][0, 0] = np.int8(held_turn)
    cur_view["p"]["main_x"][0, 0] = np.int8(held_turn)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_TURN
    assert int(out0["facing"][0]) == 1
    assert int(out0["on_ground"][0]) == 1


def test_dash_mid_iasa_opposite_flick_prioritizes_turn_over_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    dash_iasa_x44 = float(_common_attr("dash_iasa_x44"))
    dash_iasa_x4c = float(_common_attr("dash_iasa_x4c"))
    dash_flick_abs = float(_common_attr("dash_flick_abs"))
    assert dash_iasa_x44 > 0.0
    assert dash_iasa_x4c > dash_iasa_x44
    assert dash_flick_abs > 0.0

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(int(dash_iasa_x44) + 1)
    seed["anim_frame_f32"][0, 0] = np.float32(dash_iasa_x44 + 1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DASH)
    seed["facing"][0, 0] = np.uint8(0)  # left
    seed["dash_x4"][0, 0] = np.uint8(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Decomp: the mid Dash IASA branch checks ftCo_Dash_CheckInput before fn_800CAF78, so an
    # opposite-facing x3C/x40 flick still enters Turn even if Y is newly pressed on the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(int(np.ceil(dash_flick_abs * 80.0)))
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_Y)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_TURN
    assert int(out0["jumps_left"][0]) == 2


def test_wait_jump_enters_kneebend_with_action_frame_0() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["buttons"][0, 0] = np.uint16(0)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_KNEEBEND
    assert int(out0["animation_index"][0]) == SM_KNEEBEND
    assert int(out0["action_frame"][0]) == 0


def test_attackdash_iasa_jump_button_enters_kneebend() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_DASH)
    seed["action_frame"][0, 0] = np.int16(37)
    seed["anim_frame_f32"][0, 0] = np.float32(37.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_DASH)
    seed["facing"][0, 0] = np.uint8(1)
    seed["attackdash_x0"][0, 0] = np.int16(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Decomp: once AttackDash IASA clears the x0 pre-gate and allow_interrupt is active, it
    # delegates into Wait_IASA, so jump button edges still enter KneeBend.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_KNEEBEND
    assert int(out0["action_frame"][0]) == 0
    assert int(out0["animation_index"][0]) == SM_KNEEBEND


def test_attackdash_iasa_a_button_enters_attackhi3() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_DASH)
    seed["action_frame"][0, 0] = np.int16(35)
    seed["anim_frame_f32"][0, 0] = np.float32(35.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_DASH)
    seed["facing"][0, 0] = np.uint8(1)
    seed["attackdash_x0"][0, 0] = np.int16(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Wait_IASA delegation keeps grounded A-attack selection live for AttackDash button edges.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_A)
    cur_view["p"]["main_y"][0, 0] = np.int8(39)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_ATTACK_HI3
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_ATTACK_HI3


def test_attackdash_iasa_same_facing_hold_enters_walkslow() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_DASH)
    seed["action_frame"][0, 0] = np.int16(35)
    seed["anim_frame_f32"][0, 0] = np.float32(35.0)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_DASH)
    seed["facing"][0, 0] = np.uint8(0)  # left
    seed["attackdash_x0"][0, 0] = np.int16(0)
    seed["ground_id"][0, 0] = np.uint16(1)
    seed["pos_x"][0, 0] = np.float32(-49.859596)
    seed["pos_y"][0, 0] = np.float32(1.0e-4)
    seed["speed_ground_x_self"][0, 0] = np.float32(-0.07242203)
    seed["tilt_timer_x"][0, 0] = np.uint8(254)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Decomp: once AttackDash IASA clears ftCo_800D8AE0 and allow_interrupt is active, it falls
    # through to ftCo_Wait_IASA. A sustained same-facing stick hold can therefore enter WalkSlow
    # through ftCo_Walk_CheckInput, not remain in AttackDash.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_CheckInput
    prev_view["p"]["main_x"][0, 0] = np.int8(-90)
    cur_view["p"]["main_x"][0, 0] = np.int8(-90)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_WALK_SLOW
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_WALK_SLOW
    assert int(out0["on_ground"][0]) == 1


def test_wait_flick_forward_enters_dash_with_action_frame_1() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["facing"][0, 0] = np.uint8(1)  # right

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(127)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_DASH
    assert int(out0["animation_index"][0]) == SM_DASH
    assert int(out0["action_frame"][0]) == 1


def test_damagefall_cstick_back_edge_enters_attackairb() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["jumps_left"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGEFALL)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["facing"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # DamageFall IASA still consults ftCo_AttackAir_CheckInput through the shared airborne
    # interrupt path, including C-stick edges.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckInput
    cur_view["p"]["main_x"][0, 0] = np.int8(-90)
    cur_view["p"]["c_x"][0, 0] = np.int8(-127)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_ATTACK_AIR_B
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_ATTACK_AIR_B


def test_damagefall_a_button_enters_attackairn() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["jumps_left"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGEFALL)
    seed["action_frame"][0, 0] = np.int16(14)
    seed["anim_frame_f32"][0, 0] = np.float32(14.0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["facing"][0, 0] = np.uint8(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_A)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_ATTACK_AIR_N


def test_grounded_damageair2_down_stick_enters_squat() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_AIR_2)
    seed["action_frame"][0, 0] = np.int16(22)
    seed["anim_frame_f32"][0, 0] = np.float32(22.0)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_AIR_2)
    seed["facing"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Grounded Damage_IASA delegates to Wait_IASA when x221C_b6 is clear, and Wait_IASA checks
    # Squat after guard/jump and before Turn/Walk.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_SQUAT
    assert int(out0["action_frame"][0]) == 1
    assert int(out0["animation_index"][0]) == SM_SQUAT
    assert int(out0["on_ground"][0]) == 1
    assert int(out0["jumps_left"][0]) == 2


def test_wait_flick_backward_enters_turn_with_action_frame_1() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["facing"][0, 0] = np.uint8(1)  # right

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(-127)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["animation_index"][0]) == SM_TURN
    assert int(out0["action_frame"][0]) == 1


def test_ucf_dashback_turn_frame2_enters_dash_and_sets_x670_to_fe() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    dash_flick_abs = float(_common_attr("dash_flick_abs"))
    assert dash_flick_abs > 0.0

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    # locomotion_update_pre advances action_frame by +1 before gates, so seed 1 -> check 2.
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(0)
    seed["turn_has_turned"][0, 0] = np.uint8(1)
    seed["facing"][0, 0] = np.uint8(1)  # right

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Fresh right flick so x670_pre < 2 for the UCF dashback hold-time gate.
    prev_view["p"]["main_x"][0, 0] = np.int8(0)
    cur_view["p"]["main_x"][0, 0] = np.int8(80)

    out, internals = _step_once_with_internals(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_DASH
    assert int(out["action_frame"][0]) == 1
    assert int(out["animation_index"][0]) == SM_DASH

    dash_init = np.float32(_fox_attr("dash_initial_velocity"))
    # Later ground accel runs in the same step, so gr_vel may exceed the initial dash velocity.
    assert out["speed_ground_x_self"][0] >= dash_init

    # Dash entry override: fp->x670_timer_lstick_tilt_x = 0xFE (ftCo_Dash.c:62).
    assert int(internals["tilt_timer_x"][0]) == 0xFE

def test_kneebend_takeoff_enters_jumpf_and_consumes_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["kneebend_jump_input"][0, 0] = np.uint8(3)  # JumpInput_XY (refs/melee/.../ftCommon/forward.h)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Hold X and hold stick right.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 1
    assert int(out["on_ground"][0]) == 0
    # Decomp: JumpF/B phys skips ft_80084DB0 (and thus gravity) on the first frame after entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Phys_Inner
    expected_vy = np.float32(_fox_attr("jump_v_initial_velocity"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_wait_press_jump_enters_kneebend() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Press X this frame.
    prev_view["p"]["buttons"][0, 0] = np.uint16(0)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_KNEEBEND
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 2
    assert int(out["on_ground"][0]) == 1


def test_kneebend_release_jump_short_hops() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["kneebend_jump_input"][0, 0] = np.uint8(3)  # JumpInput_XY

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Release X on takeoff frame.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(0)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    assert int(out["action_frame"][0]) == 0
    expected_vy = np.float32(_fox_attr("hop_v_initial_velocity"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_kneebend_seeded_jump_input_lstick_short_hops_even_if_xy_held() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["kneebend_jump_input"][0, 0] = np.uint8(1)  # JumpInput_LStick

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Hold X, but keep stick released below tap_jump_release_threshold.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["main_y"][0, 0] = np.int8(0)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    expected_vy = np.float32(_fox_attr("hop_v_initial_velocity"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_kneebend_seeded_short_hop_latch_is_respected() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["anim_frame_f32"][0, 0] = np.float32(2.0)
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)
    seed["kneebend_jump_input"][0, 0] = np.uint8(3)  # JumpInput_XY
    seed["kneebend_is_short_hop"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Even if X is held, a pre-latched short hop must remain short.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    expected_vy = np.float32(_fox_attr("hop_v_initial_velocity"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_air_jump_consumes_jump_and_enters_jump_aerial() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["buttons"][0, 0] = np.uint16(0)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    # JumpAerialF
    assert int(out["action_id"][0]) == 0x001B
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 0


def test_landing_resets_jumps_and_enters_landing() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["ground_id"][0, 0] = np.uint16(1)  # prefer main FD floor segment
    # Arrange an ECB-bottom floor crossing in one frame (airborne: bottom_rel_y comes from ECB table).
    bot0 = _fox_ecb_bottom_rel_y(SM_FALL, 0)
    seed["pos_y"][0, 0] = np.float32(-bot0 + 0.10)
    grav = np.float32(_fox_attr("grav"))
    # Choose a small downward speed so gravity moves us below the floor this frame.
    bot1 = _fox_ecb_bottom_rel_y(SM_FALL, 1)
    seed["speed_y_self"][0, 0] = np.float32(min(-0.05, -(0.20 + (bot1 - bot0)) + grav))
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 1
    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 2


def test_walk_off_consumes_ground_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(85.4)  # FD floor edge is at ~85.5657 (data/stages/final_destination.json)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)  # crosses offstage in one frame
    # Steady Wait at a floor edge now has a dedicated replay-real teeter lock; keep this generic
    # "walk/run off the stage" guard on a non-teetering locomotion owner.
    seed["action_id"][0, 0] = np.uint16(ACT_RUN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_RUN)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 0
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["jumps_left"][0]) == 1


def test_wait_walk_off_enters_ottotto_grounded_without_consuming_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(85.4)  # FD floor edge is at ~85.5657 (data/stages/final_destination.json)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)  # crosses offstage in one frame
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out, contacts = _step_once_with_collision_contacts(seed, prev_inp, inp)

    # Decomp: ftCo_8009A3C8 checks Collide_Edge during steady Wait edge loss and calls
    # ftCo_8009A410 for grounded Ottotto entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    assert int(contacts["coll_env_flags"][0]) & MSL_COLLIDE_EDGE
    assert int(out["action_id"][0]) == ACT_OTTOTTO
    assert int(out["animation_index"][0]) == SM_OTTOTTO
    assert int(out["action_frame"][0]) == 0
    assert int(out["on_ground"][0]) == 1
    assert int(out["jumps_left"][0]) == 2


def test_run_off_does_not_snap_to_floor_edge() -> None:
    # Regression/safety guard for FD floor-edge handling: floor-edge snap behavior is intended to be
    # scoped to Down* states (mpColl_8004A45C_Floor-style) and must not keep normal locomotion
    # grounded when crossing the ledge.
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(85.4)  # FD floor edge is at ~85.5657 (data/stages/final_destination.json)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.0)  # crosses offstage in one frame
    seed["action_id"][0, 0] = np.uint16(ACT_RUN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_RUN)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 0
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["jumps_left"][0]) == 1


def test_jump_end_enters_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    # Keep the fighter airborne so collision doesn't force a Landing transition.
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["speed_y_self"][0, 0] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_JUMPF)
    seed["action_frame"][0, 0] = np.int16(120)
    seed["anim_frame_f32"][0, 0] = np.float32(120.0)
    seed["animation_index"][0, 0] = np.uint32(SM_JUMPF)
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == SM_FALL


def test_turn_reseed_does_not_flip_immediately_when_action_frame_is_0() -> None:
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["facing"][0, 0] = np.uint8(1)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(1)
    seed["turn_has_turned"][0, 0] = np.uint8(0)

    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out0, out1, out2 = _step_many(seed, prev_inp, inp, 3)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["facing"][0]) == 1
    assert int(out1["action_id"][0]) == ACT_TURN
    assert int(out1["facing"][0]) == 0
    assert int(out2["action_id"][0]) == ACT_TURN
    assert int(out2["facing"][0]) == 0


def test_turn_seeded_has_turned_prevents_double_flip() -> None:
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["facing"][0, 0] = np.uint8(0)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(0)
    seed["turn_has_turned"][0, 0] = np.uint8(1)

    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out0, out1 = _step_many(seed, prev_inp, inp, 2)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["facing"][0]) == 0
    assert int(out1["action_id"][0]) == ACT_TURN
    assert int(out1["facing"][0]) == 0


def test_fastfall_requires_vy_negative_stick_down_and_x671_lt_x8c() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    fast_fall_v = np.float32(_fox_attr("fast_fall_velocity"))
    grav = np.float32(_fox_attr("grav"))
    stick_thresh = float(_common_attr("fastfall_stick_threshold"))
    tilt_max = int(_common_attr("fastfall_tilt_max_frames"))

    assert tilt_max >= 1
    assert stick_thresh > 0.0

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["pos_y"][0, 0] = np.float32(10.0)
    seed["jumps_left"][0, 0] = np.uint8(2)

    # Trigger: vy<0, stick_y <= -x88, x671 < x8C (via a fresh down flick).
    seed_a = seed.copy()
    seed_a["speed_y_self"][0, 0] = np.float32(-1.0)
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_y"][0, 0] = np.int8(0)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)
    out_a = _step_once(seed_a, prev_inp, inp)
    assert np.isclose(out_a["speed_y_self"][0], np.float32(-fast_fall_v))

    # No trigger if vy >= 0 (even with stick held down).
    seed_b = seed.copy()
    seed_b["speed_y_self"][0, 0] = np.float32(1.0)
    out_b = _step_once(seed_b, prev_inp, inp)
    assert np.isclose(out_b["speed_y_self"][0], np.float32(1.0 - grav))

    # No trigger if x671 >= x8C (held down too long).
    # Make x671_pre == tilt_max by seeding x671_post=tilt_max-1 and holding down on both prev+cur.
    seed_c = seed.copy()
    seed_c["speed_y_self"][0, 0] = np.float32(-1.0)
    seed_c["tilt_timer_y"][0, 0] = np.uint8(max(tilt_max - 1, 0))
    prev_inp_c = _mk_input_bytes(1, input_stride)
    inp_c = _mk_input_bytes(1, input_stride)
    prev_view_c = prev_inp_c.view(INPUT_DTYPE).reshape((1,))
    cur_view_c = inp_c.view(INPUT_DTYPE).reshape((1,))
    prev_view_c["p"]["main_y"][0, 0] = np.int8(-80)
    cur_view_c["p"]["main_y"][0, 0] = np.int8(-80)
    out_c = _step_once(seed_c, prev_inp_c, inp_c)
    assert np.isclose(out_c["speed_y_self"][0], np.float32(-1.0 - grav))


def test_fastfall_latched_sets_vy_to_minus_fast_fall_velocity_each_frame() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    fast_fall_v = np.float32(_fox_attr("fast_fall_velocity"))

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["pos_y"][0, 0] = np.float32(10.0)
    seed["speed_y_self"][0, 0] = np.float32(-fast_fall_v)
    seed["fall_fast"][0, 0] = np.uint8(1)
    # Slippi post-frame fp+0x221A bit 0x08 is "isFastFalling" (raw byte captured in state_flags[1]).
    seed["state_flags"][0, 0, 1] = np.uint8(0x08)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    assert np.isclose(out["speed_y_self"][0], np.float32(-fast_fall_v))


def test_fall_fast_clears_on_landing_and_does_not_persist_off_stage() -> None:
    import json
    from pathlib import Path

    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    right_edge = -1.0
    fd = json.loads(Path("data/stages/final_destination.json").read_text())
    unit_scale = float(fd.get("unit_scale", 1.0))
    for s in fd["segments"]:
        if s["kind"] != "floor" or bool(s["platform"]):
            continue
        right_edge = max(right_edge, unit_scale * float(max(s["x0"], s["x1"])))
    assert right_edge > 0.0

    vx = np.float32(5.0)

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["pos_x"][0, 0] = np.float32(right_edge) - vx - np.float32(0.1)
    seed["ground_id"][0, 0] = np.uint16(2)  # prefer right FD floor segment
    # Ensure we land this frame under ECB-bottom grounding, accounting for fastfall and ECB offsets.
    bot0 = _fox_ecb_bottom_rel_y(SM_FALL, 0)
    seed["pos_y"][0, 0] = np.float32(-bot0 + 0.10)
    seed["speed_air_x_self"][0, 0] = vx
    # Any negative speed is sufficient (we fastfall on this step and clamp to -fast_fall_velocity).
    seed["speed_y_self"][0, 0] = np.float32(-0.25)
    seed["jumps_left"][0, 0] = np.uint8(2)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        # Step 1: flick down to fastfall, and land this frame.
        prev0 = np.zeros((1, input_stride), dtype=np.uint8)
        cur0 = np.zeros((1, input_stride), dtype=np.uint8)
        prev0_v = prev0.view(INPUT_DTYPE).reshape((1,))
        cur0_v = cur0.view(INPUT_DTYPE).reshape((1,))
        prev0_v["p"]["main_y"][0, 0] = np.int8(0)
        cur0_v["p"]["main_y"][0, 0] = np.int8(-80)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev0, cur0)
        msl_binding.write_compare(handle, out)
        out1 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out1["on_ground"][0]) == 1

        # Step 2: grounded movement carries us off the right edge.
        prev1 = cur0
        cur1 = np.zeros((1, input_stride), dtype=np.uint8)
        msl_binding.step_input(handle, prev1, cur1)
        msl_binding.write_compare(handle, out)
        out2 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out2["on_ground"][0]) == 0

        # Step 3: first airborne frame after leaving ground should apply gravity (not fall-fast).
        prev2 = cur1
        cur2 = np.zeros((1, input_stride), dtype=np.uint8)
        msl_binding.step_input(handle, prev2, cur2)
        msl_binding.write_compare(handle, out)
        out3 = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        assert int(out3["on_ground"][0]) == 0
        # After leaving ground, vertical speed should be governed by gravity/terminal velocity,
        # not by fall-fast (which would clamp to -fast_fall_velocity).
        terminal_v = np.float32(_fox_attr("terminal_vel"))
        eps = np.float32(1024.0) * np.finfo(np.float32).eps
        assert out3["speed_y_self"][0] >= np.float32(-terminal_v) - eps
    finally:
        msl_binding.destroy(handle)


def test_damage_fall_can_trigger_fastfall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    fast_fall_v = np.float32(_fox_attr("fast_fall_velocity"))

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGEFALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(0)
    seed["pos_y"][0, 0] = np.float32(10.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_y"][0, 0] = np.int8(0)
    cur_view["p"]["main_y"][0, 0] = np.int8(-80)

    out = _step_once(seed, prev_inp, inp)
    assert np.isclose(out["speed_y_self"][0], np.float32(-fast_fall_v))


def test_damage_fall_does_not_force_fall_fast_from_speed_y_self() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    fast_fall_v = np.float32(_fox_attr("fast_fall_velocity"))
    terminal_v = np.float32(_fox_attr("terminal_vel"))

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGEFALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(0)
    seed["pos_y"][0, 0] = np.float32(10.0)
    seed["speed_y_self"][0, 0] = np.float32(-fast_fall_v)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    # A too-broad fall_fast inference (e.g. from speed_y_self alone) would force vy=-fast_fall_v here.
    assert np.isclose(out["speed_y_self"][0], np.float32(-terminal_v))
