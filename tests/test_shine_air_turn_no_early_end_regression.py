from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_shine_air_turn_does_not_drop_to_end_one_step_early() -> None:
    # Regression for a common one-step mismatch cluster:
    # - seed_t.action_id == ref_t1.action_id == 369 (SpecialAirLwTurn)
    # - out.action_id becomes 368 (SpecialAirLwEnd) one step early.
    #
    # Representative records (seed==ref at t, ref stays in TURN at t+1):
    # - TreasuredBackKangaroo.slpz record 943 p=0
    # - AttachedGoodNaturedGuanaco.slpz record 2741 p=1
    root = Path(__file__).resolve().parents[1]
    base = "replays/validation/cardinal_1.0_recent"

    cases = [
        (f"{base}/TreasuredBackKangaroo.slpz", 943, 0),
        (f"{base}/AttachedGoodNaturedGuanaco.slpz", 2741, 1),
    ]

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
        num_records = int(samples.shape[0])
        assert num_records > record, f"replay too short for regression check: num_records={num_records}"

        chunk_view = samples[record : record + 1]
        assert int(chunk_view.shape[0]) == 1

        # Seed/ref assertions (record t and ref t+1 state).
        assert int(chunk_view["seed_t"]["action_id"][0, p]) == 369
        assert int(chunk_view["ref_t1"]["action_id"][0, p]) == 369

        handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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

            out_action = int(out["action_id"][0, p])
            ref_action = int(ref["action_id"][p])
            assert (
                out_action == ref_action
            ), f"{rel} record={record} p={p} expected action_id={ref_action}, got {out_action}"

            out_anim = int(out["animation_index"][0, p])
            ref_anim = int(ref["animation_index"][p])
            assert (
                out_anim == ref_anim
            ), f"{rel} record={record} p={p} expected animation_index={ref_anim}, got {out_anim}"

            out_af = int(out["action_frame"][0, p])
            ref_af = int(ref["action_frame"][p])
            assert (
                out_af == ref_af
            ), f"{rel} record={record} p={p} expected action_frame={ref_af}, got {out_af}"
        finally:
            binding.destroy(handle)

