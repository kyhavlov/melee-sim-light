from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array


FIXTURE_DREAMLAND_DASH_LEDGE = Path(
    "tests/fixtures/modelplay/manual_repros/set11/dreamland_dash_dance_left_ledge_compact.json"
)
FIXTURE_YOSHI_PASS_LEDGE = Path(
    "tests/fixtures/modelplay/manual_repros/set11/yoshi_platform_drop_ledge_grab_compact.json"
)

ACT_FALL = 29
ACT_PASS = 244
ACT_CLIFF_CATCH = 252
ACT_CLIFF_WAIT = 253


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _input_for_frame(ranges: list[list[Any]], frame: int) -> list[float | int]:
    for start, end, values in ranges:
        if int(start) <= frame <= int(end):
            return list(values)
    raise AssertionError(f"missing input range for frame {frame}")


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _run_fixture(fixture_path: Path, *, p1_ranges_override: list[list[Any]] | None = None) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    fixture = json.loads((_root() / fixture_path).read_text(encoding="utf-8"))

    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)

    players = fixture["players"]
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["seed"]),
    )

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        if not hasattr(binding, "debug_set_player_root"):
            pytest.skip("msl_binding debug_set_player_root unavailable")
        for player, row in enumerate(players):
            binding.debug_set_player_root(
                handle,
                0,
                player,
                float(row["x"]),
                float(row["y"]),
                int(row["facing"]),
            )

        for frame_i in range(0, int(fixture["end_frame"]) + 1):
            binding.write_compare(handle, out_compare)
            history[frame_i] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if frame_i == int(fixture["end_frame"]):
                break

            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            p1_ranges = p1_ranges_override if p1_ranges_override is not None else fixture["p1_ranges"]
            _write_input_player(input_t, 0, _input_for_frame(p1_ranges, frame_i + 1))
            _write_input_player(input_t, 1, _input_for_frame(fixture["p2_ranges"], frame_i + 1))
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


def test_dreamland_left_ledge_turn_floor_loss_preserves_self_vel_not_post_dash_gr_vel() -> None:
    history = _run_fixture(FIXTURE_DREAMLAND_DASH_LEDGE)

    # Vanilla probe:
    # reports/triage/dreamland_dash_dance_left_ledge_vanilla_probe/vanilla_engine_dump.bin
    # At frame 204 Fox has walked off the left Dream Land ledge into Fall. The current-frame
    # Turn/Dash ground scalar can point inward, but ftCo_Fall_Enter clamps fp->self_vel.x and
    # ftCommon_8007D5D4 clears fp->gr_vel; it does not copy the post-Phys ground scalar into Fall.
    f204 = history[204]
    assert int(f204["action_id"][0]) == ACT_FALL
    assert int(f204["on_ground"][0]) == 0
    assert float(f204["pos_x"][0]) == pytest.approx(-77.6091, abs=1.0e-4)
    assert float(f204["speed_air_x_self"][0]) == pytest.approx(-0.45, abs=1.0e-4)
    assert float(f204["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1.0e-6)

    # The old bug injected +0.83 inward air speed, crossed back over the ledge floor, and entered
    # Landing on frame 210. Vanilla is still in Fall, drifting down-left offstage.
    f210 = history[210]
    assert int(f210["action_id"][0]) == ACT_FALL
    assert int(f210["action_frame"][0]) == 6
    assert int(f210["on_ground"][0]) == 0
    assert float(f210["pos_x"][0]) == pytest.approx(-78.9091, abs=1.0e-4)
    assert float(f210["pos_y"][0]) == pytest.approx(-4.8211, abs=1.0e-4)
    assert float(f210["speed_air_x_self"][0]) == pytest.approx(-0.19, abs=1.0e-4)

    f212 = history[212]
    assert int(f212["action_id"][0]) == ACT_FALL
    assert int(f212["on_ground"][0]) == 0
    assert float(f212["pos_x"][0]) == pytest.approx(-79.5291, abs=1.0e-4)
    assert float(f212["pos_y"][0]) == pytest.approx(-8.2711, abs=1.0e-4)


def test_yoshi_platform_drop_pass_can_grab_ledge_after_down_release() -> None:
    history = _run_fixture(FIXTURE_YOSHI_PASS_LEDGE)

    # Manual set11 repro: Fox drops through Yoshi's left platform, releases down, then falls past
    # the left ledge. Source owner is Pass_Coll -> ft_80082F28, which runs the common cliff-catch
    # check after airborne collision. This must not wait for Pass to animate into ordinary Fall.
    # Vanilla probe:
    # reports/triage/yoshi_platform_drop_ledge_vanilla_probe/vanilla_engine_dump.bin
    # Decomp:
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082F28
    f184 = history[184]
    f185 = history[185]
    f192 = history[192]

    assert int(f184["action_id"][0]) == ACT_PASS
    assert int(f184["on_ground"][0]) == 0
    assert int(f185["action_id"][0]) == ACT_CLIFF_CATCH
    assert int(f185["on_ground"][0]) == 0
    assert float(f185["pos_x"][0]) == pytest.approx(-60.6959, abs=1.0e-4)
    assert float(f185["pos_y"][0]) == pytest.approx(-16.9747, abs=1.0e-4)
    assert int(f192["action_id"][0]) == ACT_CLIFF_WAIT
    assert float(f192["pos_x"][0]) == pytest.approx(-57.92, abs=1.0e-4)
    assert float(f192["pos_y"][0]) == pytest.approx(-17.9, abs=1.0e-4)


def test_yoshi_platform_drop_pass_holding_down_still_blocks_ledge_grab() -> None:
    fixture = json.loads((_root() / FIXTURE_YOSHI_PASS_LEDGE).read_text(encoding="utf-8"))
    hold_down_ranges = [
        [0, 90, [0, 0, 0, 0, 0, 0, 0]],
        [91, 91, [0, 0, 1, 0, 0, 0, 0]],
        [92, 98, [0, -1, 1, 0, 0, 0, 0]],
        [99, 99, [0, 0, 1, 0, 0, 0, 0]],
        [100, 114, [0, 0, 0, 0, 0, 0, 0]],
        [115, 118, [0, 1, 0, 0, 0, 0, 0]],
        [119, 164, [0, 0, 0, 0, 0, 0, 0]],
        [165, int(fixture["end_frame"]), [0, -1, -1, 0, 0, 0, 0]],
    ]
    history = _run_fixture(FIXTURE_YOSHI_PASS_LEDGE, p1_ranges_override=hold_down_ranges)

    # Negative owner boundary: ftCliffCommon_80081298 rejects ledge catch while stick Y is below
    # the common-data down threshold. Adding Pass to the cliff-catch action family must not bypass
    # that input gate.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
    assert int(history[184]["action_id"][0]) == ACT_PASS
    for frame_i in range(185, 193):
        assert int(history[frame_i]["action_id"][0]) != ACT_CLIFF_CATCH
        assert int(history[frame_i]["action_id"][0]) != ACT_CLIFF_WAIT
