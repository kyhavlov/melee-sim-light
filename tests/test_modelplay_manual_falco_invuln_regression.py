from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

FIXTURE = "tests/fixtures/modelplay/manual_falco_invuln_prefix_0_562.json"
FIXTURE_AGAIN = "tests/fixtures/modelplay/manual_falco_invuln_again_prefix_0_2290.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    assert fixture["source_trace"] == "manual_repros/falco_invuln.json"
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {"start_frame": 178, "end_frame": 181, "note": "first Fox AttackAirLw BODY hit on idle Falco"},
        {"start_frame": 239, "end_frame": 242, "note": "second Fox AttackAirLw BODY hit on idle Falco"},
        {"start_frame": 410, "end_frame": 412, "note": "Fox AttackAirN BODY hit after later whiff windows"},
    ]
    return fixture


def _load_fixture_again() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE_AGAIN).read_text(encoding="utf-8"))
    assert fixture["source_trace"] == "manual_repros/falco_invuln_again.json"
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {
            "start_frame": 2000,
            "end_frame": 2000,
            "note": "Falco Wait1_0 action_frame 209 keeps live BODY hurtcaps beyond SSANIM01 matrix frame_count 120",
        },
        {
            "start_frame": 2150,
            "end_frame": 2152,
            "note": "Falco Wait1_0 keeps live BODY hurtcaps while Fox AttackDash passes nearby",
        },
        {
            "start_frame": 2257,
            "end_frame": 2257,
            "note": "later Wait1_0 BODY hurtcaps remain live during another nearby AttackDash",
        },
    ]
    return fixture


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _input_bytes_from_compact(values: list[float | int], input_stride: int) -> np.ndarray:
    arr = np.zeros((1,), dtype=INPUT_DTYPE)
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, 0]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, 0]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, 0]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, 0]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, 0]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, 0]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, 0]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))
    return arr.view(np.uint8).reshape((1, input_stride))


def _replay_fixture(
    fixture: dict[str, Any],
    *,
    end_frame: int,
    hurtcap_sample_frames: set[int] | None = None,
) -> tuple[dict[int, np.void], dict[int, np.ndarray]]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    players = sorted(fixture["players"], key=lambda p: int(p["slot"]))
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internal_char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["seed"]),
    )
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}
    hurtcaps_by_frame: dict[int, np.ndarray] = {}
    sample_frames = hurtcap_sample_frames or set()

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        binding.write_compare(handle, out_compare)
        history[0] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()

        inputs_by_frame = {int(frame): values for frame, values in fixture["inputs"]}
        for frame_i in range(0, end_frame):
            input_t = _input_bytes_from_compact(
                inputs_by_frame.get(frame_i, [0, 0, 0, 0, 0, 0, 0]), input_stride
            )
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare)
            out_frame = frame_i + 1
            history[out_frame] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if out_frame in sample_frames:
                hurtcaps, count = binding.hurtcaps_world(handle, 0, 1)
                hurtcaps_by_frame[out_frame] = hurtcaps[: int(count)].copy()
            prev_input = input_t.copy()
    finally:
        binding.destroy(handle)

    return history, hurtcaps_by_frame


@pytest.mark.integration
def test_manual_idle_falco_repro_stays_vulnerable_and_hittable() -> None:
    # Manual repro `manual_repros/falco_invuln.json` showed idle Falco feeling intangible in
    # normal gameplay. The exported trace never exposes visible invulnerability, so this lock
    # replays the compact P1 input prefix from match init and asserts the two relevant contracts:
    # Falco's output hurtbox state remains vulnerable, and the BODY overlap windows still apply
    # damage instead of being suppressed by stale hidden colanim/hitlist state.
    fixture = _load_fixture()
    history, _hurtcaps_by_frame = _replay_fixture(fixture, end_frame=562)

    falco = 1
    assert all(int(row["hurtbox_state"][falco]) == 0 for row in history.values())

    assert float(history[180]["percent"][falco]) == pytest.approx(9.0, abs=1e-4)
    assert int(history[180]["hitlag"][falco]) > 0
    assert int(history[180]["hitstun"][falco]) > 0

    assert float(history[241]["percent"][falco]) == pytest.approx(22.65, abs=1e-4)
    assert int(history[241]["hitlag"][falco]) > 0
    assert int(history[241]["hitstun"][falco]) > 0

    # The compact original repro still has a true late visible-hit window: Falco is ordinary
    # Wait1_0 beyond the baked SSANIM01 matrix length immediately before the hit, and the next
    # frame applies BODY damage/hitlag. The longer `again` fixture below only locks late hurtcap
    # liveness because its later AttackDash windows are nearby whiffs in the current sim.
    assert int(history[411]["action_id"][falco]) == 14
    assert int(history[411]["animation_index"][falco]) == 2
    assert int(history[411]["action_frame"][falco]) > 120
    assert float(history[412]["percent"][falco]) == pytest.approx(30.12, abs=1e-4)
    assert int(history[412]["hitlag"][falco]) > 0
    assert int(history[412]["hitstun"][falco]) > 0


@pytest.mark.integration
def test_manual_idle_falco_repro_keeps_late_wait_hurtcaps_live() -> None:
    # `manual_repros/falco_invuln_again.json` exposed the source of the apparent intangibility:
    # Falco Wait1_0 has SSANIMT1/FObj coverage through frame 240, but the baked SSANIM01 matrices
    # cover 120 frames. BODY hurtcaps must keep sampling the live FObj track pose after frame 120.
    # Source refs: src/anim_pose.c, refs/melee/src/sysdolphin/baselib/{aobj.c,fobj.c},
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC.
    fixture = _load_fixture_again()
    history, hurtcaps_by_frame = _replay_fixture(
        fixture, end_frame=2290, hurtcap_sample_frames={2000, 2150, 2152, 2257}
    )

    falco = 1
    # This fixture includes unrelated knockdown/getup frames. The regression target is ordinary
    # late Wait1_0 pose sampling: those idle rows must remain hittable.
    assert all(
        int(row["hurtbox_state"][falco]) == 0
        for row in history.values()
        if int(row["action_id"][falco]) == 14
    )

    for frame in (2000, 2150):
        row = history[frame]
        assert int(row["action_id"][falco]) == 14
        assert int(row["animation_index"][falco]) == 2
        if frame == 2150:
            assert int(row["action_frame"][falco]) > 120
        hurtcaps = hurtcaps_by_frame[frame]
        assert hurtcaps.shape[0] == 13
        assert int(np.count_nonzero(hurtcaps[:, 6] > 0.0)) == 13

    for frame in (2152, 2257):
        hurtcaps = hurtcaps_by_frame[frame]
        assert hurtcaps.shape[0] == 13
        assert int(np.count_nonzero(hurtcaps[:, 6] > 0.0)) == 13
