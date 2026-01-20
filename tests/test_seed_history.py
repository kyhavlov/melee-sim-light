from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import (
    compute_press_timer_u8,
    compute_tilt_timer_axis,
    compute_tilt_timer_y_pre_post_with_fall_fast,
    compute_x672_trigger_timer_pre_post,
    derive_kneebend_internals,
    derive_turn_internals,
)


def test_compute_tilt_timer_axis_basic_sequence() -> None:
    axis = np.array([0.0, 0.30, 0.30, 0.30, 0.0, -0.30, -0.30], dtype=np.float32)
    out = compute_tilt_timer_axis(axis, tilt_thresh=0.25)
    assert out.dtype == np.uint8
    assert out.tolist() == [0xFE, 0, 1, 2, 0xFE, 0, 1]


def test_compute_tilt_timer_axis_resets_on_direction_change() -> None:
    axis = np.array([0.30, 0.30, -0.30, -0.30], dtype=np.float32)
    out = compute_tilt_timer_axis(axis, tilt_thresh=0.25)
    assert out.tolist() == [0, 1, 0, 1]


def test_compute_tilt_timer_axis_saturates_at_fe() -> None:
    axis = np.full(300, 0.30, dtype=np.float32)
    out = compute_tilt_timer_axis(axis, tilt_thresh=0.25)
    assert int(out[-1]) == 0xFE


def test_derive_turn_internals_is_causal_wrt_future_frames() -> None:
    act_turn = 0x0012
    act_turn_run = 0x0013

    dash_flick_abs = 0.8
    dash_flick_tilt_max_frames = 2

    # Prefix contains a smash-turn entry at frame 2 (opposite-facing dash flick).
    # Appended suffix contains arbitrary future facing flips; outputs for the prefix must not change.
    a_prefix = np.array(
        [0x000E, 0x000E, act_turn, act_turn, act_turn, 0x000E],
        dtype=np.uint16,
    )
    facing_prefix = np.array([1, 1, 1, 0, 0, 0], dtype=np.uint8)
    stick_x_prefix = np.array([0.0, 0.0, -0.9, 0.0, 0.0, 0.0], dtype=np.float32)
    tilt_timer_x_prefix = np.array([0xFE, 0xFE, 0, 0xFE, 0xFE, 0xFE], dtype=np.uint8)
    turn_frames_prefix = np.full(a_prefix.shape[0], 4, dtype=np.uint8)

    f0, h0 = derive_turn_internals(
        action_id=a_prefix,
        facing=facing_prefix,
        stick_x_unit=stick_x_prefix,
        tilt_timer_x=tilt_timer_x_prefix,
        dash_flick_abs=dash_flick_abs,
        dash_flick_tilt_max_frames=dash_flick_tilt_max_frames,
        turn_frames=turn_frames_prefix,
        act_turn=act_turn,
        act_turn_run=act_turn_run,
    )

    a_ext = np.concatenate([a_prefix, np.array([0x000E, act_turn, act_turn, 0x000E], dtype=np.uint16)])
    facing_ext = np.concatenate([facing_prefix, np.array([1, 0, 1, 0], dtype=np.uint8)])
    stick_x_ext = np.concatenate([stick_x_prefix, np.array([0.0, 0.2, 0.2, 0.0], dtype=np.float32)])
    tilt_timer_x_ext = np.concatenate(
        [tilt_timer_x_prefix, np.array([0xFE, 0xFE, 0xFE, 0xFE], dtype=np.uint8)]
    )
    turn_frames_ext = np.full(a_ext.shape[0], 4, dtype=np.uint8)

    f1, h1 = derive_turn_internals(
        action_id=a_ext,
        facing=facing_ext,
        stick_x_unit=stick_x_ext,
        tilt_timer_x=tilt_timer_x_ext,
        dash_flick_abs=dash_flick_abs,
        dash_flick_tilt_max_frames=dash_flick_tilt_max_frames,
        turn_frames=turn_frames_ext,
        act_turn=act_turn,
        act_turn_run=act_turn_run,
    )

    assert np.array_equal(f0, f1[: f0.size])
    assert np.array_equal(h0, h1[: h0.size])


