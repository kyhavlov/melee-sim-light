from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from . import dtypes


@dataclass(slots=True)
class Buffers:
    """Preallocated fixed-length simulation buffers.

    ``length`` is the number of step frames held in the chunk. Per-frame
    outputs have ``length`` rows; ``gamestate`` and ``obs`` have ``length + 1``
    rows so they can hold the initial state plus one post-step state per frame.
    """

    length: int
    batch_size: int
    num_players: int
    action_format: str
    match_config: np.ndarray
    action: np.ndarray
    gamestate: np.ndarray
    terminal: np.ndarray
    obs: np.ndarray
    reward: np.ndarray
    done: np.ndarray

    @classmethod
    def empty(
        cls,
        length: int,
        batch_size: int,
        num_players: int = 2,
        *,
        observation: str = "native",
        action_format: str = "controller",
        obs_dim: int = 0,
    ) -> "Buffers":
        length = int(length)
        batch_size = int(batch_size)
        num_players = int(num_players)
        if length <= 0:
            raise ValueError("length must be positive")
        if batch_size <= 0:
            raise ValueError("batch_size must be positive")
        if num_players <= 0 or num_players > dtypes.MAX_PLAYERS:
            raise ValueError(f"num_players must be in 1..{dtypes.MAX_PLAYERS}")
        if observation not in ("native", "flat"):
            raise ValueError("observation must be 'native' or 'flat'")
        if action_format not in ("controller", "raw"):
            raise ValueError("action_format must be 'controller' or 'raw'")
        if obs_dim < 0:
            raise ValueError("obs_dim must be non-negative")

        action_kind = "controller_input" if action_format == "controller" else "input"
        action = dtypes.raw_sequence_buffer(length, batch_size, action_kind)
        if action_format == "controller":
            players = dtypes.view_raw_sequence(action, dtypes.controller_input_dtype())["players"]
            for field in ("main_stick_x", "main_stick_y", "c_stick_x", "c_stick_y"):
                players[field].fill(0.5)
        terminal = dtypes.raw_sequence_buffer(length, batch_size, "terminal")
        return cls(
            length=length,
            batch_size=batch_size,
            num_players=num_players,
            action_format=action_format,
            match_config=dtypes.raw_buffer(batch_size, "match_config"),
            action=action,
            gamestate=dtypes.raw_sequence_buffer(length, batch_size, "gamestate", extra_frames=1),
            terminal=terminal,
            obs=np.zeros((length + 1, batch_size, int(obs_dim)), dtype=np.float32),
            reward=np.zeros((length, batch_size), dtype=np.float32),
            done=dtypes.view_raw_sequence(terminal, dtypes.terminal_dtype())["done"],
        )

    @property
    def match_config_view(self) -> np.ndarray:
        return dtypes.view_raw(self.match_config, dtypes.match_config_dtype())

    @property
    def action_view(self) -> np.ndarray:
        dtype = dtypes.controller_input_dtype() if self.action_format == "controller" else dtypes.input_dtype()
        return dtypes.view_raw_sequence(self.action, dtype)

    @property
    def raw_action_view(self) -> np.ndarray:
        if self.action_format != "raw":
            raise ValueError("raw_action_view is only available for action_format='raw'")
        return dtypes.view_raw_sequence(self.action, dtypes.input_dtype())

    @property
    def controller_action_view(self) -> np.ndarray:
        if self.action_format != "controller":
            raise ValueError("controller_action_view is only available for action_format='controller'")
        return dtypes.view_raw_sequence(self.action, dtypes.controller_input_dtype())

    @property
    def gamestate_view(self) -> np.ndarray:
        return dtypes.view_raw_sequence(self.gamestate, dtypes.gamestate_dtype())

    @property
    def terminal_view(self) -> np.ndarray:
        return dtypes.view_raw_sequence(self.terminal, dtypes.terminal_dtype())
