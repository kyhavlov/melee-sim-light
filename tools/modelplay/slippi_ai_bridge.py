from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np


@dataclass
class ModelAgent:
    delayed_agent: object
    embed_controller: object

    def step(self, game, *, needs_reset: bool):
        batched_game = _batched_namedtuple(game)
        outputs = self.delayed_agent.step(batched_game, np.array([needs_reset], dtype=np.bool_))
        action = outputs.controller_state
        action = _map_nt(lambda x: x[0], action)
        return self.embed_controller.decode(action)


@dataclass
class FusedBatchModelAgent:
    delayed_agent: object
    embed_controller: object
    batch_size: int

    def step(self, games: list[object], *, needs_reset: np.ndarray) -> list[object]:
        if len(games) != self.batch_size:
            raise ValueError(f"expected {self.batch_size} games, got {len(games)}")
        needs_reset = np.asarray(needs_reset, dtype=np.bool_)
        if needs_reset.shape != (self.batch_size,):
            raise ValueError(f"expected needs_reset shape {(self.batch_size,)}, got {needs_reset.shape}")

        batched_game = _batch_namedtuples(games)
        outputs = self.delayed_agent.step(batched_game, needs_reset)
        action = outputs.controller_state
        return [
            self.embed_controller.decode(_map_nt(lambda x, i=i: x[i], action))
            for i in range(self.batch_size)
        ]


def import_slippi_ai(slippi_ai_root: Path):
    root = str(slippi_ai_root)
    if root not in sys.path:
        sys.path.insert(0, root)
    from slippi_ai import eval_lib  # type: ignore

    eval_lib.disable_gpus()
    return eval_lib


def build_model_agent(
    *,
    slippi_ai_root: Path,
    model_path: Path,
    name: Optional[str] = None,
):
    eval_lib = import_slippi_ai(slippi_ai_root)
    state = eval_lib.load_state(path=str(model_path))
    delayed = eval_lib.build_delayed_agent(
        state=state,
        console_delay=0,
        batch_size=1,
        async_inference=False,
        name=name,
    )
    delayed.start()
    return ModelAgent(delayed_agent=delayed, embed_controller=delayed.embed_controller)


def build_fused_batch_model_agent(
    *,
    slippi_ai_root: Path,
    model_path: Path,
    batch_size: int,
    name: Optional[str] = None,
):
    if batch_size <= 0:
        raise ValueError(f"batch_size must be positive, got {batch_size}")
    eval_lib = import_slippi_ai(slippi_ai_root)
    state = eval_lib.load_state(path=str(model_path))
    delayed = eval_lib.build_delayed_agent(
        state=state,
        console_delay=0,
        batch_size=int(batch_size),
        async_inference=False,
        name=name,
    )
    delayed.start()
    return FusedBatchModelAgent(
        delayed_agent=delayed,
        embed_controller=delayed.embed_controller,
        batch_size=int(batch_size),
    )


def stop_model_agent(agent: ModelAgent) -> None:
    agent.delayed_agent.stop()


def _map_nt(f, val):
    if isinstance(val, tuple) and hasattr(val, "_fields"):
        return type(val)(*( _map_nt(f, v) for v in val))
    return f(val)


def _batched_namedtuple(game):
    return _map_nt(lambda x: np.expand_dims(x, 0), game)


def _batch_namedtuples(values: list[object]):
    if not values:
        raise ValueError("cannot batch empty values")
    from slippi_ai import utils  # type: ignore

    return utils.batch_nest_nt(values)
