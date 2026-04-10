from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

STAGE_FD = 32

ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
ACT_DOWN_BOUND_U = 183

SM_BY_ACTION = {
    14: 2,
    20: 12,
    24: 15,
    25: 16,
    27: 18,
    28: 19,
    29: 20,
    32: 23,
    35: 26,
    39: 37,
    42: 35,
    43: 43,
    60: 62,
    67: 69,
    70: 77,
    78: 165,
    80: 170,
    85: 175,
    88: 178,
    183: 183,
    233: 42,
    236: 44,
    259: 224,
    344: 298,
    345: 299,
    360: 314,
    365: 317,
    368: 320,
}

HURTBOX_STATE = {
    "vulnerable": 0,
    "invulnerable": 1,
    "intangible": 2,
}


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


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_rerun5_trace() -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun5_collision_windows.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw in fixture["frames"]:
        players = []
        for state_values, input_values in players_raw:
            players.append(
                {
                    "state": dict(zip(state_fields, state_values, strict=True)),
                    "inputs": {"processed": dict(zip(input_fields, input_values, strict=True))},
                }
            )
        frames[int(frame_i)] = {"players": players}
    return {"frames": frames}


def _require_local_data_or_skip() -> None:
    root = _root()
    required = [
        root / "data/stages/final_destination.json",
        root / "data/anims/fox.bin",
        root / "data/anims/falco.bin",
        root / "data/anims/fox.tracks.bin",
        root / "data/anims/falco.tracks.bin",
    ]
    missing = [p.relative_to(root) for p in required if not p.exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {missing}")


def _seed_from_trace_frame(
    trace: dict[str, Any],
    *,
    start_frame: int,
    overrides: dict[tuple[int, str], Any],
) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["ground_friction_mul"][0, :2] = np.float32(1.0)

    frame = trace["frames"][start_frame]
    prev_frame = trace["frames"][start_frame - 1]
    for p in range(2):
        st = frame["players"][p]["state"]
        prev = prev_frame["players"][p]["state"]
        dx = float(st["xPosition"]) - float(prev["xPosition"])
        dy = float(st["yPosition"]) - float(prev["yPosition"])
        action = int(st["actionStateId"])

        seed["char_id"][0, p] = np.uint8(int(st["internalCharacterId"]))
        seed["facing"][0, p] = np.uint8(0 if float(st["facingDirection"]) < 0.0 else 1)
        seed["facing_dir1"][0, p] = seed["facing"][0, p]
        seed["pos_x"][0, p] = np.float32(float(st["xPosition"]))
        seed["pos_y"][0, p] = np.float32(float(st["yPosition"]))
        seed["speed_air_x_self"][0, p] = np.float32(dx)
        seed["speed_ground_x_self"][0, p] = np.float32(dx)
        seed["speed_y_self"][0, p] = np.float32(dy)
        seed["on_ground"][0, p] = np.uint8(1 if bool(st["isGrounded"]) else 0)
        seed["ground_id"][0, p] = np.uint16(0 if bool(st["isGrounded"]) else 0xFFFF)
        seed["action_id"][0, p] = np.uint16(action)
        seed["action_frame"][0, p] = np.int16(int(st["actionStateFrameCounter"]))
        seed["seed_prev_action_id"][0, p] = np.uint16(int(prev["actionStateId"]))
        seed["seed_prev_action_frame"][0, p] = np.int16(int(prev["actionStateFrameCounter"]))
        seed["animation_index"][0, p] = np.uint32(SM_BY_ACTION.get(action, action))
        seed["anim_frame_f32"][0, p] = np.float32(float(st["actionStateFrameCounter"]))
        seed["percent"][0, p] = np.float32(float(st["percent"]))
        seed["shield_hp"][0, p] = np.float32(float(st.get("shieldSize", 60.0)))
        seed["hitlag"][0, p] = np.uint8(int(st["hitlagRemaining"]))
        seed["hitstun"][0, p] = np.uint8(int(st["hitstunRemaining"]))
        seed["jumps_left"][0, p] = np.uint8(int(st.get("jumpsRemaining", 2)))
        seed["hurtbox_state"][0, p] = np.uint8(HURTBOX_STATE[st["hurtboxCollisionState"]])
        seed["stocks"][0, p] = np.uint8(int(st["stocksRemaining"]))

    for (p, field), value in overrides.items():
        seed[field][0, p] = value
    return seed


def _input_bytes_from_trace_frame(frame: dict[str, Any], input_stride: int) -> np.ndarray:
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    for p in range(2):
        processed = frame["players"][p]["inputs"]["processed"]
        input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(processed))
        input_t["p"]["main_x"][0, p] = _processed_to_stick_i8(processed["joystickX"])
        input_t["p"]["main_y"][0, p] = _processed_to_stick_i8(processed["joystickY"])
        input_t["p"]["c_x"][0, p] = _processed_to_stick_i8(processed["cStickX"])
        input_t["p"]["c_y"][0, p] = _processed_to_stick_i8(processed["cStickY"])
        input_t["p"]["l"][0, p] = np.uint8(
            int(round(max(0.0, min(1.0, float(processed["anyTrigger"]))) * 140.0))
        )
        input_t["p"]["r"][0, p] = np.uint8(0)
    return np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()


