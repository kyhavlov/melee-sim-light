from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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


def _run_rollout_records(
    dataset_path: Path, start_record: int, records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(records), "dataset too short for rollout regression check"
    assert start_record <= min(records), "rollout start must be <= first checked record"

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for rec in range(start_record, max(records) + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if rec in records:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                ref = row["ref_t1"].reshape(-1)[0].copy()
                out_by_record[rec] = (out, ref)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "attacker", "victim", "expected_thrown"),
    [
        # From --debug-float offenders: seed_t.action_id[victim]==227 (CaptureWait) -> Thrown*
        ("AttachedGoodNaturedGuanaco.msl", 4185, 1, 0, 241),
        ("QuerulousGrandDinosaur.msl", 8279, 1, 0, 240),
    ],
)
def test_capturewait_to_nonlow_thrown_entry_uses_static_attachment_offsets(
    dataset_name: str,
    record: int,
    attacker: int,
    victim: int,
    expected_thrown: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, victim]) == 227
    assert int(row["ref_t1"]["action_id"][0, victim]) == expected_thrown
    assert int(row["seed_t"]["grab_owner_port"][0, victim]) == attacker
    for p in (0, 1):
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out, ref = _run_record(dataset_path, record)

    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])

    # Decomp lock: ftCo_800DE3FC installs ftCo_800DE508, which applies fp->x1A70 from static
    # TransN-XRotN instead of preserving the pre-entry CaptureWait world position.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
    seed_x = float(row["seed_t"]["pos_x"][0, victim])
    seed_y = float(row["seed_t"]["pos_y"][0, victim])
    got_x = float(out["pos_x"][0, victim])
    got_y = float(out["pos_y"][0, victim])
    ref_x = float(ref["pos_x"][victim])
    ref_y = float(ref["pos_y"][victim])
    assert abs(got_x - seed_x) > 0.10
    assert abs(got_y - seed_y) > 1.0
    assert abs(got_x - ref_x) <= 0.05, (
        f"{dataset_name} record={record} p={victim} pos_x err too large: "
        f"got={got_x:.8g} ref={ref_x:.8g}"
    )
    assert abs(got_y - ref_y) <= 0.05, (
        f"{dataset_name} record={record} p={victim} pos_y err too large: "
        f"got={got_y:.8g} ref={ref_y:.8g}"
    )


@pytest.mark.integration
def test_throwhi_attached_rollout_uses_float_aobj_anchor() -> None:
    # Replay-real positive for non-low thrown attachment:
    # - ftCo_800DE508 samples the live XRotN JObj through lb_8000B1CC.
    # - HSD_AObjInterpretAnim owns the fractional ThrowHi joint path; integer SSANIM01 interpolation
    #   drifts on the frame-6/7 attached window and carries into release placement.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out_by_record = _run_rollout_records(dataset_path, 8392, (8407, 8408, 8411))
    for record, (out, ref) in out_by_record.items():
        assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 241, record  # ThrownHi
        assert int(out["animation_index"][1]) == int(ref["animation_index"][1])
        assert abs(float(out["pos_x"][1]) - float(ref["pos_x"][1])) <= 1.0e-4
        assert abs(float(out["pos_y"][1]) - float(ref["pos_y"][1])) <= 1.0e-4


@pytest.mark.integration
def test_throwlw_low_throw_keeps_existing_attachment_anchor_boundary() -> None:
    # Replay-real negative for the non-low float-AObj anchor:
    # - ThrownLw has a separate retained low-throw attachment owner around the frame-25 TransN2
    #   vertical handoff. Broadening the non-low float anchor into this slice moved contact/hitlag
    #   earlier in rollout.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out_by_record = _run_rollout_records(dataset_path, 8116, (8121,))
    out, ref = out_by_record[8121]
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 242  # ThrownLw
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 0
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 0


@pytest.mark.integration
def test_throwhi_release_rollout_applies_current_frame_di_and_damage_gravity() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out_by_record = _run_rollout_records(dataset_path, 5861, (5869, 5908))

    out_release, ref_release = out_by_record[5869]
    assert int(out_release["action_id"][1]) == int(ref_release["action_id"][1]) == 90
    assert int(out_release["animation_index"][1]) == int(ref_release["animation_index"][1])
    assert abs(float(out_release["speed_x_attack"][1]) - float(ref_release["speed_x_attack"][1])) <= 0.01
    assert abs(float(out_release["speed_y_attack"][1]) - float(ref_release["speed_y_attack"][1])) <= 0.06
    assert float(out_release["speed_y_self"][1]) == pytest.approx(float(ref_release["speed_y_self"][1]))

    out_follow, ref_follow = out_by_record[5908]
    assert int(out_follow["action_id"][0]) == int(ref_follow["action_id"][0]) == 213
    assert int(out_follow["action_id"][1]) == int(ref_follow["action_id"][1]) == 223
    assert abs(float(out_follow["pos_x"][0]) - float(ref_follow["pos_x"][0])) <= 0.001
    assert abs(float(out_follow["pos_x"][1]) - float(ref_follow["pos_x"][1])) <= 0.001
