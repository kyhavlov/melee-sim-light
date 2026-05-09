from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

FIXTURE = "tests/fixtures/modelplay/20260507_platform_throw_air_windows.json"

ACT_ATTACK_AIR_N = 65
ACT_LANDING_AIR_N = 70
ACT_THROW_F = 219

SM_BY_ACTION = {
    25: 16,  # JumpF
    65: 68,  # AttackAirN
    85: 175,  # DamageHi1
    86: 176,  # DamageAir3
    216: 244,  # CatchWait
    219: 247,  # ThrowF
    227: 255,  # CaptureWaitLw
    239: 262,  # ThrownF
}

HURTBOX_STATE = {
    "vulnerable": 0,
    "invulnerable": 1,
    "intangible": 2,
}


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _require_local_data_or_skip() -> None:
    root = _root()
    required = [
        root / "data/stages/battlefield.json",
        root / "data/stages/yoshis_story.json",
        root / "data/anims/fox.bin",
        root / "data/anims/falco.bin",
        root / "data/anims/fox.tracks.bin",
        root / "data/anims/falco.tracks.bin",
    ]
    missing = [p.relative_to(root) for p in required if not p.exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {missing}")


def _load_fixture_case(name: str) -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    for raw_case in fixture["cases"]:
        if raw_case["name"] != name:
            continue
        frames: dict[int, dict[str, Any]] = {}
        for frame_i, players_raw in raw_case["frames"]:
            players = []
            for state_values, input_values in players_raw:
                players.append(
                    {
                        "state": dict(zip(state_fields, state_values, strict=True)),
                        "inputs": {"processed": dict(zip(input_fields, input_values, strict=True))},
                    }
                )
            frames[int(frame_i)] = {"players": players}
        return {**raw_case, "frames": frames}
    raise AssertionError(f"missing fixture case {name}")


def _stick_i8(value: Any) -> np.int8:
    v = 0.0 if value is None else max(-1.0, min(1.0, float(value)))
    return np.int8(int(np.clip(np.rint(((v + 1.0) * 0.5) * 160.0 - 80.0), -80, 80)))


def _trigger_u8(value: Any) -> np.uint8:
    v = 0.0 if value is None else max(0.0, min(1.0, float(value)))
    return np.uint8(int(round(v * 140.0)))


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


def _input_bytes(frame: dict[str, Any], input_stride: int) -> np.ndarray:
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    for p in range(2):
        processed = frame["players"][p]["inputs"]["processed"]
        input_t["p"]["buttons"][0, p] = np.uint16(_buttons_mask(processed))
        input_t["p"]["main_x"][0, p] = _stick_i8(processed["mainStickX"])
        input_t["p"]["main_y"][0, p] = _stick_i8(processed["mainStickY"])
        input_t["p"]["c_x"][0, p] = _stick_i8(processed["cStickX"])
        input_t["p"]["c_y"][0, p] = _stick_i8(processed["cStickY"])
        input_t["p"]["l"][0, p] = _trigger_u8(processed["lTriggerAnalog"])
        input_t["p"]["r"][0, p] = _trigger_u8(processed["rTriggerAnalog"])
    return np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()


def _seed_from_case(case: dict[str, Any]) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(case["stage_id"])
    seed["num_players"][0] = np.uint8(2)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["ground_friction_mul"][0, :2] = np.float32(1.0)

    frame = case["frames"][case["start_frame"]]
    prev_frame = case["frames"][case["start_frame"] - 1]
    for p in range(2):
        st = frame["players"][p]["state"]
        prev = prev_frame["players"][p]["state"]
        action = int(st["actionStateId"])
        dx = float(st["xPosition"]) - float(prev["xPosition"])
        dy = float(st["yPosition"]) - float(prev["yPosition"])

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
        seed["shield_hp"][0, p] = np.float32(float(st["shieldSize"]))
        seed["hitlag"][0, p] = np.uint8(int(st["hitlagRemaining"]))
        seed["hitstun"][0, p] = np.uint8(int(st["hitstunRemaining"]))
        seed["jumps_left"][0, p] = np.uint8(int(st["jumpsRemaining"]))
        seed["hurtbox_state"][0, p] = np.uint8(HURTBOX_STATE[st["hurtboxCollisionState"]])
        seed["stocks"][0, p] = np.uint8(int(st["stocksRemaining"]))

    if case["name"] == "ys_throwf_edge_snap":
        seed["ground_id"][0, 0] = np.uint16(1)
        seed["ground_id"][0, 1] = np.uint16(0xFFFF)
    return seed


def _run_case(name: str) -> dict[int, np.void]:
    _require_local_data_or_skip()
    case = _load_fixture_case(name)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_case(case)
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes(case["frames"][case["start_frame"]], input_stride)
        for frame_i in range(case["start_frame"] + 1, case["end_frame"] + 1):
            input_t = _input_bytes(case["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_bytes)
            history[frame_i] = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)
    return history


@pytest.mark.integration
def test_battlefield_attackair_hitlag_floor_contact_enters_landingair() -> None:
    # Old modelplay frame 229: Fox fullhop nair hits Falco while contacting Battlefield's platform.
    # Source AttackAir_Coll still dispatches ft_80082C74 on the hitlag-start frame, so the fighter
    # must enter LandingAirN instead of remaining as grounded AttackAirN.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    history = _run_case("bf_attackair_hitlag_landing")

    out = history[229]
    assert int(out["action_id"][0]) == ACT_LANDING_AIR_N
    assert int(out["action_id"][0]) != ACT_ATTACK_AIR_N
    assert int(out["on_ground"][0]) == 1


@pytest.mark.integration
def test_yoshi_grounded_throwf_uses_edge_snap_floor_owner() -> None:
    # Old modelplay frames 8942-8958: Falco ThrowF walked past Yoshi's platform end, stayed at the
    # platform height, but lost ground ownership and hovered/slid in ThrowF. Grounded Throw*_Coll
    # routes through ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC, whose mpColl_8004A45C_Floor
    # endpoint snap keeps the throw rooted to the floor edge while the action remains grounded.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_800827A0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    history = _run_case("ys_throwf_edge_snap")

    out = history[8958]
    assert int(out["action_id"][0]) == ACT_THROW_F
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(-59.5, abs=0.25)
    assert float(out["pos_y"][0]) == pytest.approx(23.4501, abs=1e-4)
