from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.extraction.extract_fighter_hitboxes import FORMAT_VERSION as HITBOX_VERSION

from tools.slippi.combat_history import (
    _localize_last_hit_by_for_native,
    derive_combat_hitlist_seed_fields,
)
from tools.slippi.anim_timebase import EndFrameTables, derive_frame_speed_mul_f32
from tools.slippi.seed_history import (
    compute_fighter_button_timers,
    compute_fighter_stick_input_counters,
    compute_fighter_trigger_input_counters,
    compute_lr_press_timer_x67f,
    derive_instance_id_counter,
    compute_press_timer_u8,
    compute_tilt_timer_axis,
    compute_tilt_timer_y_pre_post_with_fall_fast,
    compute_x672_trigger_timer_pre_post,
    derive_colanim_internals,
    derive_camera_box_visible_x221f_b0,
    derive_camera_target_point_inside_stage_cam_bounds,
    derive_camera_target_world,
    derive_capture_mash_buttons_pressed,
    derive_capture_grab_hidden_post,
    derive_magnify_damage_counter_x1910,
    derive_rebirth_camera_anchor_y,
    derive_damage_meteor_cancel_x1a,
    derive_damage_post_hitlag_cb_kind,
    derive_guard_reflect_origin_guardon,
    derive_guard_reflect_timer_x14,
    derive_guard_reflect_timer_x18,
    derive_guard_release_lockout_and_lightshield,
    derive_guard_special_enable_timer_x1c,
    derive_guard_setoff_hitlag_damage_min,
    derive_guard_setoff_hitlag_exit_phase,
    derive_guard_setoff_post_hitlag_owner,
    derive_ecb_lock_bottom_rel_y,
    derive_ucf_pad_buffer_state,
    derive_kneebend_internals,
    derive_turn_internals,
)
from tools.slippi.validation_buffer_items import derive_illusion_ghost_pos01, derive_illusion_ghost_pos012
from tools.slippi.validation_buffer_seed import _derive_attackdash_x0_seed_lane
from tools.slippi.validation_buffer_seed import _derive_mpcoll_wall_seed_lanes
from tools.slippi.validation_buffer_seed import _derive_passivewall_timer
from tools.slippi.validation_buffer_seed import _derive_walljump_used_seed_lanes
from tools.slippi.validation_buffer_stage import _derive_grounded_overlap_hidden_pos_z
from tests.replay_buffers_loader import load_replay_buffers


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


def test_derive_instance_id_counter_running_max_seed_bridge() -> None:
    # Seed bridge contract:
    # - Counter follows running max across fighter+item instance ids (strictly causal).
    # - It does not drop when current-frame live ids drop.
    # - It skips 0 on wrap (plAttack_80037B08 shape).
    fighter_iid = np.array(
        [
            [0, 0, 0, 0],
            [10, 0, 0, 0],
            [10, 0, 0, 0],
            [0, 0, 0, 0],
            [12, 0, 0, 0],
            [0, 0, 0, 0],
            [65535, 0, 0, 0],
            [0, 0, 0, 0],
        ],
        dtype=np.uint16,
    )
    item_iid = np.array(
        [
            [0, 0, 0],
            [0, 0, 0],
            [11, 0, 0],
            [0, 0, 0],
            [0, 0, 0],
            [0, 0, 0],
            [0, 0, 0],
            [0, 0, 0],
        ],
        dtype=np.uint16,
    )

    got = derive_instance_id_counter(
        fighter_instance_id_u16_2d=fighter_iid,
        item_instance_id_u16_2d=item_iid,
    )
    assert got.dtype == np.uint16
    assert got.tolist() == [1, 11, 12, 12, 13, 13, 1, 1]


@pytest.mark.parametrize(
    ("char_id", "action_frame", "should_carry"),
    [
        (22, 8, True),  # Falco AttackAirN same-group refresh: create frame 4 -> create frame 8.
        (18, 15, False),  # Marth AttackAirN clears at frame 8 before the frame-15 create band.
    ],
)
def test_shield_contact_seed_bridge_respects_attackairn_script_lifetime(
    char_id: int, action_frame: int, should_carry: bool
) -> None:
    # The one-step bridge may seed a replay-proven shield victim into later same-group AttackAirN
    # rows only when MSLFTSC1 says the HitCapsule lifetime was not cleared/recreated.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80078C70}
    # data/scripts/{falco,marth}.bin::ftCo_SM_AttackAirN create_hitbox/clear_hitboxes
    msl_binding = pytest.importorskip("msl_binding")

    n = 4
    players = 2
    hb_count = 4
    attacker = 0
    defender = 1
    act_attackairn = np.uint16(65)
    act_guard = np.uint16(179)
    act_guard_setoff = np.uint16(181)

    shield_contact = np.zeros((n, players, hb_count, players), dtype=np.uint8)
    hitlist_valid = np.zeros((n, players, hb_count), dtype=np.uint8)
    hitlist_cd = np.zeros((n, players, hb_count, players), dtype=np.uint16)
    hitlist_iid = np.zeros((n, players, hb_count, players), dtype=np.uint16)
    action = np.zeros((n, players), dtype=np.uint16)
    hitlag = np.zeros((n, players), dtype=np.uint16)
    instance_id = np.zeros((n, players), dtype=np.uint16)
    shield = np.full((n, players), 60.0, dtype=np.float32)
    lightshield = np.zeros((n, players), dtype=np.float32)
    animation_index = np.zeros((n, players), dtype=np.uint32)
    state_age = np.zeros((n, players), dtype=np.int16)
    char = np.zeros((n, players), dtype=np.uint8)
    attack_id = np.zeros((n, players), dtype=np.uint16)
    stale_queue = np.zeros((n, players), dtype=np.uint8)
    stale_move_id = np.zeros((n, players, 10), dtype=np.uint16)
    active_shield_hit_lut = np.zeros((256, 256, 64), dtype=np.uint16)
    stale_weights = np.ones(10, dtype=np.float32)
    guard_lut = np.zeros(65536, dtype=np.uint8)
    attack_lut = np.zeros(65536, dtype=np.uint8)
    same_frame_lut = np.zeros(65536, dtype=np.uint8)
    same_frame_special_lut = np.zeros(65536, dtype=np.uint8)

    action[:, attacker] = act_attackairn
    action[:, defender] = act_guard
    action[1, defender] = act_guard_setoff
    hitlag[1, attacker] = 3
    hitlag[1, defender] = 3
    instance_id[:, defender] = np.array([10, 11, 12, 12], dtype=np.uint16)
    char[:, attacker] = np.uint8(char_id)
    state_age[2, attacker] = np.int16(action_frame)
    shield_contact[0, attacker, :, defender] = np.uint8(2)
    shield_contact[2, attacker, :, defender] = np.uint8(1)
    guard_lut[int(act_guard)] = np.uint8(1)
    guard_lut[int(act_guard_setoff)] = np.uint8(1)
    attack_lut[int(act_attackairn)] = np.uint8(1)

    msl_binding.derive_shield_contact_seed_bridge(
        shield_contact,
        hitlist_valid,
        hitlist_cd,
        hitlist_iid,
        action,
        hitlag,
        instance_id,
        shield,
        lightshield,
        animation_index,
        state_age,
        char,
        attack_id,
        stale_queue,
        stale_move_id,
        active_shield_hit_lut,
        stale_weights,
        guard_lut,
        attack_lut,
        same_frame_lut,
        same_frame_special_lut,
        players,
        int(act_guard_setoff),
        1.0,
        0.0,
        1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
    )

    expected_valid = 1 if should_carry else 0
    expected_cd = 0xFFFF if should_carry else 0
    expected_iid = int(instance_id[2, defender]) if should_carry else 0
    assert int(hitlist_valid[2, attacker, 0]) == expected_valid
    assert int(hitlist_cd[2, attacker, 0, defender]) == expected_cd
    assert int(hitlist_iid[2, attacker, 0, defender]) == expected_iid


def test_derive_ecb_lock_bottom_rel_y_preserves_desired_bottom_during_lock() -> None:
    # CollData_X130_Locked preserves desired_ecb.bottom instead of resampling EscapeAir's current
    # pose or forcing root-y. The native producer carries the last non-locked desired bottom through
    # the lock window using extracted data/ecb tables.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    char = np.array([1, 1, 1, 1], dtype=np.uint8)
    action = np.array([0x001D, 0x001B, 0x00EC, 0x00EC], dtype=np.uint16)
    anim = np.array([20, 27, 44, 44], dtype=np.uint32)  # Fall -> JumpAerialF -> EscapeAir
    anim_frame = np.array([5.0, 6.0, 1.0, 2.0], dtype=np.float32)
    on_ground = np.array([0, 0, 0, 0], dtype=np.uint8)
    lock = np.array([0, 9, 8, 7], dtype=np.uint8)

    bottom, owner = derive_ecb_lock_bottom_rel_y(
        char_id_u8=char,
        action_id_u16=action,
        animation_index_u32=anim,
        anim_frame_f32=anim_frame,
        on_ground_u8=on_ground,
        ecb_lock_timer_u8=lock,
        ecb_lock_owner_u8=np.array([0, 1, 1, 1], dtype=np.uint8),
    )

    assert bottom.dtype == np.float32
    assert owner.dtype == np.uint8
    assert owner.tolist() == [0, 1, 1, 1]
    import msl_binding

    preserved = float(msl_binding.ecb_bottom_rel_y(1, 20, 5))
    assert float(bottom[1]) == pytest.approx(preserved, abs=1e-6)
    assert float(bottom[2]) == pytest.approx(preserved, abs=1e-6)
    assert float(bottom[3]) == pytest.approx(preserved, abs=1e-6)


def test_derive_ecb_lock_bottom_rel_y_clears_on_grounded_rows() -> None:
    char = np.array([1, 1, 1], dtype=np.uint8)
    action = np.array([0x001B, 0x001B, 0x00EC], dtype=np.uint16)
    anim = np.array([44, 44, 44], dtype=np.uint32)
    anim_frame = np.array([5.0, 6.0, 7.0], dtype=np.float32)
    on_ground = np.array([0, 1, 0], dtype=np.uint8)
    lock = np.array([0, 9, 8], dtype=np.uint8)

    bottom, owner = derive_ecb_lock_bottom_rel_y(
        char_id_u8=char,
        action_id_u16=action,
        animation_index_u32=anim,
        anim_frame_f32=anim_frame,
        on_ground_u8=on_ground,
        ecb_lock_timer_u8=lock,
        ecb_lock_owner_u8=np.array([0, 1, 1], dtype=np.uint8),
    )

    assert owner.tolist() == [0, 0, 0]
    assert float(bottom[2]) == pytest.approx(0.0)


def test_derive_ecb_lock_bottom_rel_y_carries_falcon_alt_helper_owner() -> None:
    # ftCommon_8007D60C sets the same CollData_X130_Locked bit as the ten-frame helper, so aerial
    # Raptor must preserve the pre-entry desired bottom through its five-frame lock.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
    char = np.array([2, 2, 2, 2], dtype=np.uint8)
    action = np.array([0x001D, 0x015F, 0x015F, 0x002B], dtype=np.uint16)
    anim = np.array([20, 305, 305, 36], dtype=np.uint32)
    anim_frame = np.array([5.0, 0.0, 1.0, 0.0], dtype=np.float32)
    on_ground = np.zeros((4,), dtype=np.uint8)
    lock = np.array([0, 4, 3, 4], dtype=np.uint8)

    bottom, owner = derive_ecb_lock_bottom_rel_y(
        char_id_u8=char,
        action_id_u16=action,
        animation_index_u32=anim,
        anim_frame_f32=anim_frame,
        on_ground_u8=on_ground,
        ecb_lock_timer_u8=lock,
        ecb_lock_owner_u8=np.array([0, 4, 4, 4], dtype=np.uint8),
    )

    import msl_binding

    preserved = float(msl_binding.ecb_bottom_rel_y(2, 20, 5))
    assert owner.tolist() == [0, 4, 4, 4]
    assert np.allclose(bottom[1:], np.float32(preserved), atol=1e-6)


def test_derive_ecb_lock_bottom_rel_y_keeps_seeded_owner_for_special_damage_output() -> None:
    bottom, owner = derive_ecb_lock_bottom_rel_y(
        char_id_u8=np.full((3,), 2, dtype=np.uint8),
        action_id_u16=np.array([0x0165, 0x0058, 0x0058], dtype=np.uint16),
        animation_index_u32=np.array([311, 20, 20], dtype=np.uint32),
        anim_frame_f32=np.array([5.0, 0.0, 1.0], dtype=np.float32),
        on_ground_u8=np.array([1, 0, 0], dtype=np.uint8),
        ecb_lock_timer_u8=np.array([0, 10, 9], dtype=np.uint8),
        ecb_lock_owner_u8=np.array([0, 1, 1], dtype=np.uint8),
    )

    assert owner.tolist() == [0, 1, 1]
    assert bottom.tolist() == pytest.approx([0.0, 0.0, 0.0], abs=1e-6)


def test_derive_instance_id_counter_prefix_invariant() -> None:
    fighter_iid = np.array(
        [
            [0, 0, 0, 0],
            [2, 0, 0, 0],
            [2, 0, 0, 0],
            [4, 0, 0, 0],
            [4, 0, 0, 0],
            [7, 0, 0, 0],
        ],
        dtype=np.uint16,
    )
    item_iid = np.array(
        [
            [0, 0],
            [0, 0],
            [3, 0],
            [0, 0],
            [5, 0],
            [0, 0],
        ],
        dtype=np.uint16,
    )

    full = derive_instance_id_counter(
        fighter_instance_id_u16_2d=fighter_iid,
        item_instance_id_u16_2d=item_iid,
    )
    for k in (1, 2, 3, 4, int(fighter_iid.shape[0])):
        got = derive_instance_id_counter(
            fighter_instance_id_u16_2d=fighter_iid[:k],
            item_instance_id_u16_2d=item_iid[:k],
        )
        assert np.array_equal(got, full[:k])


def test_guard_reflect_origin_guardon_tracks_entry_path_prefix_causal() -> None:
    # Guard/GuardOn -> GuardReflect uses ftCo_8009388C and carries GuardOn-origin provenance.
    # Direct locomotion -> GuardReflect uses ftCo_80093A50 and must not set the same lane.
    act_run = np.uint16(40)
    act_guard_on = np.uint16(178)
    act_guard = np.uint16(179)
    act_guard_reflect = np.uint16(182)
    action = np.array(
        [
            act_run,
            act_guard_reflect,
            act_guard_reflect,
            act_run,
            act_guard_on,
            act_guard_reflect,
            act_guard_reflect,
            act_guard,
            act_guard_reflect,
        ],
        dtype=np.uint16,
    )

    full = derive_guard_reflect_origin_guardon(
        action_id_u16=action,
        act_guard_reflect=int(act_guard_reflect),
        act_guard_on=int(act_guard_on),
        act_guard=int(act_guard),
    )
    assert full.tolist() == [0, 0, 0, 0, 0, 1, 1, 0, 1]

    for end in range(1, action.shape[0] + 1):
        got = derive_guard_reflect_origin_guardon(
            action_id_u16=action[:end],
            act_guard_reflect=int(act_guard_reflect),
            act_guard_on=int(act_guard_on),
            act_guard=int(act_guard),
        )
        assert np.array_equal(got, full[:end])


def test_derive_illusion_ghost_pos01_tracks_decomp_ring_order() -> None:
    # Decomp owner:
    # - ftFox_SpecialS_SetVars initializes ghostEffectPos[0..3] = cur_pos on main entry.
    # - ftFox_SpecialS_SetPhys advances the ring as ghost1 = ghost0; ghost0 = cur_pos during
    #   main/end Phys callbacks.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFox_SpecialS_SetVars,ftFox_SpecialS_SetPhys}
    act_ground_start = 347
    act_ground_main = 348
    act_ground_end = 349
    act_wait = 14

    action = np.array([[act_ground_start], [act_ground_main], [act_ground_main], [act_ground_end], [act_wait]], dtype=np.uint16)
    action_frame = np.array([[15], [0], [1], [0], [0]], dtype=np.int16)
    pos_x = np.array([[71.44235], [71.44235], [54.94235], [21.94235], [20.54235]], dtype=np.float32)
    pos_y = np.array([[0.0001], [0.0001], [0.0001], [0.0001], [0.0001]], dtype=np.float32)

    ghost0_x, ghost0_y, ghost1_x, ghost1_y = derive_illusion_ghost_pos01(
        post_action_id_u16=action,
        post_action_frame_i16=action_frame,
        post_pos_x=pos_x,
        post_pos_y=pos_y,
    )

    assert ghost0_x[:, 0].tolist() == pytest.approx(
        [71.44235, 71.44235, 54.94235, 21.94235, 21.94235], abs=5e-6
    )
    assert ghost1_x[:, 0].tolist() == pytest.approx(
        [71.44235, 71.44235, 71.44235, 54.94235, 54.94235], abs=5e-6
    )
    assert ghost0_y[:, 0].tolist() == pytest.approx([0.0001, 0.0001, 0.0001, 0.0001, 0.0001], abs=5e-6)
    assert ghost1_y[:, 0].tolist() == pytest.approx([0.0001, 0.0001, 0.0001, 0.0001, 0.0001], abs=5e-6)


def test_derive_illusion_ghost_pos012_tracks_previous_hitcapsule_endpoint() -> None:
    # Decomp owner:
    # - ftFox_SpecialS_SetPhys advances ghostEffectPos[2] from the previous ghostEffectPos[1].
    # - Illusion item collision keeps the previous HitCapsule endpoint in x58, so the seed lane
    #   needs ghostEffectPos[2] for replay rows that seed after Phys but before the next BODY
    #   contact has been applied.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetPhys
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    act_ground_start = 347
    act_ground_main = 348
    act_ground_end = 349

    action = np.array([[act_ground_start], [act_ground_main], [act_ground_main], [act_ground_end]], dtype=np.uint16)
    action_frame = np.array([[15], [0], [1], [0]], dtype=np.int16)
    pos_x = np.array([[100.0], [100.0], [80.0], [60.0]], dtype=np.float32)
    pos_y = np.array([[5.0], [5.0], [5.0], [5.0]], dtype=np.float32)

    ghost0_x, _ghost0_y, ghost1_x, _ghost1_y, ghost2_x, ghost2_y = derive_illusion_ghost_pos012(
        post_action_id_u16=action,
        post_action_frame_i16=action_frame,
        post_pos_x=pos_x,
        post_pos_y=pos_y,
    )

    assert ghost0_x[:, 0].tolist() == pytest.approx([100.0, 100.0, 80.0, 60.0], abs=5e-6)
    assert ghost1_x[:, 0].tolist() == pytest.approx([100.0, 100.0, 100.0, 80.0], abs=5e-6)
    assert ghost2_x[:, 0].tolist() == pytest.approx([100.0, 100.0, 100.0, 100.0], abs=5e-6)
    assert ghost2_y[:, 0].tolist() == pytest.approx([5.0, 5.0, 5.0, 5.0], abs=5e-6)


def test_derive_passivewall_timer_tracks_hidden_startup_hold() -> None:
    common = {"passivewall_timer_frames": 5}
    action = np.array([88, 203, 203, 203, 203, 203, 203, 203], dtype=np.uint16)
    action_frame = np.array([12, 0, 0, 0, 0, 0, 1, 2], dtype=np.int16)

    got = _derive_passivewall_timer(
        action_id_u16=action,
        action_frame_i16=action_frame,
        common=common,
    )

    assert got.dtype == np.uint8
    assert got.tolist() == [0, 5, 4, 3, 2, 1, 0, 0]


def test_derive_passivewall_timer_preserves_latch_and_detects_proven_reentry() -> None:
    action = np.array([88, 202, 202, 202, 202, 202, 203, 203, 203], dtype=np.uint16)
    action_frame = np.array([8, 0, 0, 0, 0, 0, 0, 1, 0], dtype=np.int16)

    got = _derive_passivewall_timer(
        action_id_u16=action,
        action_frame_i16=action_frame,
        common={"passivewall_timer_frames": 5},
    )

    # PassiveWall_Anim's inlineA0 preserves the current animation frame when it latches into
    # PassiveWallJump, so the 202 -> 203 row reaches timer zero. A later frame reset to zero proves
    # a fresh ftCo_800C1E64 entry and restarts the timer.
    assert got.tolist() == [0, 5, 4, 3, 2, 1, 0, 0, 5]


