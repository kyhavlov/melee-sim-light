from __future__ import annotations

import numpy as np

from tools.slippi.anim_timebase import EndFrameTables, derive_frame_speed_mul_f32
from tools.slippi.validation_buffer_fighter import _derive_landing_fallspecial_allow_interrupt_seed_lane


def _frame_speed_oracle(
    *,
    state_age_f32: np.ndarray,
    action_id: np.ndarray,
    hitlag: np.ndarray,
    char_id: np.ndarray,
    animation_index: np.ndarray,
    lr_press_timer: np.ndarray,
    shield_hp: np.ndarray | None,
    lightshield_amount: np.ndarray | None,
    end_frames: EndFrameTables,
    common_lcancel_window_frames: int,
    common_lcancel_lag_div: float,
    common_landing_fall_special_lag_frames: float,
    char_landing_air_lag_frames: dict[int, dict[str, int]],
    char_fallspecial_origin_lag: dict[int, dict[int, float]] | None,
) -> np.ndarray:
    n = int(state_age_f32.shape[0])
    out = np.empty(n, dtype=np.float32)
    if n == 0:
        return out
    last = np.float32(1.0)
    out[0] = last

    act_landing_air_n = np.uint16(0x0046)
    act_landing_air_f = np.uint16(0x0047)
    act_landing_air_b = np.uint16(0x0048)
    act_landing_air_hi = np.uint16(0x0049)
    act_landing_air_lw = np.uint16(0x004A)
    act_landing_fall_special = np.uint16(0x002B)
    act_fall_special = np.uint16(0x0023)
    act_fall_special_f = np.uint16(0x0024)
    act_fall_special_b = np.uint16(0x0025)
    act_escape_air = np.uint16(0x00EC)
    act_guard_set_off = np.uint16(0x00B5)
    fall_special_actions = {int(act_fall_special), int(act_fall_special_f), int(act_fall_special_b)}

    def landing_fall_special_lag_for_entry(i: int, cid: int) -> float:
        prev_action = int(action_id[i - 1])
        origin_action = prev_action
        if prev_action in fall_special_actions:
            j = i - 1
            while j > 0 and int(action_id[j - 1]) in fall_special_actions:
                j -= 1
            origin_action = int(action_id[j - 1]) if j > 0 else prev_action
        if origin_action == int(act_escape_air):
            return float(common_landing_fall_special_lag_frames)
        origin_map = {} if char_fallspecial_origin_lag is None else char_fallspecial_origin_lag.get(cid, {})
        lag = origin_map.get(int(origin_action))
        return float(common_landing_fall_special_lag_frames if lag is None else lag)

    for i in range(1, n):
        changed = (
            int(action_id[i]) != int(action_id[i - 1])
            or int(animation_index[i]) != int(animation_index[i - 1])
            or int(char_id[i]) != int(char_id[i - 1])
        )
        if not changed:
            if int(hitlag[i]) != 0:
                out[i] = last
                continue
            delta = np.float32(state_age_f32[i] - state_age_f32[i - 1])
            if np.isfinite(delta) and float(delta) >= 0.0:
                last = delta
            out[i] = last
            continue

        cid = int(char_id[i])
        msid_u32 = int(animation_index[i])
        msid = int(msid_u32 & 0xFFFF) if msid_u32 != 0xFFFFFFFF else None
        end_frame = None if msid is None else end_frames.by_char_id.get(cid, {}).get(int(msid))
        a = np.uint16(action_id[i])

        if a == act_landing_fall_special and end_frame is not None:
            lag = landing_fall_special_lag_for_entry(i, cid)
            if lag > 0.0:
                last = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))
                out[i] = last
                continue

        if a in (act_landing_air_n, act_landing_air_f, act_landing_air_b, act_landing_air_hi, act_landing_air_lw):
            lag_map = char_landing_air_lag_frames.get(cid)
            if lag_map is not None and end_frame is not None:
                key = (
                    "airn"
                    if a == act_landing_air_n
                    else "airf"
                    if a == act_landing_air_f
                    else "airb"
                    if a == act_landing_air_b
                    else "airhi"
                    if a == act_landing_air_hi
                    else "airlw"
                )
                lag = float(lag_map.get(key, 0))
                if lag > 0.0 and int(lr_press_timer[i]) < int(common_lcancel_window_frames):
                    int_lag = int(lag / float(common_lcancel_lag_div))
                    lag = float(1 if int_lag == 0 else int_lag)
                if lag > 0.0:
                    last = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))
                    out[i] = last
                    continue

        if a == act_guard_set_off and int(action_id[i - 1]) != int(act_guard_set_off) and end_frame is not None:
            assert shield_hp is not None
            assert lightshield_amount is not None
            shield_drop = np.float32(shield_hp[i - 1] - shield_hp[i])
            if float(shield_drop) > 0.0:
                ls = np.float32(lightshield_amount[i])
                ls = np.float32(min(max(float(ls), 0.0), 1.0))
                shield_hit_light_term = np.float32(ls * np.float32(0.2) + np.float32(0.1))
                shield_hit_den = np.float32(np.float32(1.0) * (np.float32(1.0) - shield_hit_light_term))
                shield_damage_taken = np.float32((shield_drop - np.float32(0.0)) / shield_hit_den)
                int_dmg_est = np.float32(np.trunc(max(float(shield_damage_taken), 0.0)))
                shield_stun_light_term = np.float32(ls * np.float32(0.65) + np.float32(0.05))
                setoff_f = np.float32(np.float32(1.5) * (int_dmg_est * (np.float32(1.0) - shield_stun_light_term)) + np.float32(2.0))
                if float(setoff_f) > 0.0:
                    last = np.float32((np.float32(end_frame) + np.float32(0.1)) / setoff_f)
                    out[i] = last
                    continue

        last = np.float32(1.0)
        out[i] = last
    return out


