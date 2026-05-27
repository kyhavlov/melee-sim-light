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
        # AttackAir entry clears fp->allow_interrupt before same-frame ProcessHit can replace the
        # visible action with Damage*. These replay-real rows all start from a stale seed bit
        # (0x80) plus another command bit (0x40), enter AttackAir through airborne IASA, then take a
        # BODY hit; vanilla publishes the Damage* row with only 0x40 remaining.
        #
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            record=8953,
            p=0,
            byte=0,
            seed_byte=0xC0,
            ref_byte=0x40,
            note="AttackAir entry clears stale allow_interrupt before same-frame DamageFlyLw hit",
            seed_action_id=0x0019,
            ref_action_id=0x0059,
            seed_hitlag=0,
            ref_hitlag=7,
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            record=5542,
            p=1,
            byte=0,
            seed_byte=0xC0,
            ref_byte=0x40,
            note="DamageFlyHi same-frame hit after AttackAir entry clears allow_interrupt",
            seed_action_id=0x005A,
            ref_action_id=0x0057,
            seed_hitlag=0,
            ref_hitlag=6,
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            record=7217,
            p=1,
            byte=0,
            seed_byte=0xC0,
            ref_byte=0x40,
            note="DamageFlyN same-frame hit after AttackAir entry clears allow_interrupt",
            seed_action_id=0x001D,
            ref_action_id=0x0058,
            seed_hitlag=0,
            ref_hitlag=8,
        ),
        # ProcessHit can interrupt Attack11 after the same source frame's set_jab_combo script
        # command has set fp+0x2218_b1. Damage entry does not clear that bit, so the first visible
        # DamageFlyHi row carries x2218_b1 from the interrupted Attack11 script owner.
        #
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,checkAttack12}
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl",
            record=7274,
            p=0,
            byte=0,
            seed_byte=0x04,
            ref_byte=0x44,
            note="Attack11 same-frame Damage entry carries live set_jab_combo x2218_b1",
            seed_action_id=0x002C,
            ref_action_id=0x0057,
            seed_hitlag=0,
            ref_hitlag=6,
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
    "c",
    [
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            record=6071,
            p=0,
            byte=0,
            seed_byte=0x20,
            ref_byte=0xA0,
            note="DamageFlyN entry inherits live AttackLw3 allow_interrupt",
            seed_action_id=0x0039,
            ref_action_id=0x0058,
            seed_hitlag=0,
            ref_hitlag=6,
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            record=11909,
            p=1,
            byte=0,
            seed_byte=0x00,
            ref_byte=0x80,
            note="DamageFlyN entry inherits live EscapeN allow_interrupt",
            seed_action_id=0x00EB,
            ref_action_id=0x0058,
            seed_hitlag=0,
            ref_hitlag=8,
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/yoshis_story_recent/LawfulInsistentMeerkat.msl",
            record=5818,
            p=0,
            byte=0,
            seed_byte=0xC0,
            ref_byte=0x40,
            note="DamageN2 entry clears stale allow_interrupt when Squat source has no authority",
            seed_action_id=0x0027,
            ref_action_id=0x004F,
            seed_hitlag=0,
            ref_hitlag=4,
        ),
    ],
)
def test_state_flags_2218_damage_entry_bounded_source_allow_interrupt(c: _Case) -> None:
    # ProcessHit/Damage entry does not synthesize allow_interrupt. For these bounded source
    # owners, the post-frame fp+0x2218 bit0 mirrors the interrupted source action's command-script
    # authority; otherwise stale seeded allow clears.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
    _run_case(c)


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


@pytest.mark.integration
def test_attack11_jab_chain_uses_source_x668_z_as_a_edge() -> None:
    # Attack11_IASA -> checkAttack12 reads `fp->input.x668 & HSD_PAD_A`; source input synthesis
    # folds raw Z into that A bit before callbacks run. A seeded Attack11 row with live x2218_b1
    # and a Z edge must therefore consume the same jab-chain owner as an A edge.
    #
    # refs/melee/src/melee/ft/fighter.c:1868-1896
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,checkAttack12}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 7447
    p = 0
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 0x002C
    assert int(row["seed_t"]["state_flags"][0, p, 0]) & 0x40
    assert (int(row["prev_input_t"]["p"]["buttons"][0, p]) & 0x0010) == 0
    assert (int(row["input_t"]["p"]["buttons"][0, p]) & 0x0010) != 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 0x002D

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out[field][0, p]) == int(row["ref_t1"][field][0, p]), field
        assert [int(x) for x in out["state_flags"][0, p]] == [
            int(x) for x in row["ref_t1"]["state_flags"][0, p]
        ]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attack11_jab_chain_z_does_not_reedge_already_held_source_a() -> None:
    # The raw-Z fold happens before the source input edge is computed. If A was already held on the
    # previous frame, pressing Z must not synthesize a second `input.x668 & HSD_PAD_A` edge for
    # checkAttack12.
    #
    # refs/melee/src/melee/ft/fighter.c:1868-1896
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::checkAttack12
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2559
    p = 1
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 0x002C
    assert int(row["seed_t"]["state_flags"][0, p, 0]) & 0x40
    assert (int(row["prev_input_t"]["p"]["buttons"][0, p]) & 0x0100) != 0
    assert (int(row["input_t"]["p"]["buttons"][0, p]) & 0x0110) == 0x0110
    assert int(row["ref_t1"]["action_id"][0, p]) == 0x002C

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out[field][0, p]) == int(row["ref_t1"][field][0, p]), field
        assert [int(x) for x in out["state_flags"][0, p]] == [
            int(x) for x in row["ref_t1"]["state_flags"][0, p]
        ]
    finally:
        binding.destroy(handle)
