from __future__ import annotations

import json

import numpy as np

from tools.modelplay.state_adapter import SimFrameState
from tools.viewer.msltrace1 import MslTraceWriter


ITEM_DTYPE = np.dtype(
    [
        ("exists", "u1"),
        ("type", "<u2"),
        ("state", "u1"),
        ("owner", "i1"),
        ("pos_x", "<f4"),
        ("pos_y", "<f4"),
        ("vel_x", "<f4"),
        ("vel_y", "<f4"),
        ("direction", "<f4"),
        ("damage", "<u2"),
        ("timer", "<f4"),
        ("spawn_id", "<u4"),
        ("misc0", "u1"),
        ("misc1", "u1"),
        ("misc2", "u1"),
    ],
    align=False,
)


def _state(*, action_frame: int, randall_x: float = 0.0) -> SimFrameState:
    return SimFrameState(
        frame_id=action_frame,
        stage_id=32,
        num_players=2,
        is_teams=False,
        team_id=np.array([0, 1], dtype=np.uint8),
        char_id=np.array([1, 22], dtype=np.uint8),
        pos_x=np.array([-30.0, 30.0], dtype=np.float32),
        pos_y=np.array([0.0, 0.0], dtype=np.float32),
        speed_air_x_self=np.zeros(2, dtype=np.float32),
        speed_ground_x_self=np.zeros(2, dtype=np.float32),
        speed_y_self=np.zeros(2, dtype=np.float32),
        speed_x_attack=np.zeros(2, dtype=np.float32),
        speed_y_attack=np.zeros(2, dtype=np.float32),
        facing=np.array([1, 0], dtype=np.uint8),
        on_ground=np.ones(2, dtype=np.uint8),
        is_dead=np.zeros(2, dtype=np.uint8),
        action_id=np.array([14, 14], dtype=np.uint16),
        action_frame=np.array([action_frame, action_frame], dtype=np.int16),
        jumps_left=np.array([2, 2], dtype=np.uint8),
        stocks=np.array([4, 4], dtype=np.uint8),
        percent=np.zeros(2, dtype=np.float32),
        shield_hp=np.array([60.0, 60.0], dtype=np.float32),
        state_flags=np.zeros((2, 5), dtype=np.uint8),
        hitlag=np.zeros(2, dtype=np.uint16),
        hitstun=np.zeros(2, dtype=np.uint16),
        hurtbox_state=np.zeros(2, dtype=np.uint8),
        items=np.zeros(0, dtype=ITEM_DTYPE),
        frame_pre_random_seed=12345,
        stage_randall_exists=randall_x != 0.0,
        stage_randall_x=randall_x,
        stage_randall_y=-33.2489,
    )


def test_msltrace_writer_uses_sparse_deltas(tmp_path):
    trace = MslTraceWriter()
    trace.add_frame(_state(action_frame=0), {})
    trace.add_frame(_state(action_frame=1), {})

    path = tmp_path / "trace.msltrace.json"
    trace.write_json(path)
    raw = path.read_text()
    payload = json.loads(raw)

    assert "\n " not in raw
    assert payload["format"] == "MSLTRACE1"
    assert payload["frames"]["rows"][0][0] == 0
    assert payload["frames"]["rows"][1][0] == 1
    assert payload["frames"]["rows"][1][2] is None
    assert payload["frames"]["rows"][1][3] == [[[2, 1]], [[2, 1]]]


def test_msltrace_writer_serializes_sparse_randall_stage_state(tmp_path):
    trace = MslTraceWriter()
    trace.add_frame(_state(action_frame=0), {})
    trace.add_frame(_state(action_frame=1, randall_x=74.423), {})

    payload = trace.to_payload()

    assert payload["stage"]["fields"] == ["randallExists", "randallX", "randallY"]
    assert payload["stage"]["rows"][0] == [0, 0, [0, 0, -33.248901]]
    assert payload["stage"]["rows"][1] == [1, 1, [[0, 1], [1, 74.422997]]]
