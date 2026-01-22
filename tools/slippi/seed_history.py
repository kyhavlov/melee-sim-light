from __future__ import annotations

import struct
from pathlib import Path

import numpy as np


def _sign_i16(x: np.ndarray) -> np.ndarray:
    # Match src/ucf.c::msl_sign_s8: returns +1 for 0 (though we never call it with 0).
    return np.where(x < 0, np.int16(-1), np.int16(1))


def ucf_process_stick_i8(
    raw_x: np.ndarray,
    raw_y: np.ndarray,
    *,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Vectorized mirror of src/ucf.c::ucf_process_stick_i8.

    Returns int8 arrays in Melee-legalized range (radius-clamped to 80),
    optionally with UCF 1.0 cardinals snap applied.
    """
    x0 = np.asarray(raw_x, dtype=np.int8).astype(np.int16)
    y0 = np.asarray(raw_y, dtype=np.int8).astype(np.int16)

    x = x0.copy()
    y = y0.copy()

    if ucf_enabled and ucf_cardinals_1_0_enabled:
        snap_range = np.int16(6)  # src/ucf.c::SNAP_RANGE

        cond_x = ((x0 <= -80) | (x0 >= 80)) & (y0 >= -snap_range) & (y0 <= snap_range)
        x = np.where(cond_x, _sign_i16(x0) * np.int16(80), x)
        y = np.where(cond_x, np.int16(0), y)

        cond_y = (~cond_x) & ((y0 <= -80) | (y0 >= 80)) & (x0 >= -snap_range) & (x0 <= snap_range)
        x = np.where(cond_y, np.int16(0), x)
        y = np.where(cond_y, _sign_i16(y0) * np.int16(80), y)

    fx = x.astype(np.float32)
    fy = y.astype(np.float32)
    r = np.sqrt((fx * fx) + (fy * fy))

    scale = np.ones_like(r, dtype=np.float32)
    mask = r > np.float32(80.0)
    scale[mask] = np.float32(80.0) / r[mask]

    x_clamped = np.trunc(fx * scale).astype(np.int16)
    y_clamped = np.trunc(fy * scale).astype(np.int16)

    return x_clamped.astype(np.int8), y_clamped.astype(np.int8)


def apply_deadzone(v: np.ndarray, dz: float) -> np.ndarray:
    v = np.asarray(v, dtype=np.float32)
    out = v.copy()
    out[np.abs(out) < np.float32(dz)] = np.float32(0.0)
    return out


def stick_i8_to_unit(v: np.ndarray) -> np.ndarray:
    # Match src/locomotion.c::stick_i8_to_unit (MSL_STICK_MAX_I8=80).
    return np.asarray(v, dtype=np.float32) / np.float32(80.0)


def compute_tilt_timer_axis(
    axis_unit: np.ndarray,
    *,
    tilt_thresh: float,
    start_timer: int = 0xFE,
) -> np.ndarray:
    """
    Compute fp->x670_timer_lstick_tilt_x or fp->x671_timer_lstick_tilt_y per frame.

    Decomp reference: refs/melee/src/melee/ft/fighter.c:1908-2008.
    """
    axis = np.asarray(axis_unit, dtype=np.float32).reshape(-1)
    n = int(axis.size)
    out = np.empty(n, dtype=np.uint8)

    prev_axis = np.float32(0.0)
    timer = int(start_timer) & 0xFF

    thresh = np.float32(tilt_thresh)

    for i in range(n):
        a = np.float32(axis[i])
        if a >= thresh:
            if prev_axis >= thresh:
                timer += 1
                if timer > 0xFE:
                    timer = 0xFE
            else:
                timer = 0
        elif a <= -thresh:
            if prev_axis <= -thresh:
                timer += 1
                if timer > 0xFE:
                    timer = 0xFE
            else:
                timer = 0
        else:
            timer = 0xFE

        out[i] = np.uint8(timer)
        prev_axis = a

    return out


def _ucf_popo_to_nana(x: float) -> float:
    # refs/ucf/include/util/melee/pad.h::popo_to_nana
    if x >= 0:
        return float(np.int8(np.float32(x) * np.float32(127.0))) / 127.0
    return float(np.int8(np.float32(x) * np.float32(128.0))) / 128.0


def derive_ucf_pad_buffer_state(
    raw_x: np.ndarray,
    raw_y: np.ndarray,
    *,
    stick_y_hold_time: np.ndarray,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    lstick_deadzone_x: float,
    lstick_deadzone_y: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive the UCF 0.84 pad-buffer internal state strictly causally.

    State matches refs/ucf/include/ucf/pad_buffer.h:
    - entries[4].stick (raw PADStatus stick bytes)
    - index (u8 ring index)
    - sdrop_up_frames (u8)

    Ordering matches refs/ucf/src/pad_buffer/pad_buffer.cpp:
    - index = (index + 1) & 3
    - entries[index] = get_input<0>(port).stick  (raw bytes)
    - sdrop_up_frames update uses player->input.stick (post-UCF cardinals + Melee clamp, then deadzone)

    Notes:
    - Source tie-down:
      - UCF's `player->input.stick_y_hold_time` is a u8 at offset 0x671:
        refs/ucf/include/melee/asm/player.h
      - In decomp, the per-frame update for fp->x671_timer_lstick_tilt_y is:
        refs/melee/src/melee/ft/fighter.c:1963-2008
    - Assumption: the dataset's `stick_y_hold_time` input here is the x671-style timer computed
      from the post-processed stick y (UCF cardinals snap + Melee clamp, then deadzone), and
      corresponds closely enough to UCF's hold timer for gating `check_sdrop_up`.
    - Ordering assumption to revisit if shielddrop behavior is off later:
      UCF's pad-buffer injection applies 1.0 cardinals before `check_sdrop_up`
      (refs/ucf/src/pad_buffer/pad_buffer.cpp), but we haven't proven whether Melee updates
      `stick_y_hold_time` using pre- or post-injection stick values.
    """
    rx = np.asarray(raw_x, dtype=np.int8).reshape(-1)
    ry = np.asarray(raw_y, dtype=np.int8).reshape(-1)
    hold_y = np.asarray(stick_y_hold_time, dtype=np.uint8).reshape(-1)
    if rx.shape != ry.shape:
        raise ValueError("raw_x and raw_y must have the same shape")
    n = int(rx.size)
    if int(hold_y.size) != n:
        raise ValueError("stick_y_hold_time must match raw_x/raw_y length")

    # Precompute the Melee-legalized stick used by UCF's sdrop-up gate.
    proc_x_i8, proc_y_i8 = ucf_process_stick_i8(
        rx,
        ry,
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    stick_x_unit = apply_deadzone(stick_i8_to_unit(proc_x_i8), float(lstick_deadzone_x))
    stick_y_unit = apply_deadzone(stick_i8_to_unit(proc_y_i8), float(lstick_deadzone_y))

    # Vectorized rim test for each frame.
    # refs/ucf/include/util/melee/pad.h::is_rim_coord
    bias = np.float32(0.0001)  # abs_coord_to_int
    ix = np.trunc(np.abs(stick_x_unit) * np.float32(80.0) - bias).astype(np.int32) + 2
    iy = np.trunc(np.abs(stick_y_unit) * np.float32(80.0) - bias).astype(np.int32) + 2
    is_rim = (ix * ix + iy * iy) > (80 * 80)

    # UCF check_ucf_sdrop uses delta between current and -2 raw y.
    dy = ry.astype(np.int16) - np.concatenate((np.int16([0, 0]), ry[:-2].astype(np.int16)))
    dy_sq = (dy * dy).astype(np.int32)

    index_post = np.empty(n, dtype=np.uint8)
    sdrop_up_frames_post = np.empty(n, dtype=np.uint8)
    stick_x_post = np.empty((n, 4), dtype=np.int8)
    stick_y_post = np.empty((n, 4), dtype=np.int8)

    # Local ring buffer state (packed like UCF's shared struct).
    entries_x = np.zeros(4, dtype=np.int8)
    entries_y = np.zeros(4, dtype=np.int8)
    index = np.uint8(0)
    sdrop = np.uint8(0)

    # Constants from UCF pad-buffer implementation.
    sdrop_y_thresh = _ucf_popo_to_nana(-0.6125)  # refs/ucf/src/pad_buffer/pad_buffer.cpp::check_sdrop_up
    sdrop_delta_sq_thresh = 44 * 44  # refs/ucf/src/pad_buffer/pad_buffer.cpp::check_ucf_sdrop

    for i in range(n):
        index = np.uint8((int(index) + 1) & 3)  # UCF_PAD_BUFFER_MASK
        entries_x[int(index)] = rx[i]
        entries_y[int(index)] = ry[i]

        # UCF check_sdrop_up.
        if float(stick_y_unit[i]) > float(sdrop_y_thresh):
            sdrop = np.uint8(0)
        elif not bool(is_rim[i]):
            sdrop = np.uint8(0)
        else:
            if int(sdrop) != 0:
                sdrop = np.uint8((int(sdrop) + 1) & 0xFF)
            else:
                if int(hold_y[i]) < 2 and int(dy_sq[i]) > sdrop_delta_sq_thresh:
                    sdrop = np.uint8(1)
                else:
                    sdrop = np.uint8(0)

        index_post[i] = index
        sdrop_up_frames_post[i] = sdrop
        stick_x_post[i, :] = entries_x
        stick_y_post[i, :] = entries_y

    return index_post, sdrop_up_frames_post, stick_x_post, stick_y_post


def compute_tilt_timer_axis_pre_post(
    axis_unit: np.ndarray,
    *,
    tilt_thresh: float,
    override_post_mask: np.ndarray | None = None,
    override_post_value: int = 0xFE,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Compute per-frame (timer_pre, timer_post) for x670/x671.

    - timer_pre: value after the per-frame input update (used for input gating in that frame)
    - timer_post: value after action-entry overrides that can occur later in the frame

    Decomp references:
    - fighter input update: refs/melee/src/melee/ft/fighter.c:1908-2008
    - action-entry overrides vary by action (e.g. Dash/Jump)
    """
    axis = np.asarray(axis_unit, dtype=np.float32).reshape(-1)
    n = int(axis.size)
    out_pre = np.empty(n, dtype=np.uint8)
    out_post = np.empty(n, dtype=np.uint8)

    if override_post_mask is None:
        override = None
    else:
        override = np.asarray(override_post_mask, dtype=bool).reshape(-1)
        if int(override.size) != n:
            raise ValueError("override_post_mask must match axis_unit length")

    prev_axis = np.float32(0.0)
    timer_post = int(start_timer_post) & 0xFF
    thresh = np.float32(tilt_thresh)
    override_value = int(override_post_value) & 0xFF

    for i in range(n):
        a = np.float32(axis[i])
        timer_pre = timer_post
        if a >= thresh:
            if prev_axis >= thresh:
                timer_pre += 1
                if timer_pre > 0xFE:
                    timer_pre = 0xFE
            else:
                timer_pre = 0
        elif a <= -thresh:
            if prev_axis <= -thresh:
                timer_pre += 1
                if timer_pre > 0xFE:
                    timer_pre = 0xFE
            else:
                timer_pre = 0
        else:
            timer_pre = 0xFE

        timer_post = timer_pre
        if override is not None and bool(override[i]):
            timer_post = override_value

        out_pre[i] = np.uint8(timer_pre)
        out_post[i] = np.uint8(timer_post)
        prev_axis = a

    return out_pre, out_post


def load_shield_tilt_table_meta(*, data_dir: str = "data") -> dict[int, tuple[int, int]]:
    """
    Load (neutral_frame, frame_max) for extracted MSLSHLD1 guard tilt tables.

    Source of truth: `data/shields/<character>.bin` (ISO-derived via tools/extraction/extract_shield_tilt_table.py).
    Binary format: tools/extraction/extract_shield_tilt_table.py::_write_table.
    """
    # Character ids (GALE01): Fox=1, Falco=22.
    char_files = {
        1: "fox.bin",
        22: "falco.bin",
    }
    out: dict[int, tuple[int, int]] = {}
    base = Path(str(data_dir)) / "shields"
    for char_id, fname in char_files.items():
        p = base / fname
        if not p.exists():
            raise FileNotFoundError(
                f"missing shield tilt table {p} (run tools.extraction.build_data to generate data/ artifacts)"
            )
        buf = p.read_bytes()
        if len(buf) < 8 + 4 + 2 + 2:
            raise ValueError(f"{p}: too small for MSLSHLD1 header (size={len(buf)})")
        if buf[:8] != b"MSLSHLD1":
            raise ValueError(f"{p}: bad magic (want MSLSHLD1)")

        ver = struct.unpack_from("<I", buf, 8)[0]
        if ver != 1:
            raise ValueError(f"{p}: unsupported MSLSHLD1 version={ver} (want 1)")

        frame_count, neutral_frame = struct.unpack_from("<HH", buf, 12)
        if frame_count == 0:
            raise ValueError(f"{p}: frame_count is 0")
        if neutral_frame >= frame_count:
            raise ValueError(
                f"{p}: neutral_frame out of range (neutral_frame={neutral_frame}, frame_count={frame_count})"
            )

        want = 8 + 4 + 2 + 2 + int(frame_count) * 3 * 4
        if len(buf) != want:
            raise ValueError(f"{p}: size mismatch (got {len(buf)}, want {want})")

        out[int(char_id)] = (int(neutral_frame), int(frame_count - 1))
    return out


def derive_guard_tilt_state(
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    *,
    facing: np.ndarray,
    action_id: np.ndarray,
    action_frame: np.ndarray,
    neutral_frame: np.ndarray,
    frame_max: np.ndarray,
    guard_stick_lerp_x44c: float,
    act_guard_on: int,
    act_guard: int,
    act_guard_reflect: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive decomp-shaped guard tilt pose state (mv.co.guard.x8 + mv.co.guard.x4) strictly causally.

    Mirrors refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:
    - ftCo_800921DC initializes x8=neutral (10 in GALE01) and x4=0 on GuardOn entry.
    - ftCo_80091BC4 updates:
        x8 = neutral + normalizeAngle0(normalizeAngle180(deg - (x8-neutral)) * x44C + (x8-neutral))
        x4 = x44C * (mag - x4) + x4

    Returns arrays (x8_post_u16, x4_post_f32) with length N, where "post" means after the per-frame update.
    """
    sx = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    sy = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    fac = np.asarray(facing, dtype=np.uint8).reshape(-1)
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    afr = np.asarray(action_frame, dtype=np.int16).reshape(-1)
    neu = np.asarray(neutral_frame, dtype=np.uint16).reshape(-1)
    fmax = np.asarray(frame_max, dtype=np.uint16).reshape(-1)

    n = int(sx.size)
    if int(sy.size) != n or int(fac.size) != n or int(aid.size) != n or int(afr.size) != n:
        raise ValueError("input arrays must have the same length")
    if int(neu.size) != n or int(fmax.size) != n:
        raise ValueError("neutral_frame/frame_max must have the same length as stick arrays")

    out_x8 = np.empty(n, dtype=np.uint16)
    out_x4 = np.empty(n, dtype=np.float32)

    # Persistent state (seeded across frames).
    x8 = np.uint16(0)
    x4 = np.float32(0.0)

    lerp = np.float32(float(guard_stick_lerp_x44c))
    pi = np.float32(3.14159265358979323846)
    rad_to_deg = np.float32(180.0) / pi

    for i in range(n):
        a = int(aid[i])
        neutral_i = np.uint16(neu[i])
        frame_max_i = np.uint16(fmax[i])

        # Decomp init on GuardOn entry.
        if a == int(act_guard_on) and int(afr[i]) == 0:
            x8 = neutral_i
            x4 = np.float32(0.0)

        if a == int(act_guard_on) or a == int(act_guard) or a == int(act_guard_reflect):
            facing_dir = np.float32(1.0) if int(fac[i]) != 0 else np.float32(-1.0)
            x = np.float32(sx[i]) * facing_dir
            y = np.float32(sy[i])

            rad = np.float32(np.arctan2(y, x))
            if rad < np.float32(0.0):
                rad = rad + np.float32(2.0) * pi

            deg = np.float32(rad * rad_to_deg)
            if deg < np.float32(0.0):
                deg = np.float32(0.0)
            if deg > np.float32(359.0):
                deg = np.float32(359.0)

            offset = np.float32(np.float32(x8) - np.float32(neutral_i))
            delta = np.float32(deg - offset)
            if delta > np.float32(180.0):
                delta = delta - np.float32(360.0)
            elif delta < np.float32(-180.0):
                delta = delta + np.float32(360.0)

            next_offset = np.float32(delta * lerp + offset)
            if next_offset > np.float32(360.0):
                next_offset = next_offset - np.float32(360.0)
            elif next_offset < np.float32(0.0):
                next_offset = next_offset + np.float32(360.0)

            next_x8_f = np.float32(np.float32(neutral_i) + next_offset)
            next_x8 = int(next_x8_f)  # truncation toward 0 (matches C cast for nonnegative values)
            if next_x8 < 0:
                next_x8 = 0
            if next_x8 > int(frame_max_i):
                next_x8 = int(frame_max_i)
            x8 = np.uint16(next_x8)

            mag = np.sqrt(np.float32(sx[i] * sx[i] + sy[i] * sy[i])).astype(np.float32)
            if mag > np.float32(1.0):
                mag = np.float32(1.0)
            if mag < np.float32(0.0):
                mag = np.float32(0.0)
            x4 = np.float32(lerp * (mag - x4) + x4)

        out_x8[i] = x8
        out_x4[i] = x4

    return out_x8, out_x4


def compute_tilt_timer_y_pre_post_with_fall_fast(
    stick_y_unit: np.ndarray,
    *,
    tilt_thresh: float,
    jump_entry: np.ndarray,
    fastfall_ok: np.ndarray,
    speed_y_self_post: np.ndarray,
    on_ground_post: np.ndarray,
    fastfall_stick_threshold: float,
    fastfall_tilt_max_frames: int,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute (tilt_timer_y_pre, tilt_timer_y_post, fall_fast_post) per frame, causally.

    Modeled decomp behaviors:
    - x671 per-frame update from inputs: refs/melee/src/melee/ft/fighter.c:1908-2008
    - Jump entry override: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump*.c (sets x671=0xFE)
    - Fastfall latch + x671 override: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)

    Notes:
    - `speed_y_self_post` is Slippi post-frame self velocity.
    - ftCommon_CheckFallFast uses start-of-frame `self_vel.y`, which we model as `speed_y_self_post[i-1]`.
    - `on_ground_post` is Slippi post-frame grounding; we clear fall_fast in the post snapshot when grounded.
    """
    axis = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    n = int(axis.size)

    jump_entry_b = np.asarray(jump_entry, dtype=bool).reshape(-1)
    if int(jump_entry_b.size) != n:
        raise ValueError("jump_entry must match stick_y_unit length")

    fastfall_ok_b = np.asarray(fastfall_ok, dtype=bool).reshape(-1)
    if int(fastfall_ok_b.size) != n:
        raise ValueError("fastfall_ok must match stick_y_unit length")

    vy_post = np.asarray(speed_y_self_post, dtype=np.float32).reshape(-1)
    if int(vy_post.size) != n:
        raise ValueError("speed_y_self_post must match stick_y_unit length")

    on_ground_post_b = np.asarray(on_ground_post, dtype=bool).reshape(-1)
    if int(on_ground_post_b.size) != n:
        raise ValueError("on_ground_post must match stick_y_unit length")

    out_pre = np.empty(n, dtype=np.uint8)
    out_post = np.empty(n, dtype=np.uint8)
    fall_fast_post = np.empty(n, dtype=np.uint8)

    thresh = np.float32(tilt_thresh)
    stick_thresh = np.float32(fastfall_stick_threshold)
    tilt_max = int(fastfall_tilt_max_frames)

    prev_axis = np.float32(0.0)
    timer_post = int(start_timer_post) & 0xFF
    fall_fast_prev_post = np.uint8(0)
    vy_prev_post = np.float32(0.0)
    on_ground_prev_post = bool(on_ground_post_b[0]) if n > 0 else False

    for i in range(n):
        a = np.float32(axis[i])

        # Start-of-frame values for ftCommon_CheckFallFast for this frame i.
        on_ground_start = on_ground_prev_post
        vy_start = vy_prev_post
        fall_fast_start = fall_fast_prev_post

        # x671 input update (timer_pre) from the previous post value.
        t_pre = int(timer_post) & 0xFF
        if a >= thresh:
            if prev_axis >= thresh:
                t_pre += 1
                if t_pre > 0xFE:
                    t_pre = 0xFE
            else:
                t_pre = 0
        elif a <= -thresh:
            if prev_axis <= -thresh:
                t_pre += 1
                if t_pre > 0xFE:
                    t_pre = 0xFE
            else:
                t_pre = 0
        else:
            t_pre = 0xFE

        # Start with post==pre, then apply per-action overrides.
        t_post = t_pre
        if bool(jump_entry_b[i]):
            t_post = 0xFE
            # Jump entry clears fall_fast in our core model (motion state transition does not keep it).
            # This matches the intent of keeping upward motion from being overridden by FallFast.
            fall_fast_start = np.uint8(0)

        # ftCommon_CheckFallFast (causal gate).
        # refs/melee/src/melee/ft/ftcommon.c:505-520
        ff_after = fall_fast_start
        if (not on_ground_start) and bool(fastfall_ok_b[i]):
            if (not fall_fast_start) and (vy_start < np.float32(0.0)) and (a <= -stick_thresh) and (
                t_post < tilt_max
            ):
                ff_after = np.uint8(1)
                t_post = 0xFE

        # Clear fall_fast when grounded in the post snapshot.
        ff_post = np.uint8(0) if bool(on_ground_post_b[i]) else ff_after

        out_pre[i] = np.uint8(t_pre)
        out_post[i] = np.uint8(t_post)
        fall_fast_post[i] = ff_post

        # Next frame.
        prev_axis = a
        timer_post = t_post
        fall_fast_prev_post = ff_post
        vy_prev_post = np.float32(vy_post[i])
        on_ground_prev_post = bool(on_ground_post_b[i])

    return out_pre, out_post, fall_fast_post


def derive_turn_internals(
    *,
    action_id: np.ndarray,
    facing: np.ndarray,
    stick_x_unit: np.ndarray,
    tilt_timer_x: np.ndarray,
    dash_flick_abs: float,
    dash_flick_tilt_max_frames: int,
    turn_frames: np.ndarray,
    act_turn: int,
    act_turn_run: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive TURN internals (`frames_to_turn`, `has_turned`) per frame from replay history.

    Decomp reference: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88.

    Causality: this function is strictly causal and does not look ahead to "calibrate" based on
    future outcomes (e.g. post-frame facing flips).

    Deterministic assumption (do NOT tune via one-step mismatch metrics):
    - If TURN entry-frame ordering is ambiguous, we assume `ftCo_Turn_Anim_Inner` applies once on
      the entry frame (same frame the action switches into TURN/TURN_RUN). This matches the
      simulator's current update ordering.
    - This assumption is expected to be revisited once we seed more complete entry history
      (notably KneeBend/jump-squat entry history), rather than being "trained" against outcomes.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    facing_u8 = np.asarray(facing, dtype=np.uint8).reshape(-1)
    stick_x = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    ttx = np.asarray(tilt_timer_x, dtype=np.uint8).reshape(-1)
    tf = np.asarray(turn_frames, dtype=np.uint8).reshape(-1)

    n = int(a.size)
    out_frames = np.zeros(n, dtype=np.uint8)
    out_has = np.zeros(n, dtype=np.uint8)

    frames_to_turn = 0
    has_turned = 0
    prev_in_turn = False

    dash_max = int(dash_flick_tilt_max_frames)
    dash_abs = np.float32(dash_flick_abs)

    for i in range(n):
        cur_act = int(a[i])
        cur_in_turn = cur_act in (act_turn, act_turn_run)
        if not cur_in_turn:
            frames_to_turn = 0
            has_turned = 0
            prev_in_turn = False
            continue

        if not prev_in_turn:
            # TURN entry (action transition into TURN/TURN_RUN).
            #
            # Determine standing turn vs smash turn (dash-flick opposite-facing) using current input
            # and the previous frame's facing_dir (causal).
            is_smash = False
            if cur_act == act_turn and i > 0:
                facing_before = int(facing_u8[i - 1])
                facing_dir = np.float32(1.0 if facing_before else -1.0)
                if (
                    np.abs(stick_x[i]) >= dash_abs
                    and int(ttx[i]) < dash_max
                    and (stick_x[i] * facing_dir) < np.float32(0.0)
                ):
                    is_smash = True

            frames_to_turn = 0 if is_smash else int(tf[i])
            if frames_to_turn < 0:
                frames_to_turn = 0
            if frames_to_turn > 0xFE:
                frames_to_turn = 0xFE
            has_turned = 0

        # Apply one ftCo_Turn_Anim_Inner tick for this frame (post-frame snapshot semantics).
        if frames_to_turn > 0:
            frames_to_turn -= 1
        elif not has_turned:
            has_turned = 1

        out_frames[i] = np.uint8(frames_to_turn)
        out_has[i] = np.uint8(has_turned)
        prev_in_turn = True

    return out_frames, out_has


def derive_kneebend_internals(
    *,
    action_id: np.ndarray,
    buttons: np.ndarray,
    buttons_pressed: np.ndarray,
    stick_y_unit: np.ndarray,
    cstick_y_unit: np.ndarray,
    tilt_timer_y: np.ndarray,
    tap_jump_threshold: float,
    tap_jump_tilt_max_frames: int,
    tap_jump_release_threshold: float,
    act_kneebend: int,
    button_mask_xy: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive KneeBend internals (`jump_input`, `is_short_hop`) per frame from replay history.

    Decomp references:
    - Jump input selection (L-stick vs XY): refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:34-63
    - C-stick "jump input" path:
      - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:90-104 (ftCo_800CB024)
      - refs/melee/src/melee/ft/ft_0DF1.c:239-249 (ftCo_800DF910)
      - refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28 (ftCo_KneeBend_Enter)
    - KneeBend storage + short-hop latch: refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28
      and :44-56

    Causality: strictly causal; uses only current and past frames to detect KneeBend entry and to
    latch the short-hop flag while remaining in KneeBend.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    b = np.asarray(buttons, dtype=np.uint16).reshape(-1)
    bp = np.asarray(buttons_pressed, dtype=np.uint16).reshape(-1)
    sy = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    cy = np.asarray(cstick_y_unit, dtype=np.float32).reshape(-1)
    tty = np.asarray(tilt_timer_y, dtype=np.uint8).reshape(-1)

    n = int(a.size)
    out_jump = np.zeros(n, dtype=np.uint8)
    out_short = np.zeros(n, dtype=np.uint8)

    jump_input = 0  # ftCo_JumpInput enum: 0 None, 1 LStick, 2 CStick, 3 XY.
    is_short_hop = 0
    prev_in_kneebend = False

    thr = np.float32(tap_jump_threshold)
    rel = np.float32(tap_jump_release_threshold)
    tilt_max = int(tap_jump_tilt_max_frames)
    xy = int(button_mask_xy) & 0xFFFF

    for i in range(n):
        cur_in_kneebend = int(a[i]) == int(act_kneebend)
        if not cur_in_kneebend:
            jump_input = 0
            is_short_hop = 0
            prev_in_kneebend = False
            continue

        if not prev_in_kneebend:
            # Entry into KneeBend: pick jump_input causally using current inputs.
            # ftCo_Jump_GetInput checks L-stick before XY; C-stick is a separate fallback check.
            jump_input = 0
            if sy[i] >= thr and int(tty[i]) < tilt_max:
                jump_input = 1  # JumpInput_LStick
            elif (int(bp[i]) & xy) != 0:
                jump_input = 3  # JumpInput_XY
            elif cy[i] >= thr:
                jump_input = 2  # JumpInput_CStick
            is_short_hop = 0

        if not is_short_hop:
            # ftCo_KneeBend_Check_ShortHop: latch when the original jump input is released.
            if jump_input == 3:
                if (int(b[i]) & xy) == 0:
                    is_short_hop = 1
            elif jump_input == 1:
                if sy[i] < rel:
                    is_short_hop = 1
            elif jump_input == 2:
                if cy[i] < rel:
                    is_short_hop = 1

        out_jump[i] = np.uint8(jump_input)
        out_short[i] = np.uint8(is_short_hop)
        prev_in_kneebend = True

    return out_jump, out_short


def compute_press_timer_u8(
    *,
    buttons_pressed: np.ndarray,
    press_mask: int,
    start_timer: int = 0xFF,
) -> np.ndarray:
    """
    Compute a generic "frames since last press" timer (u8, saturating at 0xFF).

    This matches the pattern used by several `fp->x67*` counters in fighter input history.

    Decomp references:
    - init/reset to 0xFF: refs/melee/src/melee/ft/fighter.c:650-707
    - per-frame update example (x67F): refs/melee/src/melee/ft/fighter.c:2078-2086
      (`if (fp->input.x668 & HSD_PAD_LR) fp->x67F = 0; else if (fp->x67F < 0xFF) fp->x67F++;`)
    """
    bp = np.asarray(buttons_pressed, dtype=np.uint16).reshape(-1)
    n = int(bp.size)
    out = np.empty(n, dtype=np.uint8)

    mask = int(press_mask) & 0xFFFF
    timer = int(start_timer) & 0xFF

    for i in range(n):
        if (int(bp[i]) & mask) != 0:
            timer = 0
        elif timer < 0xFF:
            timer += 1
        out[i] = np.uint8(timer)

    return out


def compute_x672_trigger_timer_pre_post(
    *,
    trigger_unit: np.ndarray,
    prev_trigger_unit: np.ndarray | None = None,
    trigger_min: float,
    guard_reflect_entry: np.ndarray | None = None,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Compute fp->x672_input_timer_counter per frame (pre/post), causally.

    - timer_pre: value after Fighter input update (used for powershield/GuardReflect gate that frame)
    - timer_post: value after action-entry overrides later in the frame

    Decomp references:
    - per-frame update from analog triggers (x650/x654):
      refs/melee/src/melee/ft/fighter.c:2020-2050
    - action-entry override on GuardReflect entry:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:746-804
      (sets `fp->x672_input_timer_counter = 0xFE;`)

    Notes:
    - `trigger_unit` corresponds to `fp->input.x650` (max of analog L/R, with digital L/R treated as 1.0).
    - If `prev_trigger_unit` is omitted, we model `fp->input.x654` as the previous frame's `trigger_unit`.
    """
    trig = np.asarray(trigger_unit, dtype=np.float32).reshape(-1)
    n = int(trig.size)

    if prev_trigger_unit is None:
        prev_trig = None
    else:
        prev_trig = np.asarray(prev_trigger_unit, dtype=np.float32).reshape(-1)
        if int(prev_trig.size) != n:
            raise ValueError("prev_trigger_unit must match trigger_unit length")

    if guard_reflect_entry is None:
        guard_entry = None
    else:
        guard_entry = np.asarray(guard_reflect_entry, dtype=bool).reshape(-1)
        if int(guard_entry.size) != n:
            raise ValueError("guard_reflect_entry must match trigger_unit length")

    out_pre = np.empty(n, dtype=np.uint8)
    out_post = np.empty(n, dtype=np.uint8)

    thr = np.float32(trigger_min)
    timer_post = int(start_timer_post) & 0xFF
    prev_t = np.float32(0.0)

    for i in range(n):
        cur = np.float32(trig[i])
        if prev_trig is None:
            prev = prev_t
        else:
            prev = np.float32(prev_trig[i])

        # Fighter input update (timer_pre).
        t_pre = int(timer_post) & 0xFF
        if cur >= thr:
            if prev >= thr:
                t_pre += 1
                if t_pre > 0xFE:
                    t_pre = 0xFE
            else:
                t_pre = 0
        else:
            t_pre = 0xFE

        t_post = t_pre
        if guard_entry is not None and bool(guard_entry[i]):
            t_post = 0xFE

        out_pre[i] = np.uint8(t_pre)
        out_post[i] = np.uint8(t_post)

        timer_post = t_post
        prev_t = cur

    return out_pre, out_post


def lb_8000D148(
    point0_x: float,
    point0_y: float,
    point1_x: float,
    point1_y: float,
    point2_x: float,
    point2_y: float,
    threshold: float,
) -> bool:
    """
    Port of lb_8000D148 (segment/threshold helper used by fighter input counters).

    Source: refs/melee/src/melee/lb/lb_00CE.c:163-225
    """
    # Keep float math and branching structure close to decomp for easier auditing.
    diff_01_y = np.float32(point0_y) - np.float32(point1_y)
    diff_01_x = np.float32(point1_x) - np.float32(point0_x)
    dist_squared_01 = (diff_01_x * diff_01_x) + (diff_01_y * diff_01_y)
    if dist_squared_01 < np.float32(0.00001):
        return False
    dist_01 = np.sqrt(dist_squared_01)

    var_f0 = ((np.float32(point0_x) * np.float32(point1_y)) - (np.float32(point0_y) * np.float32(point1_x))) + (
        (diff_01_x * np.float32(point2_x)) + (diff_01_y * np.float32(point2_y))
    )
    if var_f0 < np.float32(0.0):
        var_f0 = -var_f0

    thr = np.float32(threshold)
    if (var_f0 / dist_01) <= thr:
        diff_02_x = np.float32(point0_x) - np.float32(point2_x)
        diff_02_y = np.float32(point0_y) - np.float32(point2_y)
        diff_12_x = np.float32(point1_x) - np.float32(point2_x)
        diff_12_y = np.float32(point1_y) - np.float32(point2_y)
        threshold_squared = thr * thr
        dist_squared_02 = (diff_02_x * diff_02_x) + (diff_02_y * diff_02_y)
        dist_squared_12 = (diff_12_x * diff_12_x) + (diff_12_y * diff_12_y)
        if dist_squared_02 < threshold_squared:
            if dist_squared_12 > threshold_squared:
                return True
            if dist_squared_12 < threshold_squared:
                return False
            return True
        if dist_squared_02 > threshold_squared:
            if dist_squared_12 > threshold_squared:
                if (
                    ((point0_x > point2_x) and (point1_x < point2_x))
                    or ((point0_x < point2_x) and (point1_x > point2_x))
                    or ((point0_y > point2_y) and (point1_y < point2_y))
                    or ((point0_y < point2_y) and (point1_y > point2_y))
                ):
                    return True
                return False
            if dist_squared_12 < threshold_squared:
                return True
            return True
        return True
    return False


def compute_fighter_stick_input_counters(
    *,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    tilt_thresh_x: float,
    tilt_thresh_y: float,
    start_timer: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute the stick-driven fighter input counters block (u8, saturating at 0xFE), causally.

    Outputs are per-frame post-update values for:
    - x673, x679_x, x676_x (lstick x companions + age counter)
    - x674, x67A_y, x677_y (lstick y companions + age counter)

    Decomp reference: refs/melee/src/melee/ft/fighter.c:1897-2019
      (including lb_8000D148 zeroing at :2011-2017).
    """
    sx = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    sy = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    if int(sy.size) != int(sx.size):
        raise ValueError("stick_y_unit must match stick_x_unit length")
    n = int(sx.size)

    out_x673 = np.empty(n, dtype=np.uint8)
    out_x676_x = np.empty(n, dtype=np.uint8)
    out_x679_x = np.empty(n, dtype=np.uint8)
    out_x674 = np.empty(n, dtype=np.uint8)
    out_x677_y = np.empty(n, dtype=np.uint8)
    out_x67A_y = np.empty(n, dtype=np.uint8)

    thr_x = np.float32(tilt_thresh_x)
    thr_y = np.float32(tilt_thresh_y)

    x673 = int(start_timer) & 0xFF
    x676_x = int(start_timer) & 0xFF
    x679_x = int(start_timer) & 0xFF
    x674 = int(start_timer) & 0xFF
    x677_y = int(start_timer) & 0xFF
    x67A_y = int(start_timer) & 0xFF

    prev_x = np.float32(0.0)
    prev_y = np.float32(0.0)

    for i in range(n):
        cur_x = np.float32(sx[i])
        cur_y = np.float32(sy[i])

        # x676_x++
        x676_x += 1
        if x676_x > 0xFE:
            x676_x = 0xFE

        # lstick x block (x670 + x673 + x679_x) with x676_x reset on fresh entry.
        if cur_x >= thr_x:
            if prev_x >= thr_x:
                x673 += 1
                if x673 > 0xFE:
                    x673 = 0xFE
                x679_x += 1
                if x679_x > 0xFE:
                    x679_x = 0xFE
            else:
                x676_x = 0
                x673 = 0
        elif cur_x <= -thr_x:
            if prev_x <= -thr_x:
                x673 += 1
                if x673 > 0xFE:
                    x673 = 0xFE
                x679_x += 1
                if x679_x > 0xFE:
                    x679_x = 0xFE
            else:
                x676_x = 0
                x673 = 0
        else:
            x679_x = 0xFE
            x673 = 0xFE

        # x677_y++
        x677_y += 1
        if x677_y > 0xFE:
            x677_y = 0xFE

        # lstick y block (x671 + x674 + x67A_y) with x677_y reset on fresh entry.
        if cur_y >= thr_y:
            if prev_y >= thr_y:
                x674 += 1
                if x674 > 0xFE:
                    x674 = 0xFE
                x67A_y += 1
                if x67A_y > 0xFE:
                    x67A_y = 0xFE
            else:
                x677_y = 0
                x674 = 0
        elif cur_y <= -thr_y:
            if prev_y <= -thr_y:
                x674 += 1
                if x674 > 0xFE:
                    x674 = 0xFE
                x67A_y += 1
                if x67A_y > 0xFE:
                    x67A_y = 0xFE
            else:
                x677_y = 0
                x674 = 0
        else:
            x67A_y = 0xFE
            x674 = 0xFE

        if lb_8000D148(float(prev_x), float(prev_y), float(cur_x), float(cur_y), 0.0, 0.0, float(thr_x)):
            x67A_y = 0
            x679_x = 0

        out_x673[i] = np.uint8(x673)
        out_x676_x[i] = np.uint8(x676_x)
        out_x679_x[i] = np.uint8(x679_x)
        out_x674[i] = np.uint8(x674)
        out_x677_y[i] = np.uint8(x677_y)
        out_x67A_y[i] = np.uint8(x67A_y)

        prev_x = cur_x
        prev_y = cur_y

    return out_x673, out_x674, out_x676_x, out_x677_y, out_x679_x, out_x67A_y


def compute_fighter_trigger_input_counters(
    *,
    trigger_unit: np.ndarray,
    trigger_min: float,
    start_timer: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute the trigger-driven fighter input counters block (u8, saturating at 0xFE), causally.

    Outputs are per-frame post-update values for:
    - x675, x67B (companion timers)
    - x678 ("age since last change" counter)

    Decomp reference: refs/melee/src/melee/ft/fighter.c:2020-2050.
    """
    trig = np.asarray(trigger_unit, dtype=np.float32).reshape(-1)
    n = int(trig.size)
    out_x675 = np.empty(n, dtype=np.uint8)
    out_x67B = np.empty(n, dtype=np.uint8)
    out_x678 = np.empty(n, dtype=np.uint8)

    thr = np.float32(trigger_min)
    x675 = int(start_timer) & 0xFF
    x67B = int(start_timer) & 0xFF
    x678 = int(start_timer) & 0xFF
    prev = np.float32(0.0)

    for i in range(n):
        cur = np.float32(trig[i])

        x678 += 1
        if x678 > 0xFE:
            x678 = 0xFE

        if cur >= thr:
            if prev >= thr:
                x675 += 1
                if x675 > 0xFE:
                    x675 = 0xFE
                x67B += 1
                if x67B > 0xFE:
                    x67B = 0xFE
            else:
                x67B = 0
                x678 = 0
                x675 = 0
        else:
            x67B = 0xFE
            x675 = 0xFE

        out_x675[i] = np.uint8(x675)
        out_x67B[i] = np.uint8(x67B)
        out_x678[i] = np.uint8(x678)
        prev = cur

    return out_x675, out_x67B, out_x678


def compute_fighter_button_timers(
    *,
    buttons_pressed: np.ndarray,
    mask_a: int,
    mask_b: int,
    mask_xy: int,
    mask_dpad_up: int,
    mask_dpad_down: int,
    mask_lr: int,
    start_timer: int = 0xFF,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute fighter button timers (u8, saturating at 0xFF), causally.

    Outputs are per-frame post-update values for:
    - x67C (A) and x683 (capture previous x67C on A press)
    - x67D (B)
    - x67E (X/Y)
    - x681 (DPad Up)
    - x682 (DPad Down)
    - x680 (L/R) and x684 (capture previous x680 on L/R press)

    Decomp reference: refs/melee/src/melee/ft/fighter.c:2052-2094
    Init values: refs/melee/src/melee/ft/fighter.c:608-691 (reset/init to 0xFF).
    """
    bp = np.asarray(buttons_pressed, dtype=np.uint16).reshape(-1)
    n = int(bp.size)

    out_x67C = np.empty(n, dtype=np.uint8)
    out_x67D = np.empty(n, dtype=np.uint8)
    out_x67E = np.empty(n, dtype=np.uint8)
    out_x680 = np.empty(n, dtype=np.uint8)
    out_x681 = np.empty(n, dtype=np.uint8)
    out_x682 = np.empty(n, dtype=np.uint8)
    out_x683 = np.empty(n, dtype=np.uint8)
    out_x684 = np.empty(n, dtype=np.uint8)

    m_a = int(mask_a) & 0xFFFF
    m_b = int(mask_b) & 0xFFFF
    m_xy = int(mask_xy) & 0xFFFF
    m_du = int(mask_dpad_up) & 0xFFFF
    m_dd = int(mask_dpad_down) & 0xFFFF
    m_lr = int(mask_lr) & 0xFFFF

    x67C = int(start_timer) & 0xFF
    x67D = int(start_timer) & 0xFF
    x67E = int(start_timer) & 0xFF
    x680 = int(start_timer) & 0xFF
    x681 = int(start_timer) & 0xFF
    x682 = int(start_timer) & 0xFF
    x683 = int(start_timer) & 0xFF
    x684 = int(start_timer) & 0xFF

    for i in range(n):
        bpi = int(bp[i])

        if (bpi & m_a) != 0:
            x683 = x67C
            x67C = 0
        elif x67C < 0xFF:
            x67C += 1

        if (bpi & m_b) != 0:
            x67D = 0
        elif x67D < 0xFF:
            x67D += 1

        if (bpi & m_xy) != 0:
            x67E = 0
        elif x67E < 0xFF:
            x67E += 1

        if (bpi & m_du) != 0:
            x681 = 0
        elif x681 < 0xFF:
            x681 += 1

        if (bpi & m_dd) != 0:
            x682 = 0
        elif x682 < 0xFF:
            x682 += 1

        if (bpi & m_lr) != 0:
            x684 = x680
            x680 = 0
        elif x680 < 0xFF:
            x680 += 1

        out_x67C[i] = np.uint8(x67C)
        out_x67D[i] = np.uint8(x67D)
        out_x67E[i] = np.uint8(x67E)
        out_x680[i] = np.uint8(x680)
        out_x681[i] = np.uint8(x681)
        out_x682[i] = np.uint8(x682)
        out_x683[i] = np.uint8(x683)
        out_x684[i] = np.uint8(x684)

    return out_x67C, out_x67D, out_x67E, out_x680, out_x681, out_x682, out_x683, out_x684
