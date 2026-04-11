from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import importlib
import numpy as np
import pytest

from tests.test_modelplay_rerun5_collision_regressions import _input_bytes_from_trace_frame, _require_local_data_or_skip, _seed_from_trace_frame
from tests.test_stage_collision_fd_grounding import _fd_floor_pick_line_at_x
from tools.eval.dataset import COMPARE_DTYPE, read_dataset

RERUN11 = "reports/modelplay/20260410_rl_doubles_v27_7000_rerun11/trace.json"
RERUN11_PASSIVESTAND_INPUT_FIXTURE = "tests/fixtures/modelplay/rerun11_input_prefix_0_289.json"
RERUN11_DAMAGE_HITLAG_INPUT_FIXTURE = "tests/fixtures/modelplay/rerun11_input_prefix_0_305.json"
RERUN11_GUARD_INPUT_FIXTURE = "tests/fixtures/modelplay/rerun11_input_prefix_0_321.json"
RERUN11_SQUATWAIT_GUARD_INPUT_FIXTURE = "tests/fixtures/modelplay/rerun11_input_prefix_0_665.json"

ACT_DASH = 20
ACT_KNEE_BEND = 24
ACT_SQUAT_WAIT = 40
ACT_GUARD_ON = 178
ACT_GUARD_SET_OFF = 181
ACT_CATCH = 212
ACT_FX_SPECIAL_LW_START = 360
ACT_FX_SPECIAL_AIR_LW_START = 365


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_trace_fixture() -> dict[str, Any]:
    fixture_path = _root() / "tests/fixtures/modelplay/rerun11_dash_kneebend_windows.json"
    return _load_fixture(fixture_path)


def _load_rerun11_trace() -> dict[str, Any]:
    trace_path = _root() / RERUN11
    if not trace_path.exists():
        pytest.skip(f"missing local trace: {RERUN11}")
    return json.loads(trace_path.read_text(encoding="utf-8"))


def _load_fixture(fixture_path: Path) -> dict[str, Any]:
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


def _load_input_fixture(fixture_path: Path) -> dict[int, dict[str, Any]]:
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    input_fields = fixture["input_fields"]
    frames: dict[int, dict[str, Any]] = {}
    for frame_i, players_raw, source in fixture["frames"]:
        assert source == RERUN11
        players = []
        for input_values in players_raw:
            players.append({"inputs": {"processed": dict(zip(input_fields, input_values, strict=True))}})
        frames[int(frame_i)] = {"players": players}
    return frames


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


