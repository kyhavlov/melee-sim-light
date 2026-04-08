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


def stop_model_agent(agent: ModelAgent) -> None:
    agent.delayed_agent.stop()


def _map_nt(f, val):
    if isinstance(val, tuple) and hasattr(val, "_fields"):
        return type(val)(*( _map_nt(f, v) for v in val))
    return f(val)


def _batched_namedtuple(game):
    return _map_nt(lambda x: np.expand_dims(x, 0), game)