def test_native_frame_speed_mul_matches_oracle_for_entry_families() -> None:
    action_id = np.array(
        [
            0x000E,
            0x000E,
            0x000E,
            0x0042,
            0x0047,
            0x0047,
            0x0160,
            0x002B,
            0x0166,
            0x0023,
            0x002B,
            0x00B2,
            0x00B5,
            0x00B5,
        ],
        dtype=np.uint16,
    )
    state_age = np.array([0.0, 1.0, 1.0, 5.0, -1.0, 0.8272727, 20.0, -1.0, 3.0, 4.0, -1.0, -1.0, 0.0, 0.0], dtype=np.float32)
    hitlag = np.array([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2], dtype=np.uint16)
    char_id = np.full(action_id.shape[0], 1, dtype=np.uint8)
    animation_index = np.array([0, 0, 0, 100, 10, 10, 350, 36, 356, 26, 36, 37, 40, 40], dtype=np.uint32)
    lr_press_timer = np.array([0xFF, 0xFF, 0xFF, 0xFF, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF], dtype=np.uint8)
    shield_hp = np.array([60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 60.0, 55.52, 55.52], dtype=np.float32)
    lightshield = np.ones(action_id.shape[0], dtype=np.float32)
    end_frames = EndFrameTables(by_char_id={1: {10: 20.0, 36: 30.0, 40: 20.0}})
    char_lags = {1: {"airn": 15, "airf": 22, "airb": 20, "airhi": 18, "airlw": 18}}
    origin_lags = {1: {0x0160: 20.0, 0x0164: 18.0, 0x0166: 18.0, 0x0167: 18.0}}

    got = derive_frame_speed_mul_f32(
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
        char_landing_air_lag_frames=char_lags,
        char_fallspecial_origin_lag=origin_lags,
    )
    expected = _frame_speed_oracle(
        state_age_f32=state_age,
        action_id=action_id,
        hitlag=hitlag,
        char_id=char_id,
        animation_index=animation_index,
        lr_press_timer=lr_press_timer,
        shield_hp=shield_hp,
        lightshield_amount=lightshield,
        end_frames=end_frames,
        common_lcancel_window_frames=7,
        common_lcancel_lag_div=2.0,
        common_landing_fall_special_lag_frames=10.0,
        char_landing_air_lag_frames=char_lags,
        char_fallspecial_origin_lag=origin_lags,
    )

    np.testing.assert_array_equal(got, expected)


def _landing_fallspecial_allow_oracle(
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray,
    origin_allow_by_char: dict[int, dict[int, tuple[int, int]]],
) -> np.ndarray:
    action = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    char_ids = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    out = np.zeros(int(action.shape[0]), dtype=np.uint8)
    fall_actions = {0x0023, 0x0024, 0x0025}
    fallspecial_allow = 0
    lfs_allow = 0
    prev = -1
    for i, raw in enumerate(action):
        cur = int(raw)
        if i == 0 or cur != prev:
            origin_allow = origin_allow_by_char.get(int(char_ids[i]), {})
            if cur in fall_actions:
                fallspecial_allow = 0 if prev == 0x00EC else int(origin_allow.get(prev, (1, 0))[0])
                lfs_allow = 0
            elif cur == 0x002B:
                if prev in fall_actions:
                    lfs_allow = 1 if fallspecial_allow != 0 else 0
                elif prev in origin_allow:
                    lfs_allow = int(origin_allow[prev][1])
                else:
                    lfs_allow = 0
            else:
                fallspecial_allow = 0
                lfs_allow = 0
        if cur in fall_actions:
            out[i] = np.uint8(1 if fallspecial_allow != 0 else 0)
        elif cur == 0x002B:
            out[i] = np.uint8(1 if lfs_allow != 0 else 0)
        prev = cur
    return out


def test_native_landing_fallspecial_allow_matches_oracle() -> None:
    action_id = np.array(
        [
            0x00EC,
            0x0023,
            0x0023,
            0x002B,
            0x0160,
            0x002B,
            0x0166,
            0x0023,
            0x002B,
            0x015F,
            0x002B,
        ],
        dtype=np.uint16,
    )
    char_id = np.full(action_id.shape[0], 1, dtype=np.uint8)
    origin_allow = {1: {0x0160: (1, 0), 0x0166: (1, 1), 0x015F: (0, 0)}}

    got = _derive_landing_fallspecial_allow_interrupt_seed_lane(
        action_id_u16=action_id,
        char_id_u8=char_id,
        origin_allow_by_char=origin_allow,
    )
    expected = _landing_fallspecial_allow_oracle(action_id, char_id, origin_allow)

    np.testing.assert_array_equal(got, expected)
