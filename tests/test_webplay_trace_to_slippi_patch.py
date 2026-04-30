from __future__ import annotations

import pytest

from tools.modelplay.webplay_trace_to_slippi_patch import (
    WEBPLAY_INPUT_FIELDS,
    WEBPLAY_TRACE_FORMAT,
    build_patch_spec,
)


def _trace() -> dict:
    return {
        "format": WEBPLAY_TRACE_FORMAT,
        "name": "rapid_jab_probe",
        "frameCount": 3,
        "inputFields": WEBPLAY_INPUT_FIELDS,
        "p1Inputs": [
            [0, 0, 0, 0, 0, 0, 0],
            [0x0100, 0.5, -0.25, 0, 0, 0, 0],
            [0x0100, 0.5, -0.25, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0, 0],
        ],
        "p2Inputs": [
            [0, 0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 0, 0, 0],
        ],
    }


def test_webplay_trace_patch_spec_compresses_repeated_inputs() -> None:
    spec = build_patch_spec(
        _trace(),
        start_frame=0,
        end_frame=3,
        input_raw_frame_offset=-123,
        carrier_player_map=[1, 0],
        compress=True,
    )

    assert spec["input_raw_frame_offset"] == -123
    assert spec["carrier_player_map"] == [1, 0]
    patches = spec["patches"]
    assert patches[0]["start_frame"] == -123
    assert patches[0]["end_frame"] == -123
    assert patches[0]["player"] == 1
    assert patches[1]["start_frame"] == -122
    assert patches[1]["end_frame"] == -121
    assert patches[1]["input"]["a"] is True
    assert patches[1]["input"]["joystickX"] == pytest.approx(0.5)
    assert patches[1]["input"]["joystickY"] == pytest.approx(-0.25)
    assert patches[-1]["player"] == 0


def test_webplay_trace_patch_spec_can_emit_one_patch_per_frame() -> None:
    spec = build_patch_spec(
        _trace(),
        start_frame=1,
        end_frame=2,
        input_raw_frame_offset=-123,
        carrier_player_map=[0, 1],
        compress=False,
    )

    patches = spec["patches"]
    assert len(patches) == 4
    assert patches[0]["frame"] == -122
    assert patches[1]["frame"] == -121
    assert patches[2]["frame"] == -122
    assert patches[3]["frame"] == -121
