from __future__ import annotations

from melee_sim import _native
from melee_sim._native import *  # noqa: F401,F403

write_rl_observation = _native.write_gamestate


def sizes() -> dict[str, int]:
    out = dict(_native.sizes())
    out.setdefault("rl_observation", out["gamestate"])
    return out