def _derive_walljump_test_lanes(
    action: list[int] | np.ndarray,
    *,
    action_frame: list[int] | np.ndarray | None = None,
    on_ground: list[int] | np.ndarray | None = None,
    jumps_left: list[int] | np.ndarray | None = None,
    buttons_pressed: list[int] | np.ndarray | None = None,
    stick_y: list[float] | np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    action_arr = np.asarray(action, dtype=np.uint16)
    n = len(action_arr)
    max_jumps_lut = np.zeros(256, dtype=np.uint8)
    max_jumps_lut[1] = np.uint8(2)
    return _derive_walljump_used_seed_lanes(
        char_id_u8=np.ones(n, dtype=np.uint8),
        action_id_u16=action_arr,
        action_frame_i16=np.asarray(action_frame if action_frame is not None else np.zeros(n), dtype=np.int16),
        on_ground_u8=np.asarray(on_ground if on_ground is not None else np.zeros(n), dtype=np.uint8),
        jumps_left_u8=np.asarray(jumps_left if jumps_left is not None else np.full(n, 2), dtype=np.uint8),
        max_jumps_lut_u8=max_jumps_lut,
        buttons_pressed_u16=np.asarray(buttons_pressed if buttons_pressed is not None else np.zeros(n), dtype=np.uint16),
        stick_y_f32=np.asarray(stick_y if stick_y is not None else np.zeros(n), dtype=np.float32),
        button_mask_xy=0x0C00,
        tap_jump_threshold=0.6625,
    )


@pytest.mark.parametrize("producer", [38, 204, 218, 229, 244, 250, 251, 261, 263])
def test_derive_walljump_used_lanes_cover_every_common_ordinary_producer(producer: int) -> None:
    used, exponent = _derive_walljump_test_lanes([producer, 203], action_frame=[4, 0])
    assert used.tolist() == [0, 1]
    assert exponent.tolist() == [0, 0]


@pytest.mark.parametrize("producer", [88, 91, 185, 247])
def test_derive_walljump_used_lanes_keep_walltech_producers_unscaled(producer: int) -> None:
    used, exponent = _derive_walljump_test_lanes([29, 203, 29, producer, 203], action_frame=[4, 0, 4, 4, 0])
    assert used.tolist() == [0, 1, 1, 1, 1]
    assert exponent.tolist() == [0, 0, 0, 0, 0]


def test_derive_walljump_used_lanes_separate_passivewall_latch_from_proven_reentry() -> None:
    used, exponent = _derive_walljump_test_lanes(
        [88, 202, 203, 203, 203],
        action_frame=[6, 0, 0, 2, 0],
    )

    # DamageFly -> PassiveWall is a wall tech. The frame-preserving 202 -> 203 transition is
    # inlineA0's latch, while 203 frame 2 -> frame 0 proves a new ordinary walljump produced by
    # PassiveWall_Coll itself.
    assert used.tolist() == [0, 0, 0, 0, 1]
    assert exponent.tolist() == [0, 0, 0, 0, 0]


def test_derive_walljump_used_lanes_reset_before_same_frame_grounded_processhit() -> None:
    action = [29, 203, 29, 203, 29, 88, 29, 203]
    action_frame = [4, 0, 4, 0, 4, 1, 4, 0]
    jumps_left = [2, 2, 2, 2, 2, 1, 1, 1]
    used, exponent = _derive_walljump_test_lanes(
        action,
        action_frame=action_frame,
        jumps_left=jumps_left,
    )

    # The airborne DamageFly entry carries ftCommon_8007D5D4's jumpsUsed=1 signature after a
    # collision-phase ftCommon_8007D6A4 reset. The next ordinary walljump therefore starts at zero.
    assert used.tolist() == [0, 1, 1, 2, 2, 0, 0, 1]
    assert exponent.tolist() == [0, 0, 0, 1, 0, 0, 0, 0]

    for end in range(1, len(action) + 1):
        prefix_used, prefix_exponent = _derive_walljump_test_lanes(
            action[:end],
            action_frame=action_frame[:end],
            jumps_left=jumps_left[:end],
        )
        assert int(prefix_used[-1]) == int(used[end - 1])
        assert int(prefix_exponent[-1]) == int(exponent[end - 1])


def test_derive_walljump_used_lanes_do_not_reset_for_airborne_damage_without_source_edge() -> None:
    used, exponent = _derive_walljump_test_lanes(
        [29, 203, 29, 203, 29, 88, 29, 203],
        action_frame=[4, 0, 4, 0, 4, 1, 4, 0],
        jumps_left=[1] * 8,
    )
    assert int(used[-1]) == 3
    assert int(exponent[-1]) == 2


@pytest.mark.parametrize(
    ("buttons_pressed", "stick_y"),
    [([0, 0, 0, 0, 0, 0x0400, 0, 0], [0.0] * 8), ([0] * 8, [0.0] * 5 + [0.8, 0.0, 0.0])],
)
def test_derive_walljump_used_lanes_do_not_misclassify_airjump_then_hit_as_ground_reset(
    buttons_pressed: list[int],
    stick_y: list[float],
) -> None:
    used, exponent = _derive_walljump_test_lanes(
        [29, 203, 29, 203, 29, 88, 29, 203],
        action_frame=[4, 0, 4, 0, 4, 1, 4, 0],
        jumps_left=[2, 2, 2, 2, 2, 1, 1, 1],
        buttons_pressed=buttons_pressed,
        stick_y=stick_y,
    )
    assert int(used[-1]) == 3
    assert int(exponent[-1]) == 2


def test_derive_walljump_used_lanes_reset_on_visible_ground_and_rebirth() -> None:
    action = [29, 203, 29, 29, 203, 29, 12, 29, 203]
    ground = [0, 0, 1, 0, 0, 0, 0, 0, 0]
    used, exponent = _derive_walljump_test_lanes(action, on_ground=ground)
    assert used.tolist() == [0, 1, 0, 0, 1, 1, 0, 0, 1]
    assert exponent.tolist() == [0] * len(action)


def test_derive_walljump_used_lane_saturates_without_wrapping() -> None:
    ordinary_entries = 257
    action = np.empty(ordinary_entries * 2, dtype=np.uint16)
    action[0::2] = np.uint16(29)
    action[1::2] = np.uint16(203)

    used, exponent = _derive_walljump_test_lanes(action)

    assert int(used[-1]) == 255
    assert int(exponent[-1]) == 255


def test_derive_mpcoll_wall_seed_lanes_are_fd_segment_and_phase_scoped() -> None:
    # Teacher-forced DamageFlyTop wall callback seed:
    # - DCC 4809 is visibly near the left vertical wall but has no vanilla wall callback yet.
    # - DCC 4810 has reached FD left lower wall segment 13 and enters PassiveWall.
    # - HIS 1788 carries a right-wall index but does not enter PassiveWallJump yet.
    # - HIS 1789 reaches right lower wall segment 9 and enters PassiveWallJump.
    # The lane is prefix-causal from replay-visible position/action/hitstun plus extracted FD
    # collision segments; non-DamageFlyTop actions remain unseeded even at the same coordinates.
    # refs/melee/src/melee/mp/mplib.c::{mpLib_8004E398_LeftWall,mpLib_8004E684_RightWall}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    import json

    root = Path(__file__).resolve().parents[1]
    stage_segments = json.loads((root / "data/stages/final_destination.json").read_text())["segments"]
    action = np.array([90, 90, 90, 90, 88], dtype=np.uint16)
    action_frame = np.array([44, 45, 18, 19, 45], dtype=np.int16)
    hitlag = np.zeros(5, dtype=np.uint16)
    hitstun = np.array([4, 3, 46, 45, 3], dtype=np.uint16)
    pos_x = np.array([-87.799850, -88.956474, 88.924614, 88.382698, -88.956474], dtype=np.float32)
    pos_y = np.array([-9.026546, -10.818257, -10.997328, -9.983515, -10.818257], dtype=np.float32)

    kind, wall_id = _derive_mpcoll_wall_seed_lanes(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        pos_x_f32=pos_x,
        pos_y_f32=pos_y,
        stage_id_u32=32,
        stage_segments=stage_segments,
    )

    assert kind.tolist() == [0, 1, 2, 2, 0]
    assert wall_id.tolist() == [0xFFFF, 13, 10, 9, 0xFFFF]


def test_derive_grounded_overlap_hidden_pos_z_tracks_prefix_depth_lane() -> None:
    # ftCommon_8007DD7C/8007E0E4 hidden depth lane:
    # - Two grounded overlapping fighters at visible z=0 accumulate opposite +/-x454 steps.
    # - The lane is prefix-causal and clamps by x458 without consulting future combat outcomes.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    n = 6
    char_id = np.zeros((n, 4), dtype=np.uint8)
    char_id[:, 0] = 1
    char_id[:, 1] = 22
    action = np.full((n, 4), 14, dtype=np.uint16)
    on_ground = np.zeros((n, 4), dtype=np.uint8)
    on_ground[:, :2] = 1
    stocks = np.zeros((n, 4), dtype=np.uint8)
    stocks[:, :2] = 4
    pos_x = np.zeros((n, 4), dtype=np.float32)
    pos_x[:, 0] = 0.0
    pos_x[:, 1] = 0.1
    pos_z = np.zeros((n, 4), dtype=np.float32)
    facing = np.ones((n, 4), dtype=np.uint8)

    out = _derive_grounded_overlap_hidden_pos_z(
        num_players=2,
        char_id_u8=char_id,
        action_id_u16=action,
        on_ground_u8=on_ground,
        stocks_u8=stocks,
        pos_x_f32=pos_x,
        pos_z_f32=pos_z,
        facing_u8=facing,
        common={"player_nudge_z": 0.1, "player_nudge_z_max": 0.3},
    )

    assert out[:, 0].tolist() == pytest.approx([0.0, -0.1, -0.2, -0.3, -0.3, -0.3], abs=1e-6)
    assert out[:, 1].tolist() == pytest.approx([0.0, 0.1, 0.2, 0.3, 0.3, 0.3], abs=1e-6)


def test_derive_grounded_overlap_hidden_pos_z_does_not_leak_outside_grounded_scope() -> None:
    # The teacher-forced hidden-depth seed is not allowed to carry stale depth through owner
    # surfaces that are not proven action-local for this lane. Grounded DamageHi/N/Lw still runs
    # Fighter_procUpdate's ftCommon_8007E0E4 depth pass, but airborne DamageAir/Fly, GuardSetOff,
    # DownBound, Cliff, throw, and special rows keep replay-visible Slippi pos_z.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    n = 10
    char_id = np.zeros((n, 4), dtype=np.uint8)
    char_id[:, 0] = 1
    char_id[:, 1] = 22
    action = np.full((n, 4), 14, dtype=np.uint16)
    action[4, 0] = 80  # DamageN3, grounded damage still participates in ftCommon_8007E0E4.
    action[5, 0] = 181  # GuardSetOff
    action[6, 0] = 191  # DownBoundD
    action[7, 0] = 253  # CliffWait
    action[8, 0] = 219  # ThrowF
    action[9, 0] = 360  # Fox SpecialLw loop
    on_ground = np.zeros((n, 4), dtype=np.uint8)
    on_ground[:, :2] = 1
    on_ground[2, 0] = 0
    stocks = np.zeros((n, 4), dtype=np.uint8)
    stocks[:, :2] = 4
    pos_x = np.zeros((n, 4), dtype=np.float32)
    pos_x[:, 0] = 0.0
    pos_x[:, 1] = 0.1
    pos_z = np.zeros((n, 4), dtype=np.float32)
    facing = np.ones((n, 4), dtype=np.uint8)

    out = _derive_grounded_overlap_hidden_pos_z(
        num_players=2,
        char_id_u8=char_id,
        action_id_u16=action,
        on_ground_u8=on_ground,
        stocks_u8=stocks,
        pos_x_f32=pos_x,
        pos_z_f32=pos_z,
        facing_u8=facing,
        common={"player_nudge_z": 0.1, "player_nudge_z_max": 0.3},
    )

    assert out[0, 0] == pytest.approx(0.0)
    assert out[1, 0] == pytest.approx(-0.1)
    assert out[2:5, 0].tolist() == pytest.approx([0.0, 0.0, -0.1], abs=1e-6)
    assert out[5:, 0].tolist() == pytest.approx([0.0, 0.0, 0.0, 0.0, 0.0], abs=1e-6)


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
    action_frame_prefix = np.array([0, 1, 1, 2, 3, 0], dtype=np.int16)

    f0, h0, x0 = derive_turn_internals(
        action_id=a_prefix,
        action_frame_i16=action_frame_prefix,
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
    action_frame_ext = np.concatenate([action_frame_prefix, np.array([1, 1, 2, 0], dtype=np.int16)])

    f1, h1, x1 = derive_turn_internals(
        action_id=a_ext,
        action_frame_i16=action_frame_ext,
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
    assert np.array_equal(x0, x1[: x0.size])


def test_derive_kneebend_internals_is_causal_wrt_future_frames() -> None:
    import json
    from pathlib import Path

    act_wait = 0x000E
    act_turn_run = 0x0013
    act_dash = 0x0014
    act_run = 0x0015
    act_run_direct = 0x0016
    act_run_brake = 0x0017
    act_kneebend = 0x0018
    button_x = 0x0400
    button_xy = 0x0400 | 0x0800

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    tap_jump_threshold = float(common["tap_jump_threshold"])
    dash_run_jump_stick_y_threshold = float(common["dash_run_jump_stick_y_threshold"])
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
        dash_run_jump_stick_y_threshold=dash_run_jump_stick_y_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        act_dash=act_dash,
        act_run=act_run,
        act_run_direct=act_run_direct,
        act_run_brake=act_run_brake,
        act_turn_run=act_turn_run,
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
        dash_run_jump_stick_y_threshold=dash_run_jump_stick_y_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        act_dash=act_dash,
        act_run=act_run,
        act_run_direct=act_run_direct,
        act_run_brake=act_run_brake,
        act_turn_run=act_turn_run,
        button_mask_xy=button_xy,
    )

    assert np.array_equal(j0, j1[: j0.size])
    assert np.array_equal(s0, s1[: s0.size])


def test_derive_kneebend_internals_uses_dash_run_jump_threshold_on_source_entry() -> None:
    import json
    from pathlib import Path

    act_wait = 0x000E
    act_turn_run = 0x0013
    act_dash = 0x0014
    act_run = 0x0015
    act_run_direct = 0x0016
    act_run_brake = 0x0017
    act_kneebend = 0x0018
    button_xy = 0x0400 | 0x0800

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    tap_jump_threshold = float(common["tap_jump_threshold"])
    dash_run_jump_stick_y_threshold = float(common["dash_run_jump_stick_y_threshold"])
    tap_jump_release_threshold = float(common["tap_jump_release_threshold"])
    tap_jump_tilt_max_frames = int(common["tap_jump_tilt_max_frames"])

    mid_stick_jump = np.float32((tap_jump_threshold + dash_run_jump_stick_y_threshold) * 0.5)
    actions = np.array([act_wait, act_dash, act_kneebend, act_kneebend, act_kneebend], dtype=np.uint16)
    buttons = np.zeros(actions.size, dtype=np.uint16)
    buttons_pressed = np.zeros(actions.size, dtype=np.uint16)
    stick_y = np.array([0.0, 0.0, mid_stick_jump, 0.0, 0.0], dtype=np.float32)
    cstick_y = np.zeros(actions.size, dtype=np.float32)
    tilt_timer_y = np.zeros(actions.size, dtype=np.uint8)

    jump_input, short_hop = derive_kneebend_internals(
        action_id=actions,
        buttons=buttons,
        buttons_pressed=buttons_pressed,
        stick_y_unit=stick_y,
        cstick_y_unit=cstick_y,
        tilt_timer_y=tilt_timer_y,
        tap_jump_threshold=tap_jump_threshold,
        dash_run_jump_stick_y_threshold=dash_run_jump_stick_y_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        act_dash=act_dash,
        act_run=act_run,
        act_run_direct=act_run_direct,
        act_run_brake=act_run_brake,
        act_turn_run=act_turn_run,
        button_mask_xy=button_xy,
    )
    assert jump_input.tolist() == [0, 0, 1, 1, 1]
    assert short_hop.tolist() == [0, 0, 0, 1, 1]

    actions[1] = np.uint16(act_wait)
    jump_input, short_hop = derive_kneebend_internals(
        action_id=actions,
        buttons=buttons,
        buttons_pressed=buttons_pressed,
        stick_y_unit=stick_y,
        cstick_y_unit=cstick_y,
        tilt_timer_y=tilt_timer_y,
        tap_jump_threshold=tap_jump_threshold,
        dash_run_jump_stick_y_threshold=dash_run_jump_stick_y_threshold,
        tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
        tap_jump_release_threshold=tap_jump_release_threshold,
        act_kneebend=act_kneebend,
        act_dash=act_dash,
        act_run=act_run,
        act_run_direct=act_run_direct,
        act_run_brake=act_run_brake,
        act_turn_run=act_turn_run,
        button_mask_xy=button_xy,
    )
    assert jump_input.tolist() == [0, 0, 0, 0, 0]
    assert short_hop.tolist() == [0, 0, 0, 0, 0]


def test_derive_frame_speed_mul_is_prefix_invariant() -> None:
    # LandingAirF entry should use the decomp formula (not future deltas), and the derivation must
    # be prefix-invariant (strictly causal).
    #
    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_attack_air_f = np.uint16(0x0042)
    act_landing_air_f = np.uint16(0x0047)

    # Construct a prefix where frame 1 enters LandingAirF and state_age resets.
    state_age_prefix = np.array([5.0, -1.0, 0.8272727], dtype=np.float32)
    action_id_prefix = np.array([act_attack_air_f, act_landing_air_f, act_landing_air_f], dtype=np.uint16)
    hitlag_prefix = np.zeros(state_age_prefix.shape[0], dtype=np.uint16)
    char_id_prefix = np.full(state_age_prefix.shape[0], 1, dtype=np.uint8)  # Fox
    animation_index_prefix = np.array([100, 10, 10], dtype=np.uint32)
    lr_press_timer_prefix = np.array([0xFF, 0, 1], dtype=np.uint8)

    end_frames = EndFrameTables(by_char_id={1: {10: 20.0}})
    char_lags = {1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}}

    out0 = derive_frame_speed_mul_f32(
        state_age_f32=state_age_prefix,
        action_id=action_id_prefix,
        hitlag=hitlag_prefix,
        char_id=char_id_prefix,
        animation_index=animation_index_prefix,
        lr_press_timer=lr_press_timer_prefix,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames=char_lags,
    )

    # Extend with arbitrary future frames that should not affect prefix outputs.
    state_age_ext = np.concatenate([state_age_prefix, np.array([0.0, 0.0, 0.0], dtype=np.float32)])
    action_id_ext = np.concatenate([action_id_prefix, np.array([0x000E, 0x000E, 0x000E], dtype=np.uint16)])
    hitlag_ext = np.concatenate([hitlag_prefix, np.array([0, 0, 0], dtype=np.uint16)])
    char_id_ext = np.concatenate([char_id_prefix, np.array([1, 1, 1], dtype=np.uint8)])
    animation_index_ext = np.concatenate([animation_index_prefix, np.array([0, 0, 0], dtype=np.uint32)])
    lr_press_timer_ext = np.concatenate([lr_press_timer_prefix, np.array([0xFF, 0xFF, 0xFF], dtype=np.uint8)])

    out1 = derive_frame_speed_mul_f32(
        state_age_f32=state_age_ext,
        action_id=action_id_ext,
        hitlag=hitlag_ext,
        char_id=char_id_ext,
        animation_index=animation_index_ext,
        lr_press_timer=lr_press_timer_ext,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames=char_lags,
    )

    assert np.allclose(out0, out1[: out0.size])


def test_derive_frame_speed_mul_landing_fallspecial_origin_specific_lag() -> None:
    # LandingFallSpecial entry speed is source-owned:
    # - EscapeAir_Coll passes common x344 landing lag.
    # - Illusion/Phantasm SpecialAirSEnd uses da->x50.
    # - Firefox/Firebird rebound/fall uses da->x90.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    act_escape_air = np.uint16(0x00EC)
    act_special_air_s_end = np.uint16(0x0160)
    act_special_hi_fall = np.uint16(0x0166)
    act_fall_special = np.uint16(0x0023)
    act_landing_fall_special = np.uint16(0x002B)
    sm_landing_fall_special = np.uint32(36)

    action_id = np.array(
        [
            act_escape_air,
            act_landing_fall_special,
            act_special_air_s_end,
            act_landing_fall_special,
            act_special_hi_fall,
            act_fall_special,
            act_landing_fall_special,
        ],
        dtype=np.uint16,
    )
    state_age = np.array([1.0, -1.0, 20.0, -1.0, 3.0, 4.0, -1.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    char_id = np.full(action_id.shape[0], 1, dtype=np.uint8)
    animation_index = np.array([44, 36, 350, 36, 356, 26, 36], dtype=np.uint32)
    lr_press_timer = np.full(action_id.shape[0], 0xFF, dtype=np.uint8)
    end_frames = EndFrameTables(by_char_id={1: {int(sm_landing_fall_special): 30.0}})

    got = derive_frame_speed_mul_f32(
        state_age_f32=state_age,
        action_id=action_id,
        hitlag=hitlag,
        char_id=char_id,
        animation_index=animation_index,
        lr_press_timer=lr_press_timer,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames={1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}},
        char_fallspecial_origin_lag={
            # Resolved origin->lag map (fox): illusion end row + the firefox freefall rows,
            # as validation buffer builder now derives from the owners fx_special_kind lane.
            1: {0x0160: 20.0, 0x0164: 18.0, 0x0166: 18.0, 0x0167: 18.0}
        },
    )

    assert float(got[1]) == pytest.approx(3.01, abs=1e-6)
    assert float(got[3]) == pytest.approx(1.505, abs=1e-6)
    assert float(got[6]) == pytest.approx(1.6722223, abs=1e-6)

    extended = np.concatenate([action_id, np.array([0x000E, 0x000E], dtype=np.uint16)])
    got_extended = derive_frame_speed_mul_f32(
        state_age_f32=np.concatenate([state_age, np.array([0.0, 1.0], dtype=np.float32)]),
        action_id=extended,
        hitlag=np.zeros(extended.shape[0], dtype=np.uint16),
        char_id=np.full(extended.shape[0], 1, dtype=np.uint8),
        animation_index=np.concatenate([animation_index, np.array([0, 0], dtype=np.uint32)]),
        lr_press_timer=np.full(extended.shape[0], 0xFF, dtype=np.uint8),
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames={1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}},
        char_fallspecial_origin_lag={
            # Resolved origin->lag map (fox): illusion end row + the firefox freefall rows,
            # as validation buffer builder now derives from the owners fx_special_kind lane.
            1: {0x0160: 20.0, 0x0164: 18.0, 0x0166: 18.0, 0x0167: 18.0}
        },
    )
    assert np.allclose(got, got_extended[: got.size])


def test_derive_guard_setoff_hitlag_damage_min_carries_entry_damage_across_segment() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)
    action_id = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_wait],
        dtype=np.uint16,
    )
    action_frame = np.array([-1, 0, 0, 0, 2, -1], dtype=np.int16)
    hitlag = np.array([0, 6, 5, 1, 0, 0], dtype=np.uint16)

    out = derive_guard_setoff_hitlag_damage_min(
        action_id=action_id,
        action_frame_i16=action_frame,
        hitlag=hitlag,
        hitlag_dmg_mul=1.0 / 3.0,
        hitlag_base=3.0,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out.tolist() == [0, 9, 9, 9, 9, 0]


def test_derive_guard_setoff_hitlag_damage_min_is_prefix_invariant() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)

    action_id_prefix = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off],
        dtype=np.uint16,
    )
    action_frame_prefix = np.array([-1, 0, 0, 0], dtype=np.int16)
    hitlag_prefix = np.array([0, 3, 2, 1], dtype=np.uint16)

    out0 = derive_guard_setoff_hitlag_damage_min(
        action_id=action_id_prefix,
        action_frame_i16=action_frame_prefix,
        hitlag=hitlag_prefix,
        hitlag_dmg_mul=1.0 / 3.0,
        hitlag_base=3.0,
        act_guard_set_off=int(act_guard_set_off),
    )

    action_id_ext = np.concatenate([action_id_prefix, np.array([act_guard_set_off, act_wait], dtype=np.uint16)])
    action_frame_ext = np.concatenate([action_frame_prefix, np.array([6, -1], dtype=np.int16)])
    hitlag_ext = np.concatenate([hitlag_prefix, np.array([0, 0], dtype=np.uint16)])

    out1 = derive_guard_setoff_hitlag_damage_min(
        action_id=action_id_ext,
        action_frame_i16=action_frame_ext,
        hitlag=hitlag_ext,
        hitlag_dmg_mul=1.0 / 3.0,
        hitlag_base=3.0,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out0.tolist() == [0, 1, 1, 1]
    assert out1[: out0.size].tolist() == out0.tolist()


def test_derive_guard_setoff_hitlag_exit_phase_marks_last_hitlag_and_first_post_hitlag() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)
    action_id = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_wait],
        dtype=np.uint16,
    )
    hitlag = np.array([0, 3, 2, 1, 0, 0], dtype=np.uint16)

    out = derive_guard_setoff_hitlag_exit_phase(
        action_id=action_id,
        hitlag=hitlag,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out.tolist() == [0, 1, 1, 2, 3, 0]


def test_derive_guard_setoff_hitlag_exit_phase_is_prefix_invariant() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)

    action_id_prefix = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_guard_set_off],
        dtype=np.uint16,
    )
    hitlag_prefix = np.array([0, 4, 2, 1, 0], dtype=np.uint16)

    out0 = derive_guard_setoff_hitlag_exit_phase(
        action_id=action_id_prefix,
        hitlag=hitlag_prefix,
        act_guard_set_off=int(act_guard_set_off),
    )

    action_id_ext = np.concatenate([action_id_prefix, np.array([act_guard_set_off, act_wait], dtype=np.uint16)])
    hitlag_ext = np.concatenate([hitlag_prefix, np.array([0, 0], dtype=np.uint16)])

    out1 = derive_guard_setoff_hitlag_exit_phase(
        action_id=action_id_ext,
        hitlag=hitlag_ext,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out0.tolist() == [0, 1, 1, 2, 3]
    assert out1[: out0.size].tolist() == out0.tolist()


def test_derive_guard_setoff_post_hitlag_owner_marks_normal_and_powershield_handoffs() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)
    action_id = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_wait],
        dtype=np.uint16,
    )
    phase = np.array([0, 1, 2, 3, 0, 0], dtype=np.uint8)
    flags_221c_normal = np.array([0x00, 0x00, 0x00, 0x00, 0x00, 0x00], dtype=np.uint8)
    flags_221c_ps = np.array([0x00, 0x60, 0x60, 0x20, 0x20, 0x00], dtype=np.uint8)

    out_normal = derive_guard_setoff_post_hitlag_owner(
        action_id=action_id,
        guard_setoff_hitlag_exit_phase_u8=phase,
        state_flags_221c_u8=flags_221c_normal,
        act_guard_set_off=int(act_guard_set_off),
    )
    out_ps = derive_guard_setoff_post_hitlag_owner(
        action_id=action_id,
        guard_setoff_hitlag_exit_phase_u8=phase,
        state_flags_221c_u8=flags_221c_ps,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out_normal.tolist() == [0, 0, 1, 1, 0, 0]
    assert out_ps.tolist() == [0, 0, 2, 2, 0, 0]


def test_derive_guard_setoff_post_hitlag_owner_is_prefix_invariant() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)
    action_id_prefix = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off, act_guard_set_off],
        dtype=np.uint16,
    )
    phase_prefix = np.array([0, 1, 2, 3, 0], dtype=np.uint8)
    flags_221c_prefix = np.array([0x00, 0x60, 0x60, 0x20, 0x20], dtype=np.uint8)

    out0 = derive_guard_setoff_post_hitlag_owner(
        action_id=action_id_prefix,
        guard_setoff_hitlag_exit_phase_u8=phase_prefix,
        state_flags_221c_u8=flags_221c_prefix,
        act_guard_set_off=int(act_guard_set_off),
    )

    action_id_ext = np.concatenate([action_id_prefix, np.array([act_wait], dtype=np.uint16)])
    phase_ext = np.concatenate([phase_prefix, np.array([0], dtype=np.uint8)])
    flags_221c_ext = np.concatenate([flags_221c_prefix, np.array([0x00], dtype=np.uint8)])

    out1 = derive_guard_setoff_post_hitlag_owner(
        action_id=action_id_ext,
        guard_setoff_hitlag_exit_phase_u8=phase_ext,
        state_flags_221c_u8=flags_221c_ext,
        act_guard_set_off=int(act_guard_set_off),
    )

    assert out0.tolist() == [0, 0, 2, 2, 0]
    assert out1[: out0.size].tolist() == out0.tolist()


