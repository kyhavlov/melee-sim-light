from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import (
    compute_tilt_timer_axis,
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
