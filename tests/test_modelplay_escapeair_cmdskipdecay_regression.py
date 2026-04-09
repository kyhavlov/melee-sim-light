from __future__ import annotations

import importlib
import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset


def _processed_to_stick_u8(v: float) -> np.int8:
    vv = max(-1.0, min(1.0, float(v)))
    return np.int8(int(np.clip(np.rint(((vv + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _buttons_mask(processed: dict[str, bool]) -> int:
    mask = 0
    if processed["a"]:
        mask |= 0x0100
    if processed["b"]:
        mask |= 0x0200
    if processed["x"]:
        mask |= 0x0400
    if processed["y"]:
        mask |= 0x0800
    if processed["z"]:
        mask |= 0x0010
    if processed["lTriggerDigital"]:
        mask |= 0x0040
    if processed["rTriggerDigital"]:
        mask |= 0x0020
    if processed["start"]:
        mask |= 0x1000
    return mask


@pytest.mark.integration
def test_modelplay_escapeair_cmdskipdecay_handoff_prevents_hover_freeze() -> None:
    # Regression target from rerun4 viewer frame ~687:
    # an offstage EscapeAir stayed at a fixed y-position indefinitely, then later re-grabbed ledge.
    #
    # Decomp ownership:
    # - EscapeAir enter clears cmd_vars[0] (`cmd_skip_decay`).
    # - the action script later sets cmd_vars[0].
    # - EscapeAir_Phys decays self_vel only while cmd_vars[0] is clear; once set, it switches to
    #   ft_80084DB0 (common gravity / drift helper).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A9C,ftCo_EscapeAir_Phys
    # }
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    fixture_path = root / "tests/fixtures/modelplay/rerun4_escapeair_cmdskipdecay_window.json"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    frames = fixture["frames"]
    window = fixture["window"]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.samples[0:1]
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        prev_in = prev_input_bytes
        history = []

        for frame in frames:
            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            for p in range(2):
                processed = frame["players"][p]["inputs"]["processed"]
                input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(processed))
                input_t["p"]["main_x"][0, p] = _processed_to_stick_u8(processed["joystickX"])
                input_t["p"]["main_y"][0, p] = _processed_to_stick_u8(processed["joystickY"])
                input_t["p"]["c_x"][0, p] = _processed_to_stick_u8(processed["cStickX"])
                input_t["p"]["c_y"][0, p] = _processed_to_stick_u8(processed["cStickY"])
                input_t["p"]["l"][0, p] = np.uint8(
                    int(round(max(0.0, min(1.0, processed["anyTrigger"])) * 140.0))
                )
                input_t["p"]["r"][0, p] = np.uint8(0)

            inp_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()
            binding.step_input(handle, prev_in, inp_bytes)
            binding.write_compare(handle, out_compare_bytes)
            history.append(out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy())
            prev_in = inp_bytes

        victim = int(window["victim_player"])
        late_escapeair_i = int(window["late_escapeair_fixture_index"])
        descent_end_i = int(window["descent_check_end_fixture_index"])
        late_escapeair = history[late_escapeair_i]
        assert int(late_escapeair["action_id"][victim]) == 236
        assert int(late_escapeair["action_frame"][victim]) == 30
        assert float(late_escapeair["pos_y"][victim]) < float(history[late_escapeair_i - 1]["pos_y"][victim])
        assert float(late_escapeair["speed_y_self"][victim]) < 0.0

        static_run = 0
        max_static_run = 0
        prev_state = None
        for frame_i in range(late_escapeair_i, descent_end_i + 1):
            out = history[frame_i]
            state = (
                int(out["action_id"][victim]),
                int(out["action_frame"][victim]),
                int(out["on_ground"][victim]),
                float(out["pos_x"][victim]),
                float(out["pos_y"][victim]),
            )
            if prev_state == state:
                static_run += 1
            else:
                static_run = 0
            max_static_run = max(max_static_run, static_run)
            prev_state = state

        assert max_static_run == 0, f"late EscapeAir re-froze in rerun4 window: max_static_run={max_static_run}"
        assert float(history[descent_end_i]["pos_y"][victim]) < float(history[late_escapeair_i]["pos_y"][victim])
    finally:
        binding.destroy(handle)
