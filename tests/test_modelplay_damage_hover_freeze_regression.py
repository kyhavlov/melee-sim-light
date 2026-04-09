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


def _buttons_mask(buttons: dict[str, bool]) -> int:
    mask = 0
    if buttons["A"]:
        mask |= 0x0100
    if buttons["B"]:
        mask |= 0x0200
    if buttons["X"]:
        mask |= 0x0400
    if buttons["Y"]:
        mask |= 0x0800
    if buttons["Z"]:
        mask |= 0x0010
    if buttons["L"]:
        mask |= 0x0040
    if buttons["R"]:
        mask |= 0x0020
    if buttons["START"]:
        mask |= 0x1000
    return mask


@pytest.mark.integration
def test_modelplay_common_damage_airborne_rows_do_not_hover_into_midair_wait_freeze() -> None:
    # Regression target from the committed rerun3 modelplay windows:
    # repeated hover/freeze windows where an airborne common Damage row could ride the grounded
    # callback ladder, then enter Wait/Walk in midair and static-freeze.
    #
    # Decomp ownership:
    # - ftCo_Damage_Anim / ftCo_Damage_IASA / ftCo_Damage_Phys branch on fp->ground_or_air, not on
    #   whether the current common damage motion id is DamageAir* versus DamageHi/N/Lw*.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_Anim,ftCo_Damage_IASA,ftCo_Damage_Phys
    # }
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    fixture_path = root / "tests/fixtures/modelplay/rerun3_hover_freeze_window.json"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    grounded_locomotion_actions = {
        14,  # Wait
        15,  # WalkSlow
        16,  # WalkMiddle
        17,  # WalkFast
        18,  # Turn
        19,  # TurnRun
        20,  # Dash
        21,  # Run
        22,  # RunDirect
        23,  # RunBrake
    }

    ds = read_dataset(str(dataset_path))
    fixture = json.loads(fixture_path.read_text())
    frames = fixture["frames"]
    windows = fixture["windows"]

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
        compare_history = []
        max_static_run_by_window: dict[str, int] = {}
        airborne_ground_locomotion = []

        for step_i, frame in enumerate(frames):
            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            for p in range(2):
                processed = frame["players"][p]["inputs"]["processed"]
                button_bits = {
                    "A": bool(processed["a"]),
                    "B": bool(processed["b"]),
                    "X": bool(processed["x"]),
                    "Y": bool(processed["y"]),
                    "Z": bool(processed["z"]),
                    "L": bool(processed["lTriggerDigital"]),
                    "R": bool(processed["rTriggerDigital"]),
                    "START": bool(processed["start"]),
                }
                input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(button_bits))
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
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()

            compare_history.append(out.copy())

            for p in range(2):
                action_id = int(out["action_id"][p])
                on_ground = int(out["on_ground"][p])

                if on_ground == 0 and action_id in grounded_locomotion_actions:
                    airborne_ground_locomotion.append(
                        (
                            step_i,
                            p,
                            action_id,
                            int(out["action_frame"][p]),
                            float(out["pos_y"][p]),
                            int(out["hitstun"][p]),
                        )
                    )

            prev_in = inp_bytes

        for window in windows:
            victim_player = int(window["victim_player"])
            lo = int(window["fixture_start"])
            hi = int(window["fixture_end"])
            max_static_run = 0
            static_run = 0
            prev_victim_state = None

            for step_i in range(lo, hi + 1):
                out = compare_history[step_i]
                action_id = int(out["action_id"][victim_player])
                state = (
                    action_id,
                    int(out["action_frame"][victim_player]),
                    int(out["on_ground"][victim_player]),
                    int(out["hitlag"][victim_player]),
                    int(out["hitstun"][victim_player]),
                    float(out["pos_x"][victim_player]),
                    float(out["pos_y"][victim_player]),
                )
                if (
                    prev_victim_state is not None
                    and prev_victim_state == state
                    and state[2] == 0
                    and action_id not in {0, 1, 2, 4, 12, 13}
                ):
                    static_run += 1
                else:
                    static_run = 0
                if static_run > max_static_run:
                    max_static_run = static_run
                prev_victim_state = state

            max_static_run_by_window[window["label"]] = max_static_run

        assert not airborne_ground_locomotion, (
            "modelplay rerun3 fixture regression: grounded locomotion entered while airborne: "
            f"{airborne_ground_locomotion[:8]}"
        )
        assert max_static_run_by_window["hover_freeze_757"] < 8, max_static_run_by_window
        assert max_static_run_by_window["hover_freeze_987"] < 8, max_static_run_by_window
    finally:
        binding.destroy(handle)