@pytest.mark.integration
def test_modelplay_rerun11_passivestandb_releases_floor_adjacent_right_wall_latch() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the PassiveStand floor-edge fix, the remaining rerun11 gameplay drift was a pinned
    #   pos_x on grounded PassiveStandB at FD's right lip.
    # - Decomp grounded right-wall collision excludes floor-adjacent wall owners while on the
    #   current floor chain, so the tech stand can drift inward instead of hugging the seam wall.
    # refs/melee/src/melee/mp/mplib.c::{mpLib_80053394_Floor,mpLib_800536CC_Floor}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall}
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_PASSIVESTAND_INPUT_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])

    contacts_dtype = np.dtype(
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
        ],
        align=False,
    )
    assert int(contacts_dtype.itemsize) == contacts_stride

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_contacts_bytes = np.empty((1, contacts_stride), dtype=np.uint8)

    history: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 281):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 277:
                binding.write_compare(handle, out_compare_bytes)
                binding.debug_write_collision_contacts(handle, out_contacts_bytes)
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                contacts = np.frombuffer(out_contacts_bytes.tobytes(), dtype=contacts_dtype, count=1)[0].copy()
                history[frame_i] = (out, contacts)
            prev_input = input_t
    finally:
        binding.destroy(handle)

    expected_x = {
        277: 85.5638198852539,
        278: 85.56288146972656,
        279: 85.56194305419922,
        280: 85.56100463867188,
    }
    for frame_i, x_ref in expected_x.items():
        out, contacts = history[frame_i]
        assert int(out["action_id"][1]) == 201
        assert int(out["on_ground"][1]) == 1
        assert float(out["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)
        assert float(out["pos_x"][1]) == pytest.approx(x_ref, abs=0.001)
        assert int(contacts["wall_kind"][1]) == 0
        assert (int(contacts["coll_env_flags"][1]) & 0x00000FC0) == 0


@pytest.mark.integration
def test_modelplay_rerun11_passivestandb_end_runs_same_frame_wait_iasa_squat() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the grounded seam-wall fix, the next gameplay mismatch was PassiveStandB ending into
    #   Wait while vanilla entered Squat immediately on held-down input.
    # - ftCo_PassiveStand_Anim ends via ft_8008A2BC -> ft_8008A348 during Anim, so the destination
    #   Wait state still runs same-frame Wait_IASA before Phys.
    # - Wait_IASA admits Squat through ftCo_800D5FB0 when lstick.y is below x90.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Anim
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Enter}
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_PASSIVESTAND_INPUT_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 290):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 284:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    expected = {
        284: (201, 39),
        285: (39, 1),
        286: (39, 2),
        287: (39, 3),
        288: (39, 4),
        289: (39, 5),
    }
    for frame_i, (action_id, action_frame) in expected.items():
        out = history[frame_i]
        assert int(out["action_id"][1]) == action_id
        assert int(out["action_frame"][1]) == action_frame
        assert int(out["on_ground"][1]) == 1


