from __future__ import annotations

from tools.eval.dataset import SAMPLE_DTYPE, SEED_DTYPE


def test_seed_schema_includes_staling_fields() -> None:
    # PP#4 groundwork: the seed schema must carry stale queue state through reseed.
    assert "stale_queue_index" in SEED_DTYPE.fields
    assert "stale_move_id" in SEED_DTYPE.fields
    assert "stale_attack_instance" in SEED_DTYPE.fields
    assert "attack_instance" in SEED_DTYPE.fields
    # Throw-side pulse-latch schema scaffold (future causal ownership wiring).
    assert "throw_pulse_consumed" in SEED_DTYPE.fields
    assert "throw_pulse_crossed_prev_frame" in SEED_DTYPE.fields
    assert "source_clear_timer_x18c8" in SEED_DTYPE.fields
    assert "source_clear_owner_set_phase" in SEED_DTYPE.fields
    assert "source_clear_processhit_damage_pending_phase" in SEED_DTYPE.fields
    assert "damageflyroll_fighter_8006cda4_phase_hint" in SEED_DTYPE.fields
    assert "source_clear_grounded_damage_clear_phase" in SEED_DTYPE.fields
    assert "source_clear_terminal_phase" in SEED_DTYPE.fields
    # Walk callback-owned source velocity lane (ftWalkCommon_800DFDDC `mv_x0`).
    assert "walk_anim_source_vel_f32" in SEED_DTYPE.fields
    # GuardSetOff hidden x19A4 lower-bound bridge for F02 blocker rows.
    assert "guard_setoff_hitlag_damage_min" in SEED_DTYPE.fields
    # F02 blocker lane: GuardSetOff hitlag-exit ownership phase.
    assert "guard_setoff_hitlag_exit_phase_u8" in SEED_DTYPE.fields
    # F02 blocker lane: GuardSetOff post-hitlag owner class.
    assert "guard_setoff_post_hitlag_owner_u8" in SEED_DTYPE.fields
    # F04 blocker lane: replay-visible camera-box visibility bit (`fp->x221F_b0`).
    assert "camera_box_visible_x221f_b0" in SEED_DTYPE.fields
    # F04 blocker lane: hidden Rebirth camera anchor Y (`fp->mv.co.common.x8`).
    assert "rebirth_camera_anchor_y_f32" in SEED_DTYPE.fields
    # F04 blocker lane: fighter camera-subject target point (`camera_box->x1C`) and radius.
    assert "camera_target_world_x_f32" in SEED_DTYPE.fields
    assert "camera_target_world_y_f32" in SEED_DTYPE.fields
    assert "camera_target_world_z_f32" in SEED_DTYPE.fields
    assert "camera_box_radius_f32" in SEED_DTYPE.fields
    assert "camera_target_point_inside_stage_cam_bounds_u8" in SEED_DTYPE.fields
    # Damage KB stacking window (fp->dmg.x18AC_time_since_hit).
    assert "damage_time_since_hit_x18ac" in SEED_DTYPE.fields


def test_dataset_dtype_sizes_match_c_structs() -> None:
    # If these drift, preprocessing/eval will fail with record_size mismatches.
    import msl_binding

    sizes = msl_binding.sizes()
    assert int(sizes["seed"]) == SEED_DTYPE.itemsize
    assert int(sizes["sample"]) == SAMPLE_DTYPE.itemsize
