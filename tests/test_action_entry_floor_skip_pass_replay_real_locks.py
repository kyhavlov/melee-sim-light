from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_AGG_VALID = "datasets/aggregate_recent/replays/validation"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (f"{_AGG_VALID}/yoshis_story_recent/PhysicalElectricCapybara.msl", 157, 0),
        (f"{_AGG_VALID}/yoshis_story_recent/PhysicalElectricCapybara.msl", 158, 0),
        (f"{_AGG_VALID}/yoshis_story_recent/PhysicalElectricCapybara.msl", 159, 0),
        (f"{_AGG_VALID}/battlefield_recent/DelayedSuperbGuanaco.msl", 400, 1),
        (f"{_AGG_VALID}/battlefield_recent/DelayedSuperbGuanaco.msl", 401, 1),
        (f"{_AGG_VALID}/pokemon_stadium_recent/CornyDelayedOkapi.msl", 91, 1),
    ],
)
def test_pass_floor_skip_held_down_entry_blocks_fastfall_replay_real_lock(
    dataset_rel: str, record: int, p: int
) -> None:
    # Pass/floor-skip entry owner:
    # ftCo_8009A228 enters Pass, calls mpUpdateFloorSkip, and writes x671=0xFE. Held-down
    # platform-drop input therefore cannot relatch fastfall during the short Pass window even when
    # public controller history would make x671 look fresh in a teacher-forced seed.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{
    #   ftCo_8009A228,ftCo_Pass_Phys}
    # refs/melee/src/melee/mp/mpcoll.c::mpUpdateFloorSkip
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 244  # Pass
    assert int(row["seed_t"]["action_frame"][p]) <= 2
    assert int(row["ref_t1"]["state_flags"][p][1]) & 0x08 == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p])
    assert int(out_row["state_flags"][p][1]) & 0x08 == 0
    assert float(out_row["speed_y_self"][p]) == pytest.approx(float(ref_row["speed_y_self"][p]))


def test_pass_floor_skip_fresh_down_repress_can_still_fastfall_synthetic_lock() -> None:
    # Negative/synthetic owner lock: the Pass x671 reconstruction is held-down ownership only. If
    # the previous sample is not down and the current Pass sample presses down, source input history
    # can produce a fresh x671=0 edge and ftCommon_CheckFallFast may latch fastfall.
    # refs/melee/src/melee/ft/fighter.c (x671 input-history update)
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_VALID}/yoshis_story_recent/PhysicalElectricCapybara.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    record = 158
    p = 0
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 244

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_t = row["seed_t"].copy()
    prev_input_t = row["prev_input_t"].copy()
    input_t = row["input_t"].copy()
    seed_t["tilt_timer_y"][0, p] = 0
    prev_input_t["p"]["main_y"][0, p] = 0
    input_t["p"]["main_y"][0, p] = -80

    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(prev_input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][p]) == 244
    assert int(out["state_flags"][p][1]) & 0x08 == 0x08


def test_fd_cardinal_attackair_fastfall_latch_non_regression_lock() -> None:
    # FD/cardinal non-regression for the same fastfall lane: ordinary aerial fastfall ownership
    # remains ftCommon_CheckFallFast and is not suppressed by the Pass/floor-skip owner.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Phys
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_VALID}/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 301
    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 69  # AttackAirLw
    assert int(row["ref_t1"]["state_flags"][p][1]) & 0x08 == 0x08

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    assert int(out_row["state_flags"][p][1]) & 0x08 == 0x08
    assert float(out_row["speed_y_self"][p]) == pytest.approx(float(ref_row["speed_y_self"][p]))


@pytest.mark.integration
def test_pass_to_attackair_keeps_floor_skip_for_same_callback_replay_real_lock() -> None:
    # Pass -> AttackAir same-frame floor-skip lifetime:
    # Pass_IASA can enter AttackAir before the map callback, while the source collision callback
    # still observes the platform-pass floor_skip written by ftCo_8009A228/mpUpdateFloorSkip.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A228,ftCo_Pass_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_VALID}/fountain_of_dreams_recent/ElatedWearyTermite.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 2537
    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 244  # Pass
    assert int(row["seed_t"]["action_frame"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == 67  # AttackAirB
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert float(out_row["pos_y"][p]) == pytest.approx(float(ref_row["pos_y"][p]), abs=1e-6)
