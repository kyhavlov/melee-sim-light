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

RERUN13 = "reports/modelplay/20260412_rl_doubles_v27_7000_rerun13/trace.json"
RERUN13_ILLUSION_FIXTURE = "tests/fixtures/modelplay/rerun13_illusion_window_713_716.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture(fixture_path: Path) -> dict[str, Any]:
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in fixture["frames"]:
        assert source == RERUN13
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
def test_modelplay_rerun13_illusion_uses_live_owner_history_for_first_active_position() -> None:
    # Modelplay symptom lock:
    # - Falco air side-B crosses directly through grounded Fox around rerun13 frame 716.
    # - Decomp item Phys does not keep the Illusion article at its spawn point; state0/state1 copy
    #   item->pos from ftFx_SpecialS_CopyGhostPosIndexed(index=1), which is advanced by
    #   ftFox_SpecialS_SetPhys from the owner's prior world positions during the main/end Phys
    #   callbacks.
    # refs/melee/src/melee/it/items/itfoxillusion.c::{
    #   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys
    # }
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialS_CopyGhostPosIndexed,ftFox_SpecialS_SetPhys,
    #   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys
    # }
    _require_local_data_or_skip()
    trace = _load_fixture(_root() / RERUN13_ILLUSION_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=714, overrides={})
    # Fresh aerial Illusion/Phantasm item Phys copies the owner's hidden ghostEffectPos[1] ring
    # rather than the visible current position. On the first active dash frame the live ring is
    # still initialized from the entry frame (`ghost0 = ghost1 = cur_pos`).
    #
    # This modelplay fixture only seeds a short local trace slice, so it is suitable for locking
    # the first active-frame article position but not the full multi-frame Side-B hit flow. That
    # end-to-end flow is covered by replay-real unreseeded rollout locks in
    # tests/test_illusion_full_flow_rollout_regression.py.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetVars
    # refs/melee/src/melee/it/items/itfoxillusion.c::itFoxillusion_UnkMotion1_Phys
    seed["illusion_ghost_pos0_x"][0, 1] = seed["pos_x"][0, 1]
    seed["illusion_ghost_pos0_y"][0, 1] = seed["pos_y"][0, 1]
    seed["illusion_ghost_pos1_x"][0, 1] = seed["pos_x"][0, 1]
    seed["illusion_ghost_pos1_y"][0, 1] = seed["pos_y"][0, 1]
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][714], input_stride)
        for frame_i in (715,):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    out_715 = history[715]
    assert int(out_715["action_id"][0]) == 64
    assert int(out_715["hitlag"][0]) == 0
    assert float(out_715["percent"][0]) == pytest.approx(48.739998, abs=0.001)
    assert int(out_715["items"][0]["exists"]) == 1
    assert int(out_715["items"][0]["type"]) == 56
    assert int(out_715["items"][0]["state"]) == 1
    assert float(out_715["items"][0]["pos_x"]) == pytest.approx(109.668579, abs=0.001)
    assert float(out_715["items"][0]["pos_y"]) == pytest.approx(-2.200897, abs=0.001)
