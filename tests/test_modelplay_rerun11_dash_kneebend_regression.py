from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tests.test_modelplay_rerun5_collision_regressions import (
    _input_bytes_from_trace_frame,
    _require_local_data_or_skip,
    _seed_from_trace_frame,
)
from tools.eval.dataset import COMPARE_DTYPE

RERUN11 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun11/trace.json"

ACT_DASH = 20
ACT_KNEE_BEND = 24


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_trace_fixture() -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun11_dash_kneebend_windows.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in fixture["frames"]:
        assert source == RERUN11
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
def test_modelplay_rerun11_dash_to_kneebend_uses_vanilla_transition_ground_speed() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - rerun11 matches vanilla through frame 87,
    # - both enter KneeBend on frame 88,
    # - vanilla's frame-88 x displacement is 1.340000, not the sim's old 1.579998 carry.
    #
    # Decomp ownership:
    # - Dash IASA routes jump through fn_800CAF78.
    # - Dash Phys owns the dash run terminal velocity target via getAccelAndTarget.
    # - KneeBend Phys then uses ft_80084F3C ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_IASA,ftCo_Dash_Phys}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    _require_local_data_or_skip()
    trace = _load_trace_fixture()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=87, overrides={})
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input = _input_bytes_from_trace_frame(trace["frames"][87], input_stride)
    input_t = _input_bytes_from_trace_frame(trace["frames"][88], input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    assert int(seed["action_id"][0, 0]) == ACT_DASH
    assert int(seed["action_frame"][0, 0]) == 3
    assert float(seed["speed_ground_x_self"][0, 0]) == pytest.approx(1.74, abs=0.005)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input, input_t)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == ACT_KNEE_BEND
    assert int(out["action_frame"][0]) == 0
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(-55.100002, abs=0.001)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(1.340000, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun11_dash_to_kneebend_handoff_uses_full_terminal_on_partial_stick() -> None:
    # Controlled rerun11 variant:
    # - same frame-87 Dash seed,
    # - frame-88 p0 jump input changed to joystickX=0.5,
    # - vanilla engine dump still steps 1.340000 into KneeBend.
    #
    # This locks that Dash -> KneeBend uses the Dash terminal handoff observed in vanilla, not the
    # stick-scaled Dash Phys target used when Dash Phys itself runs.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_IASA,ftCo_Dash_Phys}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Phys
    _require_local_data_or_skip()
    trace = _load_trace_fixture()
    trace["frames"][88]["players"][0]["inputs"]["processed"]["joystickX"] = 0.5

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=87, overrides={})
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input = _input_bytes_from_trace_frame(trace["frames"][87], input_stride)
    input_t = _input_bytes_from_trace_frame(trace["frames"][88], input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input, input_t)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == ACT_KNEE_BEND
    assert int(out["action_frame"][0]) == 0
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(-55.100002, abs=0.001)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(1.340000, abs=0.001)
