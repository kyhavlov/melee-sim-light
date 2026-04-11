from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import CHAR_FOX, SIM_INIT_OPENING_FRAME_ID, build_match_config_array

ACT_ATTACK_HI4 = 0x003F
ACT_ATTACK_LW4 = 0x0040
ACT_CATCH = 0x00D4
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_HI_HOLD = 0x0161
ACT_FX_SPECIAL_HI = 0x0163
ACT_FX_SPECIAL_LW_START = 0x0168

FIXTURE_168 = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_168.json"
FIXTURE_207 = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_207.json"
FIXTURE_210 = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_210.json"
FIXTURE_240 = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_240.json"
FIXTURE_260 = Path(__file__).resolve().parents[1] / "tests/fixtures/modelplay/puffer_5b_selfplay_input_prefix_0_260.json"
FIXTURE_SOURCE = "reports/modelplay/puffer_5b_selfplay_4stock_4min/trace.json"


def _load_fixture(path: Path) -> dict[int, dict[str, Any]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
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


def _run_prefix(
    fixture_path: Path,
    *,
    end_frame: int,
    override_frame: int | None = None,
    override_player: int = 1,
    override_processed: dict[str, Any] | None = None,
) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    frames = _load_fixture(fixture_path)
    assert set(range(end_frame + 1)).issubset(frames.keys())

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
        for frame_i in range(1, end_frame + 1):
            current_input = _input_bytes_from_fixture_frame(
                frames[frame_i],
                input_stride,
                override_p1=(
                    override_processed
                    if override_player == 1 and override_frame is not None and frame_i == override_frame
                    else None
                ),
            )
            if override_player == 0 and override_frame is not None and frame_i == override_frame:
                current_input_arr = current_input.view(INPUT_DTYPE).reshape((1,))
                processed = dict(frames[frame_i]["players"][0]["inputs"]["processed"])
                processed.update(override_processed or {})
                current_input_arr["p"]["buttons"][0, 0] = np.uint16(_buttons_mask(processed))
                current_input_arr["p"]["main_x"][0, 0] = _processed_to_stick_i8(processed["joystickX"])
                current_input_arr["p"]["main_y"][0, 0] = _processed_to_stick_i8(processed["joystickY"])
                current_input_arr["p"]["c_x"][0, 0] = _processed_to_stick_i8(processed["cStickX"])
                current_input_arr["p"]["c_y"][0, 0] = _processed_to_stick_i8(processed["cStickY"])
                current_input_arr["p"]["l"][0, 0] = np.uint8(
                    int(round(max(0.0, min(1.0, float(processed["anyTrigger"]))) * 140.0))
                )
                current_input_arr["p"]["r"][0, 0] = np.uint8(0)
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
    rows = _run_prefix(FIXTURE_168, end_frame=168)
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
        FIXTURE_168,
        end_frame=168,
        override_frame=168,
        override_player=1,
        override_processed={
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
        FIXTURE_168,
        end_frame=168,
        override_frame=168,
        override_player=1,
        override_processed={
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


def test_puffer_grounded_wait_iasa_catch_beats_attacklw4() -> None:
    # Regression target from the next puffer comparator owner:
    # - frame 207 p0 has fresh A plus held trigger on a grounded Wait_IASA-delegating row,
    # - the same row also has strong down input that would qualify for AttackLw4,
    # - decomp checks Catch before AttackLw4, so vanilla enters Catch.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_CheckInput
    rows = _run_prefix(FIXTURE_207, end_frame=207)
    assert int(rows[206]["action_id"][0]) == ACT_ATTACK_HI4
    assert int(rows[207]["action_id"][0]) == ACT_CATCH


def test_puffer_grounded_wait_iasa_attacklw4_when_trigger_removed() -> None:
    # Control on the same prefix and frame:
    # - clear the trigger so Catch_CheckInput no longer owns the row,
    # - the same fresh A + down input should then fall through to AttackLw4.
    rows = _run_prefix(
        FIXTURE_207,
        end_frame=207,
        override_frame=207,
        override_player=0,
        override_processed={
            "a": True,
            "b": False,
            "x": False,
            "y": False,
            "z": False,
            "lTriggerDigital": False,
            "rTriggerDigital": False,
            "start": False,
            "joystickX": -0.10000002384185791,
            "joystickY": -1.0,
            "cStickX": 0.0,
            "cStickY": -1.0,
            "anyTrigger": 0.0,
        },
    )
    assert int(rows[207]["action_id"][0]) == ACT_ATTACK_LW4


def test_puffer_grounded_specialhi_launch_seeds_ground_momentum() -> None:
    # Regression target from the next puffer comparator owner:
    # - grounded SpecialHiHold anim-end transitions through ftFx_SpecialAirHi_AirToGround
    # - that launch enter seeds `fp->gr_vel = x74 * fp->facing_dir` on the same frame
    # - vanilla therefore moves immediately on the first grounded SpecialHi row
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHold_Anim,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_AirToGround
    # }
    rows = _run_prefix(FIXTURE_210, end_frame=210)
    assert int(rows[209]["action_id"][1]) == ACT_FX_SPECIAL_HI_HOLD
    assert int(rows[210]["action_id"][1]) == ACT_FX_SPECIAL_HI
    assert float(rows[210]["speed_ground_x_self"][1]) == pytest.approx(-3.8, abs=1e-5)
    assert float(rows[210]["pos_x"][1] - rows[209]["pos_x"][1]) == pytest.approx(-3.8, abs=1e-5)


def test_puffer_grounded_specialhi_landing_zeroes_ground_momentum() -> None:
    # Regression target from the next puffer comparator owner:
    # - grounded SpecialHi launch decays to small residual ground momentum before landing
    # - ftFx_SpecialHiLanding_Phys applies x7C friction immediately on landing entry
    # - when |gr_vel| < x7C, the first grounded SpecialHiLanding row should move 0 and report 0 gr_vel
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHi_Anim,ftFx_SpecialHiLanding_Phys,ftFx_SpecialHiFall_AirToGround
    # }
    rows = _run_prefix(FIXTURE_240, end_frame=240)
    assert int(rows[239]["action_id"][1]) == ACT_FX_SPECIAL_HI
    assert float(rows[239]["speed_ground_x_self"][1]) == pytest.approx(-1.3000015, abs=1e-5)
    assert int(rows[240]["action_id"][1]) == 0x0165  # SpecialHiLanding
    assert float(rows[240]["speed_ground_x_self"][1]) == pytest.approx(0.0, abs=1e-5)
    assert float(rows[240]["pos_x"][1] - rows[239]["pos_x"][1]) == pytest.approx(0.0, abs=1e-5)


def test_puffer_specialhi_landing_anim_end_wait_handoff_keeps_wait_iasa_catch() -> None:
    # Regression target from the next puffer comparator owner:
    # - SpecialHiLanding anim-end enters Wait on the same grounded frame
    # - Fighter proc order then runs destination Wait_IASA in that same frame
    # - fresh grab input must still claim Catch before the row falls through to plain Wait
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    rows = _run_prefix(FIXTURE_260, end_frame=260)
    assert int(rows[259]["action_id"][1]) == 0x0165  # SpecialHiLanding
    assert int(rows[260]["action_id"][1]) == ACT_CATCH
