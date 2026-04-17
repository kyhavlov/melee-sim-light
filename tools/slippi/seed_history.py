from __future__ import annotations

import functools
import json
import struct
from pathlib import Path

import numpy as np


def _sign_i16(x: np.ndarray) -> np.ndarray:
    # Match src/ucf.c::msl_sign_s8: returns +1 for 0 (though we never call it with 0).
    return np.where(x < 0, np.int16(-1), np.int16(1))


def _read_mslacid1_v2_x4_flags_low_bytes(path: Path) -> np.ndarray:
    """
    Read (u8)MotionState.x4_flags low bytes from ISO-derived action->x4_flags tables.

    This mirrors src/attack_id_tables.c::attack_id_x4_flags_from_action() but only keeps the low byte,
    which is what ft_800895E0 reads via `lbz` for its fp+0x2073 compare key.

    Decomp anchors:
    - refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz flags_low; compare vs fp+0x2073)
    - refs/melee/src/melee/pl/plattack.c::plAttack_80037B08 (instance_id bump)
    - x4_flags source of truth: data/attack_id/move_id/*.bin (loaded by src/attack_id_tables.c)
    """
    buf = path.read_bytes()
    if len(buf) < 24:
        raise ValueError(f"{path}: too small for MSLACID1 header (size={len(buf)})")
    if buf[:8] != b"MSLACID1":
        raise ValueError(f"{path}: bad magic (want MSLACID1)")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != 2:
        raise ValueError(f"{path}: unsupported version {int(ver)} (want 2)")
    (count,) = struct.unpack_from("<H", buf, 12)
    (flags_off,) = struct.unpack_from("<I", buf, 20)
    count_i = int(count)
    off_i = int(flags_off)
    if count_i < 0:
        raise ValueError(f"{path}: negative action_count={count_i}")
    if off_i < 0 or off_i + count_i * 4 > len(buf):
        raise ValueError(f"{path}: x4_flags table out of range (off={off_i} count={count_i})")
    lows = (np.frombuffer(buf, dtype="<u4", offset=off_i, count=count_i) & np.uint32(0xFF)).astype(
        np.uint8, copy=False
    )
    return lows


@functools.lru_cache(maxsize=1)
def _action_x4_flags_low_bytes_tables(*, data_dir: str = "data") -> dict[int, np.ndarray]:
    # Character ids (GALE01): Fox=1, Falco=22.
    base = Path(str(data_dir)) / "attack_id" / "move_id"
    out: dict[int, np.ndarray] = {}
    out[1] = _read_mslacid1_v2_x4_flags_low_bytes(base / "fox.bin")
    out[22] = _read_mslacid1_v2_x4_flags_low_bytes(base / "falco.bin")
    return out


def derive_instance_id_x2073(
    *,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    data_dir: str = "data",
) -> np.ndarray:
    """
    Derive the fp+0x2073 compare byte used by ft_800895E0 (instance_id bump gate), strictly causally.

    Representation:
    - Returns a u8 array `instance_id_x2073` with length N, representing the *post-frame* value at each
      replay frame index.

    Causality / prefix-invariance:
    - Updates only on motion-state entry events, approximated causally from post-frame action_id
      transitions (and same-action timebase restarts detected by action_frame decreasing).
    - Does not consult future frames or replay outcomes.

    Decomp anchors:
    - refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
    - refs/melee/src/melee/pl/plattack.c::plAttack_80037B08

    x4_flags mapping source:
    - data/attack_id/move_id/{fox,falco}.bin (ISO-derived; also used by C via src/attack_id_tables.c)
    """
    char = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    aid = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    afr = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    n = int(aid.size)
    if int(char.size) != n or int(afr.size) != n:
        raise ValueError("char_id_u8/action_id_u16/action_frame_i16 must have the same length")

    tables = _action_x4_flags_low_bytes_tables(data_dir=str(data_dir))

    out = np.zeros(n, dtype=np.uint8)
    x2073 = int(0)

    def _flags_low(c: int, a: int) -> int:
        t = tables.get(int(c))
        if t is None:
            return 0
        if a < 0 or a >= int(t.shape[0]):
            return 0
        return int(t[np.int64(a)])

    for i in range(n):
        cur_a = int(aid[i])
        cur_c = int(char[i])
        cur_flags_low = _flags_low(cur_c, cur_a)

        entry = False
        if i == 0:
            entry = True
        else:
            prev_a = int(aid[i - 1])
            if cur_a != prev_a:
                entry = True
            else:
                # Detect same-action timebase restarts (e.g. loop restarts) by a causal action_frame drop.
                # This mirrors "Fighter_ChangeMotionState to same action_id" patterns where Slippi state_age
                # resets, but avoids consulting future frames.
                if int(afr[i]) < int(afr[i - 1]):
                    entry = True

        if entry:
            x2073 = cur_flags_low & 0xFF

        out[i] = np.uint8(x2073)

    return out


def derive_instance_id_counter(
    *,
    fighter_instance_id_u16_2d: np.ndarray,
    item_instance_id_u16_2d: np.ndarray,
) -> np.ndarray:
    """
    Derive plAttack_80037B08's "next id" counter strictly causally from replay-visible ids.

    Source signals (Slippi-visible):
    - fighter_instance_id_u16_2d: post-frame fighter `instance_id` lanes.
    - item_instance_id_u16_2d: post-frame item `instance_id` lanes from parsed item snapshots.

    Representation:
    - Returns u16 array length N, where output[i] is the seeded "next instance_id" value to use
      after replay post-frame i (and therefore for one-step seed_t at sample i).

    Seed-bridge rationale:
    - Slippi exposes fighter/item instance ids, but not the internal global counter
      (`unk_804D6480`) read/written by plAttack_80037B08.
    - Use a strictly-causal lower-bound bridge: next nonzero id after the running max id observed
      so far across fighters + items.
    - Prefix-invariant by construction: output[i] depends only on rows <= i via running max.

    Decomp anchors:
    - refs/melee/src/melee/pl/plattack.c::plAttack_80037B08 (monotonic u16 counter; skips 0)
    """
    f = np.asarray(fighter_instance_id_u16_2d, dtype=np.uint16)
    it = np.asarray(item_instance_id_u16_2d, dtype=np.uint16)
    if f.ndim != 2:
        raise ValueError("fighter_instance_id_u16_2d must be 2D [n_frames, num_players]")
    if it.ndim != 2:
        raise ValueError("item_instance_id_u16_2d must be 2D [n_frames, max_items]")
    n = int(f.shape[0])
    if int(it.shape[0]) != n:
        raise ValueError("fighter/item instance_id arrays must have the same frame length")

    out = np.empty(n, dtype=np.uint16)
    max_seen = 0
    for i in range(n):
        cur_max = int(max(int(np.max(f[i])), int(np.max(it[i]))))
        if cur_max > max_seen:
            max_seen = cur_max
        next_id = (max_seen + 1) & 0xFFFF
        if next_id == 0:
            next_id = 1
        out[i] = np.uint16(next_id)
    return out


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


def _derive_guard_reflect_timer_plus1(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    init_frames: int,
) -> np.ndarray:
    """Shared +1-biased GuardReflect timer derivation (strictly causal)."""
    a = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    hl = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    if a.shape != hl.shape:
        raise ValueError("action_id_u16 and hitlag_u16 must have the same shape")
    n = int(a.size)
    out = np.empty(n, dtype=np.uint8)

    act = np.uint16(int(act_guard_reflect) & 0xFFFF)
    init = int(init_frames) + 1
    if init < 0:
        init = 0
    if init > 255:
        init = 255

    t = 0
    prev_in = False
    for i in range(n):
        in_gr = bool(a[i] == act)
        if not in_gr:
            t = 0
        else:
            if not prev_in:
                t = init
            else:
                # Decomp: GuardReflect_Anim runs after Fighter_8006A1BC prio-0 hitlag decrement and
                # is gated by the post-decrement lane (`!fp->x2219_b5`).
                # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
                #
                # Replay input lane here is post-frame hitlag, so for frame i the callback gate is
                # determined by frame-(i-1) post hitlag after prio-0 decrement:
                #   can_tick = (max(post_hitlag[i-1] - 1, 0) == 0)
                hl_prev = int(hl[i - 1]) if i > 0 else 0
                hl_after_prio0 = hl_prev - 1 if hl_prev > 0 else 0
                if t > 0 and hl_after_prio0 == 0:
                    t -= 1
        out[i] = np.uint8(t & 0xFF)
        prev_in = in_gr

    return out


def derive_guard_reflect_timer_x14(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    reflect_frames_x2a4: int,
) -> np.ndarray:
    """
    Derive GuardReflect's reflect-active timer (mv.co.guard.x14) as a strictly-causal u8 countdown.

    Decomp trail:
    - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
      sets `mv.co.guard.x14 = p_ftCommonData->x2A4`.
    - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      decrements `mv.co.guard.x14` and clears `fp->reflecting` when it drops below 0.
    - Hitlag gate: per-action anim callbacks (including GuardReflect_Anim) run after prio-0 hitlag
      decrement and gate on the post-decrement lane (`!fp->x2219_b5`):
      refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    """
    return _derive_guard_reflect_timer_plus1(
        action_id_u16=action_id_u16,
        hitlag_u16=hitlag_u16,
        act_guard_reflect=act_guard_reflect,
        init_frames=reflect_frames_x2a4,
    )


def derive_guard_reflect_timer_x18(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    reflect_total_frames_x2b4: int,
) -> np.ndarray:
    """
    Derive GuardReflect's powershield-active timer (mv.co.guard.x18) as a strictly-causal u8 countdown.

    Decomp trail:
    - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
      sets `mv.co.guard.x18 = p_ftCommonData->x2B4`.
    - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      decrements `mv.co.guard.x18` and clears `fp->x221C_b2` when it drops below 0.
    - Hitlag gate: per-action anim callbacks (including GuardReflect_Anim) run after prio-0 hitlag
      decrement and gate on the post-decrement lane (`!fp->x2219_b5`):
      refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    """
    return _derive_guard_reflect_timer_plus1(
        action_id_u16=action_id_u16,
        hitlag_u16=hitlag_u16,
        act_guard_reflect=act_guard_reflect,
        init_frames=reflect_total_frames_x2b4,
    )


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
        if ver not in (1, 2, 3):
            raise ValueError(f"{p}: unsupported MSLSHLD1 version={ver} (want 1, 2, or 3)")

        frame_count, neutral_frame = struct.unpack_from("<HH", buf, 12)
        if frame_count == 0:
            raise ValueError(f"{p}: frame_count is 0")
        if neutral_frame >= frame_count:
            raise ValueError(
                f"{p}: neutral_frame out of range (neutral_frame={neutral_frame}, frame_count={frame_count})"
            )

        hdr = 16 if ver == 1 else (28 if ver == 2 else 32)
        want = hdr + int(frame_count) * 3 * 4
        if ver == 3:
            guard_on_frame_count = struct.unpack_from("<H", buf, 28)[0]
            want += int(guard_on_frame_count) * 3 * 4
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


