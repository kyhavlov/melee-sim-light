from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
)


def _run_rollout_to_record(ds_path: Path, *, start_record: int, target_record: int) -> np.void:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > target_record
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(samples[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            )
            input_bytes[0, :] = np.frombuffer(samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8)
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_damage_anim_end_wait_iasa_squat_rollout_preserves_intermediate_instance_bump() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    p = 0
    start_record = 7818
    target_record = 7832
    seed = samples[start_record]["seed_t"]
    ref = samples[target_record]["ref_t1"]

    assert int(seed["action_id"][p]) == 77  # DamageHi3
    assert int(seed["action_frame"][p]) == 15
    assert int(ref["action_id"][p]) == 39  # Squat
    assert int(ref["action_frame"][p]) == 1
    assert int(ref["instance_id"][p]) == 1624

    out = _run_rollout_to_record(ds_path, start_record=start_record, target_record=target_record)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_damage_anim_end_wait_iasa_instance_bump_does_not_apply_before_anim_end() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")
    p = 0
    seed, ref, out = _run_one_step_row(ds_path, 7831, p)
    assert int(seed["action_id"][p]) == 77  # DamageHi3
    assert int(seed["action_frame"][p]) == 28
    assert int(ref["action_id"][p]) == 77
    assert int(out["action_id"][p]) == 77
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p]) == int(seed["instance_id"][p])
