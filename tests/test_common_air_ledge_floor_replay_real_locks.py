from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_MAJ_DATASET = "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"


def _run_one_step(dataset_path: Path, record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = samples[record : record + 1]
    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]


def _run_rollout_to_record(dataset_path: Path, *, start_record: int, target_record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > target_record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed_bytes = samples[start_record : start_record + 1]["seed_t"].view("u1").reshape(1, seed_stride).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, target_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
            input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]


@pytest.mark.integration
def test_fall_ledge_floor_first_root_crossing_stays_airborne_then_lands() -> None:
    # Replay-real lock for ordinary Fall over the FD left ledge floor.
    #
    # Decomp path:
    # - Fall_Coll delegates to ft_800831CC -> mpColl_80047E14(flags=6).
    # - mpColl_80047E14 loads/interpolates CollData.ecb before mpColl_80044628_Floor.
    # - Dolphin forensic row `reports/triage/maj5100_fall_ledge_dolphin` shows Falco's live
    #   CollData.ecb.bottom.y remains above the ledge floor on the first root crossing, so vanilla
    #   stays Fall for one more frame and only lands on the following frame.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80043754,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _MAJ_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_MAJ_DATASET}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0

    seed_5100 = samples[5100]["seed_t"]
    ref_5100 = samples[5100]["ref_t1"]
    assert int(seed_5100["action_id"][p]) == 29  # Fall
    assert int(seed_5100["action_frame"][p]) == 2
    assert float(seed_5100["floor_sweep_prev_pos_y_f32"][p]) > 0.0
    assert float(seed_5100["pos_y"][p]) < 0.0
    assert int(ref_5100["action_id"][p]) == 29
    assert int(ref_5100["on_ground"][p]) == 0

    one_step = _run_one_step(dataset_path, 5100)
    assert int(one_step["action_id"][p]) == int(ref_5100["action_id"][p])
    assert int(one_step["on_ground"][p]) == 0
    assert float(one_step["pos_y"][p]) == pytest.approx(float(ref_5100["pos_y"][p]), abs=1.0e-6)

    ref_5101 = samples[5101]["ref_t1"]
    assert int(ref_5101["action_id"][p]) == 42  # Landing
    assert int(ref_5101["on_ground"][p]) == 1
    rollout = _run_rollout_to_record(dataset_path, start_record=5070, target_record=5101)
    assert int(rollout["action_id"][p]) == int(ref_5101["action_id"][p])
    assert int(rollout["action_frame"][p]) == int(ref_5101["action_frame"][p])
    assert int(rollout["on_ground"][p]) == 1
    assert float(rollout["pos_x"][p]) == pytest.approx(float(ref_5101["pos_x"][p]), abs=1.0e-6)
    assert float(rollout["pos_y"][p]) == pytest.approx(float(ref_5101["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_fall_ledge_floor_lands_after_root_is_already_below_floor() -> None:
    # Negative/control: the first-root-crossing guard must not suppress the following Fall_Coll
    # frame, where CollData has already carried the below-floor position and vanilla enters Landing.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _MAJ_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_MAJ_DATASET}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0
    seed = samples[5101]["seed_t"]
    ref = samples[5101]["ref_t1"]
    assert int(seed["action_id"][p]) == 29
    assert int(seed["action_frame"][p]) == 3
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) < 0.0
    assert int(ref["action_id"][p]) == 42

    one_step = _run_one_step(dataset_path, 5101)
    assert int(one_step["action_id"][p]) == int(ref["action_id"][p])
    assert int(one_step["on_ground"][p]) == 1
    assert float(one_step["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_fox_fall_ledge_floor_first_root_crossing_still_lands() -> None:
    # Negative/control for the expanded-collision-model live-ECB guard above. Fox's extracted
    # model_scaling is below 1.0 and Fall ledge-floor crossings at the same visible boundary land
    # immediately in replay, so the guard must not become a shared ordinary-Fall ledge suppressor.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 5729
    p = 1
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["char_id"][p]) == 1  # Fox
    assert int(seed["action_id"][p]) == 29
    assert int(seed["action_frame"][p]) == 2
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) > 0.0
    assert float(seed["pos_y"][p]) < 0.0
    assert int(ref["action_id"][p]) == 42

    one_step = _run_one_step(dataset_path, record)
    assert int(one_step["action_id"][p]) == int(ref["action_id"][p])
    assert int(one_step["on_ground"][p]) == 1
    assert float(one_step["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)
