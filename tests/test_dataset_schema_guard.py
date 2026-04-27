from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import SAMPLE_DTYPE, SEED_DTYPE
from tools.slippi.make_dataset_from_slp import (
    _derive_landing_fallspecial_allow_interrupt_seed_lane,
    _derive_walljump_phase_seed_lanes,
)


def test_seed_schema_includes_staling_fields() -> None:
    # PP#4 groundwork: the seed schema must carry stale queue state through reseed.
    assert "stale_queue_index" in SEED_DTYPE.fields
    assert "stale_move_id" in SEED_DTYPE.fields
    assert "stale_attack_instance" in SEED_DTYPE.fields
    assert "attack_instance" in SEED_DTYPE.fields
    # Throw-side pulse-latch schema scaffold (future causal ownership wiring).
    assert "throw_pulse_consumed" in SEED_DTYPE.fields
    assert "throw_pulse_crossed_prev_frame" in SEED_DTYPE.fields
    assert "throw_command_pending_pulse_frame" in SEED_DTYPE.fields
    assert "item_hitlist_victim_port" in SEED_DTYPE.fields
    assert "item_hitlist_victim_cd" in SEED_DTYPE.fields
    assert "item_hitlist_victim_hitbox_mask" in SEED_DTYPE.fields
    assert "item_hitlist_victim_iid" in SEED_DTYPE.fields
    assert "item_reflect_transfer_port" in SEED_DTYPE.fields
    assert "item_reflect_transfer_iid" in SEED_DTYPE.fields
    assert "item_shield_bounce_valid" in SEED_DTYPE.fields
    assert "item_hidden_body_hit_victim_port" in SEED_DTYPE.fields
    assert "item_hidden_callback_flags" in SEED_DTYPE.fields
    assert "source_clear_timer_x18c8" in SEED_DTYPE.fields
    assert "source_clear_owner_set_phase" in SEED_DTYPE.fields
    assert "source_clear_processhit_damage_pending_phase" in SEED_DTYPE.fields
    assert "fighter_8006cda4_pre_gate_consume_count" in SEED_DTYPE.fields
    assert "source_clear_grounded_damage_clear_phase" in SEED_DTYPE.fields
    assert "source_clear_terminal_phase" in SEED_DTYPE.fields
    # Walk callback-owned source velocity lane (ftWalkCommon_800DFDDC `mv_x0`).
    assert "walk_anim_source_vel_f32" in SEED_DTYPE.fields
    # Walk retarget source lane for the hidden ft_GetGroundFrictionMultiplier branch.
    assert "walk_retarget_tick_source_vel_f32" in SEED_DTYPE.fields
    # Run callback-owned source velocity lane (ftCo_Run_Anim `vel`).
    assert "run_anim_source_vel_f32" in SEED_DTYPE.fields
    # Turn->KneeBend hidden-facing owner lane.
    assert "turn_kneebend_facing_override_u8" in SEED_DTYPE.fields
    # Capture/grab hidden owner lanes.
    assert "handicap" in SEED_DTYPE.fields
    assert "capture_grab_timer_f32" in SEED_DTYPE.fields
    assert "capture_wait_counter_f32" in SEED_DTYPE.fields
    assert "capture_wait_anim_rate_timer_f32" in SEED_DTYPE.fields
    assert "capture_wait_jump_latch_u8" in SEED_DTYPE.fields
    assert "capture_breakout_pending_u8" in SEED_DTYPE.fields
    # GuardSetOff hidden x19A4 lower-bound bridge for F02 blocker rows.
    assert "guard_setoff_hitlag_damage_min" in SEED_DTYPE.fields
    # F02 blocker lane: GuardSetOff hitlag-exit ownership phase.
    assert "guard_setoff_hitlag_exit_phase_u8" in SEED_DTYPE.fields
    # F02 blocker lane: GuardSetOff post-hitlag owner class.
    assert "guard_setoff_post_hitlag_owner_u8" in SEED_DTYPE.fields
    assert "guard_setoff_exit_frame_speed_mul_f32" in SEED_DTYPE.fields
    # Same-frame fighter-proc order lane for plAttack_80037B08 instance_id entries.
    assert "motion_entry_instance_id_override_u16" in SEED_DTYPE.fields
    # Raw Slippi/controller source-owner domain for replay-facing last_hit_by parity.
    assert "source_port0" in SEED_DTYPE.fields
    # Hidden FallSpecial -> LandingFallSpecial interrupt carry bit.
    assert "landing_fallspecial_allow_interrupt" in SEED_DTYPE.fields
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
    assert "passivewall_timer" in SEED_DTYPE.fields
    assert "walljump_input_timer" in SEED_DTYPE.fields
    assert "walljump_wall_side_i8" in SEED_DTYPE.fields
    # Damage KB stacking window (fp->dmg.x18AC_time_since_hit).
    assert "damage_time_since_hit_x18ac" in SEED_DTYPE.fields
    # Fighter phantom/tip-log delayed damage lane (`dmg.x1898` + source).
    assert "phantom_damage_pending_x1898" in SEED_DTYPE.fields
    # Legacy name; stores local simulator slot or 0xFF, not raw Slippi source-port domain.
    assert "phantom_damage_source_port" in SEED_DTYPE.fields
    # Grounded attacker shield-pushback scalar (`fp->xF4_ground_attacker_shield_kb_vel`).
    assert "attacker_shield_ground_kb_vel" in SEED_DTYPE.fields
    assert "combat_shield_contact_hb_kind" in SEED_DTYPE.fields
    assert "combat_shield_hit_int_damage" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_x_f32" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_y_f32" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_valid_u8" in SEED_DTYPE.fields
    # mpColl persisted wall-side/index callback state for narrow DamageFlyTop wall contact rows.
    assert "mpcoll_wall_kind_seed_u8" in SEED_DTYPE.fields
    assert "mpcoll_wall_id_seed_u16" in SEED_DTYPE.fields
    # Side-B hidden ghost ring lanes for end-to-end Illusion/Phantasm ownership.
    assert "illusion_ghost_pos0_x" in SEED_DTYPE.fields
    assert "illusion_ghost_pos0_y" in SEED_DTYPE.fields
    assert "illusion_ghost_pos1_x" in SEED_DTYPE.fields
    assert "illusion_ghost_pos1_y" in SEED_DTYPE.fields
    assert "illusion_ghost_pos2_x" in SEED_DTYPE.fields
    assert "illusion_ghost_pos2_y" in SEED_DTYPE.fields
    # Combat hitbox x58/x4C continuity seed lanes for BODY collision-space followup.
    assert "combat_hitbox_prev_valid" in SEED_DTYPE.fields
    assert "combat_hitbox_prev_x" in SEED_DTYPE.fields
    assert "combat_hitbox_prev_y" in SEED_DTYPE.fields
    assert "combat_hitbox_prev_z" in SEED_DTYPE.fields


