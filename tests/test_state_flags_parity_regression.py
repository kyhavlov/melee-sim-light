from __future__ import annotations

import importlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    byte: int
    seed_byte: int
    ref_byte: int
    note: str
    seed_action_id: int | None = None
    ref_action_id: int | None = None
    seed_guard_reflect_timer_x14: int | None = None


def _run_case(c: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / c.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {c.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > c.record, f"dataset too short: num_records={num_records} record={c.record}"

    row = samples[c.record : c.record + 1]

    # Replay-real preconditions (seed vs ref bytes at t/t+1).
    got_seed = int(row["seed_t"]["state_flags"][0, c.p, c.byte])
    got_ref = int(row["ref_t1"]["state_flags"][0, c.p, c.byte])
    assert got_seed == c.seed_byte, f"{c.note}: seed byte mismatch: got={got_seed:#04x} want={c.seed_byte:#04x}"
    assert got_ref == c.ref_byte, f"{c.note}: ref byte mismatch: got={got_ref:#04x} want={c.ref_byte:#04x}"

    if c.seed_action_id is not None:
        got = int(row["seed_t"]["action_id"][0, c.p])
        assert got == c.seed_action_id, f"{c.note}: seed action_id mismatch: got={got:#06x} want={c.seed_action_id:#06x}"
    if c.ref_action_id is not None:
        got = int(row["ref_t1"]["action_id"][0, c.p])
        assert got == c.ref_action_id, f"{c.note}: ref action_id mismatch: got={got:#06x} want={c.ref_action_id:#06x}"
    if c.seed_guard_reflect_timer_x14 is not None:
        got = int(row["seed_t"]["guard_reflect_timer_x14"][0, c.p])
        assert got == c.seed_guard_reflect_timer_x14, (
            f"{c.note}: seed guard_reflect_timer_x14 mismatch: got={got} want={c.seed_guard_reflect_timer_x14}"
        )

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["state_flags"][0, c.p, c.byte])
        assert got == c.ref_byte, (
            f"{c.note}: record={c.record} p={c.p} byte={c.byte} "
            f"expected out==ref=={c.ref_byte:#04x}, got {got:#04x}"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "c",
    [
        # 0x221A (state_flags[1]) bit 0x08 isFastFalling.
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=131,
            p=0,
            byte=1,
            seed_byte=0x00,
            ref_byte=0x08,
            note="0x221A isFastFalling (0->0x08)",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
            ),
            record=159,
            p=1,
            byte=1,
            seed_byte=0x08,
            ref_byte=0x00,
            note="0x221A isFastFalling (0x08->0)",
        ),
        # 0x221B (state_flags[2]) bit 0x80 isShieldActive.
        #
        # Decomp: GuardReflect entry clears fp->x221B_b0 (shield active) until the reflect window
        # timer expires and the shield desc is recreated. See src/shields.c for references.
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=113,
            p=1,
            byte=2,
            seed_byte=0x00,
            ref_byte=0x00,
            note="0x221B isShieldActive during GuardReflect reflect-window (expect 0)",
            seed_action_id=0x00B6,
            ref_action_id=0x00B6,
            seed_guard_reflect_timer_x14=2,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=429,
            p=1,
            byte=2,
            seed_byte=0x00,
            ref_byte=0x00,
            note="0x221B isShieldActive during GuardReflect reflect-window (expect 0) (alt)",
            seed_action_id=0x00B6,
            ref_action_id=0x00B6,
            seed_guard_reflect_timer_x14=2,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=1598,
            p=1,
            byte=2,
            seed_byte=0x80,
            ref_byte=0x80,
            note="0x221B isShieldActive stays set during GuardReflect reflect-window (expect 0x80)",
            seed_action_id=0x00B6,
            ref_action_id=0x00B6,
            seed_guard_reflect_timer_x14=2,
        ),
        # 0x221C (state_flags[3]) bit 0x02 isHitstun.
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=3586,
            p=1,
            byte=3,
            seed_byte=0x02,
            ref_byte=0x00,
            note="0x221C isHitstun clears with hitstun end (0x02->0)",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=6869,
            p=0,
            byte=3,
            seed_byte=0x02,
            ref_byte=0x00,
            note="0x221C isHitstun clears with hitstun end (0x02->0) (alt)",
        ),
    ],
)
def test_state_flags_parity_regression(c: _Case) -> None:
    _run_case(c)
