from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


def _dataset(root: Path):
    path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not path.exists():
        pytest.skip(f"missing dataset: {path}")
    return read_dataset(str(path))


def _step_one(ds, record: int, *, mutate_input=None) -> np.void:
    binding = _load_binding()
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

        row = ds.samples[record]
        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input = np.array(row["prev_input_t"], dtype=INPUT_DTYPE).reshape(())
        cur_input = np.array(row["input_t"], dtype=INPUT_DTYPE).reshape(())
        if mutate_input is not None:
            mutate_input(prev_input, cur_input)
        prev_input_bytes[:] = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)
        input_bytes[:] = np.frombuffer(cur_input.tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _rollout_rows(ds, start_record: int, end_record_inclusive: int, *, mutate_seed=None) -> dict[int, np.void]:
    binding = _load_binding()
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

        samples = ds.samples
        seed_bytes[:] = np.frombuffer(samples[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        if mutate_seed is not None:
            seed = np.zeros((), dtype=samples[start_record]["seed_t"].dtype)
            seed[...] = samples[start_record]["seed_t"]
            mutate_seed(seed)
            seed_bytes[:] = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        binding.reseed_seed_rollout(handle, seed_bytes)

        out: dict[int, np.void] = {}
        for record in range(start_record, end_record_inclusive + 1):
            row = samples[record]
            prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, input_stride
            )
            input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out[record] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        return out
    finally:
        binding.destroy(handle)


def test_opening_lock_timer_one_allows_landing_jump_iasa_maj83() -> None:
    # MAJ:83 is the opening-countdown boundary behind a top SpecialN rollout cluster. The seeded
    # post-frame countdown is 1, which means fn_8016B7F8's clear occurs before the current input
    # that produces t+1, so Landing_IASA must see the XY edge and enter KneeBend.
    # refs/melee/src/melee/gm/gm_16AE.c::fn_8016B7F8
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    root = Path(__file__).resolve().parents[1]
    ds = _dataset(root)
    out = _step_one(ds, 83)
    ref = ds.samples[83]["ref_t1"]

    assert int(ds.samples[83]["seed_t"]["opening_input_lock_timer"][0]) == 1
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 24
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0


def test_opening_lock_timer_two_still_blocks_landing_jump_edge() -> None:
    # Negative boundary for the same owner: with two locked steps remaining, the current input is
    # still blanked by Fighter_UnkInitLoad_80068914_Inner1 and must not jump from Landing.
    root = Path(__file__).resolve().parents[1]
    ds = _dataset(root)

    def press_xy(prev_input: np.ndarray, cur_input: np.ndarray) -> None:
        prev_input["p"][0]["buttons"] = np.uint16(0)
        cur_input["p"][0]["buttons"] = np.uint16(0x0C00)
        cur_input["p"][0]["main_x"] = np.int8(127)

    out = _step_one(ds, 82, mutate_input=press_xy)

    assert int(ds.samples[82]["seed_t"]["opening_input_lock_timer"][0]) == 2
    assert int(out["action_id"][0]) == 42
    assert int(out["action_frame"][0]) == 7


def test_opening_lock_boundary_rollout_reaches_aerial_blaster_maj72() -> None:
    # Runtime rollout lock for the disruptive F19 entry point:
    # - seed at MAJ:72 is still inside the opening countdown,
    # - MAJ:83 must consume the first legal XY edge from Landing into KneeBend,
    # - MAJ:91 then resolves B as aerial SpecialN rather than grounded SpecialN.
    root = Path(__file__).resolve().parents[1]
    ds = _dataset(root)
    rows = _rollout_rows(ds, 72, 91)

    assert int(rows[83]["action_id"][0]) == int(ds.samples[83]["ref_t1"]["action_id"][0]) == 24
    assert int(rows[88]["action_id"][0]) == int(ds.samples[88]["ref_t1"]["action_id"][0]) == 25
    assert int(rows[91]["action_id"][0]) == int(ds.samples[91]["ref_t1"]["action_id"][0]) == 344
    assert int(rows[91]["on_ground"][0]) == int(ds.samples[91]["ref_t1"]["on_ground"][0]) == 0


def test_opening_lock_timer_one_rollout_requires_real_frame_boundary() -> None:
    # Negative for the rollout path: `reseed_seed_rollout` alone must not make every callback-local
    # timer=1 state legal. The seed starts at 2 because rollout reseed advances the opening
    # countdown once before the first step. Outside the VS-clear boundary shape, Fighter_UnkInitLoad
    # still blanks the XY edge.
    root = Path(__file__).resolve().parents[1]
    ds = _dataset(root)

    def move_off_boundary(seed: np.ndarray) -> None:
        seed["frame_id"] = np.int32(100)
        seed["opening_input_lock_timer"][0] = np.uint8(2)
        seed["action_frame"][0] = np.int16(6)

    rows = _rollout_rows(ds, 83, 83, mutate_seed=move_off_boundary)
    assert int(rows[83]["action_id"][0]) == 42
    assert int(rows[83]["action_frame"][0]) == 8
