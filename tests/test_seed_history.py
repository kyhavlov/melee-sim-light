from __future__ import annotations

from pathlib import Path

import numpy as np

from tools.slippi.combat_history import derive_combat_rehit_seed_fields
from tools.slippi.seed_history import (
    compute_fighter_button_timers,
    compute_fighter_stick_input_counters,
    compute_fighter_trigger_input_counters,
    compute_press_timer_u8,
    compute_tilt_timer_axis,
    compute_tilt_timer_y_pre_post_with_fall_fast,
    compute_x672_trigger_timer_pre_post,
    derive_ucf_pad_buffer_state,
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


def _read_hitbox_msid_frames(path: str, *, limit: int = 512) -> list[tuple[int, int]]:
    import struct

    buf = Path(path).read_bytes()
    if len(buf) < 16 or buf[:8] != b"MSLHITB1":
        raise ValueError("bad MSLHITB1")
    ver = int.from_bytes(buf[8:12], "little", signed=False)
    if ver != 1:
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


def test_derive_combat_rehit_seed_fields_is_causal_wrt_future_frames() -> None:
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
        on_ground = z_u8.copy()
        on_ground[:, 0] = np.uint8(1)
        on_ground[:, 1] = np.uint8(1)
        pos_x = z_f32.copy()
        pos_y = z_f32.copy()
        scale_y = np.ones((n, 4), dtype=np.float32)
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

        rehit_active, _hb_id, _att_msid, _def_iid = derive_combat_rehit_seed_fields(
            num_players=2,
            is_teams=False,
            team_id=z_u8,
            char_id=char_id,
            action_id=action_id,
            action_frame=action_frame,
            animation_index=animation_index,
            on_ground=on_ground,
            pos_x=pos_x,
            pos_y=pos_y,
            fighter_scale_y=scale_y,
            stocks=stocks,
            shield_hp=shield_hp,
            hurtbox_state=z_u8,
            instance_id=instance_id,
            input_buttons=input_buttons,
            input_l=input_l,
            input_r=input_r,
            data_root="data",
        )
        if int(rehit_active[0, 0, 1]) == 1:
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
    on_ground0 = base_u8.copy()
    on_ground0[:, 0] = np.uint8(1)
    on_ground0[:, 1] = np.uint8(1)
    pos_x0 = base_f32.copy()
    pos_y0 = base_f32.copy()
    scale0 = np.ones((n0, 4), dtype=np.float32)
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

    a0, hb0, ms0, di0 = derive_combat_rehit_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=base_u8,
        char_id=char0,
        action_id=action0,
        action_frame=af0,
        animation_index=anim0,
        on_ground=on_ground0,
        pos_x=pos_x0,
        pos_y=pos_y0,
        fighter_scale_y=scale0,
        stocks=stocks0,
        shield_hp=shield0,
        hurtbox_state=base_u8,
        instance_id=iid0,
        input_buttons=buttons0,
        input_l=l0,
        input_r=r0,
        data_root="data",
    )

    assert int(a0[0, 0, 1]) == 1

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
    on_ground1 = np.zeros((n1, 4), dtype=np.uint8)
    on_ground1[:n0] = on_ground0
    pos_x1 = np.zeros((n1, 4), dtype=np.float32)
    pos_x1[:n0] = pos_x0
    pos_y1 = np.zeros((n1, 4), dtype=np.float32)
    pos_y1[:n0] = pos_y0
    scale1 = np.ones((n1, 4), dtype=np.float32)
    stocks1 = np.zeros((n1, 4), dtype=np.uint8)
    stocks1[:n0] = stocks0
    shield1 = np.zeros((n1, 4), dtype=np.float32)
    iid1 = np.zeros((n1, 4), dtype=np.uint16)
    iid1[:n0] = iid0
    buttons1 = np.zeros((n1, 4), dtype=np.uint16)
    l1 = np.zeros((n1, 4), dtype=np.uint8)
    r1 = np.zeros((n1, 4), dtype=np.uint8)

    a1, hb1, ms1, di1 = derive_combat_rehit_seed_fields(
        num_players=2,
        is_teams=False,
        team_id=np.zeros((n1, 4), dtype=np.uint8),
        char_id=char1,
        action_id=action1,
        action_frame=af1,
        animation_index=anim1,
        on_ground=on_ground1,
        pos_x=pos_x1,
        pos_y=pos_y1,
        fighter_scale_y=scale1,
        stocks=stocks1,
        shield_hp=shield1,
        hurtbox_state=np.zeros((n1, 4), dtype=np.uint8),
        instance_id=iid1,
        input_buttons=buttons1,
        input_l=l1,
        input_r=r1,
        data_root="data",
    )

    assert np.array_equal(a0, a1[: a0.shape[0]])
    assert np.array_equal(hb0, hb1[: hb0.shape[0]])
    assert np.array_equal(ms0, ms1[: ms0.shape[0]])
    assert np.array_equal(di0, di1[: di0.shape[0]])


def test_fighter_stick_input_counters_are_causal_wrt_future_frames() -> None:
    # Prefix includes a neutral->right entry (lb_8000D148 zeroing) and a right->left flip.
    sx_prefix = np.array([0.0, 0.9, 0.9, -0.9, -0.9], dtype=np.float32)
    sy_prefix = np.zeros(sx_prefix.shape[0], dtype=np.float32)

    x673_0, x674_0, x676_x_0, x677_y_0, x679_x_0, x67A_y_0 = compute_fighter_stick_input_counters(
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

    sx_ext = np.concatenate([sx_prefix, np.array([0.0, 0.9, 0.0], dtype=np.float32)])
    sy_ext = np.zeros(sx_ext.shape[0], dtype=np.float32)
    x673_1, x674_1, x676_x_1, x677_y_1, x679_x_1, x67A_y_1 = compute_fighter_stick_input_counters(
        stick_x_unit=sx_ext,
        stick_y_unit=sy_ext,
        tilt_thresh_x=0.25,
        tilt_thresh_y=0.25,
        start_timer=0xFE,
    )

    assert np.array_equal(x673_0, x673_1[: x673_0.size])
    assert np.array_equal(x674_0, x674_1[: x674_0.size])
    assert np.array_equal(x676_x_0, x676_x_1[: x676_x_0.size])
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
