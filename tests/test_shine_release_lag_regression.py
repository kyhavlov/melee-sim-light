from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_shine_loop_release_lag_holds_loop_until_countdown_expires() -> None:
    # Regression lock for seed==ref action_id triple:
    # - seed/ref/out was 361/361/363 at TBK rec=4455 p=1
    # - decomp model uses {releaseLag,isRelease}; Loop exits only when (releaseLag<=0 && isRelease).
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwHit_Check}
    root = Path(__file__).resolve().parents[1]
    rel = "replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
    record = 4455
    p = 1

    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 361
    assert int(row["ref_t1"]["action_id"][0, p]) == 361
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["shine_is_release"][0, p]) == 1
    assert int(row["seed_t"]["shine_release_lag"][0, p]) > 0

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
        assert int(out["action_id"][p]) == 361
        assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][0, p])
        assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_shine_turn_to_loop_followup_does_not_double_tick_release_lag_hvg() -> None:
    # HVG 2923 -> 2937 runs Shine Loop -> Turn -> Loop while B is released. Turn_Anim owns one
    # releaseLag tick before ftFx_SpecialLwHit_Check returns to Loop; the same-frame destination
    # Loop IASA may run, but Loop_Anim must not double-tick the hidden releaseLag or End starts a
    # frame early.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Check,ftFx_SpecialLwLoop_IASA}
    root = Path(__file__).resolve().parents[1]
    rel = "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz"
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    ds = load_replay_buffers(str(dataset_path))
    start_record = 2658
    loop_record = 2936
    end_record = 2937
    p = 1

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(
            handle,
            np.frombuffer(ds.rows[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride),
        )
        loop_out = None
        end_out = None
        for record in range(start_record, end_record + 1):
            row = ds.rows[record]
            binding.step_input(
                handle,
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride),
            )
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            if record == loop_record:
                loop_out = out
            if record == end_record:
                end_out = out

        assert loop_out is not None
        assert end_out is not None
        loop_ref = ds.rows[loop_record]["ref_t1"]
        end_ref = ds.rows[end_record]["ref_t1"]
        assert int(loop_out["action_id"][p]) == int(loop_ref["action_id"][p]) == 361
        assert int(loop_out["action_frame"][p]) == int(loop_ref["action_frame"][p]) == 10
        assert int(end_out["action_id"][p]) == int(end_ref["action_id"][p]) == 363
        assert int(end_out["action_frame"][p]) == int(end_ref["action_frame"][p]) == 0
    finally:
        binding.destroy(handle)
