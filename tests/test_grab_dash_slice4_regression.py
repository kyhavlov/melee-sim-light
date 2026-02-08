from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE, read_dataset


_BASE_REL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_BUTTON_A = 0x0100
_BUTTON_Z = 0x0010
_BUTTON_L = 0x0040
_BUTTON_R = 0x0020

_ACT_WAIT = 14
_ACT_DASH = 20
_ACT_CATCH = 212
_ACT_CATCH_PULL = 213
_ACT_CATCH_DASH = 214
_ACT_CATCH_DASH_PULL = 215
_ACT_CAPTURE_PULLED_HI = 223
_ACT_CAPTURE_PULLED_LW = 226
_SM_WAIT1_0 = 2
_SM_CATCH_DASH = 243

_HIT_GROUNDED = 1 << 9
_HIT_AERIAL = 1 << 10
_HIT_ELEMENT_CATCH = 8
_REQUIRED_ARTIFACTS = (
    "data/moves/fox.json",
    "data/moves/falco.json",
    "data/hit_status/fox.bin",
    "data/hit_status/falco.bin",
    "data/hurtbox_states/fox.bin",
    "data/hurtbox_states/falco.bin",
)


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _buttons(row: np.ndarray, field: str, port: int) -> int:
    return int(row[field]["p"][0, port]["buttons"])


def _button_edge(row: np.ndarray, port: int, mask: int) -> bool:
    cur = _buttons(row, "input_t", port)
    prev = _buttons(row, "prev_input_t", port)
    return (cur & mask) != 0 and (prev & mask) == 0


def _shield_or_trigger_held(row: np.ndarray, port: int) -> bool:
    cur_buttons = _buttons(row, "input_t", port)
    if (cur_buttons & (_BUTTON_L | _BUTTON_R)) != 0:
        return True
    return int(row["input_t"]["p"][0, port]["l"]) > 0 or int(row["input_t"]["p"][0, port]["r"]) > 0


def _grab_attempt_edge(row: np.ndarray, port: int) -> bool:
    z_edge = _button_edge(row, port, _BUTTON_Z)
    a_edge = _button_edge(row, port, _BUTTON_A)
    return z_edge or (a_edge and _shield_or_trigger_held(row, port))


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        ref = row["ref_t1"].reshape(-1)[0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "attacker"),
    [
        ("GracefulAttachedTurtle.msl", 157, 1),
        ("GracefulAttachedTurtle.msl", 2059, 1),
    ],
)
def test_dash_grab_enters_catchdash(dataset_name: str, record: int, attacker: int) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    victim = 1 - attacker

    # Replay lock preconditions for dash-grab entry.
    assert int(row["seed_t"]["action_id"][0, attacker]) == _ACT_DASH
    assert int(row["ref_t1"]["action_id"][0, attacker]) == _ACT_CATCH_DASH
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert _grab_attempt_edge(row, attacker)

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "attacker", "victim"),
    [
        ("GracefulAttachedTurtle.msl", 3402, 1, 0),
        ("QuerulousGrandDinosaur.msl", 5369, 0, 1),
    ],
)
def test_replay_catchdash_connect_enters_pull_and_capture_pulled(
    dataset_name: str,
    record: int,
    attacker: int,
    victim: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay-real lock preconditions for CatchDash connect.
    assert int(row["seed_t"]["action_id"][0, attacker]) == _ACT_CATCH_DASH
    assert int(row["seed_t"]["grab_owner_port"][0, attacker]) == 0xFF
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == 0xFF
    assert int(row["ref_t1"]["action_id"][0, attacker]) == _ACT_CATCH_DASH_PULL
    assert int(row["ref_t1"]["action_id"][0, victim]) in (_ACT_CAPTURE_PULLED_HI, _ACT_CAPTURE_PULLED_LW)
    assert int(row["seed_t"]["hitlag"][0, attacker]) == 0
    assert int(row["seed_t"]["hitlag"][0, victim]) == 0
    assert int(row["seed_t"]["hitstun"][0, attacker]) == 0
    assert int(row["seed_t"]["hitstun"][0, victim]) == 0
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][0, attacker]) == int(ref["action_frame"][attacker])
    assert int(out["action_frame"][0, victim]) == int(ref["action_frame"][victim])
    assert int(out["animation_index"][0, attacker]) == int(ref["animation_index"][attacker])
    assert int(out["animation_index"][0, victim]) == int(ref["animation_index"][victim])


@pytest.mark.parametrize(
    ("victim_on_ground", "expected_victim_action"),
    [
        (1, _ACT_CAPTURE_PULLED_LW),
        (0, _ACT_CAPTURE_PULLED_HI),
    ],
)
def test_catchdash_connect_enters_catchdashpull_and_capture_variant(
    victim_on_ground: int,
    expected_victim_action: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
        seed = seed_bytes.view(SEED_DTYPE).reshape(-1)

        seed["num_players"][0] = np.uint8(2)
        seed["char_id"][0, 0] = np.uint8(1)  # Fox
        seed["char_id"][0, 1] = np.uint8(22)  # Falco
        seed["stocks"][0, :2] = np.uint8(4)
        seed["instance_id"][0, 0] = np.uint16(1001)
        seed["instance_id"][0, 1] = np.uint16(2002)
        seed["grab_owner_port"][0, :] = np.uint8(0xFF)

        seed["action_id"][0, 0] = np.uint16(_ACT_CATCH_DASH)
        seed["animation_index"][0, 0] = np.uint32(_SM_CATCH_DASH)
        seed["on_ground"][0, 0] = np.uint8(0)
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_y"][0, 0] = np.float32(0.0)

        seed["action_id"][0, 1] = np.uint16(_ACT_WAIT)
        seed["animation_index"][0, 1] = np.uint32(_SM_WAIT1_0)
        # Decomp tie-down: fn_800DAADC picks CapturePulledLw/Hi from callback-target xE0 lane
        # (mapped to victim on_ground in the sim), not owner grounding.
        # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
        seed["on_ground"][0, 1] = np.uint8(victim_on_ground)
        seed["pos_x"][0, 1] = np.float32(0.0)
        seed["pos_y"][0, 1] = np.float32(0.0)

        binding.reseed_seed(handle, seed_bytes)
        binding.debug_refresh_combat_geometry(handle)

        # Force a deterministic catch overlap to isolate CatchDash connect->pull chain semantics.
        binding.debug_clear_hitboxes_world(handle, 0, 0)
        binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 3.0, 0.0, 1)
        binding.debug_set_hitbox_element(handle, 0, 0, 0, _HIT_ELEMENT_CATCH)
        binding.debug_set_hitbox_flags(handle, 0, 0, 0, _HIT_GROUNDED | _HIT_AERIAL)

        # Keep grabbable slot flags from table refresh; only force overlap geometry.
        binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0)

        _, filtered_count = binding.debug_combat_contacts_filtered(handle, 0, 8)
        assert filtered_count >= 1

        binding.debug_combat_resolve(handle)

        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

        assert int(out["action_id"][0]) == _ACT_CATCH_DASH_PULL
        assert int(out["action_id"][1]) == expected_victim_action
    finally:
        binding.destroy(handle)
