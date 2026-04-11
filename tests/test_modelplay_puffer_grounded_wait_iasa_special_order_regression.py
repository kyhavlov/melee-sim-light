from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import CHAR_FOX, SIM_INIT_OPENING_FRAME_ID, build_match_config_array

ACT_ATTACK_HI4 = 0x003F
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_HI_HOLD = 0x0161
ACT_FX_SPECIAL_LW_START = 0x0168

FIXTURE = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_168.json"
FIXTURE_SOURCE = "reports/modelplay/puffer_5b_selfplay_4stock_4min/trace.json"


def _load_fixture() -> dict[int, dict[str, Any]]:
    payload = json.loads(FIXTURE.read_text(encoding="utf-8"))
    input_fields = payload["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in payload["frames"]:
        assert source == FIXTURE_SOURCE
        players = []
        for input_values in players_raw:
            players.append({"inputs": {"processed": dict(zip(input_fields, input_values, strict=True))}})
        frames[int(frame_i)] = {"players": players}
    return frames


def _processed_to_stick_i8(v: float) -> np.int8:
    vv = max(-1.0, min(1.0, float(v)))
    return np.int8(int(np.clip(np.rint(((vv + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _buttons_mask(processed: dict[str, Any]) -> int:
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


def _input_bytes_from_fixture_frame(
    frame: dict[str, Any],
    input_stride: int,
    *,
    override_p1: dict[str, Any] | None = None,
) -> np.ndarray:
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    for p in range(2):
        processed = dict(frame["players"][p]["inputs"]["processed"])
        if p == 1 and override_p1 is not None:
            processed.update(override_p1)
        input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(processed))
        input_t["p"]["main_x"][0, p] = _processed_to_stick_i8(processed["joystickX"])
        input_t["p"]["main_y"][0, p] = _processed_to_stick_i8(processed["joystickY"])
        input_t["p"]["c_x"][0, p] = _processed_to_stick_i8(processed["cStickX"])
        input_t["p"]["c_y"][0, p] = _processed_to_stick_i8(processed["cStickY"])
        input_t["p"]["l"][0, p] = np.uint8(int(round(max(0.0, min(1.0, float(processed["anyTrigger"]))) * 140.0)))
        input_t["p"]["r"][0, p] = np.uint8(0)
    return input_t.view(np.uint8).reshape((1, input_stride)).copy()


def _run_prefix(*, override_p1_frame_168: dict[str, Any] | None = None) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    frames = _load_fixture()
    assert set(range(169)).issubset(frames.keys())

    handle = binding.init(batch_size=1, num_players=2)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    rows: dict[int, np.void] = {}
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
        binding.write_compare(handle, out_compare_bytes)
        rows[0] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()

        prev_input = _input_bytes_from_fixture_frame(frames[0], input_stride)
        for frame_i in range(1, 169):
            current_input = _input_bytes_from_fixture_frame(
                frames[frame_i],
                input_stride,
                override_p1=override_p1_frame_168 if frame_i == 168 else None,
            )
            binding.step_input(handle, prev_input, current_input)
            binding.write_compare(handle, out_compare_bytes)
            rows[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = current_input
    finally:
        binding.destroy(handle)
    return rows


def test_puffer_grounded_wait_iasa_b_up_enters_specialhi_before_attack() -> None:
    # Regression target from the puffer Fox/Fox comparator:
    # - frame 167 p1 is still grounded AttackHi4,
    # - frame 168 has fresh B + up-stick on a grounded Wait_IASA-delegating row,
    # - vanilla enters Fox SpecialHiHold immediately.
    #
    # Decomp ordering:
    # - Wait_IASA checks SpecialS -> SpecialHi -> SpecialN -> SpecialLw before grounded attacks.
    # - Grounded Attack* IASA callbacks delegate into Wait_IASA when allow_interrupt is set.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHi_Enter
    rows = _run_prefix()
    assert int(rows[167]["action_id"][1]) == ACT_ATTACK_HI4
    assert int(rows[168]["action_id"][1]) == ACT_FX_SPECIAL_HI_HOLD


def test_puffer_grounded_wait_iasa_neutralb_stays_before_reflector() -> None:
    # Control on the same init-match prefix:
    # - grounded B with neutral stick should enter Neutral-B first,
    # - grounded B with down-stick should still fall through to Reflector after Neutral-B.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_800D6824,ftCo_800D68C0
    # }
    neutral_rows = _run_prefix(
        override_p1_frame_168={
            "a": False,
            "b": True,
            "x": False,
            "y": False,
            "z": False,
            "lTriggerDigital": False,
            "rTriggerDigital": False,
            "start": False,
            "joystickX": 0.0,
            "joystickY": 0.0,
            "cStickX": 0.0,
            "cStickY": 0.0,
            "anyTrigger": 0.0,
        }
    )
    down_rows = _run_prefix(
        override_p1_frame_168={
            "a": False,
            "b": True,
            "x": False,
            "y": False,
            "z": False,
            "lTriggerDigital": False,
            "rTriggerDigital": False,
            "start": False,
            "joystickX": 0.0,
            "joystickY": -1.0,
            "cStickX": 0.0,
            "cStickY": 0.0,
            "anyTrigger": 0.0,
        }
    )
    assert int(neutral_rows[168]["action_id"][1]) == ACT_FX_SPECIAL_N_START
    assert int(down_rows[168]["action_id"][1]) == ACT_FX_SPECIAL_LW_START