def test_dataset_dtype_sizes_match_c_structs() -> None:
    # If these drift, preprocessing/eval will fail with record_size mismatches.
    import msl_binding

    sizes = msl_binding.sizes()
    assert int(sizes["seed"]) == SEED_DTYPE.itemsize
    assert int(sizes["sample"]) == SAMPLE_DTYPE.itemsize


def test_item_common_data_exports_shield_bounce_threshold_source() -> None:
    # Item_80269DC8 uses 90 + it_804D6D28->unk_degrees; keep the runtime predicate data-backed by
    # the extracted ItCo.dat item common file instead of a naked gameplay constant.
    data = json.loads(Path("data/items/item_common.json").read_text())
    assert data["shield_bounce_extra_degrees"] == 45.0


def test_landing_fallspecial_allow_interrupt_lane_is_prefix_causal() -> None:
    # Action ids: FallSpecial=35, LandingFallSpecial=43, EscapeAir=236.
    # EscapeAir-owned freefall enters LandingFallSpecial with allow_interrupt=false, while
    # non-EscapeAir FallSpecial sources use the common true-carry path.
    action = np.array([236, 35, 35, 43, 43, 14, 358, 35, 43, 14, 236, 43], dtype=np.uint16)
    got = _derive_landing_fallspecial_allow_interrupt_seed_lane(action_id_u16=action)
    assert got.tolist() == [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0]

    extended = np.concatenate([action, np.array([35, 43, 43], dtype=np.uint16)])
    got_extended = _derive_landing_fallspecial_allow_interrupt_seed_lane(action_id_u16=extended)
    np.testing.assert_array_equal(got_extended[: action.shape[0]], got)


