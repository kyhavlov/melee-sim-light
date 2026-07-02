from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_escapeair_ecb_lock_seed_eq_ref_snapshot_rows_stay_in_escapeair() -> None:
    # Regression lock for the seed==ref action_id cluster:
    # - seed action_id == 236 (EscapeAir)
    # - ref_t1 action_id == 236
    # - previous approximation in src/mpcoll_ground.c landed early to 43.
    #
    # Locked rows from baseline triage (suite aggregate 236/236/43 count=6):
    # - AttachedGoodNaturedGuanaco.slpz rec=5724 p=0
    # - GracefulAttachedTurtle.slpz rec=7155 p=1
    # - GracefulAttachedTurtle.slpz rec=9388 p=0
    # - QuerulousGrandDinosaur.slpz rec=156 p=0
    # - QuerulousGrandDinosaur.slpz rec=197 p=0
    # - QuerulousGrandDinosaur.slpz rec=1246 p=0
    root = Path(__file__).resolve().parents[1]
    base = "replays/validation/cardinal_1.0_recent"
    cases = [
        (f"{base}/AttachedGoodNaturedGuanaco.slpz", 5724, 0),
        (f"{base}/GracefulAttachedTurtle.slpz", 7155, 1),
        (f"{base}/GracefulAttachedTurtle.slpz", 9388, 0),
        (f"{base}/QuerulousGrandDinosaur.slpz", 156, 0),
        (f"{base}/QuerulousGrandDinosaur.slpz", 197, 0),
        (f"{base}/QuerulousGrandDinosaur.slpz", 1246, 0),
    ]

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

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for rel, record, p in cases:
        dataset_path = root / rel
        if not dataset_path.exists():
            pytest.skip(f"missing local replay: {rel}")

        ds = load_replay_buffers(str(dataset_path))
        samples = ds.rows
        assert int(samples.shape[0]) > record, f"replay too short for regression check: {rel}"
        row = samples[record : record + 1]

        assert int(row["seed_t"]["action_id"][0, p]) == 236
        assert int(row["ref_t1"]["action_id"][0, p]) == 236
        assert int(row["seed_t"]["on_ground"][0, p]) == 0
        assert int(row["ref_t1"]["on_ground"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

        handle = binding.init(batch_size=1, num_players=int(ds.num_players))
        try:
            seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
            prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            input_bytes = np.empty((1, input_stride), dtype=np.uint8)
            out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

            seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, seed_stride
            )
            prev_input_bytes[:] = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, input_stride
            )

            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
            assert int(out["action_id"][p]) == 236, f"{rel} rec={record} p={p}"
            assert int(out["on_ground"][p]) == 0, f"{rel} rec={record} p={p}"
            assert int(out["ground_id"][p]) == int(row["ref_t1"]["ground_id"][0, p]), f"{rel} rec={record} p={p}"
        finally:
            binding.destroy(handle)

