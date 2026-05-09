from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SAMPLE_DTYPE, SEED_DTYPE
from tools.slippi.known_data_artifacts import (
    fountain_of_dreams_default_platform_heights,
    fountain_of_dreams_platform_motion_params,
)
from tools.slippi.make_dataset_from_slp import (
    _derive_jab_rapid_count_seed_lane,
    _derive_landing_fallspecial_allow_interrupt_seed_lane,
    _fod_platform_heights_from_frames,
    _fod_platform_heights_with_ground_contact,
    _fod_platform_motion_with_ground_contact,
    _derive_specialhi_rotate_model_seed_lane,
    _derive_walljump_phase_seed_lanes,
)
from tools.slippi.seed_history import derive_item_spawn_id_counter
from tests.test_hitboxes_pose import _read_hitbox_events


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
    # Item spawn-id global counter (`it_804D6D10` -> item->x1C).
    assert "item_spawn_id_counter" in SEED_DTYPE.fields
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
    assert "magnify_damage_counter_x1910" in SEED_DTYPE.fields
    assert "passivewall_timer" in SEED_DTYPE.fields
    assert "walljump_input_timer" in SEED_DTYPE.fields
    assert "walljump_wall_side_i8" in SEED_DTYPE.fields
    # Damage KB stacking window (fp->dmg.x18AC_time_since_hit).
    assert "damage_time_since_hit_x18ac" in SEED_DTYPE.fields
    # Rejected DamageAir2 source-step residue must not remain as a seed lane.
    assert "damage_aobj_pose_lag_u8" not in SEED_DTYPE.fields
    # Fighter phantom/tip-log delayed damage lane (`dmg.x1898` + x189C countdown + source).
    assert "phantom_damage_pending_x1898" in SEED_DTYPE.fields
    assert "phantom_damage_timer_x189c" in SEED_DTYPE.fields
    # Legacy name; stores local simulator slot or 0xFF, not raw Slippi source-port domain.
    assert "phantom_damage_source_port" in SEED_DTYPE.fields
    # Grounded attacker shield-pushback scalar (`fp->xF4_ground_attacker_shield_kb_vel`).
    assert "attacker_shield_ground_kb_vel" in SEED_DTYPE.fields
    assert "combat_shield_contact_hb_kind" in SEED_DTYPE.fields
    assert "combat_shield_hit_int_damage" in SEED_DTYPE.fields
    assert "combat_shield_damage_taken" in SEED_DTYPE.fields
    # ReboundStop queued xE8 ground-accel lane.
    assert "rebound_ground_accel_2_f32" in SEED_DTYPE.fields
    assert "rebound_anim_rate_f32" in SEED_DTYPE.fields
    assert "guard_reflect_origin_guardon_u8" in SEED_DTYPE.fields
    assert "guard_special_enable_timer_x1c" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_x_f32" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_y_f32" in SEED_DTYPE.fields
    assert "floor_sweep_prev_pos_valid_u8" in SEED_DTYPE.fields
    assert "ecb_lock_bottom_rel_y_f32" in SEED_DTYPE.fields
    assert "ecb_lock_bottom_rel_y_valid_u8" in SEED_DTYPE.fields
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
    # Firefox/Firebird hidden XRotN pose owner.
    assert "specialhi_rotate_model_f32" in SEED_DTYPE.fields
    assert "specialhi_rotate_model_valid_u8" in SEED_DTYPE.fields
    # Attack100 rapid-jab counter (`fp+0x1A54`) for mid-jab teacher-forced seeds.
    assert "jab_rapid_count" in SEED_DTYPE.fields
    # Repeated-hit combo push timer (`fp->x2092`) for grounded attacker drift after x2090 reaches x4C4.
    assert "combo_push_timer_x2092" in SEED_DTYPE.fields
    # FoD current platform heights are causal current-stage state for transformed platform floors.
    assert "stage_fod_platform_height_f32" in SEED_DTYPE.fields
    assert "stage_fod_platform_height_valid_u8" in SEED_DTYPE.fields
    assert "stage_fod_platform_velocity_f32" in SEED_DTYPE.fields
    assert "stage_fod_platform_velocity_valid_u8" in SEED_DTYPE.fields
    # Prefix-causal Yoshi Shy Guy dynamic-bone velocity scratch.
    assert "item_shyguy_prev_vel_y" in SEED_DTYPE.fields
    assert "item_shyguy_prev_vel_y_valid" in SEED_DTYPE.fields
    assert "item_shyguy_dyn_y_phase_u8" in SEED_DTYPE.fields
    assert "item_shyguy_dyn_y_phase_valid_u8" in SEED_DTYPE.fields
    assert "stage_yoshi_shyguy_timer_u16" in SEED_DTYPE.fields
    assert "stage_yoshi_shyguy_pattern_u8" in SEED_DTYPE.fields
    assert "stage_yoshi_shyguy_valid_u8" in SEED_DTYPE.fields
    assert "stage_yoshi_shyguy_spawn_rng_seed_u32" in SEED_DTYPE.fields
    assert "stage_yoshi_shyguy_spawn_rng_seed_valid_u8" in SEED_DTYPE.fields
    assert "stage_dream_whispy_wind_dir_u8" in SEED_DTYPE.fields
    assert "stage_dream_whispy_wind_valid_u8" in SEED_DTYPE.fields
    assert "cliff_ledge_floor_segment_id_u16" in SEED_DTYPE.fields
    assert "item_shyguy_speed_index_u8" in SEED_DTYPE.fields
    assert "item_shyguy_speed_index_valid_u8" in SEED_DTYPE.fields
    assert "item_shyguy_delay_u16" in SEED_DTYPE.fields
    assert "item_shyguy_delay_valid_u8" in SEED_DTYPE.fields
    assert "item_shyguy_hitlag_u8" in SEED_DTYPE.fields
    assert "item_shyguy_hitlag_valid_u8" in SEED_DTYPE.fields


