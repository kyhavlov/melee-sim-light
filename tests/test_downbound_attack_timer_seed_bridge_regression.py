from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_CASES: tuple[tuple[str, int, int], ...] = (
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
        2121,
        0,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
        5847,
        0,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
        10510,
        1,
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
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
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    lo = target_record - 1
    hi = target_record + 2
    assert lo >= 0
    assert int(samples.shape[0]) > hi - 1, (
        f"dataset too short for lock row: record={target_record} num_records={int(samples.shape[0])}"
    )
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
            # Keep strict replay-real transition lock coverage for both players on target±1.
            for pp in (0, 1):
                _assert_transition_lock_fields_match_ref(
                    out_row=out[i], ref_row=chunk["ref_t1"][i], record=rec, p=pp
                )
    finally:
        binding.destroy(handle)
