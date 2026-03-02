from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    MSL_BUTTON_A,
    _assert_transition_lock_fields_match_ref,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_CASES: tuple[tuple[str, int, int, int, int], ...] = (
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
        6432,
        1,
        42,
        44,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        2931,
        0,
        42,
        44,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        7169,
        0,
        42,
        56,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
        3240,
        0,
        42,
        44,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize(("dataset_rel", "target_record", "p_target", "seed_action", "ref_action"), _CASES)
def test_landing_iasa_attack_entry_target_pm1_both_players_strict_lock(
    dataset_rel: str,
    target_record: int,
    p_target: int,
    seed_action: int,
    ref_action: int,
) -> None:
    # Replay-real target±1 lock for Landing IASA grounded-attack admission and immediate grounded
    # attack Phys ownership.
    #
    # Decomp refs:
    # - Landing IASA attack ordering/admission:
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # - Grounded attack Phys ownership:
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c::ftCo_AttackHi3_Phys
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
    #   refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80084FA8,ft_80085030}
    pytest.importorskip("msl_binding")
    binding = importlib.import_module("msl_binding")

    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    lo = target_record - 1
    hi = target_record + 2
    assert lo >= 0
    assert int(samples.shape[0]) > hi - 1, (
        f"dataset too short for lock row: record={target_record} num_records={int(samples.shape[0])}"
    )

    target = samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == int(seed_action)
    assert int(target["ref_t1"]["action_id"][p_target]) == int(ref_action)
    assert int(target["input_t"]["p"]["buttons"][p_target]) & int(MSL_BUTTON_A)
    assert (int(target["prev_input_t"]["p"]["buttons"][p_target]) & int(MSL_BUTTON_A)) == 0

    chunk = samples[lo:hi]
    assert int(chunk.shape[0]) == 3

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=3, num_players=int(ds.header["num_players"]))
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
            for p in range(int(ds.header["num_players"])):
                _assert_transition_lock_fields_match_ref(
                    out_row=out[i], ref_row=chunk["ref_t1"][i], record=rec, p=p
                )
    finally:
        binding.destroy(handle)
