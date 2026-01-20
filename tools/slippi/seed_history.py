from __future__ import annotations

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