def test_native_dream_whispy_wind_derivation_rejects_short_rows() -> None:
    import msl_binding

    rows = 2
    seed = np.zeros((rows, SEED_DTYPE.itemsize), dtype=np.uint8)
    prev_input = np.zeros((rows, INPUT_DTYPE.itemsize), dtype=np.uint8)
    input_t = np.zeros((rows, INPUT_DTYPE.itemsize), dtype=np.uint8)
    short_ref = np.zeros((rows, COMPARE_DTYPE.itemsize - 1), dtype=np.uint8)
    with pytest.raises(ValueError, match="short row stride"):
        msl_binding.derive_dream_whispy_wind_seed_lanes(
            seed,
            prev_input,
            input_t,
            short_ref,
            2,
            28,
            0.2,
            0.025,
        )


def test_dataset_dtype_sizes_match_c_structs() -> None:
    # If these drift, preprocessing/eval will fail with record_size mismatches.
    import msl_binding

    sizes = msl_binding.sizes()
    assert int(sizes["seed"]) == SEED_DTYPE.itemsize
    assert int(sizes["sample"]) == SAMPLE_DTYPE.itemsize


def test_removed_nonfd_bridge_seed_lanes_stay_absent() -> None:
    removed_lanes = (
        "deadupfall_pos_delta_x_f32",
        "deadupfall_pos_delta_y_f32",
        "deadupfall_pos_delta_valid_u8",
        "stage_platform_floor_y_f32",
        "stage_platform_floor_y_valid_u8",
        "stage_item_next_vel_valid",
        "stage_item_next_vel_x",
        "stage_item_next_vel_y",
        "stage_item_heiho_delay_x24",
    )
    for lane in removed_lanes:
        assert lane not in SEED_DTYPE.fields


class _FakeFrameType:
    def __init__(self, *, has_fod_platform: bool) -> None:
        self._has_fod_platform = has_fod_platform

    def get_field_index(self, name: str) -> int:
        return 0 if name == "fod_platform" and self._has_fod_platform else -1


class _FakeFrameField:
    def __init__(self, events: list[list[dict]]) -> None:
        self._events = events

    def to_pylist(self) -> list[list[dict]]:
        return self._events