def test_derive_guard_release_lightshield_persists_through_guard_set_off() -> None:
    # GuardSetOff entry uses the already-latched fp->lightshield_amount and does not reset it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060

    action_id = np.array(
        [act_wait, act_guard_on, act_guard_on, act_guard_set_off, act_guard_set_off, act_wait],
        dtype=np.uint16,
    )
    shield_hp = np.array([60.0, 60.0, 59.0, 55.0, 55.0, 55.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    trigger = np.ones(action_id.shape[0], dtype=np.float32)
    trigger[-1] = np.float32(0.0)
    buttons_held = np.zeros(action_id.shape[0], dtype=np.uint16)
    x_c, x10, light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    # GuardSetOff snapshots preserve the lockout lanes; the GuardSetOff -> Guard path does not
    # call ftCo_800921DC (no xC/x10 reinit on this transition).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_800921DC}
    assert x_c.tolist() == [0, 0, 0, 0, 0, 0]
    assert x10.tolist() == [0, 7, 6, 6, 6, 0]
    assert light.tolist() == [0.0, 1.0, 1.0, 1.0, 1.0, 0.0]


def test_derive_guard_release_lockout_guard_setoff_to_guard_carries_lanes() -> None:
    # GuardSetOff -> Guard snapshots must carry xC/x10 (no ftCo_800921DC reinit on this path).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_800921DC}
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060

    action_id = np.array(
        [act_wait, act_guard_on, act_guard_on, act_guard_set_off, act_guard_set_off, act_guard, act_guard, act_wait],
        dtype=np.uint16,
    )
    shield_hp = np.array([60.0, 60.0, 59.0, 55.0, 55.0, 55.0, 55.0, 55.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    trigger = np.array([0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0], dtype=np.float32)
    buttons_held = np.zeros(action_id.shape[0], dtype=np.uint16)
    x_c, x10, light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    assert x_c.tolist() == [0, 0, 0, 0, 0, 1, 1, 0]
    assert x10.tolist() == [0, 7, 6, 6, 6, 5, 4, 0]
    assert light.tolist() == [0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0]


def test_derive_guard_release_lockout_direct_setoff_seeds_current_trigger_lightshield() -> None:
    # Direct non-guard -> GuardSetOff snapshots are missing the preceding shield-hit owner at the
    # replay surface. ftCo_80092F2C consumes the already-current lightshield_amount from the
    # collision frame. These arrays are post-frame indexed, so seed it from the direct GuardSetOff
    # row's x650 trigger lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_800925A4,ftCo_80092F2C}
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_attack_air_n = 0x004F
    button_mask_lr = 0x0060

    action_id = np.array(
        [act_attack_air_n, act_guard_set_off, act_guard_set_off, act_guard],
        dtype=np.uint16,
    )
    shield_hp = np.array([56.9, 46.2, 46.2, 46.2], dtype=np.float32)
    hitlag = np.array([0, 7, 6, 0], dtype=np.uint16)
    trigger = np.array([0.0, 0.32156864, 1.0, 0.0], dtype=np.float32)
    buttons_held = np.zeros(action_id.shape[0], dtype=np.uint16)

    x_c, x10, light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    expected_light = (np.float32(0.32156864) - np.float32(0.3)) / np.float32(0.7)
    assert x_c.tolist() == [0, 0, 0, 0]
    assert x10.tolist() == [0, 8, 8, 8]
    assert abs(float(light[1]) - float(expected_light)) < 1e-6
    assert abs(float(light[2]) - float(expected_light)) < 1e-6
    assert abs(float(light[3]) - float(expected_light)) < 1e-6


def test_derive_guard_release_lockout_reinitializes_on_non_setoff_snapshot_guard_entry_bridge() -> None:
    # Snapshot bridge lane: direct Guard entry without a GuardSetOff predecessor should reseed.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_800928CC}
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060

    action_id = np.array([act_wait, act_wait, act_guard, act_guard, act_wait], dtype=np.uint16)
    shield_hp = np.array([60.0, 60.0, 60.0, 60.0, 60.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    trigger = np.array([0.0, 0.0, 0.0, 0.0, 0.0], dtype=np.float32)
    buttons_held = np.zeros(action_id.shape[0], dtype=np.uint16)
    x_c, x10, _light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    assert x_c.tolist() == [0, 0, 1, 1, 0]
    assert x10.tolist() == [0, 0, 7, 6, 0]


def test_derive_guard_special_enable_timer_x1c_arms_on_powershield_setoff() -> None:
    # ftColl_80076CBC -> ftCo_80094138 arms mv.co.guard.x1C from p_ftCommonData->x2B8 on the
    # powershield-active shield-hit branch. GuardOff preserves that timer for its IASA gate.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80094138,ftCo_GuardOff_IASA}
    act_wait = 0x000E
    act_guard = 0x00B3
    act_guard_off = 0x00B4
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6

    action_id = np.array(
        [act_wait, act_guard_reflect, act_guard_set_off, act_guard_off, act_wait],
        dtype=np.uint16,
    )
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    state_flags = np.zeros((action_id.shape[0], 5), dtype=np.uint8)
    state_flags[2, 3] = np.uint8(0x20)

    out = derive_guard_special_enable_timer_x1c(
        action_id=action_id,
        hitlag=hitlag,
        state_flags_u8=state_flags,
        guard_special_enable_frames=4,
        act_guard_on=0x00B2,
        act_guard=act_guard,
        act_guard_off=act_guard_off,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    assert out.tolist() == [0, 0, 4, 4, 0]


def test_derive_guard_special_enable_timer_x1c_decrements_only_on_guard_continue() -> None:
    # inlineC0 decrements x1C when GuardOn/Guard/GuardReflect continue shielding, but exits to
    # GuardOff before decrementing. Hitlag freezes the callback owner.
    act_guard = 0x00B3
    act_guard_off = 0x00B4
    act_guard_set_off = 0x00B5
    action_id = np.array(
        [act_guard_set_off, act_guard, act_guard, act_guard, act_guard_off],
        dtype=np.uint16,
    )
    hitlag = np.array([0, 0, 2, 0, 0], dtype=np.uint16)
    state_flags = np.zeros((action_id.shape[0], 5), dtype=np.uint8)
    state_flags[0, 3] = np.uint8(0x20)

    out = derive_guard_special_enable_timer_x1c(
        action_id=action_id,
        hitlag=hitlag,
        state_flags_u8=state_flags,
        guard_special_enable_frames=4,
        act_guard_on=0x00B2,
        act_guard=act_guard,
        act_guard_off=act_guard_off,
        act_guard_reflect=0x00B6,
        act_guard_set_off=act_guard_set_off,
    )

    assert out.tolist() == [4, 3, 2, 2, 2]


def test_derive_guard_release_lockout_uses_buttons_held_lr_proxy() -> None:
    # ftCo_80092BCC uses held_inputs & HSD_PAD_LR; keep xC unlatch while LR buttons are held even
    # when analog trigger is below deadzone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060

    action_id = np.array([act_wait, act_guard_on, act_guard, act_guard, act_wait], dtype=np.uint16)
    shield_hp = np.array([60.0, 60.0, 60.0, 60.0, 60.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    trigger = np.zeros(action_id.shape[0], dtype=np.float32)
    buttons_held = np.array([0, button_mask_lr, button_mask_lr, 0, 0], dtype=np.uint16)

    x_c, x10, _light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    assert x_c.tolist() == [0, 0, 0, 1, 0]
    assert x10.tolist() == [0, 7, 6, 5, 0]


def test_derive_guard_release_lockout_uses_z_mapped_lr_proxy() -> None:
    # Fighter input synthesis maps Z into the same held LR lane that ftCo_80092BCC checks. A
    # Z-held no-submotion Guard snapshot must not seed a fake release latch.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060
    button_mask_z = 0x0010

    action_id = np.array([act_wait, act_guard_on, act_guard, act_guard, act_wait], dtype=np.uint16)
    shield_hp = np.array([60.0, 60.0, 60.0, 60.0, 60.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    trigger = np.zeros(action_id.shape[0], dtype=np.float32)
    buttons_held = np.array([0, button_mask_z, button_mask_z, 0, 0], dtype=np.uint16)

    x_c, x10, _light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=button_mask_z,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    assert x_c.tolist() == [0, 0, 0, 1, 0]
    assert x10.tolist() == [0, 7, 6, 5, 0]


def test_derive_guard_release_lockout_analog_only_hold_falls_back_to_trigger() -> None:
    # Decomp ownership uses held_inputs & HSD_PAD_LR; dataset seeds may carry analog-only trigger
    # holds (no digital L/R held bits). In that lane, fallback to trigger deadzone should keep
    # guard hold semantics stable.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_wait = 0x000E
    button_mask_lr = 0x0060

    action_id = np.array([act_wait, act_guard_on, act_guard, act_guard, act_wait], dtype=np.uint16)
    shield_hp = np.array([60.0, 60.0, 60.0, 60.0, 60.0], dtype=np.float32)
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)
    # Analog-only hold (trigger>=deadzone while digital L/R bits are clear) on the two guard frames.
    trigger = np.array([0.0, 1.0, 1.0, 0.0, 0.0], dtype=np.float32)
    buttons_held = np.zeros(action_id.shape[0], dtype=np.uint16)

    x_c, x10, _light = derive_guard_release_lockout_and_lightshield(
        action_id=action_id,
        shield_hp=shield_hp,
        hitlag=hitlag,
        buttons_held=buttons_held,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
        act_guard_set_off=act_guard_set_off,
    )

    # Hold remains active through frame 2; release latches once trigger drops on frame 3.
    assert x_c.tolist() == [0, 0, 0, 1, 0]
    assert x10.tolist() == [0, 7, 6, 5, 0]


def test_derive_guard_release_lockout_is_prefix_invariant() -> None:
    act_guard_on = np.uint16(0x00B2)
    act_guard = np.uint16(0x00B3)
    act_guard_set_off = np.uint16(0x00B5)
    act_guard_reflect = np.uint16(0x00B6)
    act_wait = np.uint16(0x000E)
    button_mask_lr = 0x0060

    action_id_prefix = np.array(
        [act_wait, act_guard_on, act_guard_on, act_guard_set_off, act_guard_set_off, act_guard, act_guard],
        dtype=np.uint16,
    )
    shield_hp_prefix = np.array([60.0, 60.0, 59.0, 55.0, 55.0, 55.0, 55.0], dtype=np.float32)
    hitlag_prefix = np.zeros(action_id_prefix.shape[0], dtype=np.uint16)
    trigger_prefix = np.array([0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0], dtype=np.float32)
    buttons_prefix = np.zeros(action_id_prefix.shape[0], dtype=np.uint16)
    x_c0, x100, light0 = derive_guard_release_lockout_and_lightshield(
        action_id=action_id_prefix,
        shield_hp=shield_hp_prefix,
        hitlag=hitlag_prefix,
        buttons_held=buttons_prefix,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger_prefix,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=int(act_guard_on),
        act_guard=int(act_guard),
        act_guard_reflect=int(act_guard_reflect),
        act_guard_set_off=int(act_guard_set_off),
    )

    action_id_ext = np.concatenate([action_id_prefix, np.array([act_wait, act_wait, act_wait], dtype=np.uint16)])
    shield_hp_ext = np.concatenate([shield_hp_prefix, np.array([55.0, 55.0, 55.0], dtype=np.float32)])
    hitlag_ext = np.concatenate([hitlag_prefix, np.array([0, 0, 0], dtype=np.uint16)])
    trigger_ext = np.concatenate([trigger_prefix, np.array([0.0, 0.0, 0.0], dtype=np.float32)])
    buttons_ext = np.concatenate([buttons_prefix, np.array([0, 0, 0], dtype=np.uint16)])
    x_c1, x101, light1 = derive_guard_release_lockout_and_lightshield(
        action_id=action_id_ext,
        shield_hp=shield_hp_ext,
        hitlag=hitlag_ext,
        buttons_held=buttons_ext,
        button_mask_lr=button_mask_lr,
        button_mask_z=0x0010,
        trigger_unit=trigger_ext,
        trigger_deadzone=0.3,
        guard_x10_init_frames=8,
        act_guard_on=int(act_guard_on),
        act_guard=int(act_guard),
        act_guard_reflect=int(act_guard_reflect),
        act_guard_set_off=int(act_guard_set_off),
    )

    assert np.array_equal(x_c0, x_c1[: x_c0.size])
    assert np.array_equal(x100, x101[: x100.size])
    assert np.allclose(light0, light1[: light0.size])


def test_derive_frame_speed_mul_guard_set_off_entry_rate_from_shield_drop() -> None:
    # GuardSetOff entry rate should follow ftCo_80092F2C using a causal estimate of x19A4 from
    # shield HP drop and latched lightshield amount; the derived rate must persist through hitlag.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    act_guard_on = np.uint16(0x00B2)
    act_guard_set_off = np.uint16(0x00B5)

    state_age = np.array([-1.0, 0.0, 0.0], dtype=np.float32)
    action_id = np.array([act_guard_on, act_guard_set_off, act_guard_set_off], dtype=np.uint16)
    hitlag = np.array([0, 3, 2], dtype=np.uint16)
    char_id = np.array([1, 1, 1], dtype=np.uint8)
    animation_index = np.array([37, 40, 40], dtype=np.uint32)
    lr_press_timer = np.array([0xFF, 0xFF, 0xFF], dtype=np.uint8)
    shield_hp = np.array([60.0, 55.52, 55.52], dtype=np.float32)
    lightshield = np.array([1.0, 1.0, 1.0], dtype=np.float32)

    end_frames = EndFrameTables(by_char_id={1: {40: 20.0}})

    out = derive_frame_speed_mul_f32(
        state_age_f32=state_age,
        action_id=action_id,
        hitlag=hitlag,
        char_id=char_id,
        animation_index=animation_index,
        lr_press_timer=lr_press_timer,
        shield_hp=shield_hp,
        lightshield_amount=lightshield,
        common_shield_hit_damage_mul=1.0,
        common_shield_hit_damage_base=0.0,
        common_shield_hit_lightshield_min=0.1,
        common_shield_hit_lightshield_max=0.3,
        common_shield_stun_mul=1.5,
        common_shield_stun_base=2.0,
        common_shield_stun_lightshield_min=0.05,
        common_shield_stun_lightshield_max=0.7,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames={1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}},
    )

    expected_rate = np.float32((20.0 + 0.1) / 4.7)
    assert np.isclose(out[1], expected_rate, rtol=1e-6, atol=1e-6)
    assert out[2] == out[1]


def test_derive_frame_speed_mul_guard_set_off_entry_is_prefix_invariant() -> None:
    # GuardSetOff entry-rate backsolve depends on shield_hp[i-1]-shield_hp[i] and lightshield_amount
    # at entry; extending future frames must not change prefix outputs.
    act_wait = np.uint16(0x000E)
    act_guard_set_off = np.uint16(0x00B5)

    state_age_prefix = np.array([-1.0, 0.0, 0.0, 0.0], dtype=np.float32)
    action_id_prefix = np.array(
        [act_wait, act_guard_set_off, act_guard_set_off, act_guard_set_off],
        dtype=np.uint16,
    )
    hitlag_prefix = np.array([0, 3, 2, 1], dtype=np.uint16)
    char_id_prefix = np.array([1, 1, 1, 1], dtype=np.uint8)
    animation_index_prefix = np.array([37, 40, 40, 40], dtype=np.uint32)
    lr_press_timer_prefix = np.array([0xFF, 0xFF, 0xFF, 0xFF], dtype=np.uint8)
    shield_hp_prefix = np.array([60.0, 55.52, 55.52, 55.52], dtype=np.float32)
    lightshield_prefix = np.array([1.0, 1.0, 1.0, 1.0], dtype=np.float32)

    end_frames = EndFrameTables(by_char_id={1: {40: 20.0}})
    char_lags = {1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}}

    out0 = derive_frame_speed_mul_f32(
        state_age_f32=state_age_prefix,
        action_id=action_id_prefix,
        hitlag=hitlag_prefix,
        char_id=char_id_prefix,
        animation_index=animation_index_prefix,
        lr_press_timer=lr_press_timer_prefix,
        shield_hp=shield_hp_prefix,
        lightshield_amount=lightshield_prefix,
        common_shield_hit_damage_mul=1.0,
        common_shield_hit_damage_base=0.0,
        common_shield_hit_lightshield_min=0.1,
        common_shield_hit_lightshield_max=0.3,
        common_shield_stun_mul=1.5,
        common_shield_stun_base=2.0,
        common_shield_stun_lightshield_min=0.05,
        common_shield_stun_lightshield_max=0.7,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames=char_lags,
    )

    state_age_ext = np.concatenate([state_age_prefix, np.array([5.0, 6.0, 7.0], dtype=np.float32)])
    action_id_ext = np.concatenate([action_id_prefix, np.array([act_wait, act_wait, act_wait], dtype=np.uint16)])
    hitlag_ext = np.concatenate([hitlag_prefix, np.array([0, 0, 0], dtype=np.uint16)])
    char_id_ext = np.concatenate([char_id_prefix, np.array([1, 1, 1], dtype=np.uint8)])
    animation_index_ext = np.concatenate([animation_index_prefix, np.array([0, 0, 0], dtype=np.uint32)])
    lr_press_timer_ext = np.concatenate([lr_press_timer_prefix, np.array([0xFF, 0xFF, 0xFF], dtype=np.uint8)])
    shield_hp_ext = np.concatenate([shield_hp_prefix, np.array([55.52, 55.52, 55.52], dtype=np.float32)])
    lightshield_ext = np.concatenate([lightshield_prefix, np.array([1.0, 1.0, 1.0], dtype=np.float32)])

    out1 = derive_frame_speed_mul_f32(
        state_age_f32=state_age_ext,
        action_id=action_id_ext,
        hitlag=hitlag_ext,
        char_id=char_id_ext,
        animation_index=animation_index_ext,
        lr_press_timer=lr_press_timer_ext,
        shield_hp=shield_hp_ext,
        lightshield_amount=lightshield_ext,
        common_shield_hit_damage_mul=1.0,
        common_shield_hit_damage_base=0.0,
        common_shield_hit_lightshield_min=0.1,
        common_shield_hit_lightshield_max=0.3,
        common_shield_stun_mul=1.5,
        common_shield_stun_base=2.0,
        common_shield_stun_lightshield_min=0.05,
        common_shield_stun_lightshield_max=0.7,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames=char_lags,
    )

    assert np.allclose(out0, out1[: out0.size])


def test_derive_guard_reflect_timer_counts_down_and_expires() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_reflect = np.uint16(0x00B6)

    a = np.array(
        [act_wait, act_guard_reflect, act_guard_reflect, act_guard_reflect, act_wait, act_guard_reflect],
        dtype=np.uint16,
    )
    hitlag = np.zeros(a.shape[0], dtype=np.uint16)

    out = derive_guard_reflect_timer_x14(
        action_id_u16=a,
        hitlag_u16=hitlag,
        act_guard_reflect=int(act_guard_reflect),
        reflect_frames_x2a4=1,
    )
    assert out.dtype == np.uint8
    # reflect_frames_x2a4=1 => init=(1+1)=2; tick once per GuardReflect frame (except entry).
    assert out.tolist() == [0, 2, 1, 0, 0, 2]
    out18 = derive_guard_reflect_timer_x18(
        action_id_u16=a,
        hitlag_u16=hitlag,
        act_guard_reflect=int(act_guard_reflect),
        reflect_total_frames_x2b4=3,
    )
    # reflect_total_frames_x2b4=3 => init=(3+1)=4.
    assert out18.tolist() == [0, 4, 3, 2, 0, 4]

    # Hitlag gate: GuardReflect_Anim callback ownership is based on the post-prio0 hitlag lane.
    # With post-frame replay hitlag inputs, this is modeled from frame-(i-1) after decrement.
    a2 = np.array([act_guard_reflect, act_guard_reflect, act_guard_reflect], dtype=np.uint16)
    hitlag2 = np.array([0, 2, 0], dtype=np.uint16)
    out2 = derive_guard_reflect_timer_x14(
        action_id_u16=a2,
        hitlag_u16=hitlag2,
        act_guard_reflect=int(act_guard_reflect),
        reflect_frames_x2a4=1,
    )
    assert out2.tolist() == [2, 1, 1]
    out18_2 = derive_guard_reflect_timer_x18(
        action_id_u16=a2,
        hitlag_u16=hitlag2,
        act_guard_reflect=int(act_guard_reflect),
        reflect_total_frames_x2b4=3,
    )
    assert out18_2.tolist() == [4, 3, 3]


def test_derive_capture_mash_buttons_pressed_adds_z_as_a_and_lr_edges() -> None:
    buttons = np.array(
        [
            [0x0000, 0x0000],
            [0x0010, 0x0000],
            [0x0010, 0x0000],
            [0x0000, 0x0000],
            [0x0000, 0x0020],
        ],
        dtype=np.uint16,
    )
    l_trigger = np.array(
        [
            [0, 0],
            [0, 0],
            [90, 0],
            [90, 0],
            [0, 0],
        ],
        dtype=np.uint8,
    )
    r_trigger = np.zeros_like(l_trigger)

    out = derive_capture_mash_buttons_pressed(
        buttons_u16_2d=buttons,
        l_trigger_u8_2d=l_trigger,
        r_trigger_u8_2d=r_trigger,
        trigger_deadzone=0.3,
        button_mask_a=0x0100,
        button_mask_z=0x0010,
        button_mask_lr=0x0060,
    )

    assert out.dtype == np.uint16
    assert out.tolist() == [
        [0x0000, 0x0000],
        [0x0170, 0x0000],
        [0x0000, 0x0000],
        [0x0000, 0x0000],
        [0x0000, 0x0060],
    ]


def test_derive_capture_grab_hidden_post_tracks_shared_owner_state() -> None:
    act_capturewait_lw = np.uint16(0x00E3)
    owner = np.array([0, 0, 0, 0], dtype=np.uint8)
    action_id = np.array([act_capturewait_lw, act_capturewait_lw, act_capturewait_lw, act_capturewait_lw], dtype=np.uint16)
    action_frame = np.array([0, 1, 2, 3], dtype=np.int16)
    percent = np.array([10.0, 10.0, 10.0, 10.0], dtype=np.float32)
    buttons = np.array([0x0000, 0x0100, 0x0000, 0x0000], dtype=np.uint16)
    stick_x = np.zeros(4, dtype=np.float32)
    stick_y = np.zeros(4, dtype=np.float32)
    frame_speed = np.array([1.0, 1.0, 2.0, 2.0], dtype=np.float32)
    mash_sign_x = np.zeros(4, dtype=np.int8)
    mash_sign_y = np.zeros(4, dtype=np.int8)

    (
        grab_timer,
        counter,
        anim_timer,
        jump_latch,
        breakout_pending,
    ) = derive_capture_grab_hidden_post(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        grab_owner_port_u8=owner,
        percent_f32=percent,
        buttons_pressed_u16=buttons,
        stick_x_unit=stick_x,
        stick_y_unit=stick_y,
        frame_speed_mul_f32=frame_speed,
        grab_mash_stick_x_sign_post=mash_sign_x,
        grab_mash_stick_y_sign_post=mash_sign_y,
        slot_index=1,
        handicap=9,
        capture_grab_timer_base=30.0,
        capture_grab_timer_handicap_mul=8.0,
        capture_grab_timer_handicap_base=9.0,
        capture_grab_timer_slot_mul=15.0,
        capture_grab_timer_slot_base=4.0,
        capture_grab_timer_percent_mul=1.6,
        capture_wait_grab_timer_decrement=1.0,
        capture_wait_grab_mash_damage=6.0,
        capture_wait_anim_rate_hold_frames=10.0,
        capture_wait_jump_latch_window_frames=16.0,
        grab_mash_stick_threshold=0.5,
    )

    # GrabMash for the step i -> i+1 reads fp-visible inputs that lag the serialized rows by
    # one (v12 Dolphin probe windows: GAT 2482/5677, AGNG 3208, QGD 8257, CDO 12724, PRH
    # 7602): the A edge serialized at row 1 fires GrabMash on the step 1 -> 2, so the mash
    # damage and the x2344 arming land at row 2.
    assert grab_timer.tolist() == pytest.approx([76.0, 75.0, 68.0, 67.0])
    assert counter.tolist() == pytest.approx([0.0, 1.0, 2.0, 3.0])
    assert anim_timer.tolist() == pytest.approx([0.0, 0.0, 10.0, 9.0])
    assert jump_latch.tolist() == [0, 0, 0, 0]
    assert breakout_pending.tolist() == [0, 0, 0, 0]


def test_derive_capture_grab_hidden_post_marks_wait_to_breakout_rows() -> None:
    act_capturewait_lw = np.uint16(0x00E3)
    act_capturecut = np.uint16(0x00E5)
    act_capturejump = np.uint16(0x00E6)

    (
        _grab_timer,
        _counter,
        _anim_timer,
        _jump_latch,
        breakout_pending,
    ) = derive_capture_grab_hidden_post(
        action_id_u16=np.array(
            [act_capturewait_lw, act_capturecut, act_capturewait_lw, act_capturejump],
            dtype=np.uint16,
        ),
        action_frame_i16=np.array([3, 0, 7, 0], dtype=np.int16),
        grab_owner_port_u8=np.array([0, 0, 0, 0], dtype=np.uint8),
        percent_f32=np.array([10.0, 10.0, 10.0, 10.0], dtype=np.float32),
        buttons_pressed_u16=np.zeros(4, dtype=np.uint16),
        stick_x_unit=np.zeros(4, dtype=np.float32),
        stick_y_unit=np.zeros(4, dtype=np.float32),
        frame_speed_mul_f32=np.ones(4, dtype=np.float32),
        grab_mash_stick_x_sign_post=np.zeros(4, dtype=np.int8),
        grab_mash_stick_y_sign_post=np.zeros(4, dtype=np.int8),
        slot_index=0,
        handicap=9,
        capture_grab_timer_base=30.0,
        capture_grab_timer_handicap_mul=8.0,
        capture_grab_timer_handicap_base=9.0,
        capture_grab_timer_slot_mul=15.0,
        capture_grab_timer_slot_base=4.0,
        capture_grab_timer_percent_mul=1.6,
        capture_wait_grab_timer_decrement=1.0,
        capture_wait_grab_mash_damage=6.0,
        capture_wait_anim_rate_hold_frames=10.0,
        capture_wait_jump_latch_window_frames=16.0,
        grab_mash_stick_threshold=0.5,
    )

    assert breakout_pending.tolist() == [1, 0, 1, 0]


def test_derive_capture_grab_hidden_post_is_prefix_invariant() -> None:
    act_capturewait_lw = np.uint16(0x00E3)
    base_kwargs = dict(
        action_id_u16=np.array([act_capturewait_lw, act_capturewait_lw, act_capturewait_lw], dtype=np.uint16),
        action_frame_i16=np.array([0, 1, 2], dtype=np.int16),
        grab_owner_port_u8=np.array([0, 0, 0], dtype=np.uint8),
        percent_f32=np.array([10.0, 10.0, 10.0], dtype=np.float32),
        buttons_pressed_u16=np.array([0x0000, 0x0C00, 0x0000], dtype=np.uint16),
        stick_x_unit=np.zeros(3, dtype=np.float32),
        stick_y_unit=np.zeros(3, dtype=np.float32),
        frame_speed_mul_f32=np.array([1.0, 1.0, 2.0], dtype=np.float32),
        grab_mash_stick_x_sign_post=np.zeros(3, dtype=np.int8),
        grab_mash_stick_y_sign_post=np.zeros(3, dtype=np.int8),
        slot_index=0,
        handicap=9,
        capture_grab_timer_base=30.0,
        capture_grab_timer_handicap_mul=8.0,
        capture_grab_timer_handicap_base=9.0,
        capture_grab_timer_slot_mul=15.0,
        capture_grab_timer_slot_base=4.0,
        capture_grab_timer_percent_mul=1.6,
        capture_wait_grab_timer_decrement=1.0,
        capture_wait_grab_mash_damage=6.0,
        capture_wait_anim_rate_hold_frames=10.0,
        capture_wait_jump_latch_window_frames=16.0,
        grab_mash_stick_threshold=0.5,
    )
    out0 = derive_capture_grab_hidden_post(**base_kwargs)
    out1 = derive_capture_grab_hidden_post(
        **{
            **base_kwargs,
            "action_id_u16": np.concatenate([base_kwargs["action_id_u16"], np.array([act_capturewait_lw], dtype=np.uint16)]),
            "action_frame_i16": np.concatenate([base_kwargs["action_frame_i16"], np.array([3], dtype=np.int16)]),
            "grab_owner_port_u8": np.concatenate([base_kwargs["grab_owner_port_u8"], np.array([0], dtype=np.uint8)]),
            "percent_f32": np.concatenate([base_kwargs["percent_f32"], np.array([10.0], dtype=np.float32)]),
            "buttons_pressed_u16": np.concatenate([base_kwargs["buttons_pressed_u16"], np.array([0x0000], dtype=np.uint16)]),
            "stick_x_unit": np.concatenate([base_kwargs["stick_x_unit"], np.array([0.0], dtype=np.float32)]),
            "stick_y_unit": np.concatenate([base_kwargs["stick_y_unit"], np.array([0.0], dtype=np.float32)]),
            "frame_speed_mul_f32": np.concatenate([base_kwargs["frame_speed_mul_f32"], np.array([2.0], dtype=np.float32)]),
            "grab_mash_stick_x_sign_post": np.concatenate([base_kwargs["grab_mash_stick_x_sign_post"], np.array([0], dtype=np.int8)]),
            "grab_mash_stick_y_sign_post": np.concatenate([base_kwargs["grab_mash_stick_y_sign_post"], np.array([0], dtype=np.int8)]),
        }
    )
    for lhs, rhs in zip(out0, out1):
        assert np.array_equal(lhs, rhs[: lhs.size])


def test_derive_guard_reflect_timer_is_prefix_invariant() -> None:
    act_wait = np.uint16(0x000E)
    act_guard_reflect = np.uint16(0x00B6)

    a_prefix = np.array([act_wait, act_guard_reflect, act_guard_reflect, act_wait], dtype=np.uint16)
    hl_prefix = np.array([0, 0, 0, 0], dtype=np.uint16)
    out0 = derive_guard_reflect_timer_x14(
        action_id_u16=a_prefix,
        hitlag_u16=hl_prefix,
        act_guard_reflect=int(act_guard_reflect),
        reflect_frames_x2a4=1,
    )

    a_ext = np.concatenate([a_prefix, np.array([act_guard_reflect, act_wait, act_guard_reflect], dtype=np.uint16)])
    hl_ext = np.concatenate([hl_prefix, np.array([0, 3, 0], dtype=np.uint16)])
    out1 = derive_guard_reflect_timer_x14(
        action_id_u16=a_ext,
        hitlag_u16=hl_ext,
        act_guard_reflect=int(act_guard_reflect),
        reflect_frames_x2a4=1,
    )

    assert np.array_equal(out0, out1[: out0.size])

    out0_18 = derive_guard_reflect_timer_x18(
        action_id_u16=a_prefix,
        hitlag_u16=hl_prefix,
        act_guard_reflect=int(act_guard_reflect),
        reflect_total_frames_x2b4=3,
    )
    out1_18 = derive_guard_reflect_timer_x18(
        action_id_u16=a_ext,
        hitlag_u16=hl_ext,
        act_guard_reflect=int(act_guard_reflect),
        reflect_total_frames_x2b4=3,
    )
    assert np.array_equal(out0_18, out1_18[: out0_18.size])

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


def test_lr_press_timer_x67f_is_causal_wrt_future_frames() -> None:
    # Decomp lane for x67F uses held LR/Z and trigger deadzone edge, not only digital LR.
    # refs/melee/src/melee/ft/fighter.c:1868-1890
    # refs/melee/src/melee/ft/fighter.c:2078-2086
    mask_lr = 0x0040 | 0x0020
    mask_z = 0x0010
    dz = 0.3

    b_prefix = np.array([0, 0, 0, mask_z, mask_z, 0], dtype=np.uint16)
    trig_prefix = np.array([0.0, 0.5, 0.5, 0.5, 0.1, 0.1], dtype=np.float32)
    t0 = compute_lr_press_timer_x67f(
        buttons=b_prefix,
        trigger_unit=trig_prefix,
        trigger_deadzone=dz,
        button_mask_lr=mask_lr,
        button_mask_z=mask_z,
        start_timer=0xFF,
    )
    assert t0.tolist() == [0xFF, 0, 1, 2, 3, 4]

    b_ext = np.concatenate([b_prefix, np.array([0, 0, mask_lr], dtype=np.uint16)])
    trig_ext = np.concatenate([trig_prefix, np.array([0.0, 0.0, 1.0], dtype=np.float32)])
    t1 = compute_lr_press_timer_x67f(
        buttons=b_ext,
        trigger_unit=trig_ext,
        trigger_deadzone=dz,
        button_mask_lr=mask_lr,
        button_mask_z=mask_z,
        start_timer=0xFF,
    )
    assert np.array_equal(t0, t1[: t0.size])


def test_lr_press_timer_x67f_latches_lr_edge_during_hitlag() -> None:
    # Decomp: when x2219_b5 is set, Fighter_Spaghetti OR-latches x668 edges (`x668 |= edge`).
    # For x67F this means an LR edge persists for the whole hitlag window.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
    # refs/melee/src/melee/ft/fighter.c:2078-2086
    mask_lr = 0x0040 | 0x0020
    mask_z = 0x0010
    dz = 0.3

    buttons = np.array([0, mask_lr, mask_lr, mask_lr, mask_lr, mask_lr], dtype=np.uint16)
    trigger = np.zeros(buttons.size, dtype=np.float32)
    # Enter hitlag on frame 1 and remain there through frame 3.
    hitlag = np.array([0, 3, 2, 1, 0, 0], dtype=np.uint16)
    out = compute_lr_press_timer_x67f(
        buttons=buttons,
        trigger_unit=trigger,
        hitlag_frames=hitlag,
        trigger_deadzone=dz,
        button_mask_lr=mask_lr,
        button_mask_z=mask_z,
        start_timer=0xFF,
    )

    # Frame 1 edge resets to 0; hitlag keeps the LR edge latched so timer stays 0 until hitlag ends.
    assert out.tolist() == [0xFF, 0, 0, 0, 1, 2]


def test_derive_colanim_internals_damage_exit_throw_and_cliff_entry() -> None:
    # Decomp anchors:
    # - ftCo_Damage_OnExitHitlag sets x1994 from p_ftCommonData->x130.
    # - Throw entry sets x1994 via ftColl_8007B7A4.
    # - CliffWait entry sets x1990 via ftColl_8007B760.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C
    act_wait = 0x000E
    act_damage_n1 = 0x004E
    act_throw_f = 0x00DB
    act_cliff_wait = 0x00FD

    action = np.array(
        [act_wait, act_damage_n1, act_damage_n1, act_damage_n1, act_wait, act_throw_f, act_throw_f, act_cliff_wait],
        dtype=np.uint16,
    )
    action_frame = np.array([1, 1, 2, 3, 1, 1, 2, 1], dtype=np.int16)
    hitlag = np.array([0, 3, 2, 0, 0, 0, 0, 0], dtype=np.uint16)
    hitstun = np.array([0, 5, 4, 3, 0, 0, 0, 0], dtype=np.uint16)
    hurt = np.array([0, 0, 0, 1, 1, 1, 1, 2], dtype=np.uint8)

    x198c, x1990, x1994, x2221, rebirth_fall_x1994 = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(act_throw_f,),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(act_damage_n1,),
    )

    assert x1994.tolist() == [0, 0, 0, 5, 4, 8, 7, 6]
    assert x1990.tolist() == [0, 0, 0, 0, 0, 0, 0, 30]
    assert x198c.tolist() == [0, 0, 0, 1, 1, 1, 1, 2]
    assert x2221.tolist() == [0] * action.size
    assert rebirth_fall_x1994.tolist() == [0] * action.size


def test_derive_colanim_internals_visible_vulnerable_clears_stale_x1990() -> None:
    # Slippi's post-frame hurtbox status is `x1988 != 0 ? x1988 : x198C`. A visible vulnerable
    # snapshot (0) therefore proves hidden x198C/x1990 are also clear; otherwise replay-derived
    # cliff invulnerability can stale-carry and incorrectly suppress BODY hits.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    act_cliff_wait = 0x00FD
    act_fall = 0x001D
    action = np.array([act_cliff_wait, act_cliff_wait, act_fall, act_fall], dtype=np.uint16)
    action_frame = np.array([1, 2, 6, 7], dtype=np.int16)
    hitlag = np.zeros(action.size, dtype=np.uint16)
    hitstun = np.zeros(action.size, dtype=np.uint16)
    hurt = np.array([2, 2, 0, 0], dtype=np.uint8)

    x198c, x1990, x1994, x2221, rebirth_fall_x1994 = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(),
    )

    assert x198c.tolist() == [2, 2, 0, 0]
    assert x1990.tolist() == [30, 29, 0, 0]
    assert x1994.tolist() == [0, 0, 0, 0]
    assert x2221.tolist() == [0, 0, 0, 0]
    assert rebirth_fall_x1994.tolist() == [0, 0, 0, 0]


def test_derive_colanim_internals_preserves_downbound_hidden_x1994_visible_zero() -> None:
    # DownBound is the narrow suite-proven exception to visible-0 clearing: replay-visible
    # hurtbox_state can be 0 while hidden x1994/x198C=1 still owns invincible-contact BODY behavior.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c
    act_damage_n1 = 0x004E
    act_down_bound_d = 0x00BF
    action = np.array([act_damage_n1, act_down_bound_d, act_down_bound_d], dtype=np.uint16)
    action_frame = np.array([1, 1, 2], dtype=np.int16)
    hitlag = np.array([2, 0, 0], dtype=np.uint16)
    hitstun = np.array([8, 7, 6], dtype=np.uint16)
    hurt = np.array([0, 0, 0], dtype=np.uint8)

    x198c, x1990, x1994, x2221, rebirth_fall_x1994 = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(),
        cliff_actions=(),
        damage_actions=(act_damage_n1,),
    )

    assert x198c.tolist() == [0, 1, 1]
    assert x1990.tolist() == [0, 0, 0]
    assert x1994.tolist() == [0, 5, 4]
    assert x2221.tolist() == [0, 0, 0]
    assert rebirth_fall_x1994.tolist() == [0, 0, 0]


def test_derive_colanim_internals_preserves_downbound_hidden_x1990_visible_zero() -> None:
    # DownBound can inherit hidden timer-owned collision status across a visible vulnerable snapshot;
    # keep the exception scoped to DownBound so generic Fall/locomotion stale x1990 still clears.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c
    act_cliff_wait = 0x00FD
    act_down_bound_d = 0x00BF
    action = np.array([act_cliff_wait, act_down_bound_d, act_down_bound_d], dtype=np.uint16)
    action_frame = np.array([1, 1, 2], dtype=np.int16)
    hitlag = np.zeros(action.size, dtype=np.uint16)
    hitstun = np.zeros(action.size, dtype=np.uint16)
    hurt = np.array([2, 0, 0], dtype=np.uint8)

    x198c, x1990, x1994, x2221, rebirth_fall_x1994 = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(),
    )

    assert x198c.tolist() == [2, 2, 2]
    assert x1990.tolist() == [30, 29, 28]
    assert x1994.tolist() == [0, 0, 0]
    assert x2221.tolist() == [0, 0, 0]
    assert rebirth_fall_x1994.tolist() == [0, 0, 0]


def test_derive_colanim_internals_clears_stale_damage_x1994_before_cliff_escapeair() -> None:
    # PJO-style provenance negative:
    # - Damage hitlag exit can seed x1994.
    # - A later visible-vulnerable non-damage/non-DownBound row proves that replay-history x1994 is
    #   no longer a source-proven hidden timer.
    # - Do not carry that stale x1994 underneath CliffWait x1990 or EscapeAir x1988, where it would
    #   leak as visible invincible status when EscapeAir lands into LandingFallSpecial.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    act_damage_n1 = 0x004E
    act_damage_fly_roll = 0x005B
    act_fall = 0x001D
    act_cliff_wait = 0x00FD
    act_escape_air = 0x00EC
    act_landing_fall_special = 0x002B

    action = np.array(
        [
            act_damage_n1,
            act_damage_fly_roll,
            act_damage_fly_roll,
            act_fall,
            act_cliff_wait,
            act_cliff_wait,
            act_escape_air,
            act_escape_air,
            act_landing_fall_special,
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([1, 1, 2, 0, 1, 2, 4, 5, 0], dtype=np.int16)
    hitlag = np.array([2, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    hitstun = np.array([8, 7, 6, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    hurt = np.array([0, 0, 0, 0, 2, 2, 2, 2, 0], dtype=np.uint8)

    x198c, x1990, x1994, x2221, rebirth_fall_x1994 = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(act_damage_n1, act_damage_fly_roll),
    )

    assert x1994.tolist() == [0, 5, 4, 0, 0, 0, 0, 0, 0]
    assert x1990.tolist() == [0, 0, 0, 0, 30, 29, 28, 27, 0]
    assert x198c.tolist() == [0, 1, 1, 0, 2, 2, 2, 2, 0]
    assert x2221.tolist() == [0] * action.size
    assert rebirth_fall_x1994.tolist() == [0] * action.size


def test_derive_colanim_internals_is_causal_wrt_future_frames() -> None:
    act_wait = 0x000E
    act_damage_n1 = 0x004E
    act_throw_f = 0x00DB
    act_cliff_wait = 0x00FD

    a0 = np.array([act_wait, act_damage_n1, act_damage_n1, act_damage_n1, act_wait, act_throw_f], dtype=np.uint16)
    af0 = np.array([1, 1, 2, 3, 1, 1], dtype=np.int16)
    hl0 = np.array([0, 3, 2, 0, 0, 0], dtype=np.uint16)
    hs0 = np.array([0, 5, 4, 3, 0, 0], dtype=np.uint16)
    hb0 = np.zeros(a0.size, dtype=np.uint8)

    out0 = derive_colanim_internals(
        action_id_u16=a0,
        action_frame_i16=af0,
        hitlag_u16=hl0,
        hitstun_u16=hs0,
        hurtbox_state_u8=hb0,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(act_throw_f,),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(act_damage_n1,),
    )

    a1 = np.concatenate([a0, np.array([act_throw_f, act_cliff_wait, act_cliff_wait, act_wait], dtype=np.uint16)])
    af1 = np.concatenate([af0, np.array([2, 1, 2, 1], dtype=np.int16)])
    hl1 = np.concatenate([hl0, np.array([0, 0, 0, 0], dtype=np.uint16)])
    hs1 = np.concatenate([hs0, np.array([0, 0, 0, 0], dtype=np.uint16)])
    hb1 = np.concatenate([hb0, np.zeros(4, dtype=np.uint8)])

    out1 = derive_colanim_internals(
        action_id_u16=a1,
        action_frame_i16=af1,
        hitlag_u16=hl1,
        hitstun_u16=hs1,
        hurtbox_state_u8=hb1,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(act_throw_f,),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(act_damage_n1,),
    )

    for x0, x1 in zip(out0, out1):
        assert np.array_equal(x0, x1[: x0.size])


def test_derive_colanim_seed_lanes_are_prefix_invariant() -> None:
    # Seed lanes added by derive_colanim_internals in validation buffer builder:
    # - seed_t.colanim_hit_status_x198c
    # - seed_t.colanim_timer_x1990
    # - seed_t.colanim_timer_x1994
    # - seed_t.colanim_lock_x2221_b0
    act_wait = 0x000E
    act_damage_n1 = 0x004E
    act_throw_f = 0x00DB
    act_cliff_wait = 0x00FD

    a_full = np.array(
        [
            act_wait,
            act_damage_n1,
            act_damage_n1,
            act_damage_n1,
            act_wait,
            act_throw_f,
            act_throw_f,
            act_cliff_wait,
            act_cliff_wait,
            act_wait,
        ],
        dtype=np.uint16,
    )
    af_full = np.array([1, 1, 2, 3, 1, 1, 2, 1, 2, 1], dtype=np.int16)
    hl_full = np.array([0, 3, 2, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    hs_full = np.array([0, 5, 4, 3, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    hb_full = np.zeros(a_full.size, dtype=np.uint8)

    full = derive_colanim_internals(
        action_id_u16=a_full,
        action_frame_i16=af_full,
        hitlag_u16=hl_full,
        hitstun_u16=hs_full,
        hurtbox_state_u8=hb_full,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        throw_actions=(act_throw_f,),
        cliff_actions=(act_cliff_wait,),
        damage_actions=(act_damage_n1,),
    )
    lane_by_name = {
        "colanim_hit_status_x198c": full[0],
        "colanim_timer_x1990": full[1],
        "colanim_timer_x1994": full[2],
        "colanim_lock_x2221_b0": full[3],
    }

    for cut in range(1, int(a_full.size) + 1):
        pref = derive_colanim_internals(
            action_id_u16=a_full[:cut],
            action_frame_i16=af_full[:cut],
            hitlag_u16=hl_full[:cut],
            hitstun_u16=hs_full[:cut],
            hurtbox_state_u8=hb_full[:cut],
            colanim_throw_x1994_frames=8,
            colanim_cliff_x1990_frames=30,
            colanim_damage_x1994_frames=5,
            throw_actions=(act_throw_f,),
            cliff_actions=(act_cliff_wait,),
            damage_actions=(act_damage_n1,),
        )
        pref_lane_by_name = {
            "colanim_hit_status_x198c": pref[0],
            "colanim_timer_x1990": pref[1],
            "colanim_timer_x1994": pref[2],
            "colanim_lock_x2221_b0": pref[3],
        }
        for lane, got in pref_lane_by_name.items():
            assert np.array_equal(got, lane_by_name[lane][:cut]), lane


def test_derive_colanim_passivewall_x1990_tracks_episode_not_action_frame() -> None:
    # PassiveWall / PassiveWallJump freezes action_frame while mv.co.passivewall.timer counts down,
    # but ftCo_800C1E64 still starts the x198C=2/x1990 timer on entry through
    # ftColl_8007B760(..., p_ftCommonData->x764). Reconstruct the timer causally from the action
    # episode, not from action_frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    act_wait = 0x000E
    act_passive_wall_jump = 0x00CB
    action = np.array(
        [act_wait, act_passive_wall_jump, act_passive_wall_jump, act_passive_wall_jump],
        dtype=np.uint16,
    )
    action_frame = np.array([1, 0, 0, 1], dtype=np.int16)
    zero_u16 = np.zeros(action.size, dtype=np.uint16)
    hurt = np.array([0, 2, 2, 2], dtype=np.uint8)

    x198c, x1990, *_ = derive_colanim_internals(
        action_id_u16=action,
        action_frame_i16=action_frame,
        hitlag_u16=zero_u16,
        hitstun_u16=zero_u16,
        hurtbox_state_u8=hurt,
        colanim_throw_x1994_frames=8,
        colanim_cliff_x1990_frames=30,
        colanim_damage_x1994_frames=5,
        colanim_passivewall_x1990_frames=14,
        throw_actions=(),
        cliff_actions=(),
        passivewall_actions=(act_passive_wall_jump,),
        damage_actions=(),
    )

    assert x1990.tolist() == [0, 14, 13, 12]
    assert x198c.tolist() == [0, 2, 2, 2]


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


def _read_hitbox_msid_frames(path: str, *, limit: int = 512) -> list[tuple[int, int]]:
    import struct

    buf = Path(path).read_bytes()
    if len(buf) < 16 or buf[:8] != b"MSLHITB1":
        raise ValueError("bad MSLHITB1")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != HITBOX_VERSION:
        raise ValueError("unsupported MSLHITB1 version")
    entry_count = int.from_bytes(buf[12:16], "little", signed=False)
    index_base = 16
    idx_bytes = 12
    rec_bytes = 44
    index_end = index_base + entry_count * idx_bytes
    out: list[tuple[int, int]] = []

    for i in range(entry_count):
        off = index_base + i * idx_bytes
        msid, rec_count, rec_len, payload_off = struct.unpack_from("<HHII", buf, off)
        # Collect candidate frames from set events only (kind=0).
        for ri in range(int(rec_count)):
            roff = int(payload_off) + ri * rec_bytes
            frame = int.from_bytes(buf[roff + 0 : roff + 2], "little", signed=False)
            kind = int(buf[roff + 2])
            hb_id = int(buf[roff + 3])
            if kind != 0:
                continue
            if 0 <= hb_id < 4:
                out.append((int(msid), int(frame)))
                if len(out) >= int(limit):
                    return out
    return out


def test_derive_combat_hitlist_seed_fields_is_causal_wrt_future_frames() -> None:
    import pytest

    if not Path("data/hitboxes/fox.bin").exists():
        pytest.skip("missing local artifact: data/hitboxes/fox.bin")
    if not Path("data/hurtcaps/fox.bin").exists():
        pytest.skip("missing local artifact: data/hurtcaps/fox.bin")
    if not Path("data/anims/fox.bin").exists():
        pytest.skip("missing local artifact: data/anims/fox.bin")

    # Find a (msid, frame) that produces at least one BODY hit under our extracted pose/hitbox/hurtcap data
    # when both fighters share the same position.
    #
    # Keep this tiny (few frames) so it stays unit-test fast.
    candidates = _read_hitbox_msid_frames("data/hitboxes/fox.bin", limit=512)
    assert candidates

    picked: tuple[int, int] | None = None
    for msid, af in candidates:
        n = 1
        z_u8 = np.zeros((n, 4), dtype=np.uint8)
        z_u16 = np.zeros((n, 4), dtype=np.uint16)
        z_u32 = np.zeros((n, 4), dtype=np.uint32)
        z_i16 = np.zeros((n, 4), dtype=np.int16)
        z_f32 = np.zeros((n, 4), dtype=np.float32)

        char_id = z_u8.copy()
        char_id[:, 0] = np.uint8(1)
        char_id[:, 1] = np.uint8(1)
        action_id = z_u16.copy()
        action_frame = z_i16.copy()
        action_frame[:, 0] = np.int16(af)
        action_frame[:, 1] = np.int16(af)
        animation_index = z_u32.copy()
        animation_index[:, 0] = np.uint32(msid)
        animation_index[:, 1] = np.uint32(msid)
        facing = z_u8.copy()
        facing[:, 0] = np.uint8(1)
        facing[:, 1] = np.uint8(1)
        on_ground = z_u8.copy()
        on_ground[:, 0] = np.uint8(1)
        on_ground[:, 1] = np.uint8(1)
        pos_x = z_f32.copy()
        pos_y = z_f32.copy()
        scale_y = np.ones((n, 4), dtype=np.float32)
        guard_tilt_x8 = z_u16.copy()
        guard_tilt_x4 = z_f32.copy()
        stocks = z_u8.copy()
        stocks[:, 0] = np.uint8(4)
        stocks[:, 1] = np.uint8(4)
        shield_hp = z_f32.copy()
        instance_id = z_u16.copy()
        instance_id[:, 0] = np.uint16(111)
        instance_id[:, 1] = np.uint16(222)

        input_buttons = z_u16.copy()
        input_l = z_u8.copy()
        input_r = z_u8.copy()

        hitlist_cd, hitlist_iid = derive_combat_hitlist_seed_fields(
            num_players=2,
            is_teams=False,
            team_id=z_u8,
            char_id=char_id,
            action_id=action_id,
            action_frame=action_frame,
            animation_index=animation_index,
            facing=facing,
            on_ground=on_ground,
            pos_x=pos_x,
            pos_y=pos_y,
            fighter_scale_y=scale_y,
            guard_tilt_x8=guard_tilt_x8,
            guard_tilt_x4=guard_tilt_x4,
            stocks=stocks,
            shield_hp=shield_hp,
            hurtbox_state=z_u8,
            instance_id=instance_id,
            input_buttons=input_buttons,
            input_l=input_l,
            input_r=input_r,
            data_root="data",
        )
        if int(np.any(hitlist_cd[0, 0, :, 1] != 0)):
            assert int(np.any(hitlist_iid[0, 0, :, 1] == np.uint16(222))) == 1
            picked = (msid, af)
            break

    assert picked is not None, "failed to find any (msid, frame) that produces a BODY hit"
    msid, af = picked

    # Prefix: stable overlap.
    n0 = 6
    base_u8 = np.zeros((n0, 4), dtype=np.uint8)
    base_u16 = np.zeros((n0, 4), dtype=np.uint16)
    base_u32 = np.zeros((n0, 4), dtype=np.uint32)
    base_i16 = np.zeros((n0, 4), dtype=np.int16)
    base_f32 = np.zeros((n0, 4), dtype=np.float32)

    char0 = base_u8.copy()
    char0[:, 0] = np.uint8(1)
    char0[:, 1] = np.uint8(1)
    action0 = base_u16.copy()
    af0 = base_i16.copy()
    af0[:, 0] = np.int16(af)
    af0[:, 1] = np.int16(af)
    anim0 = base_u32.copy()
    anim0[:, 0] = np.uint32(msid)
    anim0[:, 1] = np.uint32(msid)
    facing0 = base_u8.copy()
    facing0[:, 0] = np.uint8(1)
    facing0[:, 1] = np.uint8(1)
    on_ground0 = base_u8.copy()
    on_ground0[:, 0] = np.uint8(1)
    on_ground0[:, 1] = np.uint8(1)
    pos_x0 = base_f32.copy()
    pos_y0 = base_f32.copy()
    scale0 = np.ones((n0, 4), dtype=np.float32)
    guard_x8_0 = base_u16.copy()
    guard_x4_0 = base_f32.copy()
    stocks0 = base_u8.copy()
    stocks0[:, 0] = np.uint8(4)
    stocks0[:, 1] = np.uint8(4)
    shield0 = base_f32.copy()
    iid0 = base_u16.copy()
    iid0[:, 0] = np.uint16(111)
    iid0[:, 1] = np.uint16(222)
    buttons0 = base_u16.copy()
    l0 = base_u8.copy()
    r0 = base_u8.copy()

    cd0, iid_cd0 = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=base_u8,
        char_id=char0,
        action_id=action0,
        action_frame=af0,
        animation_index=anim0,
        facing=facing0,
        on_ground=on_ground0,
        pos_x=pos_x0,
        pos_y=pos_y0,
        fighter_scale_y=scale0,
        guard_tilt_x8=guard_x8_0,
        guard_tilt_x4=guard_x4_0,
        stocks=stocks0,
        shield_hp=shield0,
        hurtbox_state=base_u8,
        instance_id=iid0,
        input_buttons=buttons0,
        input_l=l0,
        input_r=r0,
        data_root="data",
    )

    assert int(np.any(cd0[0, 0, :, 1] != 0)) == 1
    assert int(np.any(iid_cd0[0, 0, :, 1] == np.uint16(222))) == 1

    # Append arbitrary future frames (no hitboxes via action_frame=-1): prefix outputs must not change.
    n1 = n0 + 5
    char1 = np.zeros((n1, 4), dtype=np.uint8)
    char1[:n0] = char0
    char1[n0:, 0] = np.uint8(1)
    char1[n0:, 1] = np.uint8(1)
    action1 = np.zeros((n1, 4), dtype=np.uint16)
    action1[:n0] = action0
    af1 = np.zeros((n1, 4), dtype=np.int16)
    af1[:n0] = af0
    af1[n0:, 0] = np.int16(-1)
    af1[n0:, 1] = np.int16(-1)
    anim1 = np.zeros((n1, 4), dtype=np.uint32)
    anim1[:n0] = anim0
    facing1 = np.zeros((n1, 4), dtype=np.uint8)
    facing1[:n0] = facing0
    on_ground1 = np.zeros((n1, 4), dtype=np.uint8)
    on_ground1[:n0] = on_ground0
    pos_x1 = np.zeros((n1, 4), dtype=np.float32)
    pos_x1[:n0] = pos_x0
    pos_y1 = np.zeros((n1, 4), dtype=np.float32)
    pos_y1[:n0] = pos_y0
    scale1 = np.ones((n1, 4), dtype=np.float32)
    guard_x8_1 = np.zeros((n1, 4), dtype=np.uint16)
    guard_x8_1[:n0] = guard_x8_0
    guard_x4_1 = np.zeros((n1, 4), dtype=np.float32)
    guard_x4_1[:n0] = guard_x4_0
    stocks1 = np.zeros((n1, 4), dtype=np.uint8)
    stocks1[:n0] = stocks0
    shield1 = np.zeros((n1, 4), dtype=np.float32)
    iid1 = np.zeros((n1, 4), dtype=np.uint16)
    iid1[:n0] = iid0
    buttons1 = np.zeros((n1, 4), dtype=np.uint16)
    l1 = np.zeros((n1, 4), dtype=np.uint8)
    r1 = np.zeros((n1, 4), dtype=np.uint8)

    cd1, iid_cd1 = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=np.zeros((n1, 4), dtype=np.uint8),
        char_id=char1,
        action_id=action1,
        action_frame=af1,
        animation_index=anim1,
        facing=facing1,
        on_ground=on_ground1,
        pos_x=pos_x1,
        pos_y=pos_y1,
        fighter_scale_y=scale1,
        guard_tilt_x8=guard_x8_1,
        guard_tilt_x4=guard_x4_1,
        stocks=stocks1,
        shield_hp=shield1,
        hurtbox_state=np.zeros((n1, 4), dtype=np.uint8),
        instance_id=iid1,
        input_buttons=buttons1,
        input_l=l1,
        input_r=r1,
        data_root="data",
    )

    assert np.array_equal(cd0, cd1[: cd0.shape[0]])
    assert np.array_equal(iid_cd0, iid_cd1[: iid_cd0.shape[0]])


def test_derive_combat_hitlist_seed_fields_per_hitbox_schema_shape() -> None:
    n = 3
    z_u8 = np.zeros((n, 4), dtype=np.uint8)
    z_u16 = np.zeros((n, 4), dtype=np.uint16)
    z_u32 = np.zeros((n, 4), dtype=np.uint32)
    z_i16 = np.full((n, 4), -1, dtype=np.int16)
    z_f32 = np.zeros((n, 4), dtype=np.float32)
    stocks = z_u8.copy()
    stocks[:, :2] = np.uint8(4)
    scale = np.ones((n, 4), dtype=np.float32)

    out = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=z_u8,
        char_id=z_u8,
        action_id=z_u16,
        action_frame=z_i16,
        animation_index=z_u32,
        facing=z_u8,
        on_ground=z_u8,
        pos_x=z_f32,
        pos_y=z_f32,
        fighter_scale_y=scale,
        guard_tilt_x8=z_u16,
        guard_tilt_x4=z_f32,
        stocks=stocks,
        shield_hp=z_f32,
        hurtbox_state=z_u8,
        instance_id=z_u16,
        input_buttons=z_u16,
        input_l=z_u8,
        input_r=z_u8,
        include_per_hitbox=True,
        data_root="data",
    )

    assert len(out) == 6
    group_cd, group_iid, hb_valid, hb_cd, hb_iid, shield_contact_kind = out
    assert group_cd.shape == (n, 4, 8, 4)
    assert group_iid.shape == (n, 4, 8, 4)
    assert hb_valid.shape == (n, 4, 4)
    assert hb_cd.shape == (n, 4, 4, 4)
    assert hb_iid.shape == (n, 4, 4, 4)
    assert shield_contact_kind.shape == (n, 4, 4, 4)
    assert group_cd.dtype == np.uint16
    assert hb_valid.dtype == np.uint8
    assert shield_contact_kind.dtype == np.uint8
    assert not bool(np.any(group_cd))
    assert not bool(np.any(hb_valid))
    assert not bool(np.any(shield_contact_kind))


def _derive_falcon_kick_processhit_producer(
    *,
    defender_x: float = 0.0,
    defender_invincible: bool = False,
    attacker_hitlag: tuple[int, int] = (0, 3),
    defender_hitlag: tuple[int, int] = (0, 0),
    attributed_damage_contact: bool | None = None,
    item_x: float | None = None,
    frame_count: int = 2,
) -> np.ndarray:
    from tools.eval.validation_dtypes import SEED_DTYPE

    z_u8 = np.zeros((frame_count, 4), dtype=np.uint8)
    z_u16 = np.zeros((frame_count, 4), dtype=np.uint16)
    z_u32 = np.zeros((frame_count, 4), dtype=np.uint32)
    z_i16 = np.zeros((frame_count, 4), dtype=np.int16)
    z_f32 = np.zeros((frame_count, 4), dtype=np.float32)

    char_id = z_u8.copy()
    char_id[:, :2] = np.array([2, 1], dtype=np.uint8)
    action_id = z_u16.copy()
    action_id[:, :2] = np.array([357, 14], dtype=np.uint16)
    action_frame = z_i16.copy()
    action_frame[:, :2] = np.array([14, 0], dtype=np.int16)
    animation_index = z_u32.copy()
    animation_index[:, :2] = np.array([311, 14], dtype=np.uint32)
    facing = np.ones((frame_count, 4), dtype=np.uint8)
    on_ground = z_u8.copy()
    on_ground[:, :2] = np.uint8(1)
    pos_x = z_f32.copy()
    pos_x[:, 1] = np.float32(defender_x)
    stocks = z_u8.copy()
    stocks[:, :2] = np.uint8(4)
    hurtbox_state = z_u8.copy()
    if defender_invincible:
        hurtbox_state[1, 1] = np.uint8(1)
    hitlag = z_u16.copy()
    hitlag[:2, 0] = np.asarray(attacker_hitlag, dtype=np.uint16)
    hitlag[:2, 1] = np.asarray(defender_hitlag, dtype=np.uint16)
    instance_id = z_u16.copy()
    instance_id[:, :2] = np.array([20, 30], dtype=np.uint16)
    last_hit_by = z_u8.copy()
    instance_hit_by = z_u16.copy()
    percent = z_f32.copy()
    if attributed_damage_contact is not None:
        last_hit_by[1, 1] = np.uint8(0)
        instance_hit_by[1, 1] = np.uint16(20 if attributed_damage_contact else 99)
        if not attributed_damage_contact and frame_count > 2:
            last_hit_by[2, 1] = np.uint8(0)
            instance_hit_by[2, 1] = np.uint16(20)
            hitlag[2, :2] = np.uint16(3)
            percent[2, 1] = np.float32(6.0)

    items = np.zeros((frame_count, 15), dtype=SEED_DTYPE["items"].base)
    if item_x is not None:
        # Yoshi's Story Heiho kind and hurtcaps are extracted into MSLSTIO1.
        items["exists"][:, 0] = np.uint8(1)
        items["type"][:, 0] = np.uint16(0xD2)
        items["state"][:, 0] = np.uint16(1)
        items["owner"][:, 0] = np.int8(-1)
        items["instance_id"][:, 0] = np.uint16(70)
        items["spawn_id"][:, 0] = np.uint32(700)
        items["pos_x"][:, 0] = np.float32(item_x)
        items["state"][1:, 0] = np.uint16(3)
        items["damage"][1:, 0] = np.uint16(10)

    out = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=z_u8,
        char_id=char_id,
        action_id=action_id,
        action_frame=action_frame,
        animation_index=animation_index,
        facing=facing,
        on_ground=on_ground,
        pos_x=pos_x,
        pos_y=z_f32,
        fighter_scale_y=np.ones((frame_count, 4), dtype=np.float32),
        guard_tilt_x8=z_u16,
        guard_tilt_x4=z_f32,
        stocks=stocks,
        shield_hp=z_f32,
        hurtbox_state=hurtbox_state,
        hitlag=hitlag,
        last_hit_by=last_hit_by if attributed_damage_contact is not None else None,
        instance_hit_by=instance_hit_by if attributed_damage_contact is not None else None,
        instance_id=instance_id,
        input_buttons=z_u16,
        input_l=z_u8,
        input_r=z_u8,
        anim_frame_f32=action_frame.astype(np.float32),
        percent=percent,
        items=items,
        include_per_hitbox=True,
        include_processhit_producers=True,
        include_replay_only_body_admission=True,
        data_root="data",
    )
    assert len(out) == 7
    return out[-1]


def test_falcon_kick_processhit_producer_requires_native_contact_provenance() -> None:
    # Falcon SpecialLw's frame-14 hitboxes come from data/hitboxes/falcon.bin::MSLHITB1.
    invincible_near = _derive_falcon_kick_processhit_producer(defender_invincible=True)
    invincible_far = _derive_falcon_kick_processhit_producer(
        defender_x=1000.0, defender_invincible=True
    )
    item_near = _derive_falcon_kick_processhit_producer(defender_x=1000.0, item_x=0.0)
    item_far = _derive_falcon_kick_processhit_producer(defender_x=1000.0, item_x=1000.0)

    assert int(invincible_near[1, 0]) == 1
    assert int(item_near[1, 0]) == 1
    assert int(invincible_far[1, 0]) == 0
    assert int(item_far[1, 0]) == 0


@pytest.mark.parametrize(
    ("producer_kwargs", "expected_hits"),
    [
        ({"defender_invincible": True}, 1),
        ({"defender_x": 1000.0, "item_x": 0.0}, 1),
        ({"defender_x": 1000.0, "defender_invincible": True}, 0),
        ({"defender_x": 1000.0, "item_x": 1000.0}, 0),
    ],
)
def test_falcon_kick_native_processhit_producer_drives_seed_lane(
    producer_kwargs: dict[str, object], expected_hits: int
) -> None:
    import msl_binding

    from tools.eval.validation_dtypes import SEED_DTYPE

    processhit_x1914 = _derive_falcon_kick_processhit_producer(
        **producer_kwargs, frame_count=3
    )
    rows = np.zeros((2,), dtype=SEED_DTYPE)
    rows["num_players"] = np.uint8(2)
    rows["source_port0"][:, :2] = np.array([0, 1], dtype=np.uint8)
    rows["char_id"][:, :2] = np.array([2, 1], dtype=np.uint8)
    rows["action_id"][:, 0] = np.uint16(357)
    rows["instance_id"][:, 0] = np.uint16(20)
    rows["attack_id"][:, 0] = np.uint16(9)
    rows["last_attack_landed"][:, 0] = np.uint8(9)

    msl_binding.validation_derive_falcon_speciallw_seed_lanes(
        rows.view(np.uint8).reshape((len(rows), SEED_DTYPE.itemsize)),
        processhit_x1914,
        2,
        2,
        357,
        358,
        4,
        0.6,
    )

    assert int(rows["falcon_speciallw_hits"][1, 0]) == expected_hits


def test_falcon_kick_processhit_producer_accepts_new_victim_during_hitlag_tail() -> None:
    got = _derive_falcon_kick_processhit_producer(
        attacker_hitlag=(2, 1),
        defender_hitlag=(0, 3),
        attributed_damage_contact=True,
    )

    assert int(got[1, 0]) == 1


def test_falcon_kick_processhit_producer_rejects_unattributed_defender_hitlag() -> None:
    got = _derive_falcon_kick_processhit_producer(
        attacker_hitlag=(2, 1),
        defender_hitlag=(0, 3),
        attributed_damage_contact=False,
        frame_count=3,
    )

    assert int(got[1, 0]) == 0
    assert int(got[2, 0]) == 1


def test_falcon_kick_processhit_producer_is_prefix_causal() -> None:
    prefix = _derive_falcon_kick_processhit_producer(defender_invincible=True, frame_count=2)
    extended = _derive_falcon_kick_processhit_producer(defender_invincible=True, frame_count=3)

    assert np.array_equal(prefix, extended[: len(prefix)])


def test_last_hit_by_raw_port_mapping_keeps_ambiguous_local_slots() -> None:
    # Out-of-range raw controller ports prove source_port0 ownership only on a replay-visible
    # damage onset from a prior shield-family row that also names the attacker's live instance.
    # In-range values are ambiguous with legacy local-slot seed lanes and must keep the local
    # meaning so the raw-port repair does not widen unrelated hitlist provenance.
    got = _localize_last_hit_by_for_native(
        last_hit_by=np.array([[0, 1], [2, 3], [3, 2], [4, 0]], dtype=np.uint8),
        source_port0=np.array([[1, 0], [2, 3], [0, 3], [1, 0]], dtype=np.uint8),
        action_id=np.array([[0xB2, 0xB2], [0x59, 0x59], [0x59, 0x59], [0x59, 0x59]], dtype=np.uint16),
        hitlag=np.array([[0, 0], [4, 4], [4, 4], [4, 0]], dtype=np.uint16),
        percent=np.array([[0.0, 0.0], [5.0, 5.0], [5.0, 6.0], [7.0, 6.0]], dtype=np.float32),
        instance_hit_by=np.array([[0, 0], [11, 21], [22, 12], [13, 0]], dtype=np.uint16),
        instance_id=np.array([[10, 20], [11, 21], [12, 22], [13, 23]], dtype=np.uint16),
        num_players=2,
    )
    assert got.tolist() == [[0, 1], [0, 1], [0xFF, 0xFF], [0xFF, 0]]


@pytest.mark.integration
def test_guardon_body_admission_hitlist_uses_raw_source_port_mapping() -> None:
    # RuralReasonableRat:2973 is a GuardOn ShieldDesc-miss -> BODY hit. The active BAir hitboxes
    # carry a stale dense hitlist entry from the prior no-damage GuardOn overlap, but the next
    # post-frame proves BODY damage from raw source port 3. The selected local attacker slot is 1,
    # so replay-only BODY admission must compare last_hit_by against source_port0, not local slot.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by raw controller-port lane)
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "replays/validation/sheik/RuralReasonableRat.slpz"
    ds = load_replay_buffers(str(dataset_path))
    rec = 2973
    attacker = 1
    defender = 0
    samples = ds.rows
    cur = samples[rec]["seed_t"]
    nxt = samples[rec + 1]["seed_t"]

    def stack_seed(name: str) -> np.ndarray:
        return np.ascontiguousarray(samples["seed_t"][name])

    def stack_input(name: str) -> np.ndarray:
        return np.ascontiguousarray(samples["input_t"]["p"][name])

    common_kwargs = dict(
        num_players=2,
        is_teams=False,
        team_id=stack_seed("team_id"),
        char_id=stack_seed("char_id"),
        action_id=stack_seed("action_id"),
        action_frame=stack_seed("action_frame"),
        animation_index=stack_seed("animation_index"),
        facing=stack_seed("facing"),
        on_ground=stack_seed("on_ground"),
        pos_x=stack_seed("pos_x"),
        pos_y=stack_seed("pos_y"),
        fighter_scale_y=stack_seed("fighter_scale_y"),
        guard_tilt_x8=stack_seed("guard_tilt_x8"),
        guard_tilt_x4=stack_seed("guard_tilt_x4"),
        stocks=stack_seed("stocks"),
        percent=stack_seed("percent"),
        shield_hp=stack_seed("shield_hp"),
        hurtbox_state=stack_seed("hurtbox_state"),
        hitlag=stack_seed("hitlag"),
        last_hit_by=stack_seed("last_hit_by"),
        instance_hit_by=stack_seed("instance_hit_by"),
        instance_id=stack_seed("instance_id"),
        input_buttons=stack_input("buttons"),
        input_l=stack_input("l"),
        input_r=stack_input("r"),
        anim_frame_f32=stack_seed("anim_frame_f32"),
        frame_speed_mul_f32=stack_seed("frame_speed_mul_f32"),
        include_per_hitbox=True,
        include_replay_only_body_admission=True,
        data_root="data",
    )
    assert int(cur["last_hit_by"][defender]) == 3
    assert int(cur["source_port0"][attacker]) == 3
    assert int(nxt["last_hit_by"][defender]) == 3
    assert int(nxt["instance_hit_by"][defender]) == int(cur["instance_id"][attacker])

    no_map = derive_combat_hitlist_seed_fields(**common_kwargs)
    with_map = derive_combat_hitlist_seed_fields(
        **common_kwargs,
        source_port0=stack_seed("source_port0"),
    )
    assert [int(x) for x in no_map[2][rec, attacker]] == [0, 0, 0, 0]
    assert [int(x) for x in with_map[2][rec, attacker]] == [1, 1, 1, 0]
    assert [int(with_map[3][rec, attacker, hb, defender]) for hb in range(4)] == [0, 0, 0, 0]
    assert [int(with_map[4][rec, attacker, hb, defender]) for hb in range(4)] == [0, 0, 0, 0]


def test_seed_bridge_trim_preserves_authoritative_per_hitbox_hitlist() -> None:
    from tools.slippi.validation_buffer_seed import _seed_bridge_trim_indefinite_lanes

    group_cd = np.zeros((2, 4, 8, 4), dtype=np.uint16)
    group_iid = np.zeros((2, 4, 8, 4), dtype=np.uint16)
    hb_valid = np.zeros((2, 4, 4), dtype=np.uint8)
    hb_cd = np.zeros((2, 4, 4, 4), dtype=np.uint16)
    hb_iid = np.zeros((2, 4, 4, 4), dtype=np.uint16)

    group_cd[1, 0, 0, 1] = np.uint16(0xFFFF)
    group_iid[1, 0, 0, 1] = np.uint16(345)
    hb_valid[1, 0, 1] = np.uint8(1)
    hb_cd[1, 0, 1, 1] = np.uint16(0xFFFF)
    hb_iid[1, 0, 1, 1] = np.uint16(344)
    hb_cd[1, 0, 2, 1] = np.uint16(0xFFFF)
    hb_iid[1, 0, 2, 1] = np.uint16(344)

    trimmed = _seed_bridge_trim_indefinite_lanes(
        hitlist_cd=group_cd,
        hitlist_iid=group_iid,
        hitlist_hb_valid=hb_valid,
        hitlist_hb_cd=hb_cd,
        hitlist_hb_iid=hb_iid,
        fi=1,
        attacker=0,
        defender=1,
    )

    assert trimmed
    assert int(group_cd[1, 0, 0, 1]) == 0
    assert int(group_iid[1, 0, 0, 1]) == 0
    assert int(hb_cd[1, 0, 1, 1]) == 0xFFFF
    assert int(hb_iid[1, 0, 1, 1]) == 344
    assert int(hb_cd[1, 0, 2, 1]) == 0
    assert int(hb_iid[1, 0, 2, 1]) == 0


def test_derive_combat_hitlist_seed_fields_is_prefix_invariant_wrt_shield_inputs() -> None:
    """
    Guard against accidental lookahead: adding new inputs to hitlist derivation must not make
    prefix outputs depend on future frames.

    This specifically stress-tests the new shield-bubble inputs used by the strictly-causal
    hitlist seeding path:
    - facing
    - guard_tilt_x8 / guard_tilt_x4 (shield bubble center)
    """
    from tools.slippi.combat_history import derive_combat_hitlist_seed_fields

    # Choose a real (msid, frame) for Fox that has at least one active hitbox so the shield path
    # executes overlap tests (hit is not required).
    # This mirrors the "find any contact" scan in the existing causality test above.
    from pathlib import Path
    import struct

    hb_path = Path("data/hitboxes/fox.bin")
    buf = hb_path.read_bytes()
    assert buf[:8] == b"MSLHITB1"
    ver = struct.unpack_from("<I", buf, 8)[0]
    assert ver == HITBOX_VERSION
    # Table format: [u16 msid][u16 count] then count records (see tools/slippi/combat_history.py::_read_hitbox_events).
    off = 16
    msid = None
    frame = None
    while off + 4 <= len(buf):
        m = struct.unpack_from("<H", buf, off)[0]
        n = struct.unpack_from("<H", buf, off + 2)[0]
        off += 4
        rec_bytes = 34
        if off + n * rec_bytes > len(buf):
            break
        # Pick the first msid that has any "set hitbox" event at frame 0.
        for i in range(n):
            kind = buf[off + i * rec_bytes + 0]
            ev_frame = struct.unpack_from("<H", buf, off + i * rec_bytes + 2)[0]
            hitbox_id = buf[off + i * rec_bytes + 1]
            if kind == 0 and hitbox_id != 0xFF and ev_frame == 0:
                msid = int(m)
                frame = 0
                break
        if msid is not None:
            break
        off += n * rec_bytes
    assert msid is not None and frame is not None

    # Prefix inputs: shield active on defender, nonzero shield HP, deterministic facing + guard tilt.
    n0 = 8
    n1 = n0 + 7
    base_u8 = np.zeros((n0, 4), dtype=np.uint8)
    base_u16 = np.zeros((n0, 4), dtype=np.uint16)
    base_u32 = np.zeros((n0, 4), dtype=np.uint32)
    base_i16 = np.zeros((n0, 4), dtype=np.int16)
    base_f32 = np.zeros((n0, 4), dtype=np.float32)

    team_id0 = base_u8.copy()
    char0 = base_u8.copy()
    char0[:, 0] = np.uint8(1)
    char0[:, 1] = np.uint8(1)
    action0 = base_u16.copy()
    # Defender in Guard to force shield bubble computation (GALE01 ftCo_MS_Guard = 0x00B3).
    action0[:, 1] = np.uint16(0x00B3)
    af0 = base_i16.copy()
    af0[:, 0] = np.int16(frame)
    af0[:, 1] = np.int16(frame)
    anim0 = base_u32.copy()
    anim0[:, 0] = np.uint32(msid)
    anim0[:, 1] = np.uint32(msid)
    facing0 = base_u8.copy()
    facing0[:, :2] = np.uint8(1)
    on_ground0 = base_u8.copy()
    on_ground0[:, :2] = np.uint8(1)
    pos_x0 = base_f32.copy()
    pos_y0 = base_f32.copy()
    scale0 = np.ones((n0, 4), dtype=np.float32)
    guard_x8_0 = base_u16.copy()
    guard_x4_0 = base_f32.copy()
    stocks0 = base_u8.copy()
    stocks0[:, :2] = np.uint8(4)
    shield0 = base_f32.copy()
    shield0[:, 1] = np.float32(60.0)
    hurt0 = base_u8.copy()
    iid0 = base_u16.copy()
    iid0[:, 0] = np.uint16(111)
    iid0[:, 1] = np.uint16(222)
    buttons0 = base_u16.copy()
    l0 = base_u8.copy()
    r0 = base_u8.copy()
    # Full L trigger for defender to expand shield (no lookahead; constant in prefix).
    l0[:, 1] = np.uint8(255)

    cd0, iid_cd0 = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=team_id0,
        char_id=char0,
        action_id=action0,
        action_frame=af0,
        animation_index=anim0,
        facing=facing0,
        on_ground=on_ground0,
        pos_x=pos_x0,
        pos_y=pos_y0,
        fighter_scale_y=scale0,
        guard_tilt_x8=guard_x8_0,
        guard_tilt_x4=guard_x4_0,
        stocks=stocks0,
        shield_hp=shield0,
        hurtbox_state=hurt0,
        instance_id=iid0,
        input_buttons=buttons0,
        input_l=l0,
        input_r=r0,
        data_root="data",
    )

    # Append future frames with noisy shield-related inputs: prefix outputs must not change.
    team_id1 = np.zeros((n1, 4), dtype=np.uint8)
    team_id1[:n0] = team_id0
    char1 = np.zeros((n1, 4), dtype=np.uint8)
    char1[:n0] = char0
    action1 = np.zeros((n1, 4), dtype=np.uint16)
    action1[:n0] = action0
    af1 = np.zeros((n1, 4), dtype=np.int16)
    af1[:n0] = af0
    # Disable hitboxes in appended frames so any hitlist evolution happens only in the suffix.
    af1[n0:, :2] = np.int16(-1)
    anim1 = np.zeros((n1, 4), dtype=np.uint32)
    anim1[:n0] = anim0
    facing1 = np.zeros((n1, 4), dtype=np.uint8)
    facing1[:n0] = facing0
    # Flip facing randomly in the suffix to catch lookahead.
    facing1[n0:, 0] = np.uint8(0)
    facing1[n0:, 1] = np.uint8(1)
    on_ground1 = np.zeros((n1, 4), dtype=np.uint8)
    on_ground1[:n0] = on_ground0
    pos_x1 = np.zeros((n1, 4), dtype=np.float32)
    pos_x1[:n0] = pos_x0
    pos_x1[n0:, 0] = np.float32(999.0)
    pos_y1 = np.zeros((n1, 4), dtype=np.float32)
    pos_y1[:n0] = pos_y0
    pos_y1[n0:, 1] = np.float32(-999.0)
    scale1 = np.ones((n1, 4), dtype=np.float32)
    guard_x8_1 = np.zeros((n1, 4), dtype=np.uint16)
    guard_x8_1[:n0] = guard_x8_0
    guard_x8_1[n0:, 1] = np.uint16(123)
    guard_x4_1 = np.zeros((n1, 4), dtype=np.float32)
    guard_x4_1[:n0] = guard_x4_0
    guard_x4_1[n0:, 1] = np.float32(1.0)
    stocks1 = np.zeros((n1, 4), dtype=np.uint8)
    stocks1[:n0] = stocks0
    shield1 = np.zeros((n1, 4), dtype=np.float32)
    shield1[:n0] = shield0
    shield1[n0:, 1] = np.float32(1.0)
    hurt1 = np.zeros((n1, 4), dtype=np.uint8)
    hurt1[:n0] = hurt0
    iid1 = np.zeros((n1, 4), dtype=np.uint16)
    iid1[:n0] = iid0
    iid1[n0:, 1] = np.uint16(9999)
    buttons1 = np.zeros((n1, 4), dtype=np.uint16)
    buttons1[:n0] = buttons0
    l1 = np.zeros((n1, 4), dtype=np.uint8)
    l1[:n0] = l0
    l1[n0:, 1] = np.uint8(0)
    r1 = np.zeros((n1, 4), dtype=np.uint8)

    cd1, iid_cd1 = derive_combat_hitlist_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=team_id1,
        char_id=char1,
        action_id=action1,
        action_frame=af1,
        animation_index=anim1,
        facing=facing1,
        on_ground=on_ground1,
        pos_x=pos_x1,
        pos_y=pos_y1,
        fighter_scale_y=scale1,
        guard_tilt_x8=guard_x8_1,
        guard_tilt_x4=guard_x4_1,
        stocks=stocks1,
        shield_hp=shield1,
        hurtbox_state=hurt1,
        instance_id=iid1,
        input_buttons=buttons1,
        input_l=l1,
        input_r=r1,
        data_root="data",
    )

    assert np.array_equal(cd0, cd1[: cd0.shape[0]])
    assert np.array_equal(iid_cd0, iid_cd1[: iid_cd0.shape[0]])


def test_fighter_stick_input_counters_are_causal_wrt_future_frames() -> None:
    # Prefix includes a neutral->right entry (lb_8000D148 zeroing) and a right->left flip.
    sx_prefix = np.array([0.0, 0.9, 0.9, -0.9, -0.9], dtype=np.float32)
    sy_prefix = np.zeros(sx_prefix.shape[0], dtype=np.float32)

    x673_0, x674_0, x676_x_0, x2228_b7_0, x677_y_0, x679_x_0, x67A_y_0 = compute_fighter_stick_input_counters(
        stick_x_unit=sx_prefix,
        stick_y_unit=sy_prefix,
        tilt_thresh_x=0.25,
        tilt_thresh_y=0.25,
        start_timer=0xFE,
    )

    # Spot-check a few reset/hold behaviors (off-by-one guardrails).
    assert int(x676_x_0[0]) == 0xFE
    assert int(x676_x_0[1]) == 0  # fresh entry to >= threshold resets x676_x
    assert int(x679_x_0[1]) == 0  # lb_8000D148 zeroing on neutral->tilt
    assert int(x673_0[2]) == 1  # second consecutive frame >= threshold increments
    assert int(x679_x_0[3]) == 0  # right->left flip crosses (0,0) => lb zeroing
    assert int(x2228_b7_0[1]) == 1  # fresh right entry
    assert int(x2228_b7_0[3]) == 0  # fresh left entry

    sx_ext = np.concatenate([sx_prefix, np.array([0.0, 0.9, 0.0], dtype=np.float32)])
    sy_ext = np.zeros(sx_ext.shape[0], dtype=np.float32)
    x673_1, x674_1, x676_x_1, x2228_b7_1, x677_y_1, x679_x_1, x67A_y_1 = compute_fighter_stick_input_counters(
        stick_x_unit=sx_ext,
        stick_y_unit=sy_ext,
        tilt_thresh_x=0.25,
        tilt_thresh_y=0.25,
        start_timer=0xFE,
    )

    assert np.array_equal(x673_0, x673_1[: x673_0.size])
    assert np.array_equal(x674_0, x674_1[: x674_0.size])
    assert np.array_equal(x676_x_0, x676_x_1[: x676_x_0.size])
    assert np.array_equal(x2228_b7_0, x2228_b7_1[: x2228_b7_0.size])
    assert np.array_equal(x677_y_0, x677_y_1[: x677_y_0.size])
    assert np.array_equal(x679_x_0, x679_x_1[: x679_x_0.size])
    assert np.array_equal(x67A_y_0, x67A_y_1[: x67A_y_0.size])


def test_fighter_trigger_input_counters_are_causal_wrt_future_frames() -> None:
    trig_prefix = np.array([0.0, 1.0, 1.0, 0.0, 1.0], dtype=np.float32)
    x675_0, x67B_0, x678_0 = compute_fighter_trigger_input_counters(
        trigger_unit=trig_prefix,
        trigger_min=0.25,
        start_timer=0xFE,
    )

    # Spot-check reset/hold behavior.
    assert int(x675_0[0]) == 0xFE
    assert int(x678_0[1]) == 0  # fresh press resets x678 to 0
    assert int(x675_0[2]) == 1  # second consecutive pressed frame increments
    assert int(x67B_0[2]) == 1

    trig_ext = np.concatenate([trig_prefix, np.array([1.0, 0.0, 0.0], dtype=np.float32)])
    x675_1, x67B_1, x678_1 = compute_fighter_trigger_input_counters(
        trigger_unit=trig_ext,
        trigger_min=0.25,
        start_timer=0xFE,
    )

    assert np.array_equal(x675_0, x675_1[: x675_0.size])
    assert np.array_equal(x67B_0, x67B_1[: x67B_0.size])
    assert np.array_equal(x678_0, x678_1[: x678_0.size])


def test_fighter_button_timers_are_causal_and_capture_previous_on_press() -> None:
    A = np.uint16(0x0100)
    L = np.uint16(0x0040)
    R = np.uint16(0x0020)
    XY = np.uint16(0x0C00)
    D_UP = np.uint16(0x0008)
    D_DOWN = np.uint16(0x0004)
    LR = np.uint16(L | R)

    bp_prefix = np.array([0, A, 0, 0, A, LR, 0, XY, D_UP, D_DOWN], dtype=np.uint16)
    x67C_0, x67D_0, x67E_0, x680_0, x681_0, x682_0, x683_0, x684_0 = compute_fighter_button_timers(
        buttons_pressed=bp_prefix,
        mask_a=int(A),
        mask_b=0x0200,
        mask_xy=int(XY),
        mask_dpad_up=int(D_UP),
        mask_dpad_down=int(D_DOWN),
        mask_lr=int(LR),
        start_timer=0xFF,
    )

    # On the second A press, x683 captures the previous x67C (which should be 2 at that point).
    assert int(x67C_0[1]) == 0
    assert int(x67C_0[2]) == 1
    assert int(x67C_0[3]) == 2
    assert int(x683_0[4]) == 2
    assert int(x67C_0[4]) == 0

    # On the first L/R press, x684 captures the previous x680 (which starts at 0xFF).
    assert int(x684_0[5]) == 0xFF
    assert int(x680_0[5]) == 0

    bp_ext = np.concatenate([bp_prefix, np.array([0, A, 0], dtype=np.uint16)])
    x67C_1, x67D_1, x67E_1, x680_1, x681_1, x682_1, x683_1, x684_1 = compute_fighter_button_timers(
        buttons_pressed=bp_ext,
        mask_a=int(A),
        mask_b=0x0200,
        mask_xy=int(XY),
        mask_dpad_up=int(D_UP),
        mask_dpad_down=int(D_DOWN),
        mask_lr=int(LR),
        start_timer=0xFF,
    )

    assert np.array_equal(x67C_0, x67C_1[: x67C_0.size])
    assert np.array_equal(x67D_0, x67D_1[: x67D_0.size])
    assert np.array_equal(x67E_0, x67E_1[: x67E_0.size])
    assert np.array_equal(x680_0, x680_1[: x680_0.size])
    assert np.array_equal(x681_0, x681_1[: x681_0.size])
    assert np.array_equal(x682_0, x682_1[: x682_0.size])
    assert np.array_equal(x683_0, x683_1[: x683_0.size])
    assert np.array_equal(x684_0, x684_1[: x684_0.size])


def test_fighter_button_timers_map_z_edge_to_a_macro_without_physical_lr() -> None:
    A = np.uint16(0x0100)
    Z = np.uint16(0x0010)
    L = np.uint16(0x0040)
    R = np.uint16(0x0020)
    XY = np.uint16(0x0C00)
    D_UP = np.uint16(0x0008)
    D_DOWN = np.uint16(0x0004)
    LR = np.uint16(L | R)

    x67C, x67D, x67E, x680, x681, x682, x683, x684 = compute_fighter_button_timers(
        buttons_pressed=np.array([0, Z, 0, 0], dtype=np.uint16),
        mask_a=int(A),
        mask_b=0x0200,
        mask_xy=int(XY),
        mask_dpad_up=int(D_UP),
        mask_dpad_down=int(D_DOWN),
        mask_lr=int(LR),
        mask_z=int(Z),
        start_timer=0xFF,
    )

    # Fighter_Spaghetti_8006AD10 maps Z onto HSD_PAD_A plus the LR macro before x668 is consumed.
    # The compact MSL button domain does not have the synthetic HSD_PAD_LR bit, so this seed lane
    # maps only the A timer; B/XY/DPad/physical-LR timers remain unrelated.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
    assert [int(v) for v in x67C] == [0xFF, 0, 1, 2]
    assert [int(v) for v in x680] == [0xFF, 0xFF, 0xFF, 0xFF]
    assert int(x683[1]) == 0xFF
    assert int(x684[1]) == 0xFF
    assert [int(v) for v in x67D] == [0xFF, 0xFF, 0xFF, 0xFF]
    assert [int(v) for v in x67E] == [0xFF, 0xFF, 0xFF, 0xFF]
    assert [int(v) for v in x681] == [0xFF, 0xFF, 0xFF, 0xFF]
    assert [int(v) for v in x682] == [0xFF, 0xFF, 0xFF, 0xFF]


def test_fighter_button_timers_latch_x668_during_hitlag_for_tech_debounce() -> None:
    L = np.uint16(0x0040)
    R = np.uint16(0x0020)
    A = np.uint16(0x0100)
    XY = np.uint16(0x0C00)
    D_UP = np.uint16(0x0008)
    D_DOWN = np.uint16(0x0004)
    LR = np.uint16(L | R)

    # Digital L pressed during active hitlag is OR-latched in
    # Fighter_Spaghetti_8006AD10_Inner1. The latched x668 bit is then consumed by the same
    # button-timer block on each still-hitlag frame. For techs this means later hitlag frames
    # overwrite x684 with the just-reset x680, keeping ftCo_800986B0's debounce gate closed.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
    buttons_pressed = np.array([0, L, 0, 0, 0, 0], dtype=np.uint16)
    hitlag = np.array([0, 3, 2, 1, 0, 0], dtype=np.uint16)

    _, _, _, x680, _, _, _, x684 = compute_fighter_button_timers(
        buttons_pressed=buttons_pressed,
        hitlag_frames=hitlag,
        mask_a=int(A),
        mask_b=0x0200,
        mask_xy=int(XY),
        mask_dpad_up=int(D_UP),
        mask_dpad_down=int(D_DOWN),
        mask_lr=int(LR),
        start_timer=0xFF,
    )

    assert [int(v) for v in x680] == [0xFF, 0, 0, 0, 1, 2]
    assert [int(v) for v in x684] == [0xFF, 0xFF, 0, 0, 0, 0]


def test_fighter_button_timers_preserve_pre_hitlag_lr_debounce_for_floor_tech() -> None:
    L = np.uint16(0x0040)
    A = np.uint16(0x0100)
    XY = np.uint16(0x0C00)
    D_UP = np.uint16(0x0008)
    D_DOWN = np.uint16(0x0004)

    # When the L/R edge happens before hitlag starts, the DamageFly floor-contact tech callback
    # still observes the first debounce capture rather than a hitlag-latched replay edge. This keeps
    # ftCo_800986B0's x684 gate open for legitimate pre-hitlag tech inputs.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
    buttons_pressed = np.array([0, L, 0, 0, 0, 0], dtype=np.uint16)
    hitlag = np.array([0, 0, 3, 2, 1, 0], dtype=np.uint16)

    _, _, _, x680, _, _, _, x684 = compute_fighter_button_timers(
        buttons_pressed=buttons_pressed,
        hitlag_frames=hitlag,
        mask_a=int(A),
        mask_b=0x0200,
        mask_xy=int(XY),
        mask_dpad_up=int(D_UP),
        mask_dpad_down=int(D_DOWN),
        mask_lr=int(L),
        start_timer=0x2F,
    )

    assert [int(v) for v in x680] == [0x30, 0, 1, 2, 3, 4]
    assert [int(v) for v in x684] == [0x2F, 0x30, 0x30, 0x30, 0x30, 0x30]


def test_attackdash_x0_seed_lane_reconstructs_entry_countdown_when_misc_as_is_zero() -> None:
    act_attack_dash = np.uint16(0x0032)
    act_dash = np.uint16(0x0014)
    action_id = np.array(
        [act_dash, act_attack_dash, act_attack_dash, act_attack_dash, act_attack_dash],
        dtype=np.uint16,
    )
    action_frame = np.array([5, 1, 2, 3, 4], dtype=np.int16)
    misc_as = np.zeros(action_id.shape[0], dtype=np.float32)

    out = _derive_attackdash_x0_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        misc_as_f32=misc_as,
        act_attack_dash=int(act_attack_dash),
        attackdash_x0_init_frames=3,
    )

    # ftCo_AttackDash.c::doEnter clears x0, then ftCo_AttackDash_SetMv0 writes
    # p_ftCommonData->x68. Public Slippi misc_as can be zero for this short lane, so preprocessing
    # reconstructs the source-published countdown from action age.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::doEnter
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_SetMv0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8AE0
    assert out.tolist() == [0, 3, 2, 1, 0]


def test_attackdash_x0_seed_lane_preserves_nonzero_misc_as_source_value() -> None:
    act_attack_dash = np.uint16(0x0032)
    out = _derive_attackdash_x0_seed_lane(
        action_id_u16=np.array([act_attack_dash, act_attack_dash], dtype=np.uint16),
        action_frame_i16=np.array([1, 4], dtype=np.int16),
        misc_as_f32=np.array([2.0, 5.0], dtype=np.float32),
        act_attack_dash=int(act_attack_dash),
        attackdash_x0_init_frames=3,
    )

    assert out.tolist() == [2, 5]


def test_jump_to_jump_aerial_entry_clears_fall_fast_and_overrides_x671() -> None:
    # When transitioning into JumpAerial (e.g. JumpF -> JumpAerialF), treat it as a post-input fresh
    # entry: clear fall_fast and set x671_post=0xFE on that entry frame.
    #
    # Decomp refs:
    # - ftCommon_CheckFallFast: refs/melee/src/melee/ft/ftcommon.c:505-520
    # - JumpAerial entry override: refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:152-156
    act_jump_f = np.uint16(0x0019)
    act_jump_aerial_f = np.uint16(0x001B)

    post_state = np.array([act_jump_f, act_jump_f, act_jump_aerial_f, act_jump_aerial_f], dtype=np.uint16)
    prev_state = np.concatenate(([post_state[0]], post_state[:-1]))
    pre_input_jump_entry = (post_state == act_jump_f) & (post_state != prev_state)
    jump_entry = (post_state == act_jump_aerial_f) & (post_state != prev_state)

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
        pre_input_jump_entry=pre_input_jump_entry,
        fastfall_ok=fastfall_ok,
        speed_y_self_post=speed_y_self_post,
        on_ground_post=on_ground_post,
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
    )

    assert int(ff_post[1]) == 1
    assert int(t_post[2]) == 0xFE
    assert int(ff_post[2]) == 0


def test_pre_input_jumpf_entry_clears_fall_fast_without_forcing_x671_post() -> None:
    # KneeBend Anim enters JumpF/B before Fighter_Spaghetti refreshes input history. The jump entry
    # clears fall_fast, but a same-frame stick threshold crossing still leaves x671_post=0 rather
    # than ftCo_Jump_Enter's transient 0xFE write.
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_KneeBend.c,ftCo_Jump.c}
    stick_y = np.array([0.0, 0.30], dtype=np.float32)
    _, t_post, ff_post = compute_tilt_timer_y_pre_post_with_fall_fast(
        stick_y,
        tilt_thresh=0.25,
        jump_entry=np.array([False, False]),
        pre_input_jump_entry=np.array([False, True]),
        fastfall_ok=np.array([True, True]),
        speed_y_self_post=np.array([-1.0, -1.0], dtype=np.float32),
        on_ground_post=np.array([False, False]),
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
    )

    assert int(t_post[1]) == 0
    assert int(ff_post[1]) == 0

    # If stick Y was already past the tilt threshold, Fighter_Spaghetti increments from the
    # transient 0xFE write and clamps back to 0xFE. It must not carry the previous low timer.
    _, held_post, _ = compute_tilt_timer_y_pre_post_with_fall_fast(
        np.array([0.30, 0.30], dtype=np.float32),
        tilt_thresh=0.25,
        jump_entry=np.array([False, False]),
        pre_input_jump_entry=np.array([False, True]),
        fastfall_ok=np.array([True, True]),
        speed_y_self_post=np.array([-1.0, -1.0], dtype=np.float32),
        on_ground_post=np.array([False, False]),
        fastfall_stick_threshold=0.6,
        fastfall_tilt_max_frames=4,
        start_timer_post=1,
    )
    assert int(held_post[1]) == 0xFE


def test_derive_ucf_pad_buffer_state_is_causal_wrt_future_frames() -> None:
    raw_x_prefix = np.zeros(6, dtype=np.int8)
    raw_y_prefix = np.array([0, 0, -127, -127, -127, 0], dtype=np.int8)
    hold_y_prefix = np.array([0xFE, 0xFE, 0, 1, 2, 0xFE], dtype=np.uint8)

    i0, s0, x0, y0 = derive_ucf_pad_buffer_state(
        raw_x_prefix,
        raw_y_prefix,
        stick_y_hold_time=hold_y_prefix,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
        lstick_deadzone_x=0.0,
        lstick_deadzone_y=0.0,
    )

    raw_x_ext = np.concatenate([raw_x_prefix, np.array([0, 0, 0, 0], dtype=np.int8)])
    raw_y_ext = np.concatenate([raw_y_prefix, np.array([0, -127, -127, 0], dtype=np.int8)])
    hold_y_ext = np.concatenate([hold_y_prefix, np.array([0xFE, 0, 1, 0xFE], dtype=np.uint8)])
    i1, s1, x1, y1 = derive_ucf_pad_buffer_state(
        raw_x_ext,
        raw_y_ext,
        stick_y_hold_time=hold_y_ext,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
        lstick_deadzone_x=0.0,
        lstick_deadzone_y=0.0,
    )

    assert np.array_equal(i0, i1[: i0.size])
    assert np.array_equal(s0, s1[: s0.size])
    assert np.array_equal(x0, x1[: x0.shape[0], :])
    assert np.array_equal(y0, y1[: y0.shape[0], :])


def test_derive_ucf_pad_buffer_state_basic_sequence() -> None:
    raw_x = np.zeros(6, dtype=np.int8)
    raw_y = np.array([0, 0, -127, -127, -127, 0], dtype=np.int8)
    hold_y = np.array([0xFE, 0xFE, 0, 1, 2, 0xFE], dtype=np.uint8)

    index, sdrop, entries_x, entries_y = derive_ucf_pad_buffer_state(
        raw_x,
        raw_y,
        stick_y_hold_time=hold_y,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
        lstick_deadzone_x=0.0,
        lstick_deadzone_y=0.0,
    )

    assert index.tolist() == [1, 2, 3, 0, 1, 2]
    assert sdrop.tolist() == [0, 0, 1, 2, 3, 0]

    # Spot-check ring content after the first sdrop-up trigger frame (frame 2).
    assert entries_x[2].tolist() == [0, 0, 0, 0]
    assert entries_y[2].tolist() == [0, 0, 0, -127]


def test_derive_damage_post_hitlag_cb_kind_prefix_invariant() -> None:
    act_wait = np.uint16(14)
    act_damage_fly_n = np.uint16(88)
    act_damage_fall = np.uint16(91)

    action = np.array(
        [act_wait, act_damage_fly_n, act_damage_fly_n, act_damage_fall, act_wait, act_damage_fly_n],
        dtype=np.uint16,
    )
    hitstun = np.array([0, 36, 35, 34, 0, 22], dtype=np.uint16)
    damage_actions = (int(act_damage_fly_n), int(act_damage_fall))

    full = derive_damage_post_hitlag_cb_kind(
        action_id=action,
        hitstun_u16=hitstun,
        damage_actions=damage_actions,
    )
    assert full.tolist() == [0, 1, 1, 1, 0, 1]

    for k in (1, 2, 3, 4, 5, int(action.size)):
        got = derive_damage_post_hitlag_cb_kind(
            action_id=action[:k],
            hitstun_u16=hitstun[:k],
            damage_actions=damage_actions,
        )
        assert np.array_equal(got, full[:k])


def test_derive_damage_post_hitlag_cb_kind_fresh_hit_reentry_stays_active() -> None:
    act_damage_fly_n = np.uint16(88)
    action = np.array([act_damage_fly_n, act_damage_fly_n, act_damage_fly_n, act_damage_fly_n], dtype=np.uint16)
    hitstun = np.array([30, 29, 40, 39], dtype=np.uint16)  # rising hitstun on frame 2 => fresh hit.

    got = derive_damage_post_hitlag_cb_kind(
        action_id=action,
        hitstun_u16=hitstun,
        damage_actions=(int(act_damage_fly_n),),
    )
    assert got.tolist() == [1, 1, 1, 1]


def test_derive_damage_meteor_cancel_x1a_samples_entry_angle_only() -> None:
    act_wait = np.uint16(14)
    act_damage_fly_n = np.uint16(88)
    action = np.array(
        [act_wait, act_damage_fly_n, act_damage_fly_n, act_wait, act_damage_fly_n],
        dtype=np.uint16,
    )
    hitstun = np.array([0, 25, 24, 0, 29], dtype=np.uint16)
    # First episode is Sakurai angle 361, which may become visibly vertical but never calls
    # ftColl_8007AC68. The second episode starts inside the source 260..280 window and carries true.
    source_angle = np.array([0xFFFF, 361, 361, 0xFFFF, 270], dtype=np.uint16)

    got = derive_damage_meteor_cancel_x1a(
        action_id=action,
        hitstun_u16=hitstun,
        source_angle_u16=source_angle,
        angle_min_deg=260,
        angle_max_deg=280,
        damage_actions=(int(act_damage_fly_n),),
    )
    assert got.tolist() == [0, 0, 0, 0, 1]

    for k in range(1, int(action.size) + 1):
        prefix = derive_damage_meteor_cancel_x1a(
            action_id=action[:k],
            hitstun_u16=hitstun[:k],
            source_angle_u16=source_angle[:k],
            angle_min_deg=260,
            angle_max_deg=280,
            damage_actions=(int(act_damage_fly_n),),
        )
        assert np.array_equal(prefix, got[:k])


def test_derive_camera_box_visible_x221f_b0_prefix_invariant() -> None:
    state_flags_prefix = np.array(
        [
            [0, 0, 0, 0, 0x00],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x00],
        ],
        dtype=np.uint8,
    )
    full = derive_camera_box_visible_x221f_b0(state_flags_u8=state_flags_prefix)
    assert full.tolist() == [0, 1, 1, 0]

    state_flags_ext = np.concatenate(
        [
            state_flags_prefix,
            np.array(
                [
                    [0, 0, 0, 0, 0x00],
                    [0, 0, 0, 0, 0x80],
                    [0, 0, 0, 0, 0x00],
                ],
                dtype=np.uint8,
            ),
        ],
        axis=0,
    )
    ext = derive_camera_box_visible_x221f_b0(state_flags_u8=state_flags_ext)
    assert np.array_equal(ext[: state_flags_prefix.shape[0]], full)


def test_derive_magnify_damage_counter_x1910_backfills_observed_ticks_and_resets() -> None:
    wait = np.uint16(14)
    rebirth = np.uint16(12)
    action = np.array([wait, wait, wait, wait, wait, rebirth, wait], dtype=np.uint16)
    state_flags = np.array(
        [
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x00],
            [0, 0, 0, 0, 0x80],
            [0, 0, 0, 0, 0x80],
        ],
        dtype=np.uint8,
    )
    percent = np.array([7.0, 7.0, 7.0, 8.0, 8.0, 0.0, 8.0], dtype=np.float32)
    outside = np.array([0, 0, 0, 1, 0, 0, 0], dtype=np.uint8)
    hitlag = np.zeros(action.shape[0], dtype=np.uint16)
    hitstun = np.zeros(action.shape[0], dtype=np.uint16)
    instance_hit_by = np.zeros(action.shape[0], dtype=np.uint16)
    last_hit_by = np.full(action.shape[0], 0xFF, dtype=np.uint8)
    full = derive_magnify_damage_counter_x1910(
        action_id_u16=action,
        state_flags_u8=state_flags,
        camera_target_point_inside_stage_cam_bounds_u8=outside,
        percent_f32=percent,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        instance_hit_by_u16=instance_hit_by,
        last_hit_by_u8=last_hit_by,
        interval_frames=3,
        percent_limit=150,
        damage_amount=1,
    )
    # The third visible live-fighter row reaches the interval during the step, so the next seed
    # counter is reset. Inside-camera and match-flow camera bits do not count as magnifying-glass
    # damage time.
    assert full.tolist() == [0, 1, 2, 0, 0, 0, 0]

    ext = derive_magnify_damage_counter_x1910(
        action_id_u16=np.concatenate([action, np.array([wait, wait], dtype=np.uint16)]),
        state_flags_u8=np.concatenate(
            [state_flags, np.array([[0, 0, 0, 0, 0x80], [0, 0, 0, 0, 0x80]], dtype=np.uint8)],
            axis=0,
        ),
        camera_target_point_inside_stage_cam_bounds_u8=np.concatenate(
            [outside, np.array([0, 0], dtype=np.uint8)]
        ),
        percent_f32=np.concatenate([percent, np.array([8.0, 8.0], dtype=np.float32)]),
        hitlag_u16=np.concatenate([hitlag, np.zeros(2, dtype=np.uint16)]),
        hitstun_u16=np.concatenate([hitstun, np.zeros(2, dtype=np.uint16)]),
        instance_hit_by_u16=np.concatenate([instance_hit_by, np.zeros(2, dtype=np.uint16)]),
        last_hit_by_u8=np.concatenate([last_hit_by, np.full(2, 0xFF, dtype=np.uint8)]),
        interval_frames=3,
        percent_limit=150,
        damage_amount=1,
    )
    assert np.array_equal(ext[: action.shape[0]], full)

    no_tick = derive_magnify_damage_counter_x1910(
        action_id_u16=np.array([wait, wait, wait, wait], dtype=np.uint16),
        state_flags_u8=np.array([[0, 0, 0, 0, 0x80]] * 4, dtype=np.uint8),
        camera_target_point_inside_stage_cam_bounds_u8=np.zeros(4, dtype=np.uint8),
        percent_f32=np.array([7.0, 7.0, 7.0, 7.0], dtype=np.float32),
        hitlag_u16=np.zeros(4, dtype=np.uint16),
        hitstun_u16=np.zeros(4, dtype=np.uint16),
        instance_hit_by_u16=np.zeros(4, dtype=np.uint16),
        last_hit_by_u8=np.full(4, 0xFF, dtype=np.uint8),
        interval_frames=3,
        percent_limit=150,
        damage_amount=1,
    )
    assert no_tick.tolist() == [0, 0, 0, 0]

    disabled = derive_magnify_damage_counter_x1910(
        action_id_u16=np.array([wait, wait, wait, wait], dtype=np.uint16),
        state_flags_u8=np.array([[0, 0, 0, 0, 0x88]] * 4, dtype=np.uint8),
        camera_target_point_inside_stage_cam_bounds_u8=np.zeros(4, dtype=np.uint8),
        percent_f32=np.array([7.0, 7.0, 7.0, 8.0], dtype=np.float32),
        hitlag_u16=np.zeros(4, dtype=np.uint16),
        hitstun_u16=np.zeros(4, dtype=np.uint16),
        instance_hit_by_u16=np.zeros(4, dtype=np.uint16),
        last_hit_by_u8=np.full(4, 0xFF, dtype=np.uint8),
        interval_frames=3,
        percent_limit=150,
        damage_amount=1,
    )
    assert disabled.tolist() == [0, 0, 0, 0]

    contact_percent_gain = derive_magnify_damage_counter_x1910(
        action_id_u16=np.array([wait, wait, wait, wait], dtype=np.uint16),
        state_flags_u8=np.array([[0, 0, 0, 0, 0x80]] * 4, dtype=np.uint8),
        camera_target_point_inside_stage_cam_bounds_u8=np.zeros(4, dtype=np.uint8),
        percent_f32=np.array([7.0, 7.0, 7.0, 8.0], dtype=np.float32),
        hitlag_u16=np.array([0, 0, 0, 1], dtype=np.uint16),
        hitstun_u16=np.zeros(4, dtype=np.uint16),
        instance_hit_by_u16=np.array([0, 0, 0, 17], dtype=np.uint16),
        last_hit_by_u8=np.array([0xFF, 0xFF, 0xFF, 1], dtype=np.uint8),
        interval_frames=3,
        percent_limit=150,
        damage_amount=1,
    )
    assert contact_percent_gain.tolist() == [0, 0, 0, 0]


def test_camera_target_point_inside_uses_mslstg01_camera_bounds() -> None:
    # FoD's MSLSTG01 camera point bounds are narrower than the hand-authored display range. The
    # magnify damage owner uses the point-only Camera_80030CD8 branch input, not radius overlap.
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/cm/camera.c::Camera_80030CD8
    # data/stages/bin/griz.bin::MSLSTG01 cam_bounds_world
    inside = derive_camera_target_point_inside_stage_cam_bounds(
        stage_id_u32=2,
        camera_target_world_x_f32=np.array([0.0, 126.08677, -124.0], dtype=np.float32),
        camera_target_world_y_f32=np.array([0.0, 98.49836, 20.0], dtype=np.float32),
        camera_box_radius_f32=np.array([11.2, 11.2, 11.2], dtype=np.float32),
    )
    assert inside.tolist() == [1, 0, 0]


def test_derive_rebirth_camera_anchor_y_prefix_invariant() -> None:
    action_prefix = np.array([0x0000, 0x000C, 0x000C, 0x000C, 0x0014], dtype=np.uint16)
    full = derive_rebirth_camera_anchor_y(
        action_id_u16=action_prefix,
        stage_id_u32=np.uint32(32),
        respawn_point_y=45.0,
    )
    np.testing.assert_allclose(full, np.array([0.0, 45.0, 45.0, 45.0, 0.0], dtype=np.float32))

    action_ext = np.concatenate([action_prefix, np.array([0x000C, 0x0046, 0x000C], dtype=np.uint16)])
    ext = derive_rebirth_camera_anchor_y(
        action_id_u16=action_ext,
        stage_id_u32=np.uint32(32),
        respawn_point_y=45.0,
    )
    np.testing.assert_allclose(ext[: action_prefix.shape[0]], full)

    battlefield_stage = derive_rebirth_camera_anchor_y(
        action_id_u16=action_prefix,
        stage_id_u32=np.uint32(31),
        respawn_point_y=80.0,
    )
    np.testing.assert_allclose(
        battlefield_stage,
        np.array([0.0, 80.0, 80.0, 80.0, 0.0], dtype=np.float32),
    )


def test_derive_camera_target_world_prefix_invariant() -> None:
    char_prefix = np.array([1, 1, 22, 1], dtype=np.uint8)
    anim_prefix = np.array([2, 2, 2, 0xFFFFFFFF], dtype=np.uint32)
    anim_frame_prefix = np.array([30.0, 31.0, 31.0, -1.0], dtype=np.float32)
    scale_prefix = np.array([1.0, 1.0, 1.0, 1.0], dtype=np.float32)
    facing_prefix = np.array([1, 0, 0, 1], dtype=np.uint8)
    pos_x_prefix = np.array([-50.0, 16.0, 16.0, -12.0], dtype=np.float32)
    pos_y_prefix = np.array([78.34997, 78.34997, 77.19997, 10.0], dtype=np.float32)
    pos_z_prefix = np.zeros(4, dtype=np.float32)

    full = derive_camera_target_world(
        char_id_u8=char_prefix,
        animation_index_u32=anim_prefix,
        anim_frame_f32=anim_frame_prefix,
        fighter_scale_y_f32=scale_prefix,
        facing_u8=facing_prefix,
        pos_x_f32=pos_x_prefix,
        pos_y_f32=pos_y_prefix,
        pos_z_f32=pos_z_prefix,
    )

    np.testing.assert_allclose(
        full[0],
        np.array([-49.639732, 15.665469, 15.083799, 0.0], dtype=np.float32),
        atol=1e-5,
    )
    np.testing.assert_allclose(
        full[1],
        np.array([86.5845, 86.56182, 86.3869, 0.0], dtype=np.float32),
        atol=1e-4,
    )
    np.testing.assert_allclose(
        full[2],
        np.array([0.07064841, -0.07946108, -0.1099591, 0.0], dtype=np.float32),
        atol=1e-5,
    )
    np.testing.assert_allclose(
        full[3],
        np.array([11.2, 11.2, 11.2, 0.0], dtype=np.float32),
        atol=1e-5,
    )

    char_ext = np.concatenate([char_prefix, np.array([1, 1], dtype=np.uint8)])
    anim_ext = np.concatenate([anim_prefix, np.array([177, 178], dtype=np.uint32)])
    anim_frame_ext = np.concatenate([anim_frame_prefix, np.array([29.0, 29.0], dtype=np.float32)])
    scale_ext = np.concatenate([scale_prefix, np.array([1.0, 1.0], dtype=np.float32)])
    facing_ext = np.concatenate([facing_prefix, np.array([1, 1], dtype=np.uint8)])
    pos_x_ext = np.concatenate([pos_x_prefix, np.array([-157.48572, -168.23122], dtype=np.float32)])
    pos_y_ext = np.concatenate([pos_y_prefix, np.array([84.70425, 13.72867], dtype=np.float32)])
    pos_z_ext = np.concatenate([pos_z_prefix, np.zeros(2, dtype=np.float32)])

    ext = derive_camera_target_world(
        char_id_u8=char_ext,
        animation_index_u32=anim_ext,
        anim_frame_f32=anim_frame_ext,
        fighter_scale_y_f32=scale_ext,
        facing_u8=facing_ext,
        pos_x_f32=pos_x_ext,
        pos_y_f32=pos_y_ext,
        pos_z_f32=pos_z_ext,
    )
    for got, expected in zip(ext, full, strict=True):
        np.testing.assert_allclose(got[: char_prefix.shape[0]], expected, atol=1e-5)


def test_derive_camera_target_point_inside_stage_cam_bounds_prefix_invariant() -> None:
    x_prefix = np.array([15.083799, -158.3988, 15.63973, 0.0], dtype=np.float32)
    y_prefix = np.array([86.3869, 89.15867, 86.5845, 0.0], dtype=np.float32)
    r_prefix = np.array([11.2, 11.2, 11.2, 0.0], dtype=np.float32)

    full = derive_camera_target_point_inside_stage_cam_bounds(
        stage_id_u32=np.uint32(32),
        camera_target_world_x_f32=x_prefix,
        camera_target_world_y_f32=y_prefix,
        camera_box_radius_f32=r_prefix,
    )
    assert full.tolist() == [1, 1, 1, 0]

    x_ext = np.concatenate([x_prefix, np.array([171.0, -171.0], dtype=np.float32)])
    y_ext = np.concatenate([y_prefix, np.array([60.0, 60.0], dtype=np.float32)])
    r_ext = np.concatenate([r_prefix, np.array([11.2, 11.2], dtype=np.float32)])
    ext = derive_camera_target_point_inside_stage_cam_bounds(
        stage_id_u32=np.uint32(32),
        camera_target_world_x_f32=x_ext,
        camera_target_world_y_f32=y_ext,
        camera_box_radius_f32=r_ext,
    )
    assert np.array_equal(ext[: x_prefix.shape[0]], full)

    battlefield = derive_camera_target_point_inside_stage_cam_bounds(
        stage_id_u32=np.uint32(31),
        camera_target_world_x_f32=x_prefix,
        camera_target_world_y_f32=y_prefix,
        camera_box_radius_f32=r_prefix,
    )
    assert battlefield.tolist() == [1, 1, 1, 0]
