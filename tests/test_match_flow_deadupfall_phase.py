from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


STAGE_FD = 32
CHAR_FOX = 1
ACT_WAIT = 14
ACT_DASH = 20
ACT_DAMAGE_FLY_TOP = 90
ACT_DEAD_UP_STAR = 4
ACT_DEAD_UP_FALL = 6
ACT_DEAD_UP_FALL_HIT_CAMERA = 7
SM_WAIT = 2
SM_DAMAGE_FALL = 29
SM_DEAD_UP_FALL_HIT_CAMERA = 0
ROLLOUT_CLOCK_HSD_RAND_STREAM = 1
CAMERA_MODE_GAME = 0
CAMERA_MODE_FREE = 1
MAX_PLAYERS = 4


DEBUG_INTERNALS_DTYPE = np.dtype(
    [
        ("tilt_timer_x", ("u1", (MAX_PLAYERS,))),
        ("turn_frames_to_turn", ("u1", (MAX_PLAYERS,))),
        ("turn_has_turned", ("u1", (MAX_PLAYERS,))),
        ("guard_reflect_timer_x14", ("u1", (MAX_PLAYERS,))),
        ("entry_end_fall_lock", ("u1", (MAX_PLAYERS,))),
        ("attack_id", ("<u2", (MAX_PLAYERS,))),
        ("attack_instance", ("<u2", (MAX_PLAYERS,))),
        ("attack_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_x2073", ("u1", (MAX_PLAYERS,))),
        ("instance_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_counter", "<u2"),
        ("item_spawn_id_counter", "<u4"),
        ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_victim_port", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_hit_idx", ("u1", (MAX_PLAYERS,))),
        ("attached_victim_port", ("u1", (MAX_PLAYERS,))),
        ("dead_up_fall_offset_y", ("<f4", (MAX_PLAYERS,))),
        ("dead_up_fall_vel_y", ("<f4", (MAX_PLAYERS,))),
    ],
    align=False,
)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(40.0)
    seed["on_ground"][0, :2] = np.uint8(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)

    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT)
    return seed


def _step_once(
    seed: np.ndarray,
    *,
    replay_rollout: bool = False,
    replay_frame_rng_step: bool = False,
    hsd_rng_owned: bool = False,
    camera_mode: int = CAMERA_MODE_GAME,
) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        if replay_rollout:
            msl_binding.reseed_seed_rollout(handle, seed_bytes)
        else:
            msl_binding.reseed_seed(handle, seed_bytes)
        if hsd_rng_owned:
            # Debug-only hook for synthetic source locks: this makes the top-blast branch consume
            # the modeled HSD stream without deriving stream phase from a future replay label.
            # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
            msl_binding.debug_set_rollout_clock_mode(handle, 0, ROLLOUT_CLOCK_HSD_RAND_STREAM)
        if camera_mode != CAMERA_MODE_GAME:
            # Camera_8003010C is mode == CAMERA_FREE; this is hidden CObj/debug camera state, not a
            # replay outcome lane.
            # refs/melee/src/melee/cm/camera.c::Camera_8003010C
            msl_binding.debug_set_camera_mode(handle, 0, int(camera_mode))
        if replay_frame_rng_step:
            msl_binding.step_input_replay_frame_rng(handle, seed_bytes, prev_inp, inp)
        else:
            msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_deadupfall_countdown_enters_hitcamera_without_future_position_bridge() -> None:
    # ftCo_DeadUpFall_Anim case 1 transitions through ftCo_800D481C when x40 expires.
    # This uses only the causal x520/x528 timer owner, not a replay-next position delta.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D481C}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["match_flow_timer"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(out["animation_index"][0]) == SM_DEAD_UP_FALL_HIT_CAMERA