class _FakeFrames:
    def __init__(self, events: list[list[dict]] | None) -> None:
        self.type = _FakeFrameType(has_fod_platform=events is not None)
        self._events = events or []

    def field(self, name: str) -> _FakeFrameField:
        assert name == "fod_platform"
        return _FakeFrameField(self._events)


def test_fod_platform_seed_defaults_are_stage_artifact_backed() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights, valid, fresh = _fod_platform_heights_from_frames(
        _FakeFrames(None),
        4,
        default_heights=defaults,
    )
    assert heights.shape == (4, 2)
    assert np.all(heights == np.asarray(defaults, dtype=np.float32).reshape(1, 2))
    assert np.all(valid == 0)
    assert np.all(fresh == 0)


def test_fod_platform_motion_params_are_stage_artifact_backed() -> None:
    params = fountain_of_dreams_platform_motion_params(Path("data"))
    assert params["home_height"] == pytest.approx(25.0)
    assert params["hidden_target_height"] == pytest.approx(-1.0)
    assert params["max_height"] == pytest.approx(35.0)
    assert params["min_visible_height"] == pytest.approx(15.0)
    assert params["up_speed"] == pytest.approx(0.15)
    assert params["down_speed"] == pytest.approx(0.1)


def test_fod_platform_seed_derivation_uses_no_future_events() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    events = [
        [],
        [{"platform": 1, "height": 18.5}],
        [],
        [{"platform": 0, "height": 25.25}],
    ]
    full_heights, full_valid, full_fresh = _fod_platform_heights_from_frames(
        _FakeFrames(events),
        len(events),
        default_heights=defaults,
    )
    prefix_heights, prefix_valid, prefix_fresh = _fod_platform_heights_from_frames(
        _FakeFrames(events[:3]),
        3,
        default_heights=defaults,
    )
    assert full_heights[:3].tolist() == prefix_heights.tolist()
    assert full_valid[:3].tolist() == prefix_valid.tolist()
    assert full_fresh[:3].tolist() == prefix_fresh.tolist()
    assert float(full_heights[1, 1]) == pytest.approx(18.5)
    assert int(full_valid[1, 1]) == 1
    assert int(full_fresh[1, 1]) == 1
    assert float(full_heights[2, 1]) == pytest.approx(18.5)
    assert int(full_fresh[2, 1]) == 0
    assert int(full_valid[2, 0]) == 0
    assert float(full_heights[3, 0]) == pytest.approx(25.25)
    assert int(full_valid[3, 0]) == 1
    assert int(full_fresh[3, 0]) == 1


def test_fod_platform_seed_derivation_uses_current_grounded_contact_prefix() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights = np.asarray([defaults, defaults, defaults], dtype=np.float32)
    valid = np.zeros((3, 2), dtype=np.uint8)
    line_transforms = {0: (1, 0.75, 1.125)}
    out_h, out_v = _fod_platform_heights_with_ground_contact(
        heights,
        valid,
        post_on_ground_u8=np.asarray([[0], [1], [0]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0xFFFF], [0], [0xFFFF]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[0.0], [25.9748], [0.0]], dtype=np.float32),
        line_transforms=line_transforms,
    )

    inferred = np.float32((25.9748 - 1.125) / 0.75)
    assert int(out_v[0, 1]) == 0
    assert int(out_v[1, 1]) == 1
    assert float(out_h[1, 1]) == pytest.approx(float(inferred))
    # The inferred current-stage owner is carried forward causally, but no earlier frame changes.
    assert int(out_v[2, 1]) == 1
    assert float(out_h[2, 1]) == pytest.approx(float(inferred))
    assert float(out_h[0, 1]) == pytest.approx(float(defaults[1]))