@pytest.mark.integration
def test_modelplay_rerun11_grounded_damagehi3_hitlag_sdi_stays_floor_pinned() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the PassiveStand end fix, the next gameplay mismatch was grounded DamageHi3 hitlag
    #   SDI lifting Falco 4.125 units above the floor while vanilla kept the row floor-pinned.
    # - Decomp keeps grounded Damage_Coll on ft_800848DC -> ft_80082708 -> mpColl_8004B108.
    # - Damage_OnEveryHitlag still mutates cur_pos before that collision callback, so grounded
    #   floor resolution must be allowed to snap the row back down to the floor on the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll
    # }
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_DAMAGE_HITLAG_INPUT_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 306):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 293:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    for frame_i, hitlag in ((293, 4), (294, 3), (295, 2), (296, 1)):
        out = history[frame_i]
        assert int(out["action_id"][1]) == 77
        assert int(out["action_frame"][1]) == 1
        assert int(out["on_ground"][1]) == 1
        assert int(out["hitlag"][1]) == hitlag
        assert float(out["pos_x"][1]) == pytest.approx(81.44209289550781, abs=0.001)
        assert float(out["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)

    for frame_i, action_frame in ((297, 2), (298, 3), (299, 4), (300, 5), (301, 6), (302, 7), (303, 8)):
        out = history[frame_i]
        assert int(out["action_id"][1]) == 77
        assert int(out["action_frame"][1]) == action_frame
        assert int(out["on_ground"][1]) == 1
        assert float(out["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun11_grounded_damage_wait_iasa_enters_guard_before_escape() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the grounded damage floor-resnap fix, the next mismatch was DamageHi3 ending into
    #   KneeBend while vanilla entered GuardOn, then EscapeN on the next frame.
    # - Decomp guard entry checks `held_inputs & HSD_PAD_LR`, which includes partial analog
    #   shoulder holds before the lightshield deadzone.
    # - Once Damage_IASA delegates to Wait_IASA and enters GuardOn, GuardOn_IASA must not consume
    #   the same held-shield/downward input again on that fresh entry frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800923B4,ftCo_GuardOn_IASA
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_GUARD_INPUT_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 322):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 318:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    out_319 = history[319]
    assert int(out_319["action_id"][1]) == 178
    assert int(out_319["action_frame"][1]) == -1
    assert int(out_319["on_ground"][1]) == 1
    assert float(out_319["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)

    out_320 = history[320]
    assert int(out_320["action_id"][1]) == 235
    assert int(out_320["action_frame"][1]) == 1
    assert int(out_320["on_ground"][1]) == 1
    assert float(out_320["pos_y"][1]) == pytest.approx(0.000100, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun11_shield_recharge_continues_through_air_shine_hitlag() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the grounded Damage_IASA / GuardOn owner fix, the next drift was p0 shield HP
    #   freezing during aerial ReflectorStart hitlag.
    # - Fighter_ProcessHit_8006D1EC owns shield recharge gated on `!fp->x221A_b7`, so once the
    #   shield owner is inactive the recharge tick continues even if locomotion is skipped by hitlag.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace = _load_rerun11_trace()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][0], input_stride)
        for frame_i in range(1, 342):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 337:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    expected = {
        337: (ACT_FX_SPECIAL_AIR_LW_START, 1, 4, 53.77002716064453),
        338: (ACT_FX_SPECIAL_AIR_LW_START, 1, 3, 53.84002685546875),
        339: (ACT_FX_SPECIAL_AIR_LW_START, 1, 2, 53.91002655029297),
        340: (ACT_FX_SPECIAL_AIR_LW_START, 1, 1, 53.98002624511719),
        341: (ACT_FX_SPECIAL_AIR_LW_START, 2, 0, 54.050025939941406),
    }
    for frame_i, (action_id, action_frame, hitlag, shield_hp) in expected.items():
        out = history[frame_i]
        assert int(out["action_id"][0]) == action_id
        assert int(out["action_frame"][0]) == action_frame
        assert int(out["hitlag"][0]) == hitlag
        assert int(out["on_ground"][0]) == 0
        assert float(out["shield_hp"][0]) == pytest.approx(shield_hp, abs=0.001)


@pytest.mark.integration
def test_modelplay_rerun11_squatwait_guard_preempts_jump_cancel_catch() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the DamageFly direct-aerial owner fix, the next gameplay mismatch was a crouch-family
    #   branch where the sim went SquatWait -> KneeBend -> Catch while vanilla went
    #   SquatWait -> GuardOn.
    # - Decomp Squat{,Wait,Rv}_IASA ordering is attacks -> guard -> jump.
    # - A held-shield row in SquatWait must therefore resolve ftCo_80091A4C before any KneeBend /
    #   JC-grab owner can consume the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::ftCo_SquatWait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800923B4}
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_SQUATWAIT_GUARD_INPUT_FIXTURE)

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 666):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 659:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    out_659 = history[659]
    assert int(out_659["action_id"][1]) == ACT_SQUAT_WAIT
    assert int(out_659["action_frame"][1]) == 0
    assert int(out_659["on_ground"][1]) == 1

    out_660 = history[660]
    assert int(out_660["action_id"][1]) == ACT_GUARD_ON
    assert int(out_660["action_frame"][1]) == -1
    assert int(out_660["on_ground"][1]) == 1
    assert float(out_660["shield_hp"][1]) == pytest.approx(60.0, abs=0.001)

    out_661 = history[661]
    assert int(out_661["action_id"][1]) == ACT_GUARD_ON
    assert int(out_661["action_frame"][1]) == -1
    assert int(out_661["on_ground"][1]) == 1
    assert float(out_661["shield_hp"][1]) == pytest.approx(59.986000061035156, abs=0.001)
    assert int(out_661["action_id"][1]) != ACT_CATCH


@pytest.mark.integration
def test_modelplay_rerun11_guardon_laser_hits_after_entry_blend_not_on_fresh_frame() -> None:
    # Modelplay-vs-vanilla lock:
    # - rerun11's next projectile shield-owner issue is a Falco laser crossing Fox's frozen
    #   GuardOn entry.
    # - Decomp GuardOn entry initializes mv.co.guard.{x8,x4}=neutral/0 and then blends the
    #   shield-bone translation by mv.co.guard.x0 / fp->x2E8 through ftCo_80091E78.
    # - Frozen Slippi GuardOn rows do not expose x0 in action_frame, so this sim seeds x0
    #   explicitly; the laser must not hit on the fresh GuardOn frame, but must hit once the entry
    #   blend advances.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_800921DC,ftCo_GuardOn_Anim,ftCo_80091E78}
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace_frames = _load_input_fixture(root / RERUN11_SQUATWAIT_GUARD_INPUT_FIXTURE)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace_frames[0], input_stride)
        for frame_i in range(1, 666):
            input_t = _input_bytes_from_trace_frame(trace_frames[frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 660:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    out_660 = history[660]
    assert int(out_660["action_id"][1]) == ACT_GUARD_ON
    assert int(out_660["action_frame"][1]) == -1
    assert float(out_660["shield_hp"][1]) == pytest.approx(60.0, abs=0.001)
    assert int(out_660["hitlag"][1]) == 0
    assert int(out_660["items"][0]["exists"]) == 1

    out_661 = history[661]
    assert int(out_661["action_id"][1]) == ACT_GUARD_ON
    assert int(out_661["action_frame"][1]) == -1
    assert float(out_661["shield_hp"][1]) == pytest.approx(59.986000061035156, abs=0.001)
    assert int(out_661["hitlag"][1]) == 0
    assert int(out_661["items"][0]["exists"]) == 1

    out_662 = history[662]
    assert int(out_662["action_id"][1]) == ACT_GUARD_SET_OFF
    assert int(out_662["action_frame"][1]) == 0
    assert float(out_662["shield_hp"][1]) == pytest.approx(57.27199935913086, abs=0.001)
    assert int(out_662["hitlag"][1]) == 4


@pytest.mark.integration
def test_modelplay_rerun11_damageflytop_buffered_jump_can_chain_into_dair() -> None:
    # Modelplay-vs-vanilla comparator lock:
    # - after the shield recharge owner fix, the next gameplay mismatch was Falco staying in
    #   DamageFlyTop after hitstun exit while vanilla consumed a buffered jump into AttackAirLw.
    # - DamageFly_IASA calls doIasa while x221C_b6 is set; once it clears, DamageFly_IASA delegates
    #   to DamageFall_IASA. A recent mv.co.damage.x14 buffer can still admit JumpAerial before the
    #   same-frame aerial-attack follow-up consumes the c-stick edge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_CheckInput
    _require_local_data_or_skip()
    root = _root()
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    trace = _load_rerun11_trace()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    seed_bytes = np.frombuffer(ds.samples[0:1]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_input = _input_bytes_from_trace_frame(trace["frames"][0], input_stride)
        for frame_i in range(1, 398):
            input_t = _input_bytes_from_trace_frame(trace["frames"][frame_i], input_stride)
            binding.step_input(handle, prev_input, input_t)
            if frame_i >= 395:
                binding.write_compare(handle, out_compare_bytes)
                history[frame_i] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t
    finally:
        binding.destroy(handle)

    expected = {
        395: (69, 1, 0, 94.95549011230469, 4.236337661743164),
        396: (69, 2, 0, 94.92276763916016, 2.6512908935546875),
        397: (69, 3, 0, 94.80474090576172, 1.015521764755249),
    }
    for frame_i, (action_id, action_frame, on_ground, pos_x, pos_y) in expected.items():
        out = history[frame_i]
        assert int(out["action_id"][1]) == action_id
        assert int(out["action_frame"][1]) == action_frame
        assert int(out["on_ground"][1]) == on_ground
        assert float(out["pos_x"][1]) == pytest.approx(pos_x, abs=0.001)
        assert float(out["pos_y"][1]) == pytest.approx(pos_y, abs=0.001)
