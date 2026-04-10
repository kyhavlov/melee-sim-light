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

RERUN10 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun10/trace.json"

ACT_FALL = 29
ACT_GUARD_ON = 178
ACT_GUARD = 179
ACT_GUARD_OFF = 180
ACT_GUARD_SET_OFF = 181
ACT_GUARD_REFLECT = 182
GUARD_ACTIONS = {ACT_GUARD_ON, ACT_GUARD, ACT_GUARD_OFF, ACT_GUARD_SET_OFF, ACT_GUARD_REFLECT}


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_trace_fixture() -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun10_guard_jump_windows.json"
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    state_fields = fixture["state_fields"]
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in fixture["frames"]:
        assert source == RERUN10
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
    start_frame: int,
    end_frame: int,
    overrides: dict[tuple[int, str], Any],
) -> dict[int, np.void]:
    _require_local_data_or_skip()
    trace = _load_trace_fixture()

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
def test_modelplay_rerun10_6637_airborne_guardon_drops_shield_and_consumes_ground_jump() -> None:
    # Rerun10 viewer frame 6637: Falco has GuardOn but no floor at FD's left ledge. GuardOn_Coll
    # is a grounded collision callback and must leave shield once its common floor helper reports
    # no floor; otherwise the sim carries a full ground jump count while airborne.
    #
    # Decomp ownership:
    # - GuardOn_Coll calls ft_800845B4.
    # - ft_800845B4 leaves the shield motion on floor loss and calls ftCo_Fall_Enter.
    # - ftCo_Fall_Enter calls ftCommon_8007D5D4 for GA_Ground sources, consuming the ground jump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800845B4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    history = _run_trace_window(
        start_frame=6637,
        end_frame=6638,
        overrides={(0, "ground_id"): np.uint16(0xFFFF)},
    )

    out = history[6637]
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["on_ground"][0]) == 0
    assert int(out["jumps_left"][0]) == 1
    assert int(out["action_id"][0]) not in GUARD_ACTIONS
    assert out["state_flags"][0].tolist() == [0, 0, 0, 0, 0]


@pytest.mark.integration
def test_modelplay_rerun10_6667_orphaned_guardsetoff_cannot_hover_airborne() -> None:
    # Rerun10 viewer frame 6667: the trace has an already-airborne GuardSetOff at x=-87 with no
    # hitlag, shield still active, and jumpsRemaining=2. This is the stuck-shield/no-pushback
    # symptom: shield pushback is grounded, so preserving shield while airborne creates a hover.
    #
    # Decomp ownership:
    # - GuardSetOff_Coll calls ft_800845B4, or ft_80084104 while shield SDI is allowed.
    # - Both helpers leave the shield motion when floor ownership is gone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800845B4}
    history = _run_trace_window(
        start_frame=6667,
        end_frame=6668,
        overrides={(0, "ground_id"): np.uint16(0xFFFF)},
    )

    for frame_i in (6667, 6668):
        out = history[frame_i]
        assert int(out["action_id"][0]) not in GUARD_ACTIONS
        assert int(out["on_ground"][0]) == 0
        assert int(out["jumps_left"][0]) <= 1
        assert out["state_flags"][0].tolist() == [0, 0, 0, 0, 0]
