from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

FIXTURE = "tests/fixtures/modelplay/manual_ledge_collision_repros.json"

MSL_COLLIDE_SIDE_EDGE_MASK = 0x00300000


def _collision_contacts_dtype() -> np.dtype:
    return np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert [trace["name"] for trace in fixture["traces"]] == [
        "spacie_upb_backwards_fastfall_neutral_ledgegrab",
        "falco_left_ledge_damagefly_wall_reflect",
        "teeter_edges",
        "grounded_sideb_to_ledge",
    ]
    return fixture


def _trace_by_name(name: str) -> dict[str, Any]:
    for trace in _load_fixture()["traces"]:
        if trace["name"] == name:
            return trace
    raise AssertionError(f"missing trace fixture {name!r}")


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _inputs_by_frame(ranges: list[list[Any]]) -> dict[int, list[float | int]]:
    out: dict[int, list[float | int]] = {}
    for start, end, values in ranges:
        for frame in range(int(start), int(end) + 1):
            out[frame] = values
    return out


def _replay_trace(trace: dict[str, Any]) -> dict[int, tuple[np.void, np.void]]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_dtype = _collision_contacts_dtype()
    contacts_stride = int(sizes["collision_contacts"])
    assert int(contacts_dtype.itemsize) == contacts_stride

    players = sorted(trace["players"], key=lambda p: int(p["slot"]))
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internal_char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(trace["stage_id"]),
        frame_id=0,
        random_seed=int(trace["seed"]),
    )
    p1_inputs = _inputs_by_frame(trace["p1_input_ranges"])
    p2_inputs = _inputs_by_frame(trace["p2_input_ranges"])

    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, tuple[np.void, np.void]] = {}

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame_i in range(0, int(trace["end_frame"]) + 1):
            binding.write_compare(handle, out_compare)
            binding.debug_write_collision_contacts(handle, out_contacts)
            out = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            contacts = out_contacts.view(contacts_dtype).reshape(-1)[0].copy()
            history[frame_i] = (out, contacts)
            if frame_i == int(trace["end_frame"]):
                break

            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            _write_input_player(input_t, 0, p1_inputs.get(frame_i + 1, [0, 0, 0, 0, 0, 0, 0]))
            _write_input_player(input_t, 1, p2_inputs.get(frame_i + 1, [0, 0, 0, 0, 0, 0, 0]))
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_grounded_sideb_to_ledge_end_stays_grounded_instead_of_fall() -> None:
    # Source owner: grounded Fox/Falco SpecialSEnd collision uses ft_800827A0, which can keep the
    # fighter grounded at the floor endpoint through mpColl_8004B2DC / mpColl_8004A45C_Floor.
    # This is distinct from grounded SpecialS main's ft_80082708 ground-to-air callback.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialSEnd_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800827A0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
    history = _replay_trace(_trace_by_name("grounded_sideb_to_ledge"))

    fox = 0
    out_168, _ = history[168]
    out_169, contacts_169 = history[169]
    assert int(out_168["action_id"][fox]) == 349  # Grounded SpecialSEnd before endpoint snap.
    assert int(out_169["action_id"][fox]) == 349  # Stays in SpecialSEnd, not actionable Fall.
    assert int(out_169["on_ground"][fox]) == 1
    assert float(out_169["pos_x"][fox]) == pytest.approx(-85.5657, abs=0.01)
    assert int(contacts_169["coll_env_flags"][fox]) & MSL_COLLIDE_SIDE_EDGE_MASK
