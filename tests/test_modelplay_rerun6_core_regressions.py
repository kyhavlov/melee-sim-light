from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

STAGE_FD = 32

ACT_FALL = 29
ACT_DAMAGE_AIR_2 = 85
ACT_GUARD_SET_OFF = 181
ACT_OTTOTTO = 245
ACT_OTTOTTO_WAIT = 246
ACT_FX_SPECIAL_LW_HIT = 363

SM_BY_ACTION = {
    14: 2,
    15: 7,
    18: 10,
    20: 12,
    21: 13,
    24: 15,
    25: 16,
    27: 18,
    29: 20,
    38: 29,
    39: 30,
    40: 31,
    42: 35,
    43: 36,
    65: 68,
    67: 70,
    69: 72,
    70: 77,
    74: 81,
    85: 88,
    88: 178,
    90: 180,
    181: 40,
    182: 38,
    245: 290,
    246: 291,
    252: 286,
    253: 287,
    344: 298,
    345: 299,
    360: 314,
    361: 315,
    363: 317,
    364: 318,
    365: 319,
    366: 320,
}

HURTBOX_STATE = {
    "vulnerable": 0,
    "invulnerable": 1,
    "intangible": 2,
}


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture_trace() -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun6_core_windows.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    item_fields = fixture.get("item_fields", [])
    frames: dict[int, dict[str, Any]] = {}
    for frame_raw in fixture["frames"]:
        frame_i = frame_raw[0]
        players_raw = frame_raw[1]
        items_raw = frame_raw[2] if len(frame_raw) > 2 else []
        players = []
        for state_values, input_values in players_raw:
            players.append(
                {
                    "state": dict(zip(state_fields, state_values, strict=True)),
                    "inputs": {"processed": dict(zip(input_fields, input_values, strict=True))},
                }
            )
        items = [dict(zip(item_fields, item_values, strict=True)) for item_values in items_raw]
        frames[int(frame_i)] = {"players": players, "items": items}
    return {"frames": frames}


