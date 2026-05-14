from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import importlib
import numpy as np
import pytest

from tests.test_modelplay_rerun5_collision_regressions import (
    _input_bytes_from_trace_frame,
    _require_local_data_or_skip,
    _seed_from_trace_frame,
)
from tools.eval.dataset import COMPARE_DTYPE


TRACE_NAME = "reports/modelplay/20260412_rl_doubles_v27_7000_fox_fox_2/trace.json"
FIXTURE = "tests/fixtures/modelplay/fox_fox_sideb_bodyhit_window_1312_1319.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture(path: Path) -> dict[str, Any]:
    fixture = json.loads(path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in fixture["frames"]:
        assert source == TRACE_NAME
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


@pytest.mark.integration
def test_modelplay_fox_fox_sideb_body_hit_launches_victim_instead_of_freezing() -> None:
    # Modelplay symptom lock:
    # - Fox Illusion body hit in the fox_fox_2 trace used to leave the victim effectively frozen
    #   in DamageFly on the floor.
    # - The clean owner split is:
    #   1) Side-B BODY hits persist the Illusion article without generic item hitlag instead of
    #      consuming it (`itFoxIllusion_Logic14_DmgDealt` returns false and clears xCA8),
    #   2) item BODY hits use the same ftCo_8008DCE0 grounded-vs-airborne knockback install as
    #      fighter BODY hits.
    # Horizontal direction is owned by the item-damage facing source, so this symptom lock only
    # asserts that the victim enters airborne hitstun and moves instead of freezing on the floor.
    # refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,checkHitLag}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    _require_local_data_or_skip()
    trace = _load_fixture(_root() / FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=1308, overrides={})
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][1308], input_stride)
        for frame_i in range(1309, 1320):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    hit_frame = history[1313]
    assert int(hit_frame["action_id"][1]) == 90
    assert int(hit_frame["hitlag"][1]) == 5
    assert int(hit_frame["hitstun"][1]) == 46
    assert int(hit_frame["on_ground"][1]) == 0
    assert float(hit_frame["percent"][1]) == pytest.approx(87.509995, abs=1e-3)
    assert float(hit_frame["speed_y_attack"][1]) > 0.0

    post_hitlag = history[1318]
    assert int(post_hitlag["hitlag"][1]) == 0
    assert int(post_hitlag["hitstun"][1]) == 45
    assert int(post_hitlag["on_ground"][1]) == 0
    assert float(post_hitlag["pos_y"][1]) > 1.0
    assert abs(float(post_hitlag["pos_x"][1]) - float(seed["pos_x"][0, 1])) > 0.5
