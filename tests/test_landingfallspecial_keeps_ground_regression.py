from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_landingfallspecial_keeps_ground_contact_one_step() -> None:
    # Regression for a common seed==ref on_ground mismatch cluster:
    # - seed_t.on_ground == ref_t1.on_ground == 1
    # - ref_t1.ground_id == 1 (FD main floor segment)
    # But sim previously dropped to airborne at t+1 (out.on_ground == 0).
    #
    # Representative records (seed==ref at t, ref stays grounded at t+1):
    # - AttachedGoodNaturedGuanaco.msl record 438 p=1
    # - GracefulAttachedTurtle.msl record 750 p=0
    # - QuerulousGrandDinosaur.msl record 886 p=0
    # - TreasuredBackKangaroo.msl record 356 p=0
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
        (f"{base}/AttachedGoodNaturedGuanaco.msl", 438, 1),
        (f"{base}/GracefulAttachedTurtle.msl", 750, 0),
        (f"{base}/QuerulousGrandDinosaur.msl", 886, 0),
        (f"{base}/TreasuredBackKangaroo.msl", 356, 0),
    ]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for rel, record, p in cases:
        dataset_path = root / rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {rel}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        num_records = int(samples.shape[0])
        assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

        chunk_view = samples[record : record + 1]
        assert int(chunk_view.shape[0]) == 1

        # Seed/ref assertions (record t and ref t+1 state).
        assert int(chunk_view["seed_t"]["on_ground"][0, p]) == 1
        assert int(chunk_view["ref_t1"]["on_ground"][0, p]) == 1

        # These cases are wavedash-style transitions: KneeBend (jump squat) at t, then
        # EscapeAir landing into LandingFallSpecial by t+1.
        assert int(chunk_view["seed_t"]["action_id"][0, p]) == 24
        assert int(chunk_view["ref_t1"]["action_id"][0, p]) == 43

        assert int(chunk_view["ref_t1"]["ground_id"][0, p]) == 1

        assert int(chunk_view["ref_t1"]["hitlag"][0, p]) == 0
        assert int(chunk_view["ref_t1"]["hitstun"][0, p]) == 0

        handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
        try:
            seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
            prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

            seed_bytes[:] = np.frombuffer(
                chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, seed_stride)
            prev_input_bytes[:] = np.frombuffer(
                chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes[:] = np.frombuffer(
                chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)

            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
            ref = chunk_view["ref_t1"][0]

            assert int(out["hitlag"][0, p]) == 0
            assert int(out["hitstun"][0, p]) == 0

            assert int(out["on_ground"][0, p]) == 1, f"{rel} record={record} p={p} dropped airborne"
            assert int(out["ground_id"][0, p]) == 1, f"{rel} record={record} p={p} wrong ground_id"

            out_action = int(out["action_id"][0, p])
            assert out_action == 43, f"{rel} record={record} p={p} expected action_id=43, got {out_action}"

            out_anim = int(out["animation_index"][0, p])
            ref_anim = int(ref["animation_index"][p])
            assert (
                out_anim == ref_anim
            ), f"{rel} record={record} p={p} expected animation_index={ref_anim}, got {out_anim}"
        finally:
            binding.destroy(handle)