def test_character_attrs_include_ordered_walljump_setup_threshold() -> None:
    root = Path(__file__).resolve().parents[1]
    for character in ("fox", "falco"):
        data = json.loads((root / "data" / "characters" / f"{character}.json").read_text())
        keys = list(data.keys())
        assert data["walljump_setup_x_delta_threshold"] == 0.5
        assert keys.index("wall_jump_vertical_velocity") < keys.index("walljump_setup_x_delta_threshold")
        assert keys.index("walljump_setup_x_delta_threshold") < keys.index("camera_zoom_target_bone_part_id")


def test_walljump_phase_seed_lane_is_prefix_causal() -> None:
    action = np.array([27, 27, 27, 27, 27, 27, 27], dtype=np.uint16)
    action_frame = np.array([15, 16, 17, 18, 19, 20, 21], dtype=np.int16)
    pos_x = np.array([87.0, 87.0, 87.0, 87.0, -87.0, -87.0, -87.0], dtype=np.float32)
    pos_y = np.full(action.shape, -9.0, dtype=np.float32)
    # `raw_main_x[i + 1]` is the sample's current one-step input_t for seed row i; this is not
    # future reference state. The final reconstruction row is unused by dataset emission.
    raw_main_x = np.array([0, 0, 0, 84, 0, -84, -84], dtype=np.int8)

    timer, side = _derive_walljump_phase_seed_lanes(
        action_id_u16=action,
        action_frame_i16=action_frame,
        pos_x_f32=pos_x,
        pos_y_f32=pos_y,
        raw_main_x_i8=raw_main_x,
    )
    assert timer.tolist() == [254, 254, 9, 254, 11, 12, 13]
    assert side.tolist() == [0, 0, -1, 0, 1, 1, 1]

    for end in range(2, action.shape[0] + 1):
        prefix_timer, prefix_side = _derive_walljump_phase_seed_lanes(
            action_id_u16=action[:end],
            action_frame_i16=action_frame[:end],
            pos_x_f32=pos_x[:end],
            pos_y_f32=pos_y[:end],
            raw_main_x_i8=raw_main_x[:end],
        )
        np.testing.assert_array_equal(prefix_timer[:-1], timer[: end - 1])
        np.testing.assert_array_equal(prefix_side[:-1], side[: end - 1])


def test_walljump_phase_seed_lane_rejects_generic_wall_hug_rows() -> None:
    # These are seed-lane guardrails, not gameplay admission tests:
    # - early common-air wall hugs remain sentinel until the hidden timer phase is reconstructable;
    # - floor/ledge-near rows and non-common-air rows do not get generic walljump authority;
    # - wrong-way or neutral stick rows leave runtime CollData as the owner.
    action = np.array([27, 27, 88, 27, 27, 27, 0], dtype=np.uint16)
    action_frame = np.array([7, 18, 18, 18, 20, 20, 20], dtype=np.int16)
    pos_x = np.array([87.0, 87.0, 87.0, 50.0, 87.0, -87.0, -87.0], dtype=np.float32)
    pos_y = np.array([-9.0, -2.0, -9.0, -9.0, -9.0, -9.0, -9.0], dtype=np.float32)
    raw_main_x = np.array([84, 84, 84, 84, 0, -84, 84], dtype=np.int8)

    timer, side = _derive_walljump_phase_seed_lanes(
        action_id_u16=action,
        action_frame_i16=action_frame,
        pos_x_f32=pos_x,
        pos_y_f32=pos_y,
        raw_main_x_i8=raw_main_x,
    )
    assert timer.tolist() == [254, 254, 254, 254, 254, 254, 254]
    assert side.tolist() == [0, 0, 0, 0, 0, 0, 0]