def _require_local_data_or_skip() -> None:
    root = _root()
    required = [
        root / "data/stages/final_destination.json",
        root / "data/anims/fox.bin",
        root / "data/anims/falco.bin",
        root / "data/anims/fox.tracks.bin",
        root / "data/anims/falco.tracks.bin",
        root / "data/items/lasers.bin",
    ]
    missing = [p.relative_to(root) for p in required if not p.exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {missing}")


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

    for it, item in enumerate(frame.get("items", [])[:15]):
        seed["items"][0, it]["exists"] = np.uint8(1)
        seed["items"][0, it]["type"] = np.uint16(int(item["typeId"]))
        seed["items"][0, it]["state"] = np.uint8(int(item["state"]))
        seed["items"][0, it]["owner"] = np.int8(int(item.get("owner", -1)))
        seed["items"][0, it]["spawn_id"] = np.uint32(int(item.get("spawnId") or 0))
        seed["items"][0, it]["direction"] = np.float32(float(item.get("facingDirection") or 0.0))
        seed["items"][0, it]["pos_x"] = np.float32(float(item["xPosition"]))
        seed["items"][0, it]["pos_y"] = np.float32(float(item["yPosition"]))
        seed["items"][0, it]["vel_x"] = np.float32(float(item["xVelocity"]))
        seed["items"][0, it]["vel_y"] = np.float32(float(item["yVelocity"]))
        seed["items"][0, it]["damage"] = np.uint16(int(item.get("damageTaken") or 0))
        seed["items"][0, it]["timer"] = np.float32(float(item.get("expirationTimer") or 0.0))

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
    trace = _load_fixture_trace()

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
def test_modelplay_rerun6_994_facing_away_from_right_ledge_does_not_teeter() -> None:
    # Rerun6 viewer frame 994: Falco slides to FD's right ledge after LandingFallSpecial while
    # facing left, then enters Ottotto. Teeter should be a facing-toward-edge edge-state, not a
    # facing-away slide outcome.
    #
    # Decomp ownership:
    # - Ottotto entry is edge-flag owned (ftCo_8009A3C8 -> ftCo_8009A410).
    # - Ottotto_Coll chooses the tested floor endpoint from facing_dir before deciding whether to
    #   remain teetering or return to Wait.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_Ottotto_Coll}
    history = _run_trace_window(
        start_frame=995,
        end_frame=1000,
        overrides={
            (0, "ground_id"): np.uint16(2),
        },
    )

    for frame_i in range(996, 1001):
        assert int(history[frame_i]["action_id"][0]) not in (ACT_OTTOTTO, ACT_OTTOTTO_WAIT)


@pytest.mark.integration
def test_modelplay_rerun6_2967_grounded_shine_applies_friction_while_sliding() -> None:
    # Rerun6 viewer frame 2967: after a shine clank, Falco remains in grounded shine hit/loop states
    # and keeps sliding left at a fixed speed for many frames. Grounded Reflector Phys uses the
    # normal ground-friction helper, so the carried wavedash/shield-push velocity must decay.
    #
    # Decomp ownership:
    # - Grounded SpecialLw Start/Loop/Hit/Turn/End Phys call ft_80084F3C.
    # - ft_80084F3C applies ground friction before ground movement.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_Phys,ftFx_SpecialLwLoop_Phys,ftFx_SpecialLwHit_Phys,
    #   ftFx_SpecialLwTurn_Phys,ftFx_SpecialLwEnd_Phys}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    history = _run_trace_window(start_frame=2977, end_frame=2985, overrides={})

    first = history[2977]
    last = history[2985]
    assert int(first["action_id"][0]) == ACT_FX_SPECIAL_LW_HIT
    assert int(last["action_id"][0]) == ACT_FX_SPECIAL_LW_HIT
    assert int(last["on_ground"][0]) == 1
    assert abs(float(last["speed_ground_x_self"][0])) < abs(float(first["speed_ground_x_self"][0]))


@pytest.mark.integration
def test_modelplay_rerun6_3890_guardsetoff_sliding_off_ledge_drops_shield() -> None:
    # Rerun6 viewer frame 3890: Falco is pushed backward in GuardSetOff at FD's left ledge, loses
    # floor ownership, but keeps shielding and hovers at floor height until death.
    #
    # Decomp ownership:
    # - GuardSetOff_Coll uses ft_80084104 / ft_800845B4, both common grounded collision callbacks
    #   that enter Fall when the ground helper reports no floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
    history = _run_trace_window(
        start_frame=3890,
        end_frame=3894,
        overrides={
            (0, "ground_id"): np.uint16(0),
        },
    )

    out = history[3891]
    assert int(out["action_id"][0]) != ACT_GUARD_SET_OFF
    assert int(out["on_ground"][0]) == 0
    for frame_i in range(3891, 3895):
        assert int(history[frame_i]["action_id"][0]) != ACT_GUARD_SET_OFF


@pytest.mark.integration
def test_modelplay_rerun6_4815_damageair_does_not_fall_through_main_floor() -> None:
    # Rerun6 viewer frame 4815: Falco is shined near center stage and continues through FD's main
    # floor to death. This is a DamageFly collision-owner floor-contact regression.
    #
    # Decomp ownership:
    # - DamageFly_Coll calls ft_80081DD4, then ftCo_80090184 chooses Passive/DownBound on floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    history = _run_trace_window(
        start_frame=4815,
        end_frame=4828,
        overrides={
            (0, "ground_id"): np.uint16(1),
        },
    )

    assert int(history[4815]["action_id"][0]) == ACT_DAMAGE_AIR_2
    assert float(history[4816]["pos_y"][0]) < 0.0
    assert int(history[4817]["on_ground"][0]) == 1
    assert float(history[4817]["pos_y"][0]) == pytest.approx(0.0001, abs=0.001)
    for frame_i in range(4817, 4829):
        assert float(history[frame_i]["pos_y"][0]) >= -0.001