def test_fod_platform_seed_derivation_carries_prefix_velocity_without_future_contact() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights = np.asarray([defaults, defaults, defaults, defaults], dtype=np.float32)
    valid = np.zeros((4, 2), dtype=np.uint8)
    line_transforms = {0: (1, 0.75, 1.125)}
    y0 = np.float32(13.800076)
    y1 = np.float32(13.725076)
    out_h, out_v, out_vel, out_vel_v = _fod_platform_motion_with_ground_contact(
        heights,
        valid,
        post_on_ground_u8=np.asarray([[1], [1], [0], [0]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0], [0], [0xFFFF], [0xFFFF]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[y0], [y1], [0.0], [0.0]], dtype=np.float32),
        line_transforms=line_transforms,
    )

    h0 = np.float32((float(y0) - 1.125) / 0.75)
    h1 = np.float32((float(y1) - 1.125) / 0.75)
    vel = np.float32(h1 - h0)
    assert int(out_v[0, 1]) == 1
    assert int(out_vel_v[0, 1]) == 0
    assert int(out_vel_v[1, 1]) == 1
    assert float(out_vel[1, 1]) == pytest.approx(float(vel), abs=1e-6)
    assert float(out_h[2, 1]) == pytest.approx(float(np.float32(h1 + vel)), abs=1e-6)
    assert float(out_h[3, 1]) == pytest.approx(float(np.float32(h1 + vel + vel)), abs=1e-6)


def test_fod_platform_direct_events_override_contact_fallback_owner() -> None:
    # Direct Slippi FoD platform events are current grIzumi state. Grounded-contact fallback is
    # only a sparse/missing-event reconstruction and must not keep ownership once direct events
    # resume for the same platform.
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights = np.asarray(
        [
            defaults,
            defaults,
            [defaults[0], 31.0],
            [defaults[0], 31.15],
            [defaults[0], 31.15],
            [defaults[0], 31.15],
        ],
        dtype=np.float32,
    )
    valid = np.zeros((6, 2), dtype=np.uint8)
    valid[2:, 1] = np.uint8(1)
    fresh = np.zeros((6, 2), dtype=np.uint8)
    fresh[2, 1] = np.uint8(1)
    fresh[3, 1] = np.uint8(1)
    fresh[4, 1] = np.uint8(1)
    line_transforms = {0: (1, 0.75, 1.125)}
    contact_y = np.float32(24.651287  * 0.75 + 1.125)
    out_h, out_v, out_vel, out_vel_v = _fod_platform_motion_with_ground_contact(
        heights,
        valid,
        event_fresh_u8=fresh,
        post_on_ground_u8=np.asarray([[1], [0], [0], [0], [0], [0]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0], [0xFFFF], [0xFFFF], [0xFFFF], [0xFFFF], [0xFFFF]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[contact_y], [0.0], [0.0], [0.0], [0.0], [0.0]], dtype=np.float32),
        line_transforms=line_transforms,
    )

    assert int(out_v[0, 1]) == 1
    assert float(out_h[0, 1]) == pytest.approx(24.651287, abs=1e-5)
    assert float(out_h[2, 1]) == pytest.approx(31.0)
    assert float(out_h[3, 1]) == pytest.approx(31.15)
    # The repeated direct event at frame 4 is the source stop boundary; the carried value should not
    # keep the old event delta alive and drift after events stop.
    assert int(out_vel_v[4, 1]) == 0
    assert int(out_vel_v[5, 1]) == 0
    assert float(out_h[5, 1]) == pytest.approx(31.15)


def test_fod_platform_ground_contact_derivation_uses_no_future_observations() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights = np.asarray([defaults, defaults, defaults, defaults], dtype=np.float32)
    valid = np.zeros((4, 2), dtype=np.uint8)
    line_transforms = {0: (1, 0.75, 1.125)}
    full_h, full_v, full_vel, full_vel_v = _fod_platform_motion_with_ground_contact(
        heights,
        valid,
        post_on_ground_u8=np.asarray([[0], [1], [0], [1]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0xFFFF], [0], [0xFFFF], [0]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[0.0], [25.9748], [0.0], [24.9748]], dtype=np.float32),
        line_transforms=line_transforms,
    )
    prefix_h, prefix_v, prefix_vel, prefix_vel_v = _fod_platform_motion_with_ground_contact(
        heights[:3],
        valid[:3],
        post_on_ground_u8=np.asarray([[0], [1], [0]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0xFFFF], [0], [0xFFFF]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[0.0], [25.9748], [0.0]], dtype=np.float32),
        line_transforms=line_transforms,
    )

    assert full_h[:3].tolist() == prefix_h.tolist()
    assert full_v[:3].tolist() == prefix_v.tolist()
    assert full_vel[:3].tolist() == prefix_vel.tolist()
    assert full_vel_v[:3].tolist() == prefix_vel_v.tolist()


