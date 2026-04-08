from __future__ import annotations

import importlib
from dataclasses import dataclass
from typing import Mapping

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


MSL_STAGE_FINAL_DESTINATION = 32
LIBMELEE_STAGE_FINAL_DESTINATION = 25

SLIPPI_VIEWER_SETTINGS_TEMPLATE = {
    "replayFormatVersion": "3.9.0.0",
    "startTimestamp": "2026-04-08T00:00:00Z",
    "isTeams": False,
    "stageId": MSL_STAGE_FINAL_DESTINATION,
    "isPal": False,
    "isFrozenStadium": False,
    "platform": "dolphin",
    "consoleNickname": "melee-sim-light",
    "timerType": "counting down",
    "characterUiPlacesCount": 2,
    "gameType": "stock",
    "friendlyFireOn": False,
    "isBreakTheTargetsOrTitleDemo": False,
    "isClassicOrAdventureMode": False,
    "isHomeRunContestOrEventMatch": False,
    "isSingleButtonMode": False,
    "timerCountsDuringPause": False,
    "bombRain": False,
    "itemSpawnRate": "off",
    "selfDestructScoreValue": -1,
    "damageRatio": 1.0,
}


def _sa_types():
    return importlib.import_module("slippi_ai.types")


def _empty_buttons():
    sa_types = _sa_types()
    return sa_types.Buttons(
        A=np.bool_(False),
        B=np.bool_(False),
        X=np.bool_(False),
        Y=np.bool_(False),
        Z=np.bool_(False),
        L=np.bool_(False),
        R=np.bool_(False),
        D_UP=np.bool_(False),
    )


def _empty_stick():
    sa_types = _sa_types()
    return sa_types.Stick(np.float32(0.5), np.float32(0.5))


def empty_controller():
    sa_types = _sa_types()
    return sa_types.Controller(
        main_stick=_empty_stick(),
        c_stick=_empty_stick(),
        shoulder=np.float32(0.0),
        buttons=_empty_buttons(),
    )


def _empty_nana():
    sa_types = _sa_types()
    return sa_types.Nana(
        exists=np.bool_(False),
        percent=np.uint16(0),
        facing=np.bool_(False),
        x=np.float32(0.0),
        y=np.float32(0.0),
        action=np.uint16(0),
        invulnerable=np.bool_(False),
        character=np.uint8(0),
        jumps_left=np.uint8(0),
        shield_strength=np.float32(0.0),
        on_ground=np.bool_(False),
    )


def _empty_item():
    sa_types = _sa_types()
    return sa_types.Item(
        exists=np.bool_(False),
        type=np.uint16(0),
        state=np.uint8(0),
        x=np.float32(0.0),
        y=np.float32(0.0),
    )


def _facing_to_bool(v: int | np.generic) -> np.bool_:
    return np.bool_(int(v) != 0)


def _hurtbox_state_to_invulnerable(hs: int | np.generic) -> np.bool_:
    return np.bool_(int(hs) != 0)


def _clamp_percent(v: float | np.generic) -> np.uint16:
    return np.uint16(max(0, min(999, int(round(float(v))))))


def _controller_from_input_player(inp: np.void):
    sa_types = _sa_types()
    buttons = int(inp["buttons"])
    main_x = np.float32((int(inp["main_x"]) + 80) / 160)
    main_y = np.float32((int(inp["main_y"]) + 80) / 160)
    c_x = np.float32((int(inp["c_x"]) + 80) / 160)
    c_y = np.float32((int(inp["c_y"]) + 80) / 160)
    shoulder = np.float32(int(inp["l"]) / 140.0)
    return sa_types.Controller(
        main_stick=sa_types.Stick(main_x, main_y),
        c_stick=sa_types.Stick(c_x, c_y),
        shoulder=shoulder,
        buttons=sa_types.Buttons(
            A=np.bool_(bool(buttons & 0x0100)),
            B=np.bool_(bool(buttons & 0x0200)),
            X=np.bool_(bool(buttons & 0x0400)),
            Y=np.bool_(bool(buttons & 0x0800)),
            Z=np.bool_(bool(buttons & 0x0010)),
            L=np.bool_(bool(buttons & 0x0040)),
            R=np.bool_(bool(buttons & 0x0020)),
            D_UP=np.bool_(bool(buttons & 0x0008)),
        ),
    )