def test_derive_kneebend_internals_is_causal_wrt_future_frames() -> None:
    import json
    from pathlib import Path

    act_wait = 0x000E
    act_kneebend = 0x0018
    button_x = 0x0400
    button_xy = 0x0400 | 0x0800

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    tap_jump_threshold = float(common["tap_jump_threshold"])
    tap_jump_release_threshold = float(common["tap_jump_release_threshold"])
    tap_jump_tilt_max_frames = int(common["tap_jump_tilt_max_frames"])

    # Prefix enters KneeBend at frame 2 via X press; short-hop latches on release at frame 4.
    a_prefix = np.array([act_wait, act_wait, act_kneebend, act_kneebend, act_kneebend, act_wait], dtype=np.uint16)
    buttons_prefix = np.array([0, 0, button_x, button_x, 0, 0], dtype=np.uint16)
    buttons_pressed_prefix = np.array([0, 0, button_x, 0, 0, 0], dtype=np.uint16)
    stick_y_prefix = np.zeros(a_prefix.shape[0], dtype=np.float32)
    cstick_y_prefix = np.zeros(a_prefix.shape[0], dtype=np.float32)
    tilt_timer_y_prefix = np.full(a_prefix.shape[0], 0xFE, dtype=np.uint8)

    j0, s0 = derive_kneebend_internals(
        action_id=a_prefix,
        buttons=buttons_prefix,
        buttons_pressed=buttons_pressed_prefix,
        stick_y_unit=stick_y_prefix,
        cstick_y_unit=cstick_y_prefix,
        tilt_timer_y=tilt_timer_y_prefix,
        tap_jump_threshold=tap_jump_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        button_mask_xy=button_xy,
    )

    # Append arbitrary future KneeBend segments and inputs; prefix outputs must not change.
    a_ext = np.concatenate([a_prefix, np.array([act_wait, act_kneebend, act_kneebend, act_wait], dtype=np.uint16)])
    buttons_ext = np.concatenate(
        [buttons_prefix, np.array([0, button_x, button_x, 0], dtype=np.uint16)]
    )
    buttons_pressed_ext = np.concatenate([buttons_pressed_prefix, np.array([0, button_x, 0, 0], dtype=np.uint16)])
    stick_y_ext = np.concatenate([stick_y_prefix, np.zeros(4, dtype=np.float32)])
    cstick_y_ext = np.concatenate([cstick_y_prefix, np.zeros(4, dtype=np.float32)])
    tilt_timer_y_ext = np.concatenate([tilt_timer_y_prefix, np.full(4, 0xFE, dtype=np.uint8)])

    j1, s1 = derive_kneebend_internals(
        action_id=a_ext,
        buttons=buttons_ext,
        buttons_pressed=buttons_pressed_ext,
        stick_y_unit=stick_y_ext,
        cstick_y_unit=cstick_y_ext,
        tilt_timer_y=tilt_timer_y_ext,
        tap_jump_threshold=tap_jump_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        button_mask_xy=button_xy,
    )

    assert np.array_equal(j0, j1[: j0.size])
    assert np.array_equal(s0, s1[: s0.size])


def test_fall_fast_and_x671_override_is_causal_wrt_future_frames() -> None:
    # Prefix triggers a fastfall at frame 1 (based on vy_start from frame 0).
    stick_y_prefix = np.array([0.0, -0.9, -0.9, 0.0], dtype=np.float32)
    jump_entry_prefix = np.zeros(stick_y_prefix.shape[0], dtype=bool)
    speed_y_self_prefix = np.array([-1.0, -3.2, -3.2, -3.2], dtype=np.float32)
    on_ground_prefix = np.zeros(stick_y_prefix.shape[0], dtype=bool)

    t_pre0, t_post0, ff0 = compute_tilt_timer_y_pre_post_with_fall_fast(
        stick_y_prefix,
        tilt_thresh=0.25,
        jump_entry=jump_entry_prefix,
        fastfall_ok=np.ones(stick_y_prefix.shape[0], dtype=bool),
        speed_y_self_post=speed_y_self_prefix,
        on_ground_post=on_ground_prefix,
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
    )

    assert int(ff0[1]) == 1
    assert int(t_post0[1]) == 0xFE

    # Append arbitrary future frames; prefix outputs must not change.
    stick_y_ext = np.concatenate([stick_y_prefix, np.array([-0.9, 0.0, 0.0], dtype=np.float32)])
    jump_entry_ext = np.concatenate([jump_entry_prefix, np.array([False, False, False])])
    speed_y_self_ext = np.concatenate([speed_y_self_prefix, np.array([-3.2, -3.2, -3.2], dtype=np.float32)])
    on_ground_ext = np.concatenate([on_ground_prefix, np.array([False, False, False])])

    t_pre1, t_post1, ff1 = compute_tilt_timer_y_pre_post_with_fall_fast(
        stick_y_ext,
        tilt_thresh=0.25,
        jump_entry=jump_entry_ext,
        fastfall_ok=np.ones(stick_y_ext.shape[0], dtype=bool),
        speed_y_self_post=speed_y_self_ext,
        on_ground_post=on_ground_ext,
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
    )

    assert np.array_equal(t_pre0, t_pre1[: t_pre0.size])
    assert np.array_equal(t_post0, t_post1[: t_post0.size])
    assert np.array_equal(ff0, ff1[: ff0.size])


