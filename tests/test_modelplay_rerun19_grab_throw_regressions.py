from __future__ import annotations

import importlib
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

RERUN19 = "reports/modelplay/20260426_rl_doubles_v27_7000_1v1_rerun19/trace.json"
RERUN19_FIXTURE = "tests/fixtures/modelplay/rerun19_grab_throw_windows.json"

ACT_FALL = 29
ACT_CATCH = 212
ACT_CATCH_DASH = 214


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_rerun19_fixture() -> dict[str, Any]:
    fixture_path = _root() / RERUN19_FIXTURE
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    assert fixture["source_trace"] == RERUN19
    assert fixture["windows"] == [
        {"start_frame": 1845, "end_frame": 1862, "note": "jump-cancel Catch floor-loss window"},
        {"start_frame": 2863, "end_frame": 2866, "note": "Falco ThrowB laser spawn window"},
        {"start_frame": 4530, "end_frame": 4575, "note": "CatchDash friction window"},
    ]
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw in fixture["frames"]:
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


def _run_rerun19_window(
    *,
    start_frame: int,
    end_frame: int,
    overrides: dict[tuple[int, str], Any] | None = None,
) -> dict[int, np.void]:
    _require_local_data_or_skip()
    trace = _load_rerun19_fixture()

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=start_frame, overrides=overrides or {})
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][start_frame], input_stride)
        for frame_i in range(start_frame + 1, end_frame + 1):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_modelplay_rerun19_jc_grab_uses_catch_edge_snap_not_airborne_hover() -> None:
    # Rerun19 visible bug around frame 1847:
    # - Fox jump-cancels into Catch while carrying run speed near FD's right edge.
    # - Catch_Coll routes through ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC.
    # - mpColl_8004B2DC uses the flags=2 floor-edge snap helper before the fn_800D8E30
    #   floor-loss callback; successful edge snap keeps Catch grounded at the FD endpoint.
    # - The negative this guards is the old illegal airborne Catch/Wait hover, not source
    #   mpColl edge clamping.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_Catch_Coll,fn_800D8E30}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_800827A0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    history = _run_rerun19_window(start_frame=1846, end_frame=1862)

    out = history[1861]
    assert int(out["action_id"][1]) == ACT_CATCH
    assert int(out["on_ground"][1]) == 1
    assert float(out["pos_x"][1]) == pytest.approx(85.5657, abs=0.001)

    out_next = history[1862]
    assert int(out_next["action_id"][1]) == ACT_CATCH
    assert int(out_next["on_ground"][1]) == 1
    assert float(out_next["pos_x"][1]) == pytest.approx(85.5657, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun19_dash_grab_uses_catchdash_friction_fallback() -> None:
    # Rerun19 visible bug around frame 4530:
    # CatchDash_Phys calls ft_80085030. CatchDash's extracted motion-state flags do not set
    # x594_b0, so the source path is the catch-friction fallback, not stale Dash velocity and not
    # TransN root motion.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    history = _run_rerun19_window(start_frame=4531, end_frame=4575)

    out = history[4566]
    assert int(out["action_id"][1]) == ACT_CATCH_DASH
    assert int(out["on_ground"][1]) == 1
    assert float(out["pos_x"][1]) == pytest.approx(23.1966, abs=0.01)
    assert float(out["speed_ground_x_self"][1]) == pytest.approx(0.0, abs=1e-6)

    out_late = history[4575]
    assert int(out_late["on_ground"][1]) == 1
    assert float(out_late["pos_x"][1]) < 24.0


@pytest.mark.integration
def test_modelplay_rerun19_falco_throwb_laser_points_behind_toward_victim() -> None:
    # Rerun19 visible bug around frame 2866 was a viewer-facing ThrowB laser direction problem.
    # The C owner is ftFx_Throw_Anim: it samples FtGetHoldJoint/ItGetHoldJoint from the live
    # ThrowB pose and calls it_8029C6CC. Lock the exported item data so the shot remains behind
    # Falco with the upward/leftward ThrowB vector.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,ftFx_Throw_Anim}
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    history = _run_rerun19_window(
        start_frame=2864,
        end_frame=2866,
        overrides={(0, "animation_index"): np.uint32(248)},  # ftCo_SM_ThrowB
    )

    out = history[2866]
    owner_x = float(out["pos_x"][0])
    victim_x = float(out["pos_x"][1])
    victim_y = float(out["pos_y"][1])
    throw_lasers = [
        it
        for it in out["items"]
        if int(it["exists"]) and int(it["type"]) == 55 and int(it["owner"]) == 0
    ]
    assert len(throw_lasers) == 1
    laser = throw_lasers[0]
    assert float(laser["pos_x"]) < owner_x - 5.0
    assert float(laser["pos_x"]) < victim_x
    assert float(laser["pos_y"]) < victim_y
    assert float(laser["vel_x"]) < -4.0
    assert float(laser["vel_y"]) > 2.0
