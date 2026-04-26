from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_BASE_REL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_AGG_REL = "datasets/aggregate_recent/replays/validation/aggregate_recent"
_BUTTON_Z = 0x0010
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


def _button_edge(row: np.ndarray, port: int, mask: int) -> bool:
    cur = int(row["input_t"]["p"][0, port]["buttons"])
    prev = int(row["prev_input_t"]["p"][0, port]["buttons"])
    return (cur & mask) != 0 and (prev & mask) == 0


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
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
def test_replay_wait_to_catch_iasa_record_1176_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/AttachedGoodNaturedGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    port = 1
    record = 1176
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, port]) == 14
    assert int(row["seed_t"]["action_frame"][0, port]) == 0
    assert int(row["seed_t"]["animation_index"][0, port]) == 2
    assert int(row["seed_t"]["grab_owner_port"][0, port]) == 0xFF
    assert int(row["ref_t1"]["action_id"][0, port]) == 212
    assert int(row["ref_t1"]["action_frame"][0, port]) == 0
    assert int(row["ref_t1"]["animation_index"][0, port]) == 242
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 1]) == 0
    assert _button_edge(row, port, _BUTTON_Z)

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, port]) == int(ref["action_id"][port])
    assert int(out["action_frame"][0, port]) == int(ref["action_frame"][port])
    assert int(out["animation_index"][0, port]) == int(ref["animation_index"][port])


@pytest.mark.integration
def test_replay_catch_connect_escapeb_victim_record_6100_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    attacker = 0
    victim = 1
    record = 6100
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, attacker]) == 212
    assert int(row["seed_t"]["action_id"][0, victim]) == 234
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == 0xFF
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 213
    assert int(row["ref_t1"]["action_id"][0, victim]) == 226
    assert int(row["ref_t1"]["action_frame"][0, victim]) == 1
    assert int(row["ref_t1"]["animation_index"][0, victim]) == 254
    assert int(row["seed_t"]["on_ground"][0, victim]) == 1
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][0, victim]) == int(ref["action_frame"][victim])
    assert int(out["animation_index"][0, victim]) == int(ref["animation_index"][victim])


