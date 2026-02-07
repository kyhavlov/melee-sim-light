from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_damage_jump_buffer_x14


def test_damage_jump_buffer_x14_derivation_is_prefix_invariant() -> None:
    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_fall = 0x001D
    act_damage_air_1 = 0x0054
    act_damage_air_2 = 0x0055

    action_id = np.array(
        [
            act_wait,
            act_wait,
            act_damage_air_1,  # damage entry: x14 reset
            act_damage_air_1,
            act_damage_air_1,
            act_damage_air_2,  # same damage family action change: causal reset
            act_damage_air_2,
            act_fall,  # outside damage family: x14 forced to 0
            act_wait,
        ],
        dtype=np.uint16,
    )
    hitstun = np.array([0, 0, 4, 3, 2, 1, 0, 0, 0], dtype=np.uint16)
    # Press XY on frame 3 (x14=3), then tap-jump on frame 5 after the action switch (x14=1).
    buttons_pressed = np.array([0, 0, 0, 0x0C00, 0, 0, 0, 0, 0], dtype=np.uint16)
    stick_y = np.array([0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.0, 0.0, 0.0], dtype=np.float32)
    tilt_timer_y = np.array([0xFE, 0xFE, 0, 0, 0, 0, 0, 0xFE, 0xFE], dtype=np.uint8)

    full = derive_damage_jump_buffer_x14(
        action_id=action_id,
        hitstun_u16=hitstun,
        buttons_pressed=buttons_pressed,
        stick_y_unit=stick_y,
        tilt_timer_y=tilt_timer_y,
        tap_jump_threshold=0.7,
        tap_jump_tilt_max_frames=4,
        button_mask_xy=0x0C00,
        damage_actions=(act_damage_air_1, act_damage_air_2),
    )

    # Smoke-check key transitions.
    assert int(full[2]) == 0
    assert int(full[3]) == 3
    assert int(full[4]) == 3
    assert int(full[5]) == 1
    assert int(full[7]) == 0

    for k in (1, 2, 3, 4, 5, 6, 7, int(action_id.size)):
        got = derive_damage_jump_buffer_x14(
            action_id=action_id[:k],
            hitstun_u16=hitstun[:k],
            buttons_pressed=buttons_pressed[:k],
            stick_y_unit=stick_y[:k],
            tilt_timer_y=tilt_timer_y[:k],
            tap_jump_threshold=0.7,
            tap_jump_tilt_max_frames=4,
            button_mask_xy=0x0C00,
            damage_actions=(act_damage_air_1, act_damage_air_2),
        )
        assert np.array_equal(got, full[:k])