def input_array_to_controllers(input_arr: np.ndarray) -> dict[int, object]:
    row = input_arr[0]["p"]
    return {port + 1: _controller_from_input_player(row[port]) for port in range(2)}


def controller_to_input_player(controller) -> np.ndarray:
    out = np.zeros((), dtype=INPUT_DTYPE["p"].base)
    buttons = controller.buttons
    bitmask = 0
    if bool(buttons.A):
        bitmask |= 0x0100
    if bool(buttons.B):
        bitmask |= 0x0200
    if bool(buttons.X):
        bitmask |= 0x0400
    if bool(buttons.Y):
        bitmask |= 0x0800
    if bool(buttons.Z):
        bitmask |= 0x0010
    if bool(buttons.L):
        bitmask |= 0x0040
    if bool(buttons.R):
        bitmask |= 0x0020
    if bool(buttons.D_UP):
        bitmask |= 0x0008
    out["buttons"] = np.uint16(bitmask)
    out["main_x"] = np.int8(np.clip(np.rint(float(controller.main_stick.x) * 160.0 - 80.0), -80, 80))
    out["main_y"] = np.int8(np.clip(np.rint(float(controller.main_stick.y) * 160.0 - 80.0), -80, 80))
    out["c_x"] = np.int8(np.clip(np.rint(float(controller.c_stick.x) * 160.0 - 80.0), -80, 80))
    out["c_y"] = np.int8(np.clip(np.rint(float(controller.c_stick.y) * 160.0 - 80.0), -80, 80))
    analog = np.uint8(np.clip(np.rint(float(controller.shoulder) * 140.0), 0, 140))
    out["l"] = analog
    out["r"] = np.uint8(0)
    return out


def controllers_to_input_array(controllers: Mapping[int, object], *, num_players: int = 2) -> np.ndarray:
    arr = np.zeros(1, dtype=INPUT_DTYPE)
    for port in range(num_players):
        arr["p"][0, port] = controller_to_input_player(controllers.get(port + 1, empty_controller()))
    return arr


@dataclass(frozen=True)
class SimFrameState:
    frame_id: int
    stage_id: int
    num_players: int
    team_id: np.ndarray
    char_id: np.ndarray
    pos_x: np.ndarray
    pos_y: np.ndarray
    facing: np.ndarray
    on_ground: np.ndarray
    is_dead: np.ndarray
    action_id: np.ndarray
    action_frame: np.ndarray
    jumps_left: np.ndarray
    stocks: np.ndarray
    percent: np.ndarray
    shield_hp: np.ndarray
    hitlag: np.ndarray
    hitstun: np.ndarray
    hurtbox_state: np.ndarray
    items: np.ndarray
    frame_pre_random_seed: int


def frame_state_from_seed(seed: np.void) -> SimFrameState:
    num_players = int(seed["num_players"])
    return SimFrameState(
        frame_id=int(seed["frame_id"]),
        stage_id=int(seed["stage_id"]),
        num_players=num_players,
        team_id=np.array(seed["team_id"], copy=True),
        char_id=np.array(seed["char_id"], copy=True),
        pos_x=np.array(seed["pos_x"], copy=True),
        pos_y=np.array(seed["pos_y"], copy=True),
        facing=np.array(seed["facing"], copy=True),
        on_ground=np.array(seed["on_ground"], copy=True),
        is_dead=np.array(seed["stocks"] == 0, dtype=np.uint8),
        action_id=np.array(seed["action_id"], copy=True),
        action_frame=np.array(seed["action_frame"], copy=True),
        jumps_left=np.array(seed["jumps_left"], copy=True),
        stocks=np.array(seed["stocks"], copy=True),
        percent=np.array(seed["percent"], copy=True),
        shield_hp=np.array(seed["shield_hp"], copy=True),
        hitlag=np.array(seed["hitlag"], copy=True),
        hitstun=np.array(seed["hitstun"], copy=True),
        hurtbox_state=np.array(seed["hurtbox_state"], copy=True),
        items=np.array(seed["items"], copy=True),
        frame_pre_random_seed=int(seed["frame_pre_random_seed"]),
    )


