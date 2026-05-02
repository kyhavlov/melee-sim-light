from __future__ import annotations

import numpy as np
import pytest


def test_native_hitbox_prev_centers_rejects_extra_column_side_arrays() -> None:
    import msl_binding

    n = 2
    w = 4
    with pytest.raises(ValueError, match="action_id"):
        msl_binding.derive_hitbox_prev_centers(
            2,
            np.zeros((n, w), dtype=np.uint8),
            np.zeros((n, w + 1), dtype=np.uint16),
            np.zeros((n, w), dtype=np.uint32),
            np.zeros((n, w), dtype=np.int16),
            np.zeros((n, w), dtype=np.float32),
            np.zeros((n, w), dtype=np.float32),
            np.zeros((n, w), dtype=np.float32),
            None,
            np.zeros((n, w), dtype=np.uint8),
            np.ones((n, w), dtype=np.float32),
            None,
            None,
        )


def test_native_combo_push_rejects_extra_column_attack_arrays() -> None:
    import msl_binding

    with pytest.raises(ValueError, match="last_attack_landed"):
        msl_binding.derive_combo_push_timer_seed(
            np.zeros((2, 4), dtype=np.uint8),
            np.zeros((2, 5), dtype=np.uint8),
            None,
        )


def test_native_combo_seed_rejects_extra_column_side_arrays() -> None:
    import msl_binding

    n = 2
    w = 4
    with pytest.raises(ValueError, match="instance_id"):
        msl_binding.derive_combo_seed_fields(
            2,
            [1, 2],
            np.zeros((n, w), dtype=np.uint16),
            np.zeros((n, w, 5), dtype=np.uint8),
            np.zeros((n, w + 1), dtype=np.uint16),
            np.full((n, w), 0xFF, dtype=np.uint8),
            None,
        )


def test_native_instance_and_item_counters_require_2d_inputs() -> None:
    import msl_binding

    with pytest.raises(ValueError, match="fighter_instance_id"):
        msl_binding.derive_instance_id_counter(
            np.zeros((2, 4, 1), dtype=np.uint16),
            np.zeros((2, 8), dtype=np.uint16),
        )
    with pytest.raises(ValueError, match="item_exists"):
        msl_binding.derive_item_spawn_id_counter(
            np.zeros((2, 8, 1), dtype=np.uint8),
            np.zeros((2, 8), dtype=np.uint32),
        )


def test_native_staling_history_rejects_non_matching_widths() -> None:
    import msl_binding

    n = 2
    with pytest.raises(ValueError, match="width"):
        msl_binding.derive_staling_history(
            [1, 2],
            np.zeros((n, 2), dtype=np.uint8),
            np.zeros((n, 3), dtype=np.uint16),
            np.zeros((n, 2), dtype=np.float32),
            np.zeros((n, 2), dtype=np.uint32),
            np.zeros((n, 2), dtype=np.float32),
            np.zeros((n, 2), dtype=np.uint8),
            np.zeros((n, 2), dtype=np.uint16),
            np.full((n, 2), 0xFF, dtype=np.uint8),
            np.zeros((n, 2), dtype=np.uint16),
        )


def test_native_combat_hitlist_rejects_extra_column_side_arrays() -> None:
    from tools.slippi.combat_history import derive_combat_hitlist_seed_fields

    n = 2
    w = 4
    u8 = np.zeros((n, w), dtype=np.uint8)
    u16 = np.zeros((n, w), dtype=np.uint16)
    f32 = np.zeros((n, w), dtype=np.float32)
    with pytest.raises(ValueError, match="team_id"):
        derive_combat_hitlist_seed_fields(
            num_players=2,
            is_teams=False,
            team_id=np.zeros((n, w + 1), dtype=np.uint8),
            char_id=u8,
            action_id=u16,
            action_frame=np.zeros((n, w), dtype=np.int16),
            animation_index=np.zeros((n, w), dtype=np.uint32),
            facing=u8,
            on_ground=u8,
            pos_x=f32,
            pos_y=f32,
            fighter_scale_y=np.ones((n, w), dtype=np.float32),
            guard_tilt_x8=u16,
            guard_tilt_x4=f32,
            stocks=u8,
            shield_hp=f32,
            hurtbox_state=u8,
            instance_id=u16,
            input_buttons=u16,
            input_l=u8,
            input_r=u8,
        )
