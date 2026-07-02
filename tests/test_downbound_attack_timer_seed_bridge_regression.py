from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers

ACT_DOWN_BOUND_D = 191
ACT_DOWN_ATTACK_D = 195
ACT_DOWN_FOWARD_D = 196
BUTTON_A = 0x0100


_CASES: tuple[tuple[str, int, int], ...] = (
    (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        2121,
        0,
    ),
    (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        5847,
        0,
    ),
    (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        10510,
        1,
    ),
    (
        "replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
        1830,
        0,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize(("dataset_rel", "target_record", "p"), _CASES)
def test_downbound_attack_timer_seed_bridge_target_pm1_lock(
    dataset_rel: str, target_record: int, p: int
) -> None:
    # Replay-real target±1 lock for the DownBound attack-vs-roll family.
    # These rows are sensitive to x67C/x67D ownership at DownBound anim-end.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_8009794C
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
    pytest.importorskip("msl_binding")
    binding = importlib.import_module("msl_binding")

    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    lo = target_record - 1
    hi = target_record + 2
    assert lo >= 0
    assert int(samples.shape[0]) > hi - 1, (
        f"replay too short for lock row: record={target_record} num_records={int(samples.shape[0])}"
    )
    chunk = samples[lo:hi]
    assert int(chunk.shape[0]) == 3

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=3, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(chunk["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(3, seed_stride)
            .copy()
        )
        prev_input_bytes = (
            np.frombuffer(chunk["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(3, input_stride)
            .copy()
        )
        input_bytes = (
            np.frombuffer(chunk["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(3, input_stride)
            .copy()
        )
        out_compare_bytes = np.empty((3, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        for i in range(3):
            rec = lo + i
            # Keep strict replay-real transition lock coverage for both players on target±1.
            for pp in (0, 1):
                _assert_transition_lock_fields_match_ref(
                    out_row=out[i], ref_row=chunk["ref_t1"][i], record=rec, p=pp
                )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_downbound_pre_input_a_timer_beats_same_frame_roll_doubles_lock() -> None:
    # Replay-real lock for the DownBound Anim pre-input button-timer owner:
    # - DownBound_Anim runs in Fighter_8006A360 before Fighter_procUpdate refreshes current inputs.
    # - On this row the current A edge resets x67C later in the source frame, but the Anim callback
    #   still sees the pre-input x67C=9 timer and enters DownAttackD before roll-stick input.
    # - Mutating the pre-input timer stale keeps the same A edge and roll stick but must not enter
    #   DownAttackD, proving this is the source timer owner rather than a broad same-frame-A rule.
    #
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::{ftCo_80098400,ftCo_Down_CheckInput}
    pytest.importorskip("msl_binding")
    binding = importlib.import_module("msl_binding")

    root = Path(__file__).resolve().parents[1]
    dataset_rel = "replays/validation/doubles_recent/Game_20260509T152622.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 6713
    p = 1
    row = samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    cur_input = row["input_t"]["p"][p]
    prev_input = row["prev_input_t"]["p"][p]

    assert int(seed["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(seed["action_frame"][p]) == 25
    assert int(seed["x67C"][p]) == 9
    assert int(seed["x683"][p]) == 0xFF
    assert (int(cur_input["buttons"]) & BUTTON_A) != 0
    assert (int(prev_input["buttons"]) & BUTTON_A) == 0
    assert int(ref["action_id"][p]) == ACT_DOWN_ATTACK_D

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_rows = np.repeat(samples[record : record + 1]["seed_t"].copy(), 2, axis=0)
    prev_rows = np.repeat(samples[record : record + 1]["prev_input_t"].copy(), 2, axis=0)
    input_rows = np.repeat(samples[record : record + 1]["input_t"].copy(), 2, axis=0)

    seed_rows[1]["x67C"][p] = np.uint8(0xFF)
    seed_rows[1]["x683"][p] = np.uint8(0xFF)

    handle = binding.init(batch_size=2, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(seed_rows.tobytes(order="C"), dtype=np.uint8).reshape(2, seed_stride).copy()
        )
        prev_input_bytes = (
            np.frombuffer(prev_rows.tobytes(order="C"), dtype=np.uint8).reshape(2, input_stride).copy()
        )
        input_bytes = (
            np.frombuffer(input_rows.tobytes(order="C"), dtype=np.uint8).reshape(2, input_stride).copy()
        )
        out_compare_bytes = np.empty((2, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)

    _assert_transition_lock_fields_match_ref(out_row=out[0], ref_row=ref, record=record, p=p)
    assert int(out[1]["action_id"][p]) == ACT_DOWN_FOWARD_D
    assert int(out[1]["action_id"][p]) != ACT_DOWN_ATTACK_D
