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
from tests.test_stage_collision_fd_grounding import _fd_floor_pick_line_at_x
from tools.eval.dataset import COMPARE_DTYPE

RERUN11 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun11/trace.json"

ACT_DASH = 20
ACT_KNEE_BEND = 24
ACT_SQUAT_WAIT = 40
ACT_FX_SPECIAL_LW_START = 360


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


@pytest.mark.integration
def test_modelplay_rerun11_grounded_shine_start_applies_entry_friction() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the Dash->KneeBend fix, rerun11 first diverged at frame 119,
    # - both sides were grounded SpecialLwStart action_frame=1,
    # - vanilla step dx was 0.700000 while the sim carried SquatWait's 0.780000 ground speed.
    #
    # Decomp ownership: grounded Reflector Start's Phys callback calls ft_80084F3C.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    _require_local_data_or_skip()
    trace = _load_trace_fixture()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=118, overrides={})
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input = _input_bytes_from_trace_frame(trace["frames"][118], input_stride)
    input_t = _input_bytes_from_trace_frame(trace["frames"][119], input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    assert int(seed["action_id"][0, 0]) == ACT_SQUAT_WAIT
    assert int(seed["action_frame"][0, 0]) == 3
    assert float(seed["speed_ground_x_self"][0, 0]) == pytest.approx(0.78, abs=0.005)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input, input_t)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_LW_START
    assert int(out["action_frame"][0]) == 1
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) == pytest.approx(-13.179997, abs=0.001)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.700000, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun11_passivestandb_stays_grounded_through_right_ledge_segment() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the generic Damage OnEveryHitlag SDI owner fix, rerun11's next gameplay mismatch was
    #   Falco falling out of PassiveStandB near FD's right floor endpoint.
    # - PassiveStand_Coll uses ft_80084104 -> ft_800827A0 -> mpColl_8004B2DC, so the same
    #   mpColl_8004A45C_Floor edge-snap helper should keep the tech-roll grounded on this window.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800827A0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    _require_local_data_or_skip()
    trace = _load_trace_fixture()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    # Modelplay viewer `lastGroundId` is not the simulator's stable stage `ground_id` lane.
    # Seed the hidden floor owner from the FD floor line under the trace-visible x-position so this
    # window exercises the same grounded continuation owner as the comparator run.
    floor = _fd_floor_pick_line_at_x(72.83793640136719)
    seed = _seed_from_trace_frame(
        trace, start_frame=251, overrides={(1, "ground_id"): np.uint16(floor["segment_i"])}
    )
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    assert int(seed["action_id"][0, 1]) == 201
    assert int(seed["action_frame"][0, 1]) == 9
    assert int(seed["on_ground"][0, 1]) == 1

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][251], input_stride)
        for frame_i in range(252, 261):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    out = history[260]
    assert int(out["action_id"][1]) == 201
    assert int(out["action_frame"][1]) == 18
    assert int(out["on_ground"][1]) == 1
    assert float(out["pos_x"][1]) == pytest.approx(85.380241, abs=0.001)
    assert float(out["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)