def test_fod_platform_ground_contact_without_transform_does_not_invent_velocity() -> None:
    defaults = fountain_of_dreams_default_platform_heights(Path("data"))
    heights = np.asarray([defaults, defaults], dtype=np.float32)
    valid = np.zeros((2, 2), dtype=np.uint8)
    out_h, out_v, out_vel, out_vel_v = _fod_platform_motion_with_ground_contact(
        heights,
        valid,
        post_on_ground_u8=np.asarray([[1], [1]], dtype=np.uint8),
        post_ground_id_u16=np.asarray([[0], [0]], dtype=np.uint16),
        post_pos_y_f32=np.asarray([[25.9748], [25.8748]], dtype=np.float32),
        line_transforms={},
    )

    assert np.all(out_h == heights)
    assert np.all(out_v == 0)
    assert np.all(out_vel == 0.0)
    assert np.all(out_vel_v == 0)


def test_item_common_data_exports_shield_bounce_threshold_source() -> None:
    # Item_80269DC8 uses 90 + it_804D6D28->unk_degrees; keep the runtime predicate data-backed by
    # the extracted ItCo.dat item common file instead of a naked gameplay constant.
    data = json.loads(Path("data/items/item_common.json").read_text())
    assert data["shield_bounce_extra_degrees"] == 45.0


def test_ft_common_data_exports_magnify_damage_source_constants() -> None:
    data = json.loads(Path("data/common/ft_common_data.json").read_text())
    assert int(data["magnify_damage_interval_frames"]) == 60
    assert int(data["magnify_damage_percent_limit"]) == 150
    assert int(data["magnify_damage_amount"]) == 1


def test_ft_common_data_exports_deadupfall_hitcamera_source_constants() -> None:
    # ftCo_DeadUpFall_Anim/Phys use p_ftCommonData x520 timers/vectors and x550/x554/x558/x55C
    # for the DeadUpFall -> HitCamera phase owner.
    data = json.loads(Path("data/common/ft_common_data.json").read_text())
    assert int(data["dead_up_fall_select_percent"]) == 16
    assert int(data["dead_up_fall_entry_hold_frames"]) == 1
    assert int(data["dead_up_fall_lerp_frames"]) == 50
    assert int(data["dead_up_fall_hitcamera_hold_frames"]) == 3
    assert int(data["dead_up_fall_phase3_frames"]) == 40
    assert int(data["dead_up_fall_phase4_frames"]) == 35
    assert float(data["dead_up_fall_lerp_start_y"]) == pytest.approx(66.66000366210938)
    assert float(data["dead_up_fall_lerp_end_y"]) == pytest.approx(-15.0)
    assert float(data["dead_up_fall_initial_self_vel_y"]) == pytest.approx(1.0)
    assert float(data["dead_up_fall_phase3_gravity"]) == pytest.approx(0.2)
    assert float(data["dead_up_fall_phase3_terminal_vel"]) == pytest.approx(1.7)
    assert float(data["dead_up_fall_initial_self_vel_z"]) == pytest.approx(-1.0)
    assert float(data["dead_up_fall_ice_rot_speed"]) == pytest.approx(-0.17453292)


def test_ft_common_data_exports_soft_platform_callback_threshold() -> None:
    # ftCo_80096CC8 uses p_ftCommonData->x25C to admit/reject platform lines for common-air style
    # collision callbacks. Keep the runtime gate source-data-backed rather than a local constant.
    data = json.loads(Path("data/common/ft_common_data.json").read_text())
    assert float(data["platform_air_land_stick_y_threshold"]) == pytest.approx(-0.5600000023841858)


