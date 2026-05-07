from __future__ import annotations

import importlib
import json
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
    seed_hitlag: int | None = None
    ref_hitlag: int | None = None
    seed_hitstun: int | None = None
    ref_hitstun: int | None = None


def _allow_interrupt_active_from_moves(root: Path, char_id: int, action_id: int, action_frame: int) -> int:
    if int(char_id) == 1:
        move_file = root / "data/moves/fox.json"
    elif int(char_id) == 22:
        move_file = root / "data/moves/falco.json"
    else:
        raise AssertionError(f"unsupported char_id for allow_interrupt check: {char_id}")

    move_key_by_action = {
        0x0032: "ftCo_SM_AttackDash",
        0x0043: "ftCo_SM_AttackAirB",
    }
    move_key = move_key_by_action.get(int(action_id))
    if move_key is None:
        raise AssertionError(f"unsupported action_id for allow_interrupt check: {action_id:#06x}")

    d = json.loads(move_file.read_text())
    events = d["moves"][move_key]["events"]
    on_frame = None
    for e in events:
        if e.get("kind") == "allow_interrupt":
            on_frame = int(e["frame"])
            break
    if on_frame is None:
        raise AssertionError(f"missing allow_interrupt event in {move_file} for {move_key}")
    return 1 if int(action_frame) >= on_frame else 0


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
    if c.seed_hitlag is not None:
        got = int(row["seed_t"]["hitlag"][0, c.p])
        assert got == c.seed_hitlag, f"{c.note}: seed hitlag mismatch: got={got} want={c.seed_hitlag}"
    if c.ref_hitlag is not None:
        got = int(row["ref_t1"]["hitlag"][0, c.p])
        assert got == c.ref_hitlag, f"{c.note}: ref hitlag mismatch: got={got} want={c.ref_hitlag}"
    if c.seed_hitstun is not None:
        got = int(row["seed_t"]["hitstun"][0, c.p])
        assert got == c.seed_hitstun, f"{c.note}: seed hitstun mismatch: got={got} want={c.seed_hitstun}"
    if c.ref_hitstun is not None:
        got = int(row["ref_t1"]["hitstun"][0, c.p])
        assert got == c.ref_hitstun, f"{c.note}: ref hitstun mismatch: got={got} want={c.ref_hitstun}"

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
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=367,
            p=1,
            byte=2,
            seed_byte=0x80,
            ref_byte=0x00,
            note="0x221B isShieldActive clears on captured-victim transition (GuardOn->CapturePulledLw)",
            seed_action_id=0x00B2,
            ref_action_id=0x00E2,
            seed_hitlag=0,
            ref_hitlag=0,
            seed_hitstun=0,
            ref_hitstun=0,
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


@pytest.mark.integration
def test_state_flags_2218_allow_interrupt_attackairb_window_active_parity() -> None:
    # Regression lock for the AttackAirB allow_interrupt lane (fp+0x2218 bit0 => state_flags[0] 0x80):
    # this record is in-window (allow_interrupt active) and action-id-stable at t/t+1.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 1344
    p = 0
    assert int(samples.shape[0]) > record
    row = samples[record : record + 1]

    seed_action = int(row["seed_t"]["action_id"][0, p])
    ref_action = int(row["ref_t1"]["action_id"][0, p])
    assert seed_action == ref_action == 0x0043  # AttackAirB stable
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    char_id = int(row["seed_t"]["char_id"][0, p])
    ref_action_frame = int(row["ref_t1"]["action_frame"][0, p])
    expected_allow_interrupt = _allow_interrupt_active_from_moves(root, char_id, ref_action, ref_action_frame)
    assert expected_allow_interrupt == 1

    seed_byte = int(row["seed_t"]["state_flags"][0, p, 0])
    ref_byte = int(row["ref_t1"]["state_flags"][0, p, 0])
    assert (seed_byte & 0x80) != 0
    assert (ref_byte & 0x80) != 0

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

        out_action = int(out["action_id"][0, p])
        out_byte = int(out["state_flags"][0, p, 0])
        assert out_action == ref_action
        assert (out_byte & 0x80) != 0
        assert out_byte == ref_byte
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p", "ref_action", "ref_bit_set", "note"),
    [
        (
            420,
            0,
            0x000E,  # Wait
            True,
            "AttackS3 anim-end carries command-owned allow_interrupt onto Wait",
        ),
        (
            205,
            0,
            0x0035,  # AttackS3S remains in motion before the command event
            False,
            "AttackS3 in-motion row before allow_interrupt command stays clear",
        ),
    ],
)
def test_state_flags_2218_grounded_attack_anim_end_allow_interrupt_owner(
    record: int, p: int, ref_action: int, ref_bit_set: bool, note: str
) -> None:
    # Grounded Attack* Anim callbacks can enter Wait/SquatWait after the script command has set
    # fp->allow_interrupt. The post-frame x2218 bit is owned by that source callback transition, not
    # by a later prev-action row bridge.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::{
    #   ftCo_AttackS3_Anim,ftCo_AttackS3_IASA}
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 0x0035, note
    assert int(row["ref_t1"]["action_id"][0, p]) == ref_action, note
    assert (int(row["ref_t1"]["state_flags"][0, p, 0]) & 0x80 != 0) == ref_bit_set, note

    binding = importlib.import_module("msl_binding")
    seed_action_frame = int(row["seed_t"]["action_frame"][0, p])
    command_visible_next_frame = int(
        binding.move_tables_debug_query("grounded_attack_allow_interrupt", 1, 0x0035, float(seed_action_frame + 1), 0.0)
    )
    assert (command_visible_next_frame != 0) == ref_bit_set, note

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

        out_action = int(out["action_id"][0, p])
        out_byte = int(out["state_flags"][0, p, 0])
        assert out_action == ref_action, note
        assert (out_byte & 0x80 != 0) == ref_bit_set, note
        assert out_byte == int(row["ref_t1"]["state_flags"][0, p, 0]), note
    finally:
        binding.destroy(handle)
