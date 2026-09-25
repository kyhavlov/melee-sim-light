from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class Character(IntEnum):
    FOX = 1
    MEWTWO = 16
    GAMEWATCH = 24
    FALCON = 2
    DONKEY_KONG = 3
    GANONDORF = 25
    YOSHI = 14
    BOWSER = 5
    SHEIK = 7
    PEACH = 9
    ICE_CLIMBERS = 10
    PIKACHU = 12
    PICHU = 23
    KIRBY = 4
    SAMUS = 13
    NESS = 8
    LINK = 6
    YOUNG_LINK = 20
    JIGGLYPUFF = 15
    LUIGI = 17
    MARIO = 0
    DRMARIO = 21
    MARTH = 18
    ROY = 26
    ZELDA = 19
    FALCO = 22


class Stage(IntEnum):
    FOUNTAIN_OF_DREAMS = 2
    POKEMON_STADIUM = 3
    YOSHIS_STORY = 8
    DREAM_LAND_N64 = 28
    BATTLEFIELD = 31
    FINAL_DESTINATION = 32


class WhispyBlowDirection(IntEnum):
    NONE = 0
    LEFT = 1
    RIGHT = 2


@dataclass(frozen=True, slots=True)
class PlayerConfig:
    character: int | Character
    team_id: int | None = None
    facing: int | None = None
    controller_port: int | None = None
    costume: int = 0
    handicap: int = 9
    start_percent: int = 0


@dataclass(frozen=True, slots=True)
class MatchConfig:
    stage: int | Stage = Stage.FINAL_DESTINATION
    players: tuple[PlayerConfig, ...] | None = None
    seed: int | None = None
    max_frame: int = -1
    stocks: int = 4
    damage_ratio: float = 1.0
    is_teams: bool = False
    friendly_fire: bool = False
    viewpoint_player: int = 0
    ucf_cardinals_1_0_enabled: bool = True