def derive_guard_release_lockout_and_lightshield(
    *,
    action_id: np.ndarray,
    shield_hp: np.ndarray,
    hitlag: np.ndarray,
    buttons_held: np.ndarray,
    button_mask_lr: int,
    trigger_unit: np.ndarray,
    trigger_deadzone: float,
    guard_x10_init_frames: int,
    act_guard_on: int,
    act_guard: int,
    act_guard_reflect: int,
    act_guard_set_off: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive Guard release lockout state (mv.co.guard.xC + mv.co.guard.x10) and the lightshield amount
    latch (fp->lightshield_amount) strictly causally from replay history.

    Decomp (GALE01):
    - GuardOn/GuardReflect entry calls ftCo_800921DC, which initializes:
        mv.co.guard.xC = false
        mv.co.guard.x10 = p_ftCommonData->x268
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
    - Each Guard{On}/Guard/GuardReflect Anim calls ftCo_800925A4 while the shield is active
      (fp->x221B_b0), which:
        - updates fp->lightshield_amount with a "reuse previous value if negative" latch
        - decrements mv.co.guard.x10 once per frame (under !hitlag)
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
    - GuardOn/Guard/GuardReflect IASA latches mv.co.guard.xC on trigger release and exits to GuardOff
      only once (xC && x10==0) OR the shield is no longer active.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA (inlineC0)
    - GuardSetOff entry consumes the existing fp->lightshield_amount when computing anim rate, and
      does not reset that fighter field on entry.
    - GuardSetOff -> Guard path uses ftCo_800928CC (via ftCo_GuardSetOff_Anim) and does not call
      ftCo_800921DC, so mv.co.guard.xC/x10 should not be reinitialized on this transition.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}

    Returns (guard_release_latched_xc_u8, guard_x10_u8, lightshield_amount_f32) arrays with length N,
    where the values represent the *post-frame* state for each frame index.
    """
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    hp = np.asarray(shield_hp, dtype=np.float32).reshape(-1)
    hl = np.asarray(hitlag, dtype=np.uint16).reshape(-1)
    buttons = np.asarray(buttons_held, dtype=np.uint16).reshape(-1)
    trig = np.asarray(trigger_unit, dtype=np.float32).reshape(-1)
    mask_lr = int(button_mask_lr) & 0xFFFF

    n = int(aid.size)
    if int(hp.size) != n or int(hl.size) != n or int(buttons.size) != n or int(trig.size) != n:
        raise ValueError("input arrays must have the same length")
    if mask_lr == 0:
        raise ValueError("button_mask_lr must be non-zero")

    out_xc = np.zeros(n, dtype=np.uint8)
    out_x10 = np.zeros(n, dtype=np.uint8)
    out_light = np.zeros(n, dtype=np.float32)

    dz = np.float32(float(trigger_deadzone))
    denom = np.float32(1.0) - dz
    if not (float(denom) > 0.0):
        raise ValueError(f"invalid trigger_deadzone={trigger_deadzone} (1-deadzone must be >0)")

    init = int(guard_x10_init_frames)
    if init < 0:
        init = 0
    if init > 255:
        init = 255

    # Persistent per-fighter state.
    xC = False
    x10 = int(0)
    light = np.float32(0.0)

    def _is_guard(a: int) -> bool:
        return a == int(act_guard_on) or a == int(act_guard) or a == int(act_guard_reflect)

    for i in range(n):
        a = int(aid[i])
        prev_a = int(aid[i - 1]) if i > 0 else a

        in_guard = _is_guard(a)
        prev_in_guard = _is_guard(prev_a)
        in_guard_set_off = a == int(act_guard_set_off)

        # Reset on GuardOn/GuardReflect entry (ftCo_800921DC call sites).
        if (a == int(act_guard_on) and prev_a != int(act_guard_on)) or (
            a == int(act_guard_reflect) and prev_a != int(act_guard_reflect)
        ):
            xC = False
            x10 = init
            light = np.float32(0.0)

        if not in_guard and not in_guard_set_off:
            # Outside of guard states, these internals are irrelevant; seed them as 0 to keep
            # reseeding deterministic and schema-minimal.
            xC = False
            x10 = 0
            light = np.float32(0.0)
            out_xc[i] = np.uint8(0)
            out_x10[i] = np.uint8(0)
            out_light[i] = np.float32(0.0)
            continue

        if in_guard and not prev_in_guard and a == int(act_guard) and prev_a != int(act_guard_set_off):
            # Snapshot bridge for teacher-forced reseed:
            # Guard entry can appear without an explicit GuardOn/GuardReflect predecessor in replay
            # snapshots (no submotion timeline at the boundary). Seed conservatively with a fresh
            # lockout window to avoid synthesizing immediate Guard->GuardOff exits from stale xC/x10.
            #
            # Decomp exception: GuardSetOff -> Guard uses ftCo_800928CC and does not call
            # ftCo_800921DC, so xC/x10 must carry through (do not reinitialize on this path).
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_800928CC}
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_800921DC}
            # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            xC = False
            x10 = init
            light = np.float32(0.0)

        if in_guard_set_off:
            if prev_a != int(act_guard_set_off) and not prev_in_guard and x10 == 0:
                # Snapshot bridge: replay post-frames can show direct non-guard -> GuardSetOff
                # boundaries when the preceding GuardOn/Guard/GuardReflect context is absent.
                # Decomp GuardSetOff itself does not call ftCo_800921DC, but the missing guard-entry
                # context would have initialized x10 earlier in-frame before ftCo_80092F2C.
                # Seed a fresh lockout window so GuardSetOff->Guard carry does not collapse into an
                # immediate GuardOff on the next no-submotion Guard snapshot.
                # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80092F2C,ftCo_GuardSetOff_Anim}
                xC = False
                x10 = init

            # GuardSetOff consumes the already-latched fp->lightshield_amount for entry anim-rate
            # shaping and does not reinitialize guard lockout lanes on the SetOff->Guard path.
            # Preserve xC/x10 across GuardSetOff snapshots.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim,ftCo_800928CC}
            out_xc[i] = np.uint8(1 if xC else 0)
            out_x10[i] = np.uint8(x10 & 0xFF)
            out_light[i] = np.float32(light)
            continue

        # Hitlag gate for Guard anim/IASA ownership:
        # - Fighter_8006A1BC decrements hitlag at proc prio 0.
        # - Guard callbacks run later in Fighter_8006A360 / Fighter_procUpdate and are gated on
        #   the post-decrement hitlag lane (`!fp->x2219_b5`).
        # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
        #
        # Replay input here is post-frame hitlag. For frame `i`, callback gating is controlled by
        # frame-`i-1` post hitlag after the prio-0 decrement:
        #   can_update = (max(post_hitlag[i-1] - 1, 0) == 0).
        # Using current-frame post hitlag over-freezes Guard internals on frames where hitlag is
        # newly applied later in the frame by collision callbacks.
        hl_prev = int(hl[i - 1]) if i > 0 else 0
        hl_after_prio0 = hl_prev - 1 if hl_prev > 0 else 0
        # held_inputs proxy for ftCo_80092BCC:
        # - prefer replay-visible held digital bits (buttons_held & LR),
        # - fall back to analog trigger deadzone when digital bits are absent.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
        held = ((int(buttons[i]) & mask_lr) != 0) or (float(trig[i]) >= float(dz))
        # Only update these when guard callbacks can run and shield is still active.
        if hl_after_prio0 == 0 and float(hp[i]) > 0.0:
            # Lightshield amount latch (ftCo_800925A4):
            # lightshield_amount = (x650 - deadzone)/(1-deadzone) if >=0 else reuse previous.
            t = np.float32((np.float32(trig[i]) - dz) / denom)
            if float(t) >= 0.0:
                if float(t) > 1.0:
                    t = np.float32(1.0)
                light = t

            # x10 countdown tick (ftCo_800925A4).
            if x10 > 0:
                x10 -= 1
                if x10 < 0:
                    x10 = 0

            # xC latch (ftCo_80092BCC): level-held check, not edge-triggered.
            # if (!(fp->input.held_inputs & HSD_PAD_LR)) fp->mv.co.guard.xC = true;
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
            if not held:
                xC = True

        out_xc[i] = np.uint8(1 if xC else 0)
        out_x10[i] = np.uint8(x10 & 0xFF)
        out_light[i] = np.float32(light)

    return out_xc, out_x10, out_light


def derive_guard_setoff_hitlag_damage_min(
    *,
    action_id: np.ndarray,
    action_frame_i16: np.ndarray,
    hitlag: np.ndarray,
    hitlag_dmg_mul: float,
    hitlag_base: float,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive a causal lower-bound bridge for GuardSetOff's hidden x19A4 shield-hit int damage.

    Purpose:
    - `ftCo_80092F2C` shapes GuardSetOff anim rate from `fp->x19A4`, but Slippi does not expose
      that field directly.
    - The shield-hit entry does expose the resulting hitlag countdown. Use the decomp
      `ftCommon_CalcHitlag` formula to recover the minimum non-negative integer damage consistent
      with the entry hitlag, then carry that value across the contiguous GuardSetOff segment.

    Causality / prefix-invariance:
    - Updates only on GuardSetOff segment entry or same-action restart (action_frame drop or
      hitlag increase).
    - Depends only on current and previous replay frames.

    Decomp anchors:
    - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (writes fp->x19A4 = getEnvDmg(hit0->damage))
    - refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    """
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    afr = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    hl = np.asarray(hitlag, dtype=np.uint16).reshape(-1)

    n = int(aid.size)
    if int(afr.size) != n or int(hl.size) != n:
        raise ValueError("action_id/action_frame_i16/hitlag must have the same length")

    out = np.zeros(n, dtype=np.uint8)
    carried = 0
    slope = float(hitlag_dmg_mul)
    base = float(hitlag_base)
    if not (slope > 0.0):
        raise ValueError("hitlag_dmg_mul must be > 0")

    def _invert_min_damage(hitlag_frames: int) -> int:
        if hitlag_frames <= 0:
            return 0
        # Find the smallest non-negative integer damage whose decomp hitlag result matches or
        # exceeds the observed frame count. getEnvDmg returns at least 1 for nonzero float damage,
        # so clamp positive hitlag to at least 1.
        dmg = 1
        while dmg < 0xFF:
            result = int((float(dmg) * slope) + base)
            if result >= hitlag_frames:
                return dmg
            dmg += 1
        return 0xFF

    for i in range(n):
        a = int(aid[i])
        if a != int(act_guard_set_off):
            carried = 0
            out[i] = np.uint8(0)
            continue

        prev_a = int(aid[i - 1]) if i > 0 else -1
        prev_afr = int(afr[i - 1]) if i > 0 else 0
        prev_hl = int(hl[i - 1]) if i > 0 else 0
        cur_afr = int(afr[i])
        cur_hl = int(hl[i])

        segment_entry = (
            i == 0
            or prev_a != int(act_guard_set_off)
            or cur_afr < prev_afr
            or cur_hl > prev_hl
        )
        if segment_entry and cur_hl > 0:
            carried = _invert_min_damage(cur_hl)

        out[i] = np.uint8(carried & 0xFF)

    return out


