from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class Character(IntEnum):
    FOX = 1
    FALCON = 2
    DONKEY_KONG = 3
    GANONDORF = 25
    SHEIK = 7
    PEACH = 9
    ICE_CLIMBERS = 10
    PIKACHU = 12
    SAMUS = 13
    YOSHI = 14
    JIGGLYPUFF = 15
    LUIGI = 17
    MARIO = 0
    DRMARIO = 21
    MARTH = 18
    ZELDA = 19
    FALCO = 22


class Stage(IntEnum):
    FOUNTAIN_OF_DREAMS = 2
    POKEMON_STADIUM = 3
    YOSHIS_STORY = 8
    DREAM_LAND_N64 = 28
    BATTLEFIELD = 31
    FINAL_DESTINATION = 32


@dataclass(frozen=True, slots=True)
class PlayerConfig:
    character: int | Character
    team_id: int | None = None
    facing: int | None = None
    controller_port: int | None = None
    costume: int = 0
    handicap: int = 9


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
    ucf_cardinals_1_0_enabled: bool = False
