from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.compare_trace_to_vanilla import _build_patch_spec
from tools.modelplay.sim_env import CHAR_FOX, SIM_INIT_OPENING_FRAME_ID, build_match_config_array


TRACE_PATH = Path(
    "/mnt/nvme0/projects/melee-sim-light_puffer-watch/reports/modelplay/puffer_5b_selfplay_4stock_4min/trace.json"
)


def _load_fixture(path: Path) -> dict[int, dict[str, Any]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if "frames" in payload and "input_fields" in payload:
        input_fields = payload["input_fields"]
        frames: dict[int, dict[str, Any]] = {}
        for frame_i, players_raw, _source in payload["frames"]:
            players = []
            for input_values in players_raw:
                players.append({"inputs": {"processed": dict(zip(input_fields, input_values, strict=True))}})
            frames[int(frame_i)] = {"players": players}
        return frames

    frames: dict[int, dict[str, Any]] = {}
    for frame in payload["frames"]:
        frame_i = int(frame["frameNumber"])
        players = []
        for player in frame["players"]:
            players.append({"inputs": {"processed": dict(player["inputs"]["processed"])}})
        frames[frame_i] = {"players": players}
    return frames


def _processed_to_stick_i8(v: float) -> np.int8:
    vv = max(-1.0, min(1.0, float(v)))
    return np.int8(int(np.clip(np.rint(((vv + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _buttons_mask(processed: dict[str, Any]) -> int:
    mask = 0
    for key, bit in (
        ("a", 0x0100),
        ("b", 0x0200),
        ("x", 0x0400),
        ("y", 0x0800),
        ("z", 0x0010),
        ("lTriggerDigital", 0x0040),
        ("rTriggerDigital", 0x0020),
        ("start", 0x1000),
    ):
        if processed.get(key, False):
            mask |= bit
    return mask


def _input_bytes_from_fixture_frame(frame: dict[str, Any], input_stride: int) -> np.ndarray:
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    for p in range(2):
        processed = frame["players"][p]["inputs"]["processed"]
        input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(processed))
        input_t["p"]["main_x"][0, p] = _processed_to_stick_i8(processed["joystickX"])
        input_t["p"]["main_y"][0, p] = _processed_to_stick_i8(processed["joystickY"])
        input_t["p"]["c_x"][0, p] = _processed_to_stick_i8(processed["cStickX"])
        input_t["p"]["c_y"][0, p] = _processed_to_stick_i8(processed["cStickY"])
        input_t["p"]["l"][0, p] = np.uint8(
            int(round(max(0.0, min(1.0, float(processed["anyTrigger"]))) * 255.0))
        )
        input_t["p"]["r"][0, p] = np.uint8(0)
    return input_t.view(np.uint8).reshape((1, input_stride)).copy()


def test_modelplay_patch_spec_aligns_with_sim_init_compare_frame_id() -> None:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    frames = _load_fixture(TRACE_PATH)
    end_frame = 394
    assert set(range(end_frame + 1)).issubset(frames.keys())

    handle = binding.init(batch_size=1, num_players=2)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        match_config = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FOX, CHAR_FOX),
            facing=(1, 0),
            stocks=4,
            frame_id=SIM_INIT_OPENING_FRAME_ID,
            random_seed=42,
        )
        binding.init_match(handle, match_config.view(np.uint8).reshape((1, -1)))
        prev_input = _input_bytes_from_fixture_frame(frames[0], input_stride)
        for frame_i in range(1, end_frame + 1):
            current_input = _input_bytes_from_fixture_frame(frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, current_input)
            prev_input = current_input

        binding.write_compare(handle, out_compare_bytes)
        row = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    expected_compare_raw_frame = SIM_INIT_OPENING_FRAME_ID + end_frame
    expected_input_raw_frame = expected_compare_raw_frame - 1
    assert int(row["frame_id"]) == expected_compare_raw_frame

    patches = _build_patch_spec(
        frames,
        start_frame=end_frame,
        end_frame=end_frame,
        input_raw_frame_offset=SIM_INIT_OPENING_FRAME_ID - 1,
        carrier_player_map=[0, 1],
    )
    assert len(patches) == 2
    assert {int(p["frame"]) for p in patches} == {expected_input_raw_frame}