def derive_guard_setoff_hitlag_exit_phase(
    *,
    action_id: np.ndarray,
    hitlag: np.ndarray,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive a causal GuardSetOff hitlag-exit ownership phase discriminator.

    Purpose:
    - GuardSetOff action-frame parity depends on the entry-owned anim rate surviving through the
      frozen hitlag tail and then resuming on the first non-hitlag row.
    - Slippi exposes action_id, hitlag, anim_frame, and frame_speed_mul, but not the hidden
      "which step of the hitlag-exit handoff are we on?" ownership phase.
    - This lane marks that phase explicitly so runtime work can target the last-hitlag and first
      post-hitlag rows without replay-fitting broad GuardSetOff behavior.

    Phase encoding:
    - 0: not GuardSetOff, or GuardSetOff steady row outside the hitlag-exit handoff
    - 1: GuardSetOff hitlag carry row with `hitlag > 1`
    - 2: GuardSetOff last-hitlag row with `hitlag == 1`
    - 3: first non-hitlag GuardSetOff row after a same-segment hitlag row

    Causality / prefix-invariance:
    - Uses only the current and previous replay rows.

    Decomp anchors:
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    """
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    hl = np.asarray(hitlag, dtype=np.uint16).reshape(-1)

    n = int(aid.size)
    if int(hl.size) != n:
        raise ValueError("action_id/hitlag must have the same length")

    out = np.zeros(n, dtype=np.uint8)
    guard_set_off = int(act_guard_set_off)
    for i in range(n):
        if int(aid[i]) != guard_set_off:
            out[i] = np.uint8(0)
            continue
        cur_hl = int(hl[i])
        prev_a = int(aid[i - 1]) if i > 0 else -1
        prev_hl = int(hl[i - 1]) if i > 0 else 0
        if cur_hl > 1:
            out[i] = np.uint8(1)
        elif cur_hl == 1:
            out[i] = np.uint8(2)
        elif prev_a == guard_set_off and prev_hl > 0:
            out[i] = np.uint8(3)
        else:
            out[i] = np.uint8(0)
    return out


def derive_guard_setoff_post_hitlag_owner(
    *,
    action_id: np.ndarray,
    guard_setoff_hitlag_exit_phase_u8: np.ndarray,
    state_flags_221c_u8: np.ndarray,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive the GuardSetOff post-hitlag ownership discriminator for the handoff rows.

    Meaning:
    - 0: not a GuardSetOff post-hitlag handoff row
    - 1: GuardSetOff handoff row with normal (non-powershield) ownership
    - 2: GuardSetOff handoff row with powershield-active ownership (`x221C_b2` still live)

    Purpose:
    - The remaining F02 blocker rows all occur on the last-hitlag / first-post-hitlag GuardSetOff
      handoff, but they split into two ownership shapes:
    - normal GuardSetOff rows where only ftCo_GuardSetOff_Anim owns the anim-rate handoff, and
    - powershield-active rows where ftCo_80093BC0 still owns the `x221C_b2` substate while the
      same GuardSetOff handoff occurs.

    Causality / prefix-invariance:
    - Uses only the current replay row and the already-causal handoff phase lane.

    Decomp anchors:
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    """
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    phase = np.asarray(guard_setoff_hitlag_exit_phase_u8, dtype=np.uint8).reshape(-1)
    flags_221c = np.asarray(state_flags_221c_u8, dtype=np.uint8).reshape(-1)

    n = int(aid.size)
    if int(phase.size) != n or int(flags_221c.size) != n:
      raise ValueError("action_id/phase/state_flags_221c must have the same length")

    out = np.zeros(n, dtype=np.uint8)
    guard_set_off = int(act_guard_set_off)
    for i in range(n):
        if int(aid[i]) != guard_set_off:
            continue
        cur_phase = int(phase[i])
        if cur_phase != 2 and cur_phase != 3:
            continue
        out[i] = np.uint8(2 if (int(flags_221c[i]) & 0x20) != 0 else 1)
    return out


def derive_camera_box_visible_x221f_b0(*, state_flags_u8: np.ndarray) -> np.ndarray:
    """
    Extract the replay-visible `fp->x221F_b0` camera-box visibility bit as a named seed lane.

    Purpose:
    - F04 Rebirth/dead-flow rows mismatch on `state_flags[4]` because the camera callback
      (`ftCo_Rebirth_Cam`) and `ftLib_80086A8C` own `fp->x221F_b0` visibility outside the current
      lite sim's explicit state model.
    - Slippi already exposes fp+0x221F in post-frame `state_flags[...,4]`. Promote the b0 mask as
      a semantic seed lane so future runtime fixes can key on the decomp meaning directly.

    Causality / prefix-invariance:
    - Pure current-row extraction from replay-visible post-frame state. No future frames.

    Decomp / replay anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    - refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    """
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    if sf.ndim != 2 or int(sf.shape[1]) < 5:
        raise ValueError("camera_box_visible_x221f_b0 requires state_flags_u8 shape [n,5]")
    state_flags_221f_index = 4
    state_flag_221f_b0_mask = 0x80
    return ((sf[:, state_flags_221f_index] & np.uint8(state_flag_221f_b0_mask)) != 0).astype(np.uint8)


def derive_rebirth_camera_anchor_y(
    *,
    action_id_u16: np.ndarray,
    stage_id_u32: int,
    respawn_point_y: float,
) -> np.ndarray:
    """
    Derive the hidden Rebirth camera anchor Y (`fp->mv.co.common.x8`) as a seed lane.

    Purpose:
    - F04 late-Rebirth blocker rows mismatch on `fp->x221F_b0` because `ftCo_Rebirth_Cam` owns the
      camera subject outside the lite sim's current explicit state model.
    - The callback writes the camera subject Y from `fp->mv.co.common.x8` plus a camera-data offset;
      on Final Destination this hidden base lane matches the stage respawn platform Y, not the
      fighter's replay-visible `cur_pos.y`.

    Causality / prefix-invariance:
    - Strictly current-row derivation from replay-visible `action_id` plus ISO-derived stage data.
      No future frames.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    - data/stages/final_destination.json: respawn_points
    """
    action = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    out = np.zeros(action.shape[0], dtype=np.float32)

    # Final Destination only in the current suite; unsupported stages leave the foundational lane
    # zero until their ISO-derived respawn points are wired.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MS_Rebirth
    # data/stages/final_destination.json: respawn_points
    if int(stage_id_u32) != 32:
        return out

    act_rebirth = 0x000C
    rebirth_mask = action == np.uint16(act_rebirth)
    if rebirth_mask.any():
        out[rebirth_mask] = np.float32(respawn_point_y)
    return out


@functools.lru_cache(maxsize=1)
def _fd_stage_cam_bounds_world(*, data_dir: str = "data") -> tuple[float, float, float, float]:
    stage_path = Path(str(data_dir)) / "stages" / "final_destination.json"
    data = json.loads(stage_path.read_text())
    cam = data.get("cam_bounds_world")
    if not isinstance(cam, dict):
        raise ValueError(f"{stage_path}: missing cam_bounds_world")
    return (
        float(cam["left"]),
        float(cam["right"]),
        float(cam["bottom"]),
        float(cam["top"]),
    )


@functools.lru_cache(maxsize=1)
def _camera_target_seed_tables(*, data_dir: str = "data") -> dict[int, dict[str, object]]:
    from tools.slippi.combat_history import AnimPoseDB

    base = Path(str(data_dir))
    out: dict[int, dict[str, object]] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        meta = json.loads((base / "characters" / f"{key}.json").read_text())
        out[int(char_id)] = {
            "pose": AnimPoseDB((base / "anims" / f"{key}.bin").read_bytes()),
            "bone_part_id": int(meta["camera_zoom_target_bone_part_id"]),
            "offset": np.asarray(meta["camera_zoom_target_offset"], dtype=np.float32).reshape(3),
            "radius": float(meta["camera_box_radius"]),
            "model_scaling": float(meta.get("model_scaling", 1.0)),
        }
    return out


def derive_camera_target_world(
    *,
    char_id_u8: np.ndarray,
    animation_index_u32: np.ndarray,
    anim_frame_f32: np.ndarray,
    fighter_scale_y_f32: np.ndarray,
    facing_u8: np.ndarray,
    pos_x_f32: np.ndarray,
    pos_y_f32: np.ndarray,
    pos_z_f32: np.ndarray,
    data_dir: str = "data",
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive the fighter camera-subject target point (`camera_box->x1C`) and radius (`camera_box->x34.z`).

    Purpose:
    - F04 mixed-direction blockers are owned by `ftLib_80086A8C` camera-subject tests
      (`Camera_80030CD8` / `Camera_80030CFC`), not just the replay-visible `fp->x221F_b0` bit.
    - `ftLib_800866DC` writes the subject point from the fighter's camera-zoom target bone plus
      `co_attrs.x170`, and `ftCamera_80076018` scales the camera-box radius from fighter data.
    - Promote those hidden camera-target semantics as explicit seed lanes so later runtime work can
      reason from decomp-backed world-space inputs instead of replay-fit visibility toggles.

    Causality / prefix-invariance:
    - Strictly current-row derivation from replay-visible pose inputs plus ISO-derived character
      camera metadata and SSANIM pose data. No future frames.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
    - refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
    - refs/melee/src/melee/ft/fighter.c (root facing rotation via ftPartSetRotY)
    - data/characters/{fox,falco}.json: camera_zoom_target_bone_part_id,
      camera_zoom_target_offset, camera_box_radius, model_scaling
    - data/anims/{fox,falco}.bin (SSANIM01 v3 pose matrices)
    """
    char = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    anim = np.asarray(animation_index_u32, dtype=np.uint32).reshape(-1)
    anim_frame = np.asarray(anim_frame_f32, dtype=np.float32).reshape(-1)
    scale_y = np.asarray(fighter_scale_y_f32, dtype=np.float32).reshape(-1)
    facing = np.asarray(facing_u8, dtype=np.uint8).reshape(-1)
    pos_x = np.asarray(pos_x_f32, dtype=np.float32).reshape(-1)
    pos_y = np.asarray(pos_y_f32, dtype=np.float32).reshape(-1)
    pos_z = np.asarray(pos_z_f32, dtype=np.float32).reshape(-1)
    n = int(char.size)
    if (
        int(anim.size) != n
        or int(anim_frame.size) != n
        or int(scale_y.size) != n
        or int(facing.size) != n
        or int(pos_x.size) != n
        or int(pos_y.size) != n
        or int(pos_z.size) != n
    ):
        raise ValueError("camera target world derivation inputs must share length")

    out_x = np.zeros(n, dtype=np.float32)
    out_y = np.zeros(n, dtype=np.float32)
    out_z = np.zeros(n, dtype=np.float32)
    out_radius = np.zeros(n, dtype=np.float32)

    tables = _camera_target_seed_tables(data_dir=str(data_dir))
    for i in range(n):
        entry = tables.get(int(char[i]))
        if entry is None:
            continue
        msid = int(anim[i])
        if msid < 0 or msid > 0xFFFF:
            continue
        frame_f = float(anim_frame[i])
        if not np.isfinite(frame_f):
            continue
        frame = int(np.floor(frame_f))
        if frame < 0:
            continue

        pose = entry["pose"]
        bone_part_id = int(entry["bone_part_id"])
        m = pose.try_get_matrix(msid=msid, frame=frame, part_id=bone_part_id)
        if m is None:
            continue

        off = entry["offset"]
        lx = float(m[0] * off[0] + m[1] * off[1] + m[2] * off[2] + m[3])
        ly = float(m[4] * off[0] + m[5] * off[1] + m[6] * off[2] + m[7])
        lz = float(m[8] * off[0] + m[9] * off[1] + m[10] * off[2] + m[11])

        scale = float(scale_y[i])
        if not np.isfinite(scale) or scale <= 0.0:
            scale = 1.0
        model_scaling = float(entry["model_scaling"])
        if not np.isfinite(model_scaling) or model_scaling <= 0.0:
            model_scaling = 1.0

        # Runtime HSD joint matrices used by ftLib_800866DC include fighter scale and the character
        # model-scaling chain. Our SSANIM pose matrices omit those runtime scalars, so apply the same
        # scale policy here before the decomp-shaped root facing rotation.
        # refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
        # refs/melee/src/melee/ft/ftparts.c::ftParts_80074B8C
        pose_scale = scale * model_scaling
        lx *= pose_scale
        ly *= pose_scale
        lz *= pose_scale

        facing_dir = 1.0 if int(facing[i]) else -1.0
        out_x[i] = np.float32(float(pos_x[i]) + facing_dir * lz)
        out_y[i] = np.float32(float(pos_y[i]) + ly)
        out_z[i] = np.float32(float(pos_z[i]) - facing_dir * lx)

        # ftCamera_80076018 scales the camera-box extents from fighter camera data by fp->x34_scale.y.
        # refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
        out_radius[i] = np.float32(float(entry["radius"]) * scale)

    return out_x, out_y, out_z, out_radius


def derive_camera_target_point_inside_stage_cam_bounds(
    *,
    stage_id_u32: int,
    camera_target_world_x_f32: np.ndarray,
    camera_target_world_y_f32: np.ndarray,
    camera_box_radius_f32: np.ndarray,
    data_dir: str = "data",
) -> np.ndarray:
    """
    Derive the current-row Camera_80030CD8-style point-inside-stage-cam predicate.

    Purpose:
    - F04 mixed-direction rows are owned by the camera-subject point test used by
      `ftLib_80086A8C` / `Camera_80030CD8`, even when the replay-visible `fp->x221F_b0`
      outcomes differ.
    - Promote that point-inside predicate as an explicit seed lane so later runtime work can key on
      the named decomp branch input rather than recomputing it ad hoc from packed fields.

    Causality / prefix-invariance:
    - Strictly current-row derivation from the promoted camera target world point plus ISO-derived
      stage camera bounds. No future frames.
    - Require a valid camera subject (`camera_box_radius_f32 > 0`) so pre-Rebirth / no-target
      control rows stay zero instead of treating the placeholder `(0,0)` point as on-screen.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    - refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
    - data/stages/final_destination.json: cam_bounds_world
    """
    x = np.asarray(camera_target_world_x_f32, dtype=np.float32).reshape(-1)
    y = np.asarray(camera_target_world_y_f32, dtype=np.float32).reshape(-1)
    r = np.asarray(camera_box_radius_f32, dtype=np.float32).reshape(-1)
    if int(y.size) != int(x.size) or int(r.size) != int(x.size):
        raise ValueError("camera target point-inside derivation inputs must share length")

    out = np.zeros(x.shape[0], dtype=np.uint8)
    # Final Destination only in the current suite; unsupported stages remain zero until their
    # ISO-derived camera bounds are wired.
    # data/stages/final_destination.json: cam_bounds_world
    if int(stage_id_u32) != 32:
        return out

    left, right, bottom, top = _fd_stage_cam_bounds_world(data_dir=str(data_dir))
    valid = (r > np.float32(0.0)) & np.isfinite(x) & np.isfinite(y)
    inside = valid & (x >= np.float32(left)) & (x < np.float32(right)) & (y >= np.float32(bottom)) & (
        y < np.float32(top)
    )
    out[inside] = np.uint8(1)
    return out


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
    action_frame_i16: np.ndarray,
    facing: np.ndarray,
    stick_x_unit: np.ndarray,
    tilt_timer_x: np.ndarray,
    dash_flick_abs: float,
    dash_flick_tilt_max_frames: int,
    turn_frames: np.ndarray,
    act_turn: int,
    act_turn_run: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive TURN internals (`frames_to_turn`, `has_turned`, `x8`) per frame from replay history.

    Decomp reference: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88.

    Causality: this function is strictly causal and does not look ahead to "calibrate" based on
    future outcomes (e.g. post-frame facing flips).

    Deterministic assumption (do NOT tune via one-step mismatch metrics):
    - If TURN entry-frame ordering is ambiguous, we assume `ftCo_Turn_Anim_Inner` does NOT apply
      on the entry frame (same frame the action switches into TURN/TURN_RUN), matching the
      simulator's current update ordering (Turn flip is only ticked if Turn was already active at
      frame start).
    - This assumption is expected to be revisited once we seed more complete entry history
      (notably KneeBend/jump-squat entry history), rather than being "trained" against outcomes.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    afr = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    facing_u8 = np.asarray(facing, dtype=np.uint8).reshape(-1)
    stick_x = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    ttx = np.asarray(tilt_timer_x, dtype=np.uint8).reshape(-1)
    tf = np.asarray(turn_frames, dtype=np.uint8).reshape(-1)

    n = int(a.size)
    out_frames = np.zeros(n, dtype=np.uint8)
    out_has = np.zeros(n, dtype=np.uint8)
    out_x8 = np.zeros(n, dtype=np.int8)

    frames_to_turn = 0
    has_turned = 0
    x8 = 0
    prev_in_turn = False

    dash_max = int(dash_flick_tilt_max_frames)
    dash_abs = np.float32(dash_flick_abs)

    for i in range(n):
        cur_act = int(a[i])
        # Decomp: fp->mv.co.turn.* is used by AS_Turn (not AS_TurnRun).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c
        cur_in_turn = cur_act == act_turn
        if not cur_in_turn:
            frames_to_turn = 0
            has_turned = 0
            x8 = 0
            prev_in_turn = False
            continue

        same_action_restart = bool(prev_in_turn and i > 0 and int(afr[i]) < int(afr[i - 1]))
        if not prev_in_turn or same_action_restart:
            # TURN entry (action transition into TURN).
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
            # Decomp:
            # - Basic Turn: x8 init is 0 (ftCo_Turn_Enter arg3=0.0).
            # - Smash Turn: ftCo_Turn_Enter_Smash sets x8 = facing_dir (non-zero).
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Enter_Basic,ftCo_Turn_Enter_Smash}
            if is_smash:
                x8 = 1 if facing_dir > 0 else -1
            else:
                x8 = 0
            out_frames[i] = np.uint8(frames_to_turn)
            out_has[i] = np.uint8(has_turned)
            out_x8[i] = np.int8(x8)
            prev_in_turn = True
            continue

        # Apply one ftCo_Turn_Anim_Inner tick for this frame (post-frame snapshot semantics).
        if frames_to_turn > 0:
            frames_to_turn -= 1
        elif not has_turned:
            has_turned = 1

        # Apply ftCo_Turn_IASA's fn_800C9C2C latch update (post-frame snapshot semantics).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
        facing_dir_i = np.float32(1.0 if int(facing_u8[i]) else -1.0)
        facing_after = facing_dir_i if has_turned else -facing_dir_i
        if (stick_x[i] * facing_after) >= dash_abs and int(ttx[i]) < dash_max:
            x8 = 1 if facing_after > 0 else -1

        out_frames[i] = np.uint8(frames_to_turn)
        out_has[i] = np.uint8(has_turned)
        out_x8[i] = np.int8(x8)
        prev_in_turn = True

    return out_frames, out_has, out_x8


def derive_run_x0(
    *,
    action_id: np.ndarray,
    hitlag_u16: np.ndarray,
    run_x0_init_x430: float,
    act_run: int,
    act_run_direct: int,
    act_turn_run: int,
) -> np.ndarray:
    """
    Derive Run IASA lockout (`fp->mv.co.run.x0`) per post-frame from replay history.

    Decomp:
    - `fp->mv.co.run.x0` is initialized by ftCo_Run_Enter_Full (arg0).
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Enter_Full
    - It is decremented by 1.0 each frame in Run_Anim.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    - It gates TurnRun/RunBrake in Run_IASA:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA

    Suite-relevant entry:
    - TurnRun_Anim enters Run via fn_800CA644, which passes p_ftCommonData->x430 as arg0.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644

    Seed representation:
    - Store a reseed-friendly u8 countdown (clamped to 0..255).
    - Interpret it as "frames remaining while x0 > 0" for the simulator's Run IASA gate.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    n = int(a.size)
    out = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out

    init = int(run_x0_init_x430)
    init = int(np.clip(init, 0, 255))

    for i in range(1, n):
        prev_a = int(a[i - 1])
        cur_a = int(a[i])
        prev_x0 = int(out[i - 1])

        x0 = 0
        if cur_a == act_run or cur_a == act_run_direct:
            if prev_a == act_turn_run:
                # TurnRun -> Run: ftCo_Run_Enter called via fn_800CA644 with arg0=p_ftCommonData->x430.
                x0 = init
            elif prev_a == cur_a:
                x0 = prev_x0
                # Decrement once per frame when not in hitlag (Run_Anim is skipped under hitlag).
                if int(hitlag[i - 1]) == 0 and x0 > 0:
                    x0 -= 1
            else:
                # Other Run entries (e.g. Dash->Run via fn_800CA5F0) initialize x0=0.
                x0 = 0

        out[i] = np.uint8(x0)

    return out


def derive_runbrake_cmd0(
    *,
    action_id_u16: np.ndarray,
    anim_frame_f32: np.ndarray,
    char_id_u8: np.ndarray,
    cmd0_on_by_char: dict[int, int],
    cmd0_off_by_char: dict[int, int],
    act_run_brake: int,
) -> np.ndarray:
    """
    Derive RunBrake's `fp->cmd_vars[0]` gate per post-frame, strictly causally.

    Decomp:
    - ftCo_RunBrake_Enter clears `fp->cmd_vars[0] = 0`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Enter
    - ftCo_RunBrake_IASA only reaches `fn_800C9CEC` (TurnRun path) when `fp->cmd_vars[0] != 0`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
    - The command script writes `cmd_vars[0]` via `set_cmd_var`.
      refs/melee/src/melee/ft/ftaction.c::ftAction_80071820

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0)

    Representation:
    - 0: RunBrake TurnRun gate disabled on this post-frame.
    - 1: RunBrake TurnRun gate enabled on this post-frame.
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    anim_frame = np.asarray(anim_frame_f32, dtype=np.float32).reshape(-1)
    char_id = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    n = int(action_id.size)
    if int(anim_frame.size) != n or int(char_id.size) != n:
        raise ValueError("action_id_u16/anim_frame_f32/char_id_u8 must have the same length")

    out = np.zeros(n, dtype=np.uint8)
    for i in range(n):
        if int(action_id[i]) != int(act_run_brake):
            continue
        cid = int(char_id[i])
        start_af = int(cmd0_on_by_char.get(cid, -1))
        end_af = int(cmd0_off_by_char.get(cid, -1))
        if start_af < 0 or end_af < 0 or end_af < start_af:
            continue
        af = float(anim_frame[i])
        if np.isfinite(af) and af >= float(start_af) and af < float(end_af):
            out[i] = np.uint8(1)
    return out


def derive_dash_x4(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    act_dash: int = 0x0014,
    act_turn: int = 0x0012,
) -> np.ndarray:
    """
    Derive `fp->mv.co.dash.x4` per post-frame, strictly causally.

    Decomp anchors:
    - ftCo_Dash_Enter stores arg1 into `fp->mv.co.dash.x4`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
    - Turn->Dash path enters via `ftCo_Dash_Enter(gobj, 0)`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    - Dash_CheckInput enters Dash via `ftCo_Dash_Enter(gobj, 1)`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput

    Representation:
    - Returns a u8 array with length N, carrying the post-frame latch value.
    - For non-Dash frames, emits 0.

    Causal entry detection:
    - Dash entry is detected from post-frame action transitions and same-action frame resets
      (`action_frame` drop while staying in Dash).
    """
    a = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    af = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    n = int(a.size)
    out = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out
    if int(af.size) != n:
        raise ValueError("action_frame_i16 must match action_id_u16 length")

    act_dash_u = int(act_dash) & 0xFFFF
    act_turn_u = int(act_turn) & 0xFFFF

    x4 = 0
    for i in range(n):
        cur_a = int(a[i]) & 0xFFFF
        cur_af = int(af[i])

        if cur_a != act_dash_u:
            x4 = 0
            out[i] = np.uint8(0)
            continue

        prev_a = int(a[i - 1]) & 0xFFFF if i > 0 else cur_a
        prev_af = int(af[i - 1]) if i > 0 else cur_af
        dash_entry = i == 0 or cur_a != prev_a or cur_af < prev_af
        if dash_entry:
            # Decomp: Turn_IASA enters Dash with arg1=0; Dash_CheckInput paths use arg1=1.
            x4 = 0 if prev_a == act_turn_u else 1

        out[i] = np.uint8(x4)

    return out


def derive_shine_release_state(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    buttons_held_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    release_lag_init_u8: np.ndarray,
    button_mask_b: int = 0x0200,
    act_special_lw_start: int = 0x0168,
    act_special_lw_loop: int = 0x0169,
    act_special_lw_hit: int = 0x016A,
    act_special_lw_end: int = 0x016B,
    act_special_lw_turn: int = 0x016C,
    act_special_air_lw_start: int = 0x016D,
    act_special_air_lw_loop: int = 0x016E,
    act_special_air_lw_hit: int = 0x016F,
    act_special_air_lw_end: int = 0x0170,
    act_special_air_lw_turn: int = 0x0171,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive Fox/Falco shine release internals strictly causally.

    Returns:
    - shine_release_lag_u8: post-frame mirror of `fp->mv.fx.SpecialLw.releaseLag`
    - shine_is_release_u8: post-frame mirror of `fp->mv.fx.SpecialLw.isRelease` (0/1)

    Decomp anchors:
    - ftFox_SpecialLw_SetVars initializes {releaseLag,isRelease} on SpecialLw enter.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFox_SpecialLw_SetVars
    - Start anim callbacks latch isRelease from held B (no releaseLag decrement).
    - Loop/Turn/Hit anim callbacks latch isRelease and decrement releaseLag.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
          ftFx_SpecialLwStart_Anim,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Anim}
    - Loop/Turn/Hit end-vs-loop gating uses (releaseLag <= 0 && isRelease).
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Check
    """
    a = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    af = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    held = np.asarray(buttons_held_u16, dtype=np.uint16).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    lag_init = np.asarray(release_lag_init_u8, dtype=np.uint8).reshape(-1)
    n = int(a.size)

    out_lag = np.zeros(n, dtype=np.uint8)
    out_is_release = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out_lag, out_is_release
    if int(af.size) != n or int(held.size) != n or int(hitlag.size) != n or int(lag_init.size) != n:
        raise ValueError("all shine release inputs must have matching length")

    act_start_set = {
        int(act_special_lw_start) & 0xFFFF,
        int(act_special_air_lw_start) & 0xFFFF,
    }
    act_latch_set = {
        int(act_special_lw_start) & 0xFFFF,
        int(act_special_air_lw_start) & 0xFFFF,
        int(act_special_lw_loop) & 0xFFFF,
        int(act_special_lw_hit) & 0xFFFF,
        int(act_special_lw_turn) & 0xFFFF,
        int(act_special_air_lw_loop) & 0xFFFF,
        int(act_special_air_lw_hit) & 0xFFFF,
        int(act_special_air_lw_turn) & 0xFFFF,
    }
    act_tick_set = {
        int(act_special_lw_loop) & 0xFFFF,
        int(act_special_lw_hit) & 0xFFFF,
        int(act_special_lw_turn) & 0xFFFF,
        int(act_special_air_lw_loop) & 0xFFFF,
        int(act_special_air_lw_hit) & 0xFFFF,
        int(act_special_air_lw_turn) & 0xFFFF,
    }
    act_shine_all = {
        int(act_special_lw_start) & 0xFFFF,
        int(act_special_lw_loop) & 0xFFFF,
        int(act_special_lw_hit) & 0xFFFF,
        int(act_special_lw_end) & 0xFFFF,
        int(act_special_lw_turn) & 0xFFFF,
        int(act_special_air_lw_start) & 0xFFFF,
        int(act_special_air_lw_loop) & 0xFFFF,
        int(act_special_air_lw_hit) & 0xFFFF,
        int(act_special_air_lw_end) & 0xFFFF,
        int(act_special_air_lw_turn) & 0xFFFF,
    }
    mask_b = int(button_mask_b) & 0xFFFF

    lag = 0
    is_release = 0
    for i in range(n):
        cur_a = int(a[i]) & 0xFFFF
        cur_af = int(af[i])
        cur_lag_init = int(lag_init[i]) & 0xFF
        prev_a = int(a[i - 1]) & 0xFFFF if i > 0 else cur_a
        prev_af = int(af[i - 1]) if i > 0 else cur_af

        if cur_a not in act_shine_all:
            lag = 0
            is_release = 0
            out_lag[i] = np.uint8(0)
            out_is_release[i] = np.uint8(0)
            continue

        shine_start_entry = False
        skip_tick_this_frame = False
        if cur_a in act_start_set:
            if i == 0 or prev_a not in act_shine_all:
                shine_start_entry = True
            elif cur_a == prev_a and cur_af < prev_af:
                # Same-action restart safety for causal replay slices.
                shine_start_entry = True
        if shine_start_entry:
            lag = cur_lag_init
            is_release = 0
            # Entry frame uses SetVars only; Start_Anim ticking begins on subsequent frames.
            skip_tick_this_frame = True
        elif i == 0:
            # Best-effort initialization for first-frame mid-shine slices.
            lag = max(0, cur_lag_init - max(0, cur_af + 1))
            is_release = 0

        if cur_a in act_latch_set and not skip_tick_this_frame and int(hitlag[i]) == 0:
            if (int(held[i]) & mask_b) == 0:
                is_release = 1
        if cur_a in act_tick_set and not skip_tick_this_frame and int(hitlag[i]) == 0:
            if lag > 0:
                lag -= 1

        out_lag[i] = np.uint8(lag)
        out_is_release[i] = np.uint8(is_release)

    return out_lag, out_is_release


def derive_ecb_lock_timer(
    *,
    on_ground_u8: np.ndarray,
    action_id_u16: np.ndarray,
    lock_frames_ground_to_air: int = 10,
    act_jump_f: int = 0x0019,
    act_jump_b: int = 0x001A,
    act_jump_aerial_f: int = 0x001B,
    act_jump_aerial_b: int = 0x001C,
) -> np.ndarray:
    """
    Derive `fp->ecb_lock` per post-frame from replay grounding history.

    Decomp anchors:
    - ftCommon_8007D5D4 sets `fp->ecb_lock = 10` and enables CollData_X130_Locked on ground->air.
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    - Fighter_procMap decrements `fp->ecb_lock` once per map/collision callback and clears the lock
      when it reaches 0.
      refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
    - Grounding transitions clear the lock via ftCommon_UnlockECB (called by ftCommon_8007D6A4).
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4

    Representation:
    - Return u8 post-frame countdown values (clamped to [0,255]).
    - On a detected post-frame grounded->air transition, write `(lock_frames_ground_to_air - 1)`
      for that frame to account for the same-frame procMap decrement.
    """
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    n = int(on_ground.size)
    out = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out
    if int(action_id.size) != n:
        raise ValueError("action_id_u16 must match on_ground_u8 length")

    lock_frames = int(lock_frames_ground_to_air)
    lock_frames = int(np.clip(lock_frames, 0, 255))
    set_post = max(0, lock_frames - 1)
    jump_set = {
        int(act_jump_f) & 0xFFFF,
        int(act_jump_b) & 0xFFFF,
        int(act_jump_aerial_f) & 0xFFFF,
        int(act_jump_aerial_b) & 0xFFFF,
    }

    timer = 0
    prev_ground = bool(int(on_ground[0]) != 0)
    for i in range(n):
        cur_ground = bool(int(on_ground[i]) != 0)
        cur_action = int(action_id[i]) & 0xFFFF
        prev_action = int(action_id[i - 1]) & 0xFFFF if i > 0 else cur_action
        jump_entry = (i > 0) and (cur_action in jump_set) and (cur_action != prev_action)
        if cur_ground:
            timer = 0
        else:
            if jump_entry or (i > 0 and prev_ground):
                timer = set_post
            elif timer > 0:
                timer -= 1
        out[i] = np.uint8(timer)
        prev_ground = cur_ground

    return out


def derive_damage_jump_buffer_x14(
    *,
    action_id: np.ndarray,
    hitstun_u16: np.ndarray,
    buttons_pressed: np.ndarray,
    stick_y_unit: np.ndarray,
    tilt_timer_y: np.ndarray,
    tap_jump_threshold: float,
    tap_jump_tilt_max_frames: int,
    button_mask_xy: int,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Derive `fp->mv.co.damage.x14` (damage jump-buffer snapshot) per post-frame.

    Seed bridge scope:
    - Slippi does not expose `mv.co.damage.x14` directly, so this derives the internal lane from
      replay-visible signals for reseed parity.

    Decomp anchors:
    - Cleared on Damage entry in `ftCo_8008DCE0` (`mv.co.damage.x14 = 0`):
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    - Set from `mv.co.damage.x0` when jump input is detected in `doIasa`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    - Consumed by `inlineC0` gate before DamageFall enter in `ftCo_Damage_Anim` /
      `ftCo_DamageFly_Anim`, compared against `p_ftCommonData->x1D0`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineC0

    Causality / prefix-invariance:
    - Uses only <=t replay frames (current/past values), never future samples.
    - Resets on causal entry into the configured damage-action family.
    - Resets on in-family fresh-hit boundaries (`hitstun` increase), matching that
      ftCo_8008DCE0 clears x14 on damage re-entry.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    hs = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    bp = np.asarray(buttons_pressed, dtype=np.uint16).reshape(-1)
    sy = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    tty = np.asarray(tilt_timer_y, dtype=np.uint8).reshape(-1)
    n = int(a.size)
    if int(hs.size) != n or int(bp.size) != n or int(sy.size) != n or int(tty.size) != n:
        raise ValueError("all derive_damage_jump_buffer_x14 inputs must have the same length")

    out = np.zeros(n, dtype=np.uint16)
    if n == 0:
        return out

    damage_set = {int(x) & 0xFFFF for x in damage_actions}
    xy = int(button_mask_xy) & 0xFFFF
    tap_thr = np.float32(tap_jump_threshold)
    tilt_max = int(tap_jump_tilt_max_frames)

    x14 = 0
    for i in range(n):
        cur_a = int(a[i])
        in_damage = cur_a in damage_set
        if not in_damage:
            x14 = 0
            out[i] = np.uint16(0)
            continue

        prev_in_damage = i > 0 and (int(a[i - 1]) in damage_set)
        if i == 0 or not prev_in_damage:
            x14 = 0
        elif int(hs[i]) > int(hs[i - 1]):
            # Decomp: ftCo_8008DCE0 clears mv.co.damage.x14 on fresh damage entry.
            # Within the damage-family seed bridge, rising hitstun is the causal replay-visible
            # boundary for that re-entry while preserving <=t prefix-invariance.
            x14 = 0

        jump_input = False
        if (int(bp[i]) & xy) != 0:
            jump_input = True
        elif sy[i] >= tap_thr and int(tty[i]) < tilt_max:
            jump_input = True

        if int(hs[i]) > 0 and jump_input:
            x14 = int(hs[i])

        out[i] = np.uint16(max(0, min(x14, 0xFFFF)))

    return out


def derive_damage_post_hitlag_cb_kind(
    *,
    action_id: np.ndarray,
    hitstun_u16: np.ndarray,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Derive `fp->post_hitlag_cb` ownership lane for a configured action-family subset as a causal u8 enum.

    Enum encoding (u8):
    - 0: no callback
    - 1: `ftCo_Damage_OnExitHitlag`

    Decomp anchors:
    - Damage entry sets `fp->post_hitlag_cb = ftCo_Damage_OnExitHitlag`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    - Hitlag-exit path invokes `post_hitlag_cb`:
      refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
    - Callback body:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag

    Seed bridge scope:
    - Slippi does not expose callback pointers directly, so this lane is seeded from replay-visible
      action family/history for teacher-forced one-step parity.

    Causality / prefix-invariance:
    - Uses only <=t samples.
    - Uses a damage-family ownership latch and fresh-hit boundaries (`hitstun` increase) as causal
      re-entry markers.
    """
    a = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    hs = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    n = int(a.size)
    if int(hs.size) != n:
        raise ValueError("action_id and hitstun_u16 must have same length")

    out = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out

    damage_set = {int(x) & 0xFFFF for x in damage_actions}
    kind = 0
    prev_in_damage = False
    prev_hs = int(hs[0])

    for i in range(n):
        cur_a = int(a[i]) & 0xFFFF
        cur_hs = int(hs[i])
        in_damage = cur_a in damage_set

        if not in_damage:
            kind = 0
        else:
            entered_damage = (i == 0) or (not prev_in_damage)
            fresh_hit_reentry = (i > 0) and (cur_hs > prev_hs)
            if entered_damage or fresh_hit_reentry:
                kind = 1
            elif kind == 0:
                # Defensive latch restore for synthetic partial histories.
                kind = 1

        out[i] = np.uint8(kind)
        prev_in_damage = in_damage
        prev_hs = cur_hs

    return out


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


def compute_lr_press_timer_x67f(
    *,
    buttons: np.ndarray,
    trigger_unit: np.ndarray,
    hitlag_frames: np.ndarray | None = None,
    trigger_deadzone: float,
    button_mask_lr: int,
    button_mask_z: int,
    start_timer: int = 0xFF,
) -> np.ndarray:
    """
    Compute fp->x67F (frames since LR-lane press edge) causally.

    Decomp tie-down:
    - x67F update checks edge mask lane `fp->input.x668 & HSD_PAD_LR`.
      refs/melee/src/melee/ft/fighter.c:2078-2086
    - x668 edge lane derives from held-input synthesis that includes:
      - digital L/R,
      - trigger lane (`x650 > deadzone`), and
      - Z-mapped LR lane.
      refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
      refs/melee/src/melee/ft/fighter.c:1868-1890
    - When hitlag gate x2219_b5 is set, x668 edges are OR-latched (`x668 |= edge`) rather than
      overwritten, so an LR edge persists for the rest of that hitlag window.
      refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
    """
    b = np.asarray(buttons, dtype=np.uint16).reshape(-1)
    trig = np.asarray(trigger_unit, dtype=np.float32).reshape(-1)
    if int(b.size) != int(trig.size):
        raise ValueError("buttons and trigger_unit must match length")
    if hitlag_frames is None:
        hl = np.zeros(int(b.size), dtype=np.uint16)
    else:
        hl = np.asarray(hitlag_frames, dtype=np.uint16).reshape(-1)
        if int(hl.size) != int(b.size):
            raise ValueError("hitlag_frames must match buttons length")

    n = int(b.size)
    out = np.empty(n, dtype=np.uint8)
    timer = int(start_timer) & 0xFF
    prev_held = False
    x668_lr_latched = False
    mask = (int(button_mask_lr) | int(button_mask_z)) & 0xFFFF
    deadzone = np.float32(trigger_deadzone)

    for i in range(n):
        held = ((int(b[i]) & mask) != 0) or (np.float32(trig[i]) > deadzone)
        pressed_edge = held and (not prev_held)
        if int(hl[i]) > 0:
            x668_lr_latched = x668_lr_latched or pressed_edge
        else:
            x668_lr_latched = pressed_edge
        if x668_lr_latched:
            timer = 0
        elif timer < 0xFF:
            timer += 1
        out[i] = np.uint8(timer)
        prev_held = held
    return out


def _colanim_timer_remaining_from_action_frame(init_frames: int, action_frame: int) -> int:
    if int(init_frames) <= 0:
        return 0
    if int(action_frame) <= 0:
        return int(init_frames)
    rem = int(init_frames) + 1 - int(action_frame)
    if rem < 0:
        rem = 0
    if rem > 0xFFFF:
        rem = 0xFFFF
    return int(rem)


def derive_colanim_internals(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    hurtbox_state_u8: np.ndarray,
    colanim_throw_x1994_frames: int,
    colanim_cliff_x1990_frames: int,
    colanim_damage_x1994_frames: int,
    throw_actions: tuple[int, ...],
    cliff_actions: tuple[int, ...],
    damage_actions: tuple[int, ...],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive fp->x198C/x1990/x1994/x2221_b0 seed internals strictly causally.

    Why this exists:
    - Slippi exposes merged `hurtbox_state` (x1988 when nonzero else x198C), but not x198C timers.
    - One-step reseed needs timer ownership internals to avoid single-frame inference drift.

    Decomp anchors:
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (x1990/x1994 decrements + x198C updates)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398 (ftColl_8007B7A4, x1994)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C (ftColl_8007B760, x1990)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag (x1994)

    Notes:
    - `x2221_b0` currently has no reliable Slippi-exposed signal in this replay path; we keep it 0.
    - This is a seed bridge over missing internals, not a claim that every x198C source is modeled.
    """
    aid = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    afr = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    hl = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hs = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    hurt = np.asarray(hurtbox_state_u8, dtype=np.uint8).reshape(-1)
    n = int(aid.size)
    if int(afr.size) != n or int(hl.size) != n or int(hs.size) != n or int(hurt.size) != n:
        raise ValueError("action/action_frame/hitlag/hitstun/hurtbox_state arrays must match length")

    throw_set = {int(x) & 0xFFFF for x in throw_actions}
    cliff_set = {int(x) & 0xFFFF for x in cliff_actions}
    damage_set = {int(x) & 0xFFFF for x in damage_actions}

    out_x198c = np.zeros(n, dtype=np.uint8)
    out_x1990 = np.zeros(n, dtype=np.uint16)
    out_x1994 = np.zeros(n, dtype=np.uint16)
    out_x2221_b0 = np.zeros(n, dtype=np.uint8)

    x1990 = 0
    x1994 = 0
    x2221_b0 = 0
    prev_a = int(aid[0]) if n > 0 else 0
    prev_afr = int(afr[0]) if n > 0 else 0
    prev_hl = int(hl[0]) if n > 0 else 0
    prev_hs = int(hs[0]) if n > 0 else 0

    for i in range(n):
        cur_a = int(aid[i])
        cur_afr = int(afr[i])
        cur_hl = int(hl[i])
        cur_hs = int(hs[i])

        # Decomp ordering: x1990/x1994 decrement once per frame in Fighter_8006A360.
        if i > 0:
            if x1990 > 0:
                x1990 -= 1
            if x1994 > 0:
                x1994 -= 1

        entered = False
        if i == 0:
            entered = True
        elif cur_a != prev_a:
            entered = True
        elif cur_afr < prev_afr:
            # Same-action restart (e.g., self-transition with reset state_age).
            entered = True

        if entered and cur_a in throw_set:
            rem = _colanim_timer_remaining_from_action_frame(int(colanim_throw_x1994_frames), cur_afr)
            if rem > x1994:
                x1994 = rem

        if entered and cur_a in cliff_set:
            rem = _colanim_timer_remaining_from_action_frame(int(colanim_cliff_x1990_frames), cur_afr)
            if rem > x1990:
                x1990 = rem

        # Damage hitlag-exit hook: ftCo_Damage_OnExitHitlag sets x1994 via p_ftCommonData->x130.
        if i > 0 and prev_hl > 0 and cur_hl == 0:
            if cur_a in damage_set or prev_a in damage_set or cur_hs > 0 or prev_hs > 0:
                rem = int(colanim_damage_x1994_frames)
                if rem > 0xFFFF:
                    rem = 0xFFFF
                if rem > x1994:
                    x1994 = rem

        # Causal bootstrap for first frame when replay starts in an unobserved prior timer window.
        if i == 0 and x1990 == 0 and x1994 == 0:
            h0 = int(hurt[0])
            if h0 == 2:
                x1990 = 1
            elif h0 == 1:
                x1994 = 1

        if x1990 > 0 or x2221_b0:
            x198c = 2
        elif x1994 > 0:
            x198c = 1
        else:
            x198c = 0

        downbound_hidden_x1990_visible_zero = cur_a in {0x00BE, 0x00BF} and x1990 > 0
        if int(hurt[i]) == 0 and x1990 > 0 and not downbound_hidden_x1990_visible_zero:
            # Slippi post-frame emits `x1988` when nonzero, otherwise `x198C`; a replay-visible
            # hurtbox_state of 0 therefore proves move-induced status and the intangible x1990 lane
            # are clear at this snapshot. Do not let strictly-causal cliff/ledge x1990 reconstruction
            # stale-carry past that observable clear. Preserve x1994: DownBound/Damage OnExitHitlag
            # rows can expose visible 0 while the hidden invincible-contact x1994 lane remains active.
            # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
            x1990 = 0
            x2221_b0 = 0
            x198c = 1 if x1994 > 0 else 0

        out_x198c[i] = np.uint8(x198c)
        out_x1990[i] = np.uint16(x1990)
        out_x1994[i] = np.uint16(x1994)
        out_x2221_b0[i] = np.uint8(1 if x2221_b0 else 0)

        prev_a = cur_a
        prev_afr = cur_afr
        prev_hl = cur_hl
        prev_hs = cur_hs

    return out_x198c, out_x1990, out_x1994, out_x2221_b0


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
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute the stick-driven fighter input counters block (u8, saturating at 0xFE), causally.

    Outputs are per-frame post-update values for:
    - x673, x679_x, x676_x (lstick x companions + age counter)
    - x2228_b7 (most-recent fresh X-entry sign: 1 right / 0 left)
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
    out_x2228_b7 = np.empty(n, dtype=np.uint8)
    out_x679_x = np.empty(n, dtype=np.uint8)
    out_x674 = np.empty(n, dtype=np.uint8)
    out_x677_y = np.empty(n, dtype=np.uint8)
    out_x67A_y = np.empty(n, dtype=np.uint8)

    thr_x = np.float32(tilt_thresh_x)
    thr_y = np.float32(tilt_thresh_y)

    x673 = int(start_timer) & 0xFF
    x676_x = int(start_timer) & 0xFF
    x2228_b7 = int(0) & 0xFF
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
                x2228_b7 = 1
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
                x2228_b7 = 0
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
        out_x2228_b7[i] = np.uint8(x2228_b7)
        out_x679_x[i] = np.uint8(x679_x)
        out_x674[i] = np.uint8(x674)
        out_x677_y[i] = np.uint8(x677_y)
        out_x67A_y[i] = np.uint8(x67A_y)

        prev_x = cur_x
        prev_y = cur_y

    return out_x673, out_x674, out_x676_x, out_x2228_b7, out_x677_y, out_x679_x, out_x67A_y


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
    hitlag_frames: np.ndarray | None = None,
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

    When `hitlag_frames` is provided, model Fighter_Spaghetti_8006AD10_Inner1's x668
    OR-latch while fp->x2219_b5 remains active. This is causal from Slippi post-frame
    hitlag: post_hitlag[i] > 0 means the input pass for frame i still ran under the hitlag gate.
    The latch is especially important for `x680`/`x684`: repeated latched digital L/R frames
    overwrite x684 with the just-reset x680, satisfying ftCo_800986B0's debounce behavior.
    """
    bp = np.asarray(buttons_pressed, dtype=np.uint16).reshape(-1)
    n = int(bp.size)
    if hitlag_frames is None:
        hl = np.zeros(n, dtype=np.uint16)
    else:
        hl = np.asarray(hitlag_frames, dtype=np.uint16).reshape(-1)
        if int(hl.size) != n:
            raise ValueError("hitlag_frames must match buttons_pressed length")

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

    x668_latched = 0
    for i in range(n):
        raw_bpi = int(bp[i])
        if int(hl[i]) > 0:
            x668_latched |= raw_bpi
            bpi = x668_latched
        else:
            x668_latched = raw_bpi
            bpi = raw_bpi

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


def derive_downwait_timer(
    *,
    action_id_u16: np.ndarray,
    down_wait_frames: int,
    act_down_wait_u: int,
    act_down_wait_d: int,
) -> np.ndarray:
    """
    Derive fp->mv.co.downwait.x0 (DownWait timer), strictly causally from action_id.

    Decomp:
    - init on DownBound->DownWait:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
        fp->mv.co.downwait.x0 = p_ftCommonData->x424;
    - decrement each DownWait frame unless fp->x2224_b2:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Anim

    Slippi post-frame does not expose fp->mv.* union fields, so teacher-forced reseeding requires
    deriving this internal from replay state history. For v1 suite, fp->x2224_b2 is not exposed by
    Slippi either, so this derivation assumes the common case (x2224_b2 == 0) and counts down once
    per contiguous DownWait frame after entry.
    """
    aid = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    n = int(aid.size)
    out = np.zeros(n, dtype=np.int16)

    dw_u = np.uint16(int(act_down_wait_u) & 0xFFFF)
    dw_d = np.uint16(int(act_down_wait_d) & 0xFFFF)
    start = int(down_wait_frames)
    if start < 0:
        start = 0
    if start > 0x7FFF:
        start = 0x7FFF

    timer = 0
    prev_is_dw = False
    for i in range(n):
        is_dw = bool(aid[i] == dw_u or aid[i] == dw_d)
        if not is_dw:
            timer = 0
            out[i] = np.int16(0)
            prev_is_dw = False
            continue

        if not prev_is_dw:
            timer = start
        else:
            if timer > 0:
                timer -= 1

        out[i] = np.int16(timer)
        prev_is_dw = True

    return out


# Grab/throw victim attachment owner identity (seeded; suite-focused).
#
# Decomp source of truth for action ids:
# - refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`
# - refs/melee/src/melee/ft/ftmotionstates.c (comments with numeric ids)
_ACT_CAPTURE_PULLED_HI = np.uint16(0x00DF)  # ftCo_MS_CapturePulledHi (223)
_ACT_CAPTURE_WAIT_HI = np.uint16(0x00E0)  # ftCo_MS_CaptureWaitHi (224)
_ACT_CAPTURE_DAMAGE_HI = np.uint16(0x00E1)  # ftCo_MS_CaptureDamageHi (225)
_ACT_CAPTURE_PULLED_LW = np.uint16(0x00E2)  # ftCo_MS_CapturePulledLw (226)
_ACT_CAPTURE_WAIT_LW = np.uint16(0x00E3)  # ftCo_MS_CaptureWaitLw (227)
_ACT_CAPTURE_DAMAGE_LW = np.uint16(0x00E4)  # ftCo_MS_CaptureDamageLw (228)
_ACT_CAPTURE_CUT = np.uint16(0x00E5)  # ftCo_MS_CaptureCut (229)
_ACT_CAPTURE_JUMP = np.uint16(0x00E6)  # ftCo_MS_CaptureJump (230)
_ACT_CAPTURE_NECK = np.uint16(0x00E7)  # ftCo_MS_CaptureNeck (231)
_ACT_CAPTURE_FOOT = np.uint16(0x00E8)  # ftCo_MS_CaptureFoot (232)
_ACT_THROWN_F = np.uint16(0x00EF)  # ftCo_MS_ThrownF (239)
_ACT_THROWN_B = np.uint16(0x00F0)  # ftCo_MS_ThrownB (240)
_ACT_THROWN_HI = np.uint16(0x00F1)  # ftCo_MS_ThrownHi (241)
_ACT_THROWN_LW = np.uint16(0x00F2)  # ftCo_MS_ThrownLw (242)
_ACT_THROWN_LW_WOMEN = np.uint16(0x00F3)  # ftCo_MS_ThrownlwWomen (243)
_BTN_A = np.uint16(0x0100)
_BTN_B = np.uint16(0x0200)
_BTN_X = np.uint16(0x0400)
_BTN_Y = np.uint16(0x0800)
_BTN_L = np.uint16(0x0040)
_BTN_R = np.uint16(0x0020)
_BTN_XY = np.uint16(0x0C00)
_CAPTURE_ATTACH_ACTIONS = (
    _ACT_CAPTURE_PULLED_HI,
    _ACT_CAPTURE_WAIT_HI,
    _ACT_CAPTURE_DAMAGE_HI,
    _ACT_CAPTURE_PULLED_LW,
    _ACT_CAPTURE_WAIT_LW,
    _ACT_CAPTURE_DAMAGE_LW,
)
_CAPTURE_WAIT_ACTIONS = (_ACT_CAPTURE_WAIT_HI, _ACT_CAPTURE_WAIT_LW)
_CAPTURE_DAMAGE_ACTIONS = (_ACT_CAPTURE_DAMAGE_HI, _ACT_CAPTURE_DAMAGE_LW)
_CAPTURE_WAIT_OR_DAMAGE_ACTIONS = _CAPTURE_WAIT_ACTIONS + _CAPTURE_DAMAGE_ACTIONS


def derive_grab_owner_port_2p(*, action_id_u16_2p: np.ndarray) -> np.ndarray:
    """Derive per-frame grab owner identity for 2-player replays (slot domain).

    Returns an array of shape [n_frames, 2] (dtype u8) where:
    - out[i, p] = other slot index (0/1) if player p is a captured/thrown victim at frame i
    - out[i, p] = 0xFF otherwise

    Causality / prefix-invariance:
    - Depends only on the current frame's action_id values (no lookahead).
    """
    a = np.asarray(action_id_u16_2p, dtype=np.uint16)
    if a.ndim != 2 or a.shape[1] != 2:
        raise ValueError(f"action_id_u16_2p must have shape [n,2], got {a.shape}")

    # Attached captured/thrown victim action ids (common).
    #
    # CaptureCut / CaptureJump are not part of the attached owner link any more:
    # ftCo_CaptureWaitHi_Anim breaks out through ftCo_800DA698 / fn_800DC070, both of which route
    # through ftCo_800DC920 and clear owner/victim pointers before entering CatchCut/CaptureCut/
    # CaptureJump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_800DA698,fn_800DC070
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
    is_victim = (
        (a == _ACT_CAPTURE_PULLED_HI)
        | (a == _ACT_CAPTURE_WAIT_HI)
        | (a == _ACT_CAPTURE_DAMAGE_HI)
        | (a == _ACT_CAPTURE_PULLED_LW)
        | (a == _ACT_CAPTURE_WAIT_LW)
        | (a == _ACT_CAPTURE_DAMAGE_LW)
        | (a == _ACT_CAPTURE_NECK)
        | (a == _ACT_CAPTURE_FOOT)
        | (a == _ACT_THROWN_F)
        | (a == _ACT_THROWN_B)
        | (a == _ACT_THROWN_HI)
        | (a == _ACT_THROWN_LW)
        | (a == _ACT_THROWN_LW_WOMEN)
    )

    out = np.full((a.shape[0], 2), 0xFF, dtype=np.uint8)
    out[:, 0] = np.where(is_victim[:, 0], np.uint8(1), np.uint8(0xFF))
    out[:, 1] = np.where(is_victim[:, 1], np.uint8(0), np.uint8(0xFF))

    # Defensive guard: in real gameplay, "both players are grab/capture/thrown victims in the same
    # frame" should not occur. If it does appear in corrupted/weird data, do not create a cyclic
    # attachment (0<->1). Treat both as unattached.
    both_victims = is_victim[:, 0] & is_victim[:, 1]
    out[both_victims, :] = np.uint8(0xFF)
    return out


def derive_seed_prev_action_post(
    *, post_action_id_u16: np.ndarray, post_action_frame_i16: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    """Derive replay-true previous-action seed lanes for one-step rows.

    Output row i corresponds to the seed snapshot built from post-frame row i:
    - row 0 has no prior replay row inside the dataset window, so it reuses the current seed action
    - rows i>0 use post-frame row i-1 as the previous-action source
    """
    action_id = np.asarray(post_action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(post_action_frame_i16, dtype=np.int16).reshape(-1)
    if action_id.shape != action_frame.shape:
        raise ValueError(
            f"post_action_id_u16 and post_action_frame_i16 must match, got "
            f"{action_id.shape} vs {action_frame.shape}"
        )
    if action_id.size == 0:
        return np.zeros((0,), dtype=np.uint16), np.zeros((0,), dtype=np.int16)

    out_len = max(0, int(action_id.size) - 1)
    out_action_id = np.empty((out_len,), dtype=np.uint16)
    out_action_frame = np.empty((out_len,), dtype=np.int16)
    if out_len == 0:
      return out_action_id, out_action_frame

    out_action_id[0] = action_id[0]
    out_action_frame[0] = action_frame[0]
    if out_len > 1:
      out_action_id[1:] = action_id[:-2]
      out_action_frame[1:] = action_frame[:-2]
    return out_action_id, out_action_frame


def derive_grab_mash_stick_sign_post(
    *,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    grab_mash_stick_threshold: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Derive post-frame ftCommon_GrabMash stick-sign latches (`x1A50` / `x1A51`)."""
    sx = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    sy = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    if sx.shape != sy.shape:
        raise ValueError(f"stick_x_unit and stick_y_unit must match, got {sx.shape} vs {sy.shape}")
    thresh = np.float32(float(grab_mash_stick_threshold))
    out_x = np.zeros(sx.shape[0], dtype=np.int8)
    out_y = np.zeros(sy.shape[0], dtype=np.int8)
    latch_x = np.int8(0)
    latch_y = np.int8(0)
    for i in range(sx.shape[0]):
        if sx[i] < -thresh:
            latch_x = np.int8(-1)
        elif sx[i] > thresh:
            latch_x = np.int8(1)
        if sy[i] < -thresh:
            latch_y = np.int8(-1)
        elif sy[i] > thresh:
            latch_y = np.int8(1)
        out_x[i] = latch_x
        out_y[i] = latch_y
    return out_x, out_y


def derive_capture_grab_hidden_post(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    grab_owner_port_u8: np.ndarray,
    percent_f32: np.ndarray,
    buttons_held_u16: np.ndarray,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    frame_speed_mul_f32: np.ndarray,
    grab_mash_stick_x_sign_post: np.ndarray,
    grab_mash_stick_y_sign_post: np.ndarray,
    slot_index: int,
    handicap: int,
    capture_grab_timer_base: float,
    capture_grab_timer_handicap_mul: float,
    capture_grab_timer_handicap_base: float,
    capture_grab_timer_slot_mul: float,
    capture_grab_timer_slot_base: float,
    capture_grab_timer_percent_mul: float,
    capture_wait_grab_timer_decrement: float,
    capture_wait_grab_mash_damage: float,
    capture_wait_anim_rate_hold_frames: float,
    capture_wait_jump_latch_window_frames: float,
    grab_mash_stick_threshold: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive explicit CaptureWait/CaptureDamage hidden owner lanes.

    Output row `i` corresponds to the post-frame replay row `i` and is suitable for seeding the
    next one-step transition from replay row `i` to replay row `i+1`.
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    owner = np.asarray(grab_owner_port_u8, dtype=np.uint8).reshape(-1)
    percent = np.asarray(percent_f32, dtype=np.float32).reshape(-1)
    buttons = np.asarray(buttons_held_u16, dtype=np.uint16).reshape(-1)
    stick_x = np.asarray(stick_x_unit, dtype=np.float32).reshape(-1)
    stick_y = np.asarray(stick_y_unit, dtype=np.float32).reshape(-1)
    frame_speed = np.asarray(frame_speed_mul_f32, dtype=np.float32).reshape(-1)
    mash_x = np.asarray(grab_mash_stick_x_sign_post, dtype=np.int8).reshape(-1)
    mash_y = np.asarray(grab_mash_stick_y_sign_post, dtype=np.int8).reshape(-1)
    if (
        action_id.shape != action_frame.shape
        or action_id.shape != owner.shape
        or action_id.shape != percent.shape
        or action_id.shape != buttons.shape
        or action_id.shape != stick_x.shape
        or action_id.shape != stick_y.shape
        or action_id.shape != frame_speed.shape
        or action_id.shape != mash_x.shape
        or action_id.shape != mash_y.shape
    ):
        raise ValueError("capture/grab hidden derivation inputs must all match")

    n = action_id.shape[0]
    out_grab_timer = np.zeros(n, dtype=np.float32)
    out_counter = np.zeros(n, dtype=np.float32)
    out_anim_timer = np.zeros(n, dtype=np.float32)
    out_jump_latch = np.zeros(n, dtype=np.uint8)
    out_breakout_pending = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return (
            out_grab_timer,
            out_counter,
            out_anim_timer,
            out_jump_latch,
            out_breakout_pending,
        )

    def _is_attach_action(a: np.uint16) -> bool:
        return int(a) in {int(v) for v in _CAPTURE_ATTACH_ACTIONS}

    def _is_wait_action(a: np.uint16) -> bool:
        return int(a) in {int(v) for v in _CAPTURE_WAIT_ACTIONS}

    def _is_wait_or_damage_action(a: np.uint16) -> bool:
        return int(a) in {int(v) for v in _CAPTURE_WAIT_OR_DAMAGE_ACTIONS}

    def _grab_timer_init(i: int) -> np.float32:
        slot = np.float32(float(slot_index + 1))
        hcap = np.float32(float(handicap))
        return np.float32(
            capture_grab_timer_base
            + capture_grab_timer_handicap_mul * (capture_grab_timer_handicap_base - hcap)
            + capture_grab_timer_slot_mul * (capture_grab_timer_slot_base - slot)
            + percent[i] * capture_grab_timer_percent_mul
        )

    def _seed_mid_segment_row(i: int) -> tuple[np.float32, np.float32, np.float32, np.uint8]:
        timer = _grab_timer_init(i)
        counter = np.float32(0.0)
        anim_timer = np.float32(0.0)
        jump_latch = np.uint8(0)
        # Dataset windows can start mid-capture segment. Use the replay-visible state only to
        # restore the shared owner lanes, without reintroducing cross-row rate bridges.
        if _is_wait_or_damage_action(action_id[i]):
          callbacks = max(0, int(action_frame[i]))
          if callbacks > 0:
              counter = np.float32(float(callbacks))
              timer = np.float32(max(0.0, float(timer) - callbacks * capture_wait_grab_timer_decrement))
          if frame_speed[i] > np.float32(1.0):
              anim_timer = np.float32(capture_wait_anim_rate_hold_frames)
        return timer, counter, anim_timer, jump_latch

    if _is_attach_action(action_id[0]) and owner[0] != np.uint8(0xFF):
        (
            out_grab_timer[0],
            out_counter[0],
            out_anim_timer[0],
            out_jump_latch[0],
        ) = _seed_mid_segment_row(0)

    for i in range(n - 1):
        timer = float(out_grab_timer[i])
        counter = float(out_counter[i])
        anim_timer = float(out_anim_timer[i])
        jump_latch = int(out_jump_latch[i])
        next_timer = np.float32(0.0)
        next_counter = np.float32(0.0)
        next_anim_timer = np.float32(0.0)
        next_jump_latch = np.uint8(0)
        if _is_attach_action(action_id[i]) and owner[i] != np.uint8(0xFF):
            if _is_wait_or_damage_action(action_id[i]):
                counter += 1.0
                timer -= float(capture_wait_grab_timer_decrement)

                held = buttons[i + 1]
                mash_active = (
                    held & (_BTN_A | _BTN_B | _BTN_X | _BTN_Y | _BTN_L | _BTN_R)
                ) != 0
                next_x = mash_x[i]
                next_y = mash_y[i]
                if stick_x[i + 1] < -np.float32(grab_mash_stick_threshold):
                    next_x = np.int8(-1)
                elif stick_x[i + 1] > np.float32(grab_mash_stick_threshold):
                    next_x = np.int8(1)
                if stick_y[i + 1] < -np.float32(grab_mash_stick_threshold):
                    next_y = np.int8(-1)
                elif stick_y[i + 1] > np.float32(grab_mash_stick_threshold):
                    next_y = np.int8(1)
                if next_x != mash_x[i] or next_y != mash_y[i]:
                    mash_active = True
                if mash_active:
                    timer -= float(capture_wait_grab_mash_damage)

                if timer > 0.0:
                    if anim_timer != 0.0:
                        anim_timer -= 1.0
                        if anim_timer <= 0.0 and not mash_active:
                            anim_timer = 0.0
                    if anim_timer <= 0.0 and mash_active:
                        anim_timer = float(capture_wait_anim_rate_hold_frames)

            if _is_wait_action(action_id[i]):
                if counter < float(capture_wait_jump_latch_window_frames) and (
                    buttons[i + 1] & _BTN_XY
                ) != 0:
                    jump_latch = 1

            if _is_wait_action(action_id[i]) and action_id[i + 1] in (
                _ACT_CAPTURE_CUT,
                _ACT_CAPTURE_JUMP,
            ):
                out_breakout_pending[i] = np.uint8(1)

            if _is_attach_action(action_id[i + 1]) and owner[i + 1] == owner[i]:
                next_timer = np.float32(max(0.0, timer))
                next_counter = np.float32(max(0.0, counter))
                next_anim_timer = np.float32(max(0.0, anim_timer))
                next_jump_latch = np.uint8(1 if jump_latch else 0)
                if _is_wait_action(action_id[i]) and action_id[i + 1] in _CAPTURE_DAMAGE_ACTIONS:
                    next_anim_timer = np.float32(0.0)

        if next_timer == np.float32(0.0) and next_counter == np.float32(0.0) and (
            _is_attach_action(action_id[i + 1]) and owner[i + 1] != np.uint8(0xFF)
        ):
            next_timer, next_counter, next_anim_timer, next_jump_latch = _seed_mid_segment_row(i + 1)

        out_grab_timer[i + 1] = next_timer
        out_counter[i + 1] = next_counter
        out_anim_timer[i + 1] = next_anim_timer
        out_jump_latch[i + 1] = next_jump_latch
    return (
        out_grab_timer,
        out_counter,
        out_anim_timer,
        out_jump_latch,
        out_breakout_pending,
    )
