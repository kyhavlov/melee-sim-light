from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_BASE_REL = "replays/validation/cardinal_1.0_recent"
_BUTTON_A = 0x0100
_BUTTON_Z = 0x0010
_BUTTON_L = 0x0040
_BUTTON_R = 0x0020
_REQUIRED_ARTIFACTS = (
    "data/moves/fox.json",
    "data/moves/falco.json",
    # Optional but useful guards for this slice's extraction targets.
    "data/scripts/fox.bin",
    "data/scripts/falco.bin",
    "data/hurtcaps/fox.bin",
    "data/hurtcaps/falco.bin",
)


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _buttons(row: np.ndarray, field: str, port: int) -> int:
    return int(row[field]["p"]["buttons"][0, port])


def _button_edge(row: np.ndarray, port: int, mask: int) -> bool:
    cur = _buttons(row, "input_t", port)
    prev = _buttons(row, "prev_input_t", port)
    return (cur & mask) != 0 and (prev & mask) == 0


def _shield_or_trigger_held(row: np.ndarray, port: int) -> bool:
    cur_buttons = _buttons(row, "input_t", port)
    if (cur_buttons & (_BUTTON_L | _BUTTON_R)) != 0:
        return True
    return int(row["input_t"]["p"]["l"][0, port]) > 0 or int(row["input_t"]["p"]["r"][0, port]) > 0


def _grab_attempt_edge(row: np.ndarray, port: int) -> bool:
    z_edge = _button_edge(row, port, _BUTTON_Z)
    a_edge = _button_edge(row, port, _BUTTON_A)
    return z_edge or (a_edge and _shield_or_trigger_held(row, port))


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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
        ("GracefulAttachedTurtle.slpz", 212, 0),
        ("TreasuredBackKangaroo.slpz", 401, 0),
    ],
)
def test_kneebend_jc_grab_enters_catch(dataset_name: str, record: int, attacker: int) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    victim = 1 - attacker

    # Validation buffer schema only carries seed_t/ref_t1 rows. seed_t is the replay-derived t-state.
    assert int(row["seed_t"]["action_id"][0, attacker]) == 24
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 212
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0
    assert _grab_attempt_edge(row, attacker)

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])