def test_ft_common_data_exports_basic_landing_wait_velocity_threshold_constants() -> None:
    # ft_80082B1C and ftCo_AirCatchHit_Coll compare self_vel.y against ftCo_800D0EC8(fp).
    # That threshold is computed from Fighter_804D6524->x30 and p_ftCommonData->x310, so keep both
    # inputs extracted rather than duplicated as local gameplay constants.
    data = json.loads(Path("data/common/ft_common_data.json").read_text())
    assert float(data["basic_landing_wait_gravity_mult_x30"]) == pytest.approx(0.7649999856948853)
    assert float(data["basic_landing_wait_scale_param_x310"]) == pytest.approx(1.0)


def test_item_spawn_id_counter_survives_itemless_gaps() -> None:
    exists = np.array(
        [
            [0, 0, 0],
            [1, 0, 0],
            [0, 0, 0],
            [0, 1, 0],
            [0, 0, 0],
        ],
        dtype=np.uint8,
    )
    spawn_id = np.array(
        [
            [0, 0, 0],
            [0, 0, 0],
            [0, 0, 0],
            [0, 7, 0],
            [0, 0, 0],
        ],
        dtype=np.uint32,
    )
    got = derive_item_spawn_id_counter(item_exists_u8_2d=exists, item_spawn_id_u32_2d=spawn_id)
    assert got.tolist() == [0, 1, 1, 8, 8]


def test_landing_fallspecial_allow_interrupt_lane_is_prefix_causal() -> None:
    # Action ids: FallSpecial=35, LandingFallSpecial=43, EscapeAir=236.
    # EscapeAir-owned freefall enters LandingFallSpecial with allow_interrupt=false, while
    # non-EscapeAir FallSpecial sources use the common true-carry path.
    action = np.array([236, 35, 35, 43, 43, 14, 358, 35, 43, 14, 236, 43], dtype=np.uint16)
    got = _derive_landing_fallspecial_allow_interrupt_seed_lane(action_id_u16=action)
    assert got.tolist() == [0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0]

    extended = np.concatenate([action, np.array([35, 43, 43], dtype=np.uint16)])
    got_extended = _derive_landing_fallspecial_allow_interrupt_seed_lane(action_id_u16=extended)
    np.testing.assert_array_equal(got_extended[: action.shape[0]], got)


def test_character_attrs_include_ordered_walljump_setup_threshold() -> None:
    root = Path(__file__).resolve().parents[1]
    for character in ("fox", "falco"):
        data = json.loads((root / "data" / "characters" / f"{character}.json").read_text())
        keys = list(data.keys())
        assert data["rapid_jab_window"] == 4
        assert data["walljump_setup_x_delta_threshold"] == 0.5
        assert keys.index("rebound_anim_numerator_frames") < keys.index("rapid_jab_window")
        assert keys.index("rapid_jab_window") < keys.index("wall_jump_horizontal_velocity")
        assert keys.index("wall_jump_vertical_velocity") < keys.index("walljump_setup_x_delta_threshold")
        assert keys.index("walljump_setup_x_delta_threshold") < keys.index("camera_zoom_target_bone_part_id")


def test_move_data_exports_attack100_loop_checkpoint() -> None:
    root = Path(__file__).resolve().parents[1]
    for character in ("fox", "falco"):
        data = json.loads((root / "data" / "moves" / f"{character}.json").read_text())
        events = data["moves"]["ftCo_SM_Attack100Loop"]["events"]
        assert any(
            ev.get("kind") == "set_throw_flags" and ev.get("data", {}).get("hit_idx") == 0
            for ev in events
        )


