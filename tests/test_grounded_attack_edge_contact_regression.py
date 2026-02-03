from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_grounded_attacks_do_not_spuriously_drop_airborne_near_fd_edge() -> None:
    # Regression for a remaining dominant seed==ref on_ground mismatch cluster:
    # - seed_t.on_ground == ref_t1.on_ground == 1
    # - ref_t1.action_id is a grounded attack (AttackDash / AttackS4S)
    # But the sim previously dropped airborne at t+1 (out.on_ground == 0) near the FD floor edge.
    #
    # Representative offenders:
    # - AttachedGoodNaturedGuanaco.msl record 2318 p=1 (AttackDash = 50)
    # - GracefulAttachedTurtle.msl record 3023 p=1 (AttackS4S  = 60)
    # - QuerulousGrandDinosaur.msl record 9305 p=1 (AttackDash = 50)
    root = Path(__file__).resolve().parents[1]
    base = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"

    # Integration policy: skip if required ISO-derived data artifacts are missing locally.
    required = [
        "data/anims_ecb/fox.bin",
        "data/anims_ecb/falco.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/falco_extents.bin",
    ]
    missing = [path for path in required if not (root / path).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")

    cases = [
        (f"{base}/AttachedGoodNaturedGuanaco.msl", 2318, 1, 2),
        (f"{base}/GracefulAttachedTurtle.msl", 3023, 1, 0),
        (f"{base}/QuerulousGrandDinosaur.msl", 9305, 1, 2),
    ]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for rel, record, p, expected_ground_id in cases:
        dataset_path = root / rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {rel}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        num_records = int(samples.shape[0])
        assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

        chunk_view = samples[record : record + 1]
        assert int(chunk_view.shape[0]) == 1

        seed = chunk_view["seed_t"][0]
        ref = chunk_view["ref_t1"][0]

        # Preconditions: the replay stays grounded across t->t+1 for these records.
        assert int(seed["on_ground"][p]) == 1
        assert int(ref["on_ground"][p]) == 1

        seed_action = int(seed["action_id"][p])
        ref_action = int(ref["action_id"][p])
        assert seed_action == ref_action
        assert seed_action in (50, 60)

        assert int(ref["hitlag"][p]) == 0
        assert int(ref["hitstun"][p]) == 0
        assert int(ref["ground_id"][p]) == expected_ground_id

        handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
        try:
            seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
            prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

            seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, seed_stride
            )
            prev_input_bytes[:] = np.frombuffer(
                chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, input_stride
            )

            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

            assert int(out["on_ground"][0, p]) == 1, f"{rel} record={record} p={p} dropped airborne"
            assert int(out["ground_id"][0, p]) == int(ref["ground_id"][p]), (
                f"{rel} record={record} p={p} expected ground_id={int(ref['ground_id'][p])}, "
                f"got {int(out['ground_id'][0, p])}"
            )

            assert int(out["action_id"][0, p]) == ref_action, (
                f"{rel} record={record} p={p} expected action_id={ref_action}, got {int(out['action_id'][0, p])}"
            )

            out_anim = int(out["animation_index"][0, p])
            ref_anim = int(ref["animation_index"][p])
            assert out_anim == ref_anim, (
                f"{rel} record={record} p={p} expected animation_index={ref_anim}, got {out_anim}"
            )
        finally:
            binding.destroy(handle)


@pytest.mark.integration
def test_run_off_fd_ledge_still_enters_fall() -> None:
    # Negative regression: locomotion states should still be able to walk/run off the FD ledge.
    # We gate floor-edge snap to actions that use ft_80084104 (grounded attacks + Down*), so Run
    # should still become airborne (Fall) when leaving the floor.
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    required = [
        "data/anims_ecb/fox.bin",
        "data/anims_ecb/falco.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/falco_extents.bin",
    ]
    missing = [path for path in required if not (root / path).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")

    record = 3174
    p = 1

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    chunk_view = samples[record : record + 1]
    assert int(chunk_view.shape[0]) == 1

    seed = chunk_view["seed_t"][0]
    ref = chunk_view["ref_t1"][0]

    assert int(seed["on_ground"][p]) == 1
    assert int(ref["on_ground"][p]) == 0

    assert int(seed["action_id"][p]) == 21  # MSL_ACT_RUN
    assert int(ref["action_id"][p]) == 29  # MSL_ACT_FALL
    assert int(ref["hitlag"][p]) == 0
    assert int(ref["hitstun"][p]) == 0

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

        seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        assert int(out["on_ground"][0, p]) == 0, f"{rel} record={record} p={p} did not go airborne"

        out_action = int(out["action_id"][0, p])
        assert out_action == 29, f"{rel} record={record} p={p} expected action_id=29 (Fall), got {out_action}"
    finally:
        binding.destroy(handle)