def frame_state_from_compare(compare: np.void) -> SimFrameState:
    return SimFrameState(
        frame_id=int(compare["frame_id"]),
        stage_id=int(compare["stage_id"]),
        num_players=int(compare["num_players"]),
        team_id=np.array(compare["team_id"], copy=True),
        char_id=np.array(compare["char_id"], copy=True),
        pos_x=np.array(compare["pos_x"], copy=True),
        pos_y=np.array(compare["pos_y"], copy=True),
        facing=np.array(compare["facing"], copy=True),
        on_ground=np.array(compare["on_ground"], copy=True),
        is_dead=np.array(compare["is_dead"], copy=True),
        action_id=np.array(compare["action_id"], copy=True),
        action_frame=np.array(compare["action_frame"], copy=True),
        jumps_left=np.array(compare["jumps_left"], copy=True),
        stocks=np.array(compare["stocks"], copy=True),
        percent=np.array(compare["percent"], copy=True),
        shield_hp=np.array(compare["shield_hp"], copy=True),
        hitlag=np.array(compare["hitlag"], copy=True),
        hitstun=np.array(compare["hitstun"], copy=True),
        hurtbox_state=np.array(compare["hurtbox_state"], copy=True),
        items=np.array(compare["items"], copy=True),
        frame_pre_random_seed=int(compare["frame_pre_random_seed"]),
    )


def _player_from_state(
    state: SimFrameState,
    idx: int,
    controller,
):
    sa_types = _sa_types()
    return sa_types.Player(
        percent=_clamp_percent(state.percent[idx]),
        facing=_facing_to_bool(state.facing[idx]),
        x=np.float32(state.pos_x[idx]),
        y=np.float32(state.pos_y[idx]),
        action=np.uint16(state.action_id[idx]),
        invulnerable=_hurtbox_state_to_invulnerable(state.hurtbox_state[idx]),
        character=np.uint8(state.char_id[idx]),
        jumps_left=np.uint8(state.jumps_left[idx]),
        shield_strength=np.float32(state.shield_hp[idx]),
        on_ground=np.bool_(bool(state.on_ground[idx])),
        is_dead=np.bool_(bool(state.is_dead[idx])),
        stocks_left=np.uint8(state.stocks[idx]),
        controller=controller,
        nana=_empty_nana(),
    )