@pytest.mark.integration
def test_landing_iasa_z_grab_enters_catch_gracefulattachedturtle_8231() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/GracefulAttachedTurtle.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[8231 : 8232]
    attacker = 0

    # Landing IASA delegates into the grounded Wait subset after the lag gate, and that subset
    # checks Catch before Guard. Keep Z-grab Landing rows from falling through the shared
    # guard_update_grounded() pass.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_CheckInput
    assert int(row["seed_t"]["action_id"][0, attacker]) == 42
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 212
    assert _grab_attempt_edge(row, attacker)

    out, ref = _run_record(dataset_path, 8231)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_frame"][0, attacker]) == int(ref["action_frame"][attacker])
    assert int(out["on_ground"][0, attacker]) == int(ref["on_ground"][attacker])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "attacker", "victim", "record_attacker_enter", "record_victim_enter", "record_anim_end"),
    [
        ("GracefulAttachedTurtle.slpz", 0, 1, 370, 374, 397),
        ("TreasuredBackKangaroo.slpz", 0, 1, 410, 414, 437),
    ],
)
def test_catchwait_pummel_loop_and_anim_end_returns(
    dataset_name: str,
    attacker: int,
    victim: int,
    record_attacker_enter: int,
    record_victim_enter: int,
    record_anim_end: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows

    row_a = samples[record_attacker_enter : record_attacker_enter + 1]
    assert int(row_a["seed_t"]["action_id"][0, attacker]) == 216
    assert int(row_a["seed_t"]["action_id"][0, victim]) == 227
    assert int(row_a["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row_a["ref_t1"]["action_id"][0, attacker]) == 217
    assert int(row_a["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row_a["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row_a["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row_a["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row_a["ref_t1"]["hitstun"][0, victim]) == 0
    assert _button_edge(row_a, attacker, _BUTTON_A) or _button_edge(row_a, attacker, _BUTTON_Z)

    # Replay-real pummel victim damage entry has hitlag on both players in local suites.
    row_v = samples[record_victim_enter : record_victim_enter + 1]
    assert int(row_v["seed_t"]["action_id"][0, attacker]) == 217
    assert int(row_v["seed_t"]["action_id"][0, victim]) == 227
    assert int(row_v["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row_v["ref_t1"]["action_id"][0, attacker]) == 217
    assert int(row_v["ref_t1"]["action_id"][0, victim]) == 228
    assert int(row_v["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row_v["ref_t1"]["hitstun"][0, victim]) == 0
    assert int(row_v["ref_t1"]["hitlag"][0, attacker]) == 4
    assert int(row_v["ref_t1"]["hitlag"][0, victim]) == 4

    row_end = samples[record_anim_end : record_anim_end + 1]
    assert int(row_end["seed_t"]["action_id"][0, attacker]) == 217
    assert int(row_end["seed_t"]["action_id"][0, victim]) == 228
    assert int(row_end["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row_end["ref_t1"]["action_id"][0, attacker]) == 216
    assert int(row_end["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row_end["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row_end["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row_end["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row_end["ref_t1"]["hitstun"][0, victim]) == 0

    out_a, ref_a = _run_record(dataset_path, record_attacker_enter)
    assert int(out_a["action_id"][0, attacker]) == int(ref_a["action_id"][attacker])
    assert int(out_a["action_id"][0, victim]) == int(ref_a["action_id"][victim])

    out_v, ref_v = _run_record(dataset_path, record_victim_enter)
    assert int(out_v["action_id"][0, attacker]) == int(ref_v["action_id"][attacker])
    assert int(out_v["action_id"][0, victim]) == int(ref_v["action_id"][victim])

    out_end, ref_end = _run_record(dataset_path, record_anim_end)
    assert int(out_end["action_id"][0, attacker]) == int(ref_end["action_id"][attacker])
    assert int(out_end["action_id"][0, victim]) == int(ref_end["action_id"][victim])


@pytest.mark.integration
def test_replay_catchattack_capture_damage_entry_and_return_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/GracefulAttachedTurtle.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    attacker = 0
    victim = 1
    record_victim_enter = 374
    record_anim_end = 397

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows

    # Replay lock: CatchAttack -> CaptureDamageLw entry frame.
    row_v = samples[record_victim_enter : record_victim_enter + 1]
    assert int(row_v["seed_t"]["action_id"][0, attacker]) == 217
    assert int(row_v["seed_t"]["action_id"][0, victim]) == 227
    assert int(row_v["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row_v["ref_t1"]["action_id"][0, attacker]) == 217
    assert int(row_v["ref_t1"]["action_id"][0, victim]) == 228
    assert int(row_v["ref_t1"]["hitlag"][0, attacker]) == 4
    assert int(row_v["ref_t1"]["hitlag"][0, victim]) == 4
    assert int(row_v["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row_v["ref_t1"]["hitstun"][0, victim]) == 0

    # Replay lock: CatchAttack/CaptureDamage anim-end return to CatchWait/CaptureWait.
    row_end = samples[record_anim_end : record_anim_end + 1]
    assert int(row_end["seed_t"]["action_id"][0, attacker]) == 217
    assert int(row_end["seed_t"]["action_id"][0, victim]) == 228
    assert int(row_end["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row_end["ref_t1"]["action_id"][0, attacker]) == 216
    assert int(row_end["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row_end["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row_end["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row_end["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row_end["ref_t1"]["hitstun"][0, victim]) == 0

    out_v, ref_v = _run_record(dataset_path, record_victim_enter)
    assert int(out_v["action_id"][0, attacker]) == int(ref_v["action_id"][attacker])
    assert int(out_v["action_id"][0, victim]) == int(ref_v["action_id"][victim])

    out_end, ref_end = _run_record(dataset_path, record_anim_end)
    assert int(out_end["action_id"][0, attacker]) == int(ref_end["action_id"][attacker])
    assert int(out_end["action_id"][0, victim]) == int(ref_end["action_id"][victim])