def test_press_timer_u8_is_causal_wrt_future_frames() -> None:
    # Models fp->x67F update style: reset to 0 on L/R press; else increment and clamp at 0xFF.
    mask_lr = 0x0040 | 0x0020

    bp_prefix = np.array([0, 0, mask_lr, 0, 0, 0], dtype=np.uint16)
    t0 = compute_press_timer_u8(buttons_pressed=bp_prefix, press_mask=mask_lr, start_timer=0xFF)
    assert t0.tolist() == [0xFF, 0xFF, 0, 1, 2, 3]

    bp_ext = np.concatenate([bp_prefix, np.array([0, 0, mask_lr, 0], dtype=np.uint16)])
    t1 = compute_press_timer_u8(buttons_pressed=bp_ext, press_mask=mask_lr, start_timer=0xFF)
    assert np.array_equal(t0, t1[: t0.size])


def test_x672_trigger_timer_is_causal_wrt_future_frames() -> None:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    trigger_min = float(common["powershield_reflect_trigger_min"])

    # Prefix: new trigger press at frame 1 yields timer_pre=0 and timer_post=0; GuardReflect entry at frame 2
    # overrides timer_post=0xFE (decomp: ftCo_Guard.c sets x672=0xFE on GuardReflect).
    trig_prefix = np.array([0.0, 1.0, 1.0, 1.0], dtype=np.float32)
    guard_entry_prefix = np.array([False, False, True, False])

    pre0, post0 = compute_x672_trigger_timer_pre_post(
        trigger_unit=trig_prefix,
        trigger_min=trigger_min,
        guard_reflect_entry=guard_entry_prefix,
        start_timer_post=0xFE,
    )

    assert int(pre0[1]) == 0
    assert int(post0[2]) == 0xFE

    # Append arbitrary future frames; prefix outputs must not change.
    trig_ext = np.concatenate([trig_prefix, np.array([1.0, 0.0, 1.0], dtype=np.float32)])
    guard_entry_ext = np.concatenate([guard_entry_prefix, np.array([False, False, True])])
    pre1, post1 = compute_x672_trigger_timer_pre_post(
        trigger_unit=trig_ext,
        trigger_min=trigger_min,
        guard_reflect_entry=guard_entry_ext,
        start_timer_post=0xFE,
    )

    assert np.array_equal(pre0, pre1[: pre0.size])
    assert np.array_equal(post0, post1[: post0.size])


def test_jump_to_jump_aerial_entry_clears_fall_fast_and_overrides_x671() -> None:
    # When transitioning between Jump* motion states (e.g. JumpF -> JumpAerialF), treat it as a fresh
    # entry: clear fall_fast and set x671_post=0xFE on that entry frame.
    #
    # Decomp refs:
    # - ftCommon_CheckFallFast: refs/melee/src/melee/ft/ftcommon.c:505-520
    # - JumpAerial entry override: refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
    act_jump_f = np.uint16(0x0019)
    act_jump_aerial_f = np.uint16(0x001B)

    post_state = np.array([act_jump_f, act_jump_f, act_jump_aerial_f, act_jump_aerial_f], dtype=np.uint16)
    is_jump = (post_state == act_jump_f) | (post_state == act_jump_aerial_f)
    prev_state = np.concatenate(([post_state[0]], post_state[:-1]))
    jump_entry = is_jump & (post_state != prev_state)

    # Create a fastfall latch at frame 1, then ensure the JumpF->JumpAerialF transition at frame 2
    # clears it and forces x671_post=0xFE.
    stick_y = np.array([0.0, -0.9, 0.0, 0.0], dtype=np.float32)
    speed_y_self_post = np.array([-1.0, -3.2, -2.0, -2.0], dtype=np.float32)
    on_ground_post = np.zeros(post_state.shape[0], dtype=bool)
    fastfall_ok = np.ones(post_state.shape[0], dtype=bool)

    _, t_post, ff_post = compute_tilt_timer_y_pre_post_with_fall_fast(
        stick_y,
        tilt_thresh=0.25,
        jump_entry=jump_entry,
        fastfall_ok=fastfall_ok,
        speed_y_self_post=speed_y_self_post,
        on_ground_post=on_ground_post,
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
    )

    assert int(ff_post[1]) == 1
    assert int(t_post[2]) == 0xFE
    assert int(ff_post[2]) == 0
