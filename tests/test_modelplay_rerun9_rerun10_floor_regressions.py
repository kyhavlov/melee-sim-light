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
from tools.eval.validation_dtypes import COMPARE_DTYPE

RERUN9 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun9/trace.json"
RERUN10 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun10/trace.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_trace_fixture(source: str) -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun9_rerun10_floor_windows.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, frame_source in fixture["frames"]:
        if frame_source != source:
            continue
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


def _run_trace_window(
    *,
    source: str,
    start_frame: int,
    end_frame: int,
    overrides: dict[tuple[int, str], Any],
) -> dict[int, np.void]:
    _require_local_data_or_skip()
    trace = _load_trace_fixture(source)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = _seed_from_trace_frame(trace, start_frame=start_frame, overrides=overrides)
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_in = prev_input_bytes
        for frame_i in range(start_frame, end_frame + 1):
            input_bytes = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_in, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_in = input_bytes
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_modelplay_rerun10_252_shine_damageair_resolves_floor_contact_after_hitlag_sdi() -> None:
    # Rerun10 viewer frame 252: Falco is repeatedly shined near FD center and stayed below the
    # floor. The collision substrate must preserve frame-start vertical position for the floor
    # sweep through hitlag SDI/ASDI displacement instead of refreshing the whole sweep segment after
    # those callbacks.
    #
    # Decomp ownership:
    # - Damage_OnEveryHitlag can move cur_pos during hitlag.
    # - Damage_Coll resolves floor contact through the mpColl floor sweep before grounded damage
    #   handoff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
    history = _run_trace_window(
        source=RERUN10,
        start_frame=243,
        end_frame=260,
        overrides={(0, "ground_id"): np.uint16(1)},
    )

    assert float(history[252]["pos_y"][0]) == pytest.approx(0.0001, abs=0.001)
    for frame_i in range(248, 261):
        assert float(history[frame_i]["pos_y"][0]) >= -0.001


@pytest.mark.integration
def test_modelplay_rerun9_3587_ledge_damagefly_lands_instead_of_escaping_floor_chain() -> None:
    # Rerun9 viewer frame 3587: Fox is knocked down through FD's left ledge floor after a dair.
    # Keep the persisted ledge floor owner and require the DamageFly collision lane to resolve to
    # floor contact instead of falling below the stage.
    #
    # Decomp ownership:
    # - DamageFly_Coll routes through ft_80081DD4, then ftCo_80090184 can enter DownBound.
    # - mpColl floor checks use persisted floor.index plus line-graph traversal.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
    history = _run_trace_window(
        source=RERUN9,
        start_frame=3584,
        end_frame=3602,
        overrides={(1, "ground_id"): np.uint16(0)},
    )

    assert int(history[3588]["on_ground"][1]) == 1
    assert float(history[3588]["pos_y"][1]) == pytest.approx(0.0001, abs=0.001)
    for frame_i in range(3588, 3603):
        assert float(history[frame_i]["pos_y"][1]) >= -0.001


@pytest.mark.integration
def test_modelplay_rerun10_1873_damageair_floor_clip_recovers_to_floor_contact() -> None:
    # Rerun10 viewer frame 1873: Falco is visibly clipped below FD before jumping back through.
    # This locks the broader DamageAir floor-contact shape around repeated hitlag and recovery.
    #
    # Decomp ownership:
    # - Damage_Coll owns floor contact for DamageAir* states.
    # - mpColl floor sweeps use frame-start vertical position and current ECB bottom.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
    history = _run_trace_window(
        source=RERUN10,
        start_frame=1850,
        end_frame=1879,
        overrides={(0, "ground_id"): np.uint16(1)},
    )

    for frame_i in range(1852, 1879):
        assert float(history[frame_i]["pos_y"][0]) >= -0.001
    # The retained floor-contact owner is the thing under test here. Later collision-owner fixes can
    # clear the old hitlag-frame timing by frame 1873 while still keeping the clipped DamageAir/Fall
    # handoff pinned to the floor.
    assert int(history[1868]["hitlag"][0]) > 0
    assert float(history[1873]["pos_y"][0]) == pytest.approx(0.0001, abs=0.001)