def test_replay_reseeded_top_blast_does_not_phase_deadupfall_from_future_label() -> None:
    # ftCo_800D3158 uses p_ftCommonData->x520 >= HSD_Randi(100)+1 when the camera is not free.
    # A replay one-step seed does not own all prefix HSD stream consumers before that callback, so
    # the sim must not use a future DeadUpFall label or guessed stream offset to force this branch.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    seed = _seed_base()
    seed["frame_pre_random_seed"][0] = np.uint32(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["pos_y"][0, 0] = np.float32(190.0)
    seed["speed_y_attack"][0, 0] = np.float32(3.0)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_STAR


def test_replay_reseeded_top_blast_keeps_deadupstar_without_rng_offset_lane() -> None:
    # Seed 10000 produces HSD_Randi(100)+1 == 50, above x520==16, so the source branch remains
    # DeadUpStar under a live HSD stream. Replay reseeds use the same visible outcome without any
    # ref_t1 action-derived offset lane.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    seed = _seed_base()
    seed["frame_pre_random_seed"][0] = np.uint32(10000)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["pos_y"][0, 0] = np.float32(190.0)
    seed["speed_y_attack"][0, 0] = np.float32(3.0)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_STAR
    assert int(out["stocks"][0]) == 4


def test_replay_rollout_top_blast_consumes_causal_frame_seed_without_future_label() -> None:
    # Replay playback owns the current row's frame-start RNG seed and modeled prefix consumers only
    # through the combined replay-frame RNG step API. With no prefix consumers in this synthetic
    # row, seed 0 causally produces HSD_Randi(100)+1 == 1 and enters DeadUpFall. This is not an
    # offset search and does not inspect ref_t1.action_id.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    seed = _seed_base()
    seed["frame_pre_random_seed"][0] = np.uint32(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["pos_y"][0, 0] = np.float32(190.0)
    seed["speed_y_attack"][0, 0] = np.float32(3.0)
    seed["action_id"][0, 1] = np.uint16(ACT_DASH)

    out = _step_once(seed, replay_rollout=True, replay_frame_rng_step=True)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL
    assert int(out["stocks"][0]) == 4


def test_hsd_owned_top_blast_uses_percent_roll_to_enter_deadupfall() -> None:
    # Source branch:
    # - y > Stage_GetBlastZoneTopOffset and upward KB admit the top-KO path,
    # - HSD_Randi(100)+1 is consumed from the current HSD stream,
    # - p_ftCommonData->x520 (16) selects DeadUpFall when the roll is <= 16.
    # Seed 0 produces roll 1 under HSD_Randi(100)+1.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    seed = _seed_base()
    seed["frame_pre_random_seed"][0] = np.uint32(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["pos_y"][0, 0] = np.float32(190.0)
    seed["speed_y_attack"][0, 0] = np.float32(3.0)

    out = _step_once(seed, hsd_rng_owned=True)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL
    assert int(out["animation_index"][0]) == SM_DAMAGE_FALL
    assert int(out["stocks"][0]) == 4


def test_hsd_owned_top_blast_camera_free_forces_deadupstar_after_roll_consume() -> None:
    # ftCo_800D3158 samples HSD_Randi(100)+1, then requires !Camera_8003010C for DeadUpFall.
    # CAMERA_FREE therefore forces DeadUpStar even when the roll would pass x520.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/melee/src/melee/cm/camera.c::Camera_8003010C
    seed = _seed_base()
    seed["frame_pre_random_seed"][0] = np.uint32(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["pos_y"][0, 0] = np.float32(190.0)
    seed["speed_y_attack"][0, 0] = np.float32(3.0)

    out = _step_once(seed, hsd_rng_owned=True, camera_mode=CAMERA_MODE_FREE)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_STAR


def test_deadupfall_hitcamera_hold_expiry_advances_phase3_fall_velocity() -> None:
    # ftCo_DeadUpFall_Anim case 2 writes self_vel.y=x550, then ftCo_DeadUpFall_Phys case 3
    # immediately applies ftCommon_Fall with x554/x558. That velocity is added into
    # mv.co.unk_deadup.x5C/x50 for the hit-camera/effect offset; the fighter root pose and public
    # velocity lanes remain fixed.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_DeadUpFall_Phys}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(76)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert float(out["speed_y_self"][0]) == pytest.approx(float(np.float32(0.8)), abs=1e-6)
    assert float(out["pos_y"][0]) == pytest.approx(float(np.float32(40.8)), abs=1e-6)


def test_deadupfall_hitcamera_phase3_hidden_offset_advances_each_frame() -> None:
    # ftCo_DeadUpFall_Phys case 3 applies ftCommon_Fall every phase-3 frame, adds the current
    # self_vel into mv.co.unk_deadup.x5C/x50, then clears the scratch velocity before the next
    # frame. The root pose stays fixed; the hidden offset must advance by 0.8, then 0.6 with the
    # extracted x550/x554 values rather than repeating the first phase-3 tick.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpFall_Phys
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    internals_stride = int(sizes["internals"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert internals_stride == DEBUG_INTERNALS_DTYPE.itemsize

    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(76)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    internals_raw = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        msl_binding.debug_write_internals(handle, internals_raw)
        first_out = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        first_int = internals_raw.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy()

        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        msl_binding.debug_write_internals(handle, internals_raw)
        second_out = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        second_int = internals_raw.view(DEBUG_INTERNALS_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    assert int(first_out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(second_out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert float(first_out["pos_y"][0]) == pytest.approx(float(np.float32(40.8)), abs=1e-6)
    assert float(second_out["pos_y"][0]) == pytest.approx(float(np.float32(41.4)), abs=5e-6)
    assert float(first_out["speed_y_self"][0]) == pytest.approx(float(np.float32(0.8)), abs=1e-6)
    assert float(second_out["speed_y_self"][0]) == pytest.approx(float(np.float32(0.6)), abs=1e-6)
    assert float(first_int["dead_up_fall_offset_y"][0]) == pytest.approx(0.8, abs=1e-6)
    assert float(second_int["dead_up_fall_offset_y"][0]) == pytest.approx(1.4, abs=1e-6)
    assert float(first_int["dead_up_fall_vel_y"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(second_int["dead_up_fall_vel_y"][0]) == pytest.approx(0.0, abs=1e-6)


def test_deadupfall_hitcamera_phase3_expiry_loses_stock_once() -> None:
    # ftCo_DeadUpFall_Anim case 3 calls ftCo_800D34E0 when the phase-3 timer expires.
    # Lock the boundary at prev_t == x534 + 1 so the stock loss does not drift by one frame.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D34E0}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(36)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(out["stocks"][0]) == 3


def test_deadupfall_hitcamera_phase3_expiry_clears_fall_velocity_before_phase4() -> None:
    # ftCo_DeadUpFall_Anim case 3 calls ftCommon_8007E2FC before ftCo_800D34E0, so the invisible
    # phase-4 death hold does not keep integrating the last phase-3 self velocity.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpFall_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(36)
    seed["speed_y_self"][0, 0] = np.float32(-1.7)
    seed["pos_y"][0, 0] = np.float32(200.0)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(out["stocks"][0]) == 3
    assert float(out["speed_y_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["pos_y"][0]) == pytest.approx(200.0, abs=1e-6)