@pytest.mark.integration
def test_replay_capturepulled_to_capturewait_record_6101_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    attacker = 0
    victim = 1
    record = 6101
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay-real lock for the CapturePulledLw -> CaptureWaitLw handoff:
    # - strict discrete parity on owner/victim action progression and no hitlag/hitstun side effects
    # - float lanes are context-only (finite/shape guard).
    assert int(row["seed_t"]["action_id"][0, attacker]) == 213
    assert int(row["seed_t"]["action_id"][0, victim]) == 226
    assert int(row["seed_t"]["action_frame"][0, victim]) == 1
    assert int(row["seed_t"]["animation_index"][0, victim]) == 254
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row["seed_t"]["on_ground"][0, victim]) == 1
    assert int(row["seed_t"]["hitlag"][0, attacker]) == 0
    assert int(row["seed_t"]["hitlag"][0, victim]) == 0
    assert int(row["seed_t"]["hitstun"][0, attacker]) == 0
    assert int(row["seed_t"]["hitstun"][0, victim]) == 0
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 216
    assert int(row["ref_t1"]["action_frame"][0, attacker]) == 0
    assert int(row["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row["ref_t1"]["action_frame"][0, victim]) == 1
    assert int(row["ref_t1"]["animation_index"][0, victim]) == 255
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_frame"][0, attacker]) == int(ref["action_frame"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][0, victim]) == int(ref["action_frame"][victim])
    assert int(out["animation_index"][0, victim]) == int(ref["animation_index"][victim])
    assert float(out["pos_x"][0, victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=1e-6)
    assert float(out["pos_y"][0, victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-5)


@pytest.mark.integration
def test_replay_capturepulled_stays_pulled_record_408_negative_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    attacker = 0
    victim = 1
    record = 408
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Negative lock: victim is already in CapturePulledLw and should remain there on t+1.
    # This guards the CapturePulled->CaptureWait snapshot bridge from over-broad application.
    assert int(row["seed_t"]["action_id"][0, attacker]) == 213
    assert int(row["seed_t"]["action_id"][0, victim]) == 226
    assert int(row["seed_t"]["action_frame"][0, victim]) == 1
    assert int(row["seed_t"]["animation_index"][0, victim]) == 254
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == attacker
    assert int(row["seed_t"]["on_ground"][0, victim]) == 1
    assert int(row["seed_t"]["hitlag"][0, attacker]) == 0
    assert int(row["seed_t"]["hitlag"][0, victim]) == 0
    assert int(row["seed_t"]["hitstun"][0, attacker]) == 0
    assert int(row["seed_t"]["hitstun"][0, victim]) == 0
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 213
    assert int(row["ref_t1"]["action_frame"][0, attacker]) == 7
    assert int(row["ref_t1"]["action_id"][0, victim]) == 226
    assert int(row["ref_t1"]["action_frame"][0, victim]) == 2
    assert int(row["ref_t1"]["animation_index"][0, victim]) == 254
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim]) == 0
    assert int(row["ref_t1"]["hitstun"][0, attacker]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_frame"][0, attacker]) == int(ref["action_frame"][attacker])
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][0, victim]) == int(ref["action_frame"][victim])
    assert int(out["animation_index"][0, victim]) == int(ref["animation_index"][victim])
    assert float(out["pos_x"][0, victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=1e-6)
    assert float(out["pos_y"][0, victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-5)


@pytest.mark.integration
@pytest.mark.parametrize(
    "dataset_name,record,owner,victim",
    [
        ("GracefulAttachedTurtle.msl", 446, 0, 1),
        ("TreasuredBackKangaroo.msl", 5771, 0, 1),
        ("TreasuredBackKangaroo.msl", 5910, 0, 1),
    ],
)
def test_replay_capturepulledhi_grounded_handoff_enters_capturewaitlw_strict_lock(
    dataset_name: str, record: int, owner: int, victim: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay-real handoff shape:
    # - owner CatchPull frame-7 grounded -> CatchWait
    # - victim CapturePulledHi frame-2 -> CaptureWaitLw frame-1
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   fn_800DA1D8,ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8
    # }
    assert int(row["seed_t"]["action_id"][0, owner]) == 213
    assert int(row["seed_t"]["action_frame"][0, owner]) == 7
    assert int(row["seed_t"]["on_ground"][0, owner]) == 1
    assert int(row["seed_t"]["action_id"][0, victim]) == 223
    assert int(row["seed_t"]["action_frame"][0, victim]) == 2
    assert int(row["seed_t"]["animation_index"][0, victim]) == 251
    assert int(row["seed_t"]["on_ground"][0, victim]) == 0
    assert int(row["seed_t"]["ground_id"][0, victim]) != 0xFFFF
    assert int(row["ref_t1"]["action_id"][0, owner]) == 216
    assert int(row["ref_t1"]["action_frame"][0, owner]) == 0
    assert int(row["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row["ref_t1"]["action_frame"][0, victim]) == 1
    assert int(row["ref_t1"]["animation_index"][0, victim]) == 255
    assert int(row["ref_t1"]["on_ground"][0, victim]) == 1
    assert int(row["ref_t1"]["jumps_left"][0, victim]) == 2

    out, ref = _run_record(dataset_path, record)

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "hitstun",
        "ground_id",
        "instance_id",
    ):
        assert int(out[field][0, victim]) == int(ref[field][victim]), (
            f"{dataset_name} rec={record} victim={victim} field={field} "
            f"expected={int(ref[field][victim])} got={int(out[field][0, victim])}"
        )

    # The floor-mask handoff owns both the low capture state and ftCommon_8007D7FC's jump reset.
    assert int(out["jumps_left"][0, victim]) == int(ref["jumps_left"][victim])

    got_flags = tuple(int(x) for x in out["state_flags"][0, victim].tolist())
    exp_flags = tuple(int(x) for x in ref["state_flags"][victim].tolist())
    assert got_flags == exp_flags

    assert float(out["pos_x"][0, victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=1e-4)
    assert float(out["pos_y"][0, victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-4)


@pytest.mark.integration
def test_replay_capturepulledhi_grounded_handoff_resets_jump_count_from_floor_mask() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_AGG_REL}/MotionlessAggressiveJay.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    owner = 1
    victim = 0
    record = 6470
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Top disruptive rollout entry point: airborne CapturePulledHi still has one jump left in
    # seed, while the replay's grounded CaptureWaitLw result has two. The reset is owned by
    # CaptureWaitHi_Coll -> ft_80083C00 -> ft_80082578 -> mpColl_800477E0's floor result before
    # fn_800DBBF8 calls ftCommon_8007D7FC.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8
    # }
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_80082578}
    assert int(row["seed_t"]["action_id"][0, owner]) == 213
    assert int(row["seed_t"]["action_frame"][0, owner]) == 7
    assert int(row["seed_t"]["on_ground"][0, owner]) == 1
    assert int(row["seed_t"]["action_id"][0, victim]) == 223
    assert int(row["seed_t"]["action_frame"][0, victim]) == 2
    assert int(row["seed_t"]["on_ground"][0, victim]) == 0
    assert int(row["seed_t"]["jumps_left"][0, victim]) == 1
    assert int(row["ref_t1"]["action_id"][0, victim]) == 227
    assert int(row["ref_t1"]["action_frame"][0, victim]) == 0
    assert int(row["ref_t1"]["on_ground"][0, victim]) == 1
    assert int(row["ref_t1"]["jumps_left"][0, victim]) == 2

    out, ref = _run_record(dataset_path, record)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "hitstun",
        "ground_id",
    ):
        assert int(out[field][0, victim]) == int(ref[field][victim]), (
            f"MotionlessAggressiveJay rec={record} victim={victim} field={field} "
            f"expected={int(ref[field][victim])} got={int(out[field][0, victim])}"
        )

    assert int(out["jumps_left"][0, victim]) == int(ref["jumps_left"][victim])
    assert float(out["pos_x"][0, victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=1e-4)
    assert float(out["pos_y"][0, victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    "dataset_name,record,victim",
    [
        ("BlondHardHippopotamus.msl", 508, 0),
        ("BlondHardHippopotamus.msl", 557, 0),
        ("BlondHardHippopotamus.msl", 1235, 1),
        ("BlondHardHippopotamus.msl", 1278, 1),
        ("BlondHardHippopotamus.msl", 4366, 1),
        ("PutridJoyousOryx.msl", 290, 0),
    ],
)
def test_replay_capturepulledhi_fox_without_floor_mask_stays_capturewaithi(
    dataset_name: str, record: int, victim: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_AGG_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # These Fox rows exposed the old broad prior-ground shortcut: stale ground_id existed, but the
    # true mpColl_800477E0 floor-mask path does not fire, so the victim remains airborne
    # CaptureWaitHi and must not receive the ftCommon_8007D7FC jump reset.
    assert int(row["seed_t"]["char_id"][0, victim]) == 1
    assert int(row["seed_t"]["action_id"][0, victim]) == 223
    assert int(row["ref_t1"]["action_id"][0, victim]) == 224
    assert int(row["ref_t1"]["on_ground"][0, victim]) == 0

    out, ref = _run_record(dataset_path, record)
    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert int(out["on_ground"][0, victim]) == int(ref["on_ground"][victim])
    assert int(out["jumps_left"][0, victim]) == int(ref["jumps_left"][victim])


@pytest.mark.integration
@pytest.mark.parametrize(
    "record,victim,exp_ref_action",
    [
        (2693, 0, 224),  # Held-A owner context: stay in CaptureWaitHi (no low-lane bridge).
        (5815, 1, 241),  # Adjacent throw-up branch remains owner-driven.
        (5863, 1, 241),  # Adjacent throw-up branch remains owner-driven.
    ],
)
def test_replay_capturepulledhi_grounded_handoff_context_controls(
    record: int, victim: int, exp_ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, victim]) == 223
    assert int(row["ref_t1"]["action_id"][0, victim]) == exp_ref_action

    out, ref = _run_record(dataset_path, record)
    out_action = int(out["action_id"][0, victim])
    ref_action = int(ref["action_id"][victim])
    assert out_action == ref_action == exp_ref_action
    assert np.isfinite(float(out["pos_x"][0, victim]))
    assert np.isfinite(float(out["pos_y"][0, victim]))