def test_runtime_hitbox_tables_export_attack100_loop_hitboxes() -> None:
    # Attack100Loop hitboxes are consumed from generated MSLHITB1 tables at runtime. This guard
    # catches stale derived tables after move-script extraction changes.
    root = Path(__file__).resolve().parents[1]
    for character in ("fox", "falco"):
        path = root / "data" / "hitboxes" / f"{character}.bin"
        assert path.exists(), f"missing generated runtime hitbox table: {path}"
        events = _read_hitbox_events(path)[50]
        assert any(int(ev["kind"]) == 0 and int(ev["frame"]) == 2 for ev in events)
        assert any(int(ev["kind"]) == 0 and int(ev["frame"]) == 9 for ev in events)
        assert any(int(ev["kind"]) == 0 and int(ev["frame"]) == 16 for ev in events)
        assert any(int(ev["kind"]) == 1 and int(ev["hitbox_id"]) == 0xFF for ev in events)


def test_jab_rapid_count_seed_lane_resets_on_attack11_entry_and_counts_iasa_frames() -> None:
    # Action ids: Wait=14, Attack11=44, Attack12=45, Attack100Start=47.
    action = np.array([14, 44, 44, 44, 45, 45, 47, 14, 44, 44], dtype=np.uint16)
    held = np.array([0, 0x100, 0x100, 0, 0x100, 0, 0, 0, 0x100, 0], dtype=np.uint16)
    prev_held = np.concatenate(([np.uint16(0)], held[:-1]))
    released = prev_held & ~held
    pressed = np.array([0, 0x100, 0, 0, 0x100, 0, 0, 0, 0x100, 0], dtype=np.uint16)

    got = _derive_jab_rapid_count_seed_lane(
        action_id_u16=action,
        buttons_released_u16=released,
        buttons_pressed_u16=pressed,
        button_mask_a=0x100,
    )

    assert got.tolist() == [0, 0, 0, 1, 2, 3, 0, 0, 0, 1]


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


def test_specialhi_rotate_model_seed_lane_persists_and_recomputes_same_facing_wall_contact() -> None:
    # The hidden mv.fx.SpecialHi.rotateModel lane persists through Fall/Landing/Bound because those
    # callbacks do not rewrite FtPart_XRotN. A same-facing wall contact in SpecialAirHi still
    # recomputes the stored angle from current self_vel; it is not just a facing-change signal.
    stage_segments = [
        {"i": 11, "kind": "left_wall", "x0": -85.5656967, "y0": -10.0, "x1": -85.5656967, "y1": 0.0}
    ]
    action = np.array([0x0164, 0x0164, 0x0164, 0x0166, 0x0165, 0x0167, 0x001D], dtype=np.uint16)
    facing = np.ones(action.shape, dtype=np.uint8)
    pos_x = np.array([0.0, 0.0, -86.0, -86.0, -86.0, -86.0, -86.0], dtype=np.float32)
    pos_y = np.array([-5.0, -5.0, -5.0, -5.0, -5.0, -5.0, -5.0], dtype=np.float32)
    vx = np.array([1.0, 3.0, 3.0, 0.5, 0.5, 0.5, 0.5], dtype=np.float32)
    vy = np.array([1.0, 0.0, 0.0, 2.0, 2.0, 2.0, 2.0], dtype=np.float32)

    angle, valid = _derive_specialhi_rotate_model_seed_lane(
        action_id_u16=action,
        facing_u8=facing,
        pos_x_f32=pos_x,
        pos_y_f32=pos_y,
        speed_air_x_self_f32=vx,
        speed_y_self_f32=vy,
        stage_id_u32=32,
        stage_segments=stage_segments,
        act_fx_special_hi=0x0163,
        act_fx_special_air_hi=0x0164,
        act_fx_special_hi_landing=0x0165,
        act_fx_special_hi_fall=0x0166,
        act_fx_special_hi_bound=0x0167,
    )

    assert valid.tolist() == [1, 1, 1, 1, 1, 1, 0]
    assert float(angle[0]) == pytest.approx(float(np.arctan2(np.float32(1.0), np.float32(1.0))))
    assert float(angle[1]) == pytest.approx(float(angle[0]))
    assert float(angle[2]) == pytest.approx(0.0)
    assert float(angle[3]) == pytest.approx(0.0)
    assert float(angle[4]) == pytest.approx(0.0)
    assert float(angle[5]) == pytest.approx(0.0)