def build_slippi_ai_game(state: SimFrameState, controllers: Mapping[int, object], *, viewpoint_port: int):
    sa_types = _sa_types()
    if viewpoint_port not in (1, 2):
        raise ValueError(f"unsupported singles viewpoint_port={viewpoint_port}")
    self_idx = viewpoint_port - 1
    opp_idx = 1 - self_idx
    empty_player = sa_types.Player(
        percent=np.uint16(0),
        facing=np.bool_(False),
        x=np.float32(0.0),
        y=np.float32(0.0),
        action=np.uint16(0),
        invulnerable=np.bool_(False),
        character=np.uint8(0),
        jumps_left=np.uint8(0),
        shield_strength=np.float32(0.0),
        on_ground=np.bool_(False),
        is_dead=np.bool_(True),
        stocks_left=np.uint8(0),
        controller=empty_controller(),
        nana=_empty_nana(),
    )
    p0 = _player_from_state(state, self_idx, controllers.get(viewpoint_port, empty_controller()))
    p2 = _player_from_state(state, opp_idx, controllers.get(opp_idx + 1, empty_controller()))
    items = {
        f"item_{i}": sa_types.Item(
            exists=np.bool_(bool(item["exists"])),
            type=np.uint16(item["type"]),
            state=np.uint8(item["state"]),
            x=np.float32(item["pos_x"]),
            y=np.float32(item["pos_y"]),
        )
        for i, item in enumerate(state.items)
    }
    stage = LIBMELEE_STAGE_FINAL_DESTINATION if state.stage_id == MSL_STAGE_FINAL_DESTINATION else 0
    return sa_types.Game(
        p0=p0,
        p1=empty_player,
        p2=p2,
        p3=empty_player,
        stage=np.uint8(stage),
        randall_phase=np.float32(state.frame_id % 1200),
        randall=sa_types.Randall(x=np.float32(0.0), y=np.float32(0.0)),
        items=sa_types.Items(**items),
        is_teams=np.bool_(False),
    )


def _viewer_external_char_id(internal_char_id: int) -> int:
    if internal_char_id == 1:
        return 2
    if internal_char_id == 22:
        return 20
    return int(internal_char_id)


def viewer_settings_from_state(state: SimFrameState, *, start_stocks: int = 4, timer_start: int = 480) -> dict:
    settings = dict(SLIPPI_VIEWER_SETTINGS_TEMPLATE)
    settings["timerStart"] = int(timer_start)
    settings["playerSettings"] = []
    for idx in range(state.num_players):
        settings["playerSettings"].append(
            {
                "playerIndex": idx,
                "port": idx + 1,
                "externalCharacterId": _viewer_external_char_id(int(state.char_id[idx])),
                "internalCharacterIds": [int(state.char_id[idx])],
                "playerType": 0,
                "startStocks": int(start_stocks),
                "costumeIndex": 0,
                "teamShade": 0,
                "handicap": 9,
                "teamId": int(state.team_id[idx]),
                "staminaMode": False,
                "silentCharacter": False,
                "lowGravity": False,
                "invisible": False,
                "blackStockIcon": False,
                "metal": False,
                "startGameOnWarpPlatform": False,
                "rumbleEnabled": False,
                "cpuLevel": 0,
                "offenseRatio": 1.0,
                "defenseRatio": 1.0,
                "modelScale": 1.0,
                "controllerFix": "UCF",
                "nametag": "",
                "displayName": f"P{idx+1}",
                "connectCode": "",
            }
        )
    return settings


