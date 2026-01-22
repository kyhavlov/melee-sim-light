from __future__ import annotations

import json

import numpy as np

from tools.slippi.seed_history import derive_guard_tilt_state, load_shield_tilt_table_meta


def _mk_action_frame(action_id: np.ndarray) -> np.ndarray:
    aid = np.asarray(action_id, dtype=np.uint16).reshape(-1)
    out = np.empty(aid.size, dtype=np.int16)
    age = np.int16(0)
    prev = np.uint16(aid[0]) if aid.size else np.uint16(0)
    for i in range(aid.size):
        if i == 0 or np.uint16(aid[i]) != prev:
            age = np.int16(0)
        else:
            age = np.int16(int(age) + 1)
        out[i] = age
        prev = np.uint16(aid[i])
    return out


def test_guard_tilt_derivation_is_prefix_invariant() -> None:
    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_reflect = 0x00B6

    meta = load_shield_tilt_table_meta()
    neutral, frame_max = meta[1]  # Fox (char_id=1) table exists in repo data/

    guard_stick_lerp_x44c = float(json.loads(open("data/common/ft_common_data.json").read())["guard_stick_lerp_x44c"])

    n = 64
    t = np.arange(n, dtype=np.float32)
    stick_x = np.clip(np.sin(t * np.float32(0.2)), -1.0, 1.0).astype(np.float32)
    stick_y = np.clip(np.cos(t * np.float32(0.15)), -1.0, 1.0).astype(np.float32)

    facing = ((t % np.float32(17.0)) < np.float32(9.0)).astype(np.uint8)

    action_id = np.full(n, np.uint16(act_wait), dtype=np.uint16)
    action_id[5:9] = np.uint16(act_guard_on)
    action_id[9:25] = np.uint16(act_guard)
    action_id[25:30] = np.uint16(act_guard_reflect)
    action_id[30:40] = np.uint16(act_guard)

    action_frame = _mk_action_frame(action_id)

    neutral_frame = np.full(n, np.uint16(neutral), dtype=np.uint16)
    frame_max_arr = np.full(n, np.uint16(frame_max), dtype=np.uint16)

    full_x8, full_x4 = derive_guard_tilt_state(
        stick_x,
        stick_y,
        facing=facing,
        action_id=action_id,
        action_frame=action_frame,
        neutral_frame=neutral_frame,
        frame_max=frame_max_arr,
        guard_stick_lerp_x44c=guard_stick_lerp_x44c,
        act_guard_on=act_guard_on,
        act_guard=act_guard,
        act_guard_reflect=act_guard_reflect,
    )

    for k in (1, 2, 3, 7, 8, 9, 10, 17, 31, 63, 64):
        x8, x4 = derive_guard_tilt_state(
            stick_x[:k],
            stick_y[:k],
            facing=facing[:k],
            action_id=action_id[:k],
            action_frame=action_frame[:k],
            neutral_frame=neutral_frame[:k],
            frame_max=frame_max_arr[:k],
            guard_stick_lerp_x44c=guard_stick_lerp_x44c,
            act_guard_on=act_guard_on,
            act_guard=act_guard,
            act_guard_reflect=act_guard_reflect,
        )
        assert np.array_equal(x8, full_x8[:k])
        assert np.array_equal(x4, full_x4[:k])
