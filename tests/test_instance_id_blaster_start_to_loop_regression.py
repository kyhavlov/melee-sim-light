from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_instance_id_does_not_bump_on_specialairnstart_to_loop_record_108() -> None:
    # Regression lock: entering SpecialAirNLoop from SpecialAirNStart should not bump Slippi-visible
    # action-state instance_id (fp->x2088).
    #
    # Root cause fixed in src/instance_id.c: ft_80089824 is called by x21EC OnChangeAction hooks tied
    # to the *action being left*, not the action being entered; SpecialAirNStart -> Loop does not
    # install x21EC in decomp.
    #
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Anim
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 108
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # The regression is specifically for carry-through instance_id (seed==ref), but action changes.
    p = 1
    assert int(row["seed_t"]["instance_id"][0, p]) == int(row["ref_t1"]["instance_id"][0, p])
    assert int(row["seed_t"]["action_id"][0, p]) != int(row["ref_t1"]["action_id"][0, p])

    expected_action = int(row["ref_t1"]["action_id"][0, p])
    expected_iid = int(row["ref_t1"]["instance_id"][0, p])

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
        got_action = int(out["action_id"][0, p])
        got_iid = int(out["instance_id"][0, p])

        assert got_action == expected_action, (
            f"record=108 p=1 expected action_id={expected_action}, got {got_action}"
        )
        assert got_iid == expected_iid, f"record=108 p=1 expected instance_id={expected_iid}, got {got_iid}"
    finally:
        binding.destroy(handle)