def viewer_frame_from_state(state: SimFrameState, controllers: Mapping[int, object]) -> dict:
    players = []
    for idx in range(state.num_players):
        controller = controllers.get(idx + 1, empty_controller())
        processed = {
            "a": bool(controller.buttons.A),
            "b": bool(controller.buttons.B),
            "x": bool(controller.buttons.X),
            "y": bool(controller.buttons.Y),
            "z": bool(controller.buttons.Z),
            "start": False,
            "dPadLeft": False,
            "dPadRight": False,
            "dPadDown": False,
            "dPadUp": bool(controller.buttons.D_UP),
            "rTriggerDigital": bool(controller.buttons.R),
            "lTriggerDigital": bool(controller.buttons.L),
            "joystickX": float(np.float32(controller.main_stick.x * 2.0 - 1.0)),
            "joystickY": float(np.float32(controller.main_stick.y * 2.0 - 1.0)),
            "cStickX": float(np.float32(controller.c_stick.x * 2.0 - 1.0)),
            "cStickY": float(np.float32(controller.c_stick.y * 2.0 - 1.0)),
            "anyTrigger": float(np.float32(controller.shoulder)),
        }
        inputs = {
            "frameNumber": int(state.frame_id),
            "playerIndex": idx,
            "isNana": False,
            "physical": {
                "a": bool(controller.buttons.A),
                "b": bool(controller.buttons.B),
                "x": bool(controller.buttons.X),
                "y": bool(controller.buttons.Y),
                "z": bool(controller.buttons.Z),
                "start": False,
                "dPadLeft": False,
                "dPadRight": False,
                "dPadDown": False,
                "dPadUp": bool(controller.buttons.D_UP),
                "rTriggerAnalog": float(np.float32(0.0)),
                "rTriggerDigital": bool(controller.buttons.R),
                "lTriggerAnalog": float(np.float32(controller.shoulder)),
                "lTriggerDigital": bool(controller.buttons.L),
            },
            "processed": processed,
        }
        player_state = {
            "frameNumber": int(state.frame_id),
            "playerIndex": idx,
            "isNana": False,
            "internalCharacterId": int(state.char_id[idx]),
            "actionStateId": int(state.action_id[idx]),
            "xPosition": float(np.float32(state.pos_x[idx])),
            "yPosition": float(np.float32(state.pos_y[idx])),
            "facingDirection": -1.0 if int(state.facing[idx]) == 0 else 1.0,
            "percent": float(np.float32(state.percent[idx])),
            "shieldSize": float(np.float32(state.shield_hp[idx])),
            "lastHittingAttackId": 0,
            "currentComboCount": 0,
            "lastHitBy": 0,
            "stocksRemaining": int(state.stocks[idx]),
            "actionStateFrameCounter": float(np.float32(state.action_frame[idx])),
            "hitstunRemaining": int(state.hitstun[idx]),
            "isGrounded": bool(state.on_ground[idx]),
            "lastGroundId": 0,
            "jumpsRemaining": int(state.jumps_left[idx]),
            "lCancelStatus": None,
            "hurtboxCollisionState": ["vulnerable", "invulnerable", "intangible"][int(state.hurtbox_state[idx])],
            "selfInducedAirXSpeed": 0.0,
            "selfInducedAirYSpeed": 0.0,
            "attackBasedXSpeed": 0.0,
            "attackBasedYSpeed": 0.0,
            "selfInducedGroundXSpeed": 0.0,
            "hitlagRemaining": int(state.hitlag[idx]),
            "isReflectActive": False,
            "isFastfalling": False,
            "isShieldActive": bool(state.shield_hp[idx] > 0.0),
            "isInHitstun": bool(state.hitstun[idx] > 0),
            "isHittingShield": False,
            "isPowershieldActive": False,
            "isDead": bool(state.is_dead[idx]),
            "isOffscreen": False,
        }
        players.append(
            {
                "frameNumber": int(state.frame_id),
                "playerIndex": idx,
                "inputs": inputs,
                "state": player_state,
            }
        )
    items = []
    for item in state.items:
        if not bool(item["exists"]):
            continue
        items.append(
            {
                "frameNumber": int(state.frame_id),
                "typeId": int(item["type"]),
                "state": int(item["state"]),
                "facingDirection": float(np.float32(item["direction"])),
                "xVelocity": float(np.float32(item["vel_x"])),
                "yVelocity": float(np.float32(item["vel_y"])),
                "xPosition": float(np.float32(item["pos_x"])),
                "yPosition": float(np.float32(item["pos_y"])),
                "damageTaken": int(item["damage"]),
                "expirationTimer": float(np.float32(item["timer"])),
                "spawnId": int(item["spawn_id"]),
                "samusMissileType": int(item["misc0"]),
                "peachTurnipFace": int(item["misc1"]),
                "isChargeShotLaunched": False,
                "chargeShotChargeLevel": int(item["misc2"]),
                "owner": int(item["owner"]),
            }
        )
    return {
        "frameNumber": int(state.frame_id),
        "randomSeed": int(state.frame_pre_random_seed),
        "players": players,
        "items": items,
        "stage": {
            "frameNumber": int(state.frame_id),
            "fodLeftPlatformHeight": 0.0,
            "fodRightPlatformHeight": 0.0,
        },
    }