def _run_trace_window(
    *,
    start_frame: int,
    end_frame: int,
    overrides: dict[tuple[int, str], Any],
) -> dict[int, np.void]:
    _require_local_data_or_skip()
    trace = _load_rerun5_trace()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=start_frame, overrides=overrides)
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_in = prev_input_bytes
        for frame_i in range(start_frame, end_frame + 1):
            input_bytes = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_in, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_in = input_bytes
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_modelplay_rerun5_1008_fallaerial_escapeair_does_not_pass_through_main_floor() -> None:
    # Rerun5 viewer frame 1008: Falco reaches FallAerial over FD's main floor, presses R into
    # EscapeAir, and passed through the stage. Seed the trace-visible state plus the minimal hidden
    # floor owner; `speed_y_self=0` preserves the local pre-airdodge lane from the viewer window.
    #
    # Decomp ownership:
    # - FallAerial can enter EscapeAir through ftCo_80099A58.
    # - EscapeAir_Coll owns same-pass landing via ft_80082C74.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    history = _run_trace_window(
        start_frame=1008,
        end_frame=1012,
        overrides={
            (0, "ground_id"): np.uint16(1),
            (0, "speed_y_self"): np.float32(0.0),
        },
    )

    out = history[1012]
    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_y"][0]) == pytest.approx(0.0, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun5_4595_escapeair_does_not_project_from_under_ledge_to_stage() -> None:
    # Rerun5 viewer frame 4595: Fox is under the right ledge/floor owner during EscapeAir and
    # snapped from below the stage into grounded LandingFallSpecial on top of FD.
    #
    # Decomp ownership:
    # - EscapeAir_Coll uses collision floor resolution, but ledge-floor suppression is handled by
    #   mpColl edge helpers rather than blindly projecting persisted ledge floor ownership upward.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
    history = _run_trace_window(
        start_frame=4595,
        end_frame=4604,
        overrides={
            (1, "ground_id"): np.uint16(2),
        },
    )

    out = history[4604]
    assert not (
        int(out["action_id"][1]) == ACT_LANDING_FALL_SPECIAL
        and int(out["on_ground"][1]) == 1
        and float(out["pos_y"][1]) >= -0.001
    )
    assert int(out["action_id"][1]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][1]) == 0
    assert float(out["pos_y"][1]) < -20.0


@pytest.mark.integration
def test_modelplay_rerun5_7582_downbound_does_not_fall_through_main_floor() -> None:
    # Rerun5 viewer frame 7582: post-shine damage reaches the main floor and enters DownBound, then
    # lost ground owner in the middle of FD. Seed the trace-visible hitstun state plus trace-derived
    # knockback velocity split; DownBound ground/air changes are collision-owned.
    #
    # Decomp ownership:
    # - DamageFly landing can enter DownBound via ftCo_80097D40.
    # - DownBound_Coll owns allow-ground-to-air decisions via ft_80082708.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_80097D40,ftCo_DownBound_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    trace = _load_rerun5_trace()
    start = 7578
    st = trace["frames"][start]["players"][0]["state"]
    prev = trace["frames"][start - 1]["players"][0]["state"]
    dx = np.float32(float(st["xPosition"]) - float(prev["xPosition"]))
    dy = np.float32(float(st["yPosition"]) - float(prev["yPosition"]))

    history = _run_trace_window(
        start_frame=start,
        end_frame=7599,
        overrides={
            (0, "ground_id"): np.uint16(1),
            (0, "speed_air_x_self"): np.float32(0.0),
            (0, "speed_ground_x_self"): np.float32(0.0),
            (0, "speed_y_self"): np.float32(0.0),
            (0, "speed_x_attack"): dx,
            (0, "speed_y_attack"): dy,
        },
    )

    # The viewer trace symptom continues below FD after DownBound contact. The deterministic sim
    # window may transiently report DownBound airborne ownership, but grounded DownBound Phys must
    # consume stale vertical self velocity and keep the root floor-clamped instead of falling
    # through the stage.
    assert float(trace["frames"][7599]["players"][0]["state"]["yPosition"]) < -20.0
    assert float(history[7594]["speed_y_self"][0]) < 0.0
    assert float(history[7595]["speed_y_self"][0]) == pytest.approx(0.0, abs=0.000001)
    for frame_i in range(7594, 7600):
        out = history[frame_i]
        assert int(out["action_id"][0]) == ACT_DOWN_BOUND_U
        assert float(out["pos_y"][0]) >= -0.001
        assert int(out["ground_id"][0]) == 1
