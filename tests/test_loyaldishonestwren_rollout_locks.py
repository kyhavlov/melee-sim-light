from __future__ import annotations

from pathlib import Path
from typing import Callable

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


DATASET_REL = (
    "datasets/aggregate_recent/replays/validation/battlefield_recent/"
    "LoyalDishonestWren.msl"
)

ACT_CATCH = 0x00D4
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DOWN_BOUND_U = 0x00B7
ACT_ESCAPE_AIR = 0x00EC
ACT_FALL = 0x001D
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_PASSIVE_STAND_F = 0x00C8
BUTTON_L = 0x0040
BUTTON_R = 0x0020
BUTTON_Z = 0x0010


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/battlefield.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset():
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    path = root / DATASET_REL
    if not path.exists():
        pytest.skip(f"missing local dataset: {DATASET_REL}")
    return read_dataset(str(path))


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _run_one_step(
    ds,
    record: int,
    *,
    seed_mutator: Callable | None = None,
    input_mutator: Callable | None = None,
) -> np.void:
    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])
    if input_mutator is not None:
        input_mutator(row["prev_input_t"], row["input_t"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, row["seed_t"].view("u1").reshape(1, seed_stride).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
            row["input_t"].view("u1").reshape(1, input_stride).copy(),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def _run_rollout(ds, start_record: int, target_record: int) -> np.void:
    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    samples = ds.samples
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(
            handle,
            samples[start_record : start_record + 1]["seed_t"]
            .view("u1")
            .reshape(1, seed_stride)
            .copy(),
        )
        for record in range(start_record, target_record + 1):
            row = samples[record : record + 1]
            binding.step_input(
                handle,
                row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
                row["input_t"].view("u1").reshape(1, input_stride).copy(),
            )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
def test_damagefly_hitlag_exit_floor_contact_uses_live_lr_lane_owner_ldw_1330() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[1330]["ref_t1"]

    out = _run_one_step(ds, 1330)
    assert int(ds.samples[1330]["seed_t"]["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(ds.samples[1330]["seed_t"]["hitlag"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)

    rollout = _run_rollout(ds, 1326, 1330)
    assert int(rollout["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert float(rollout["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)

    def force_pre_hitlag_tech_timer(seed) -> None:
        seed["x680"][p] = 0
        seed["x684"][p] = 0xFF

    out_with_pre_hitlag_timer = _run_one_step(ds, 1330, seed_mutator=force_pre_hitlag_tech_timer)
    assert int(out_with_pre_hitlag_timer["action_id"][p]) == ACT_PASSIVE_STAND_F

    def clear_live_lr_lane(prev_input, input_) -> None:
        clear_mask = np.uint16((~(BUTTON_L | BUTTON_R | BUTTON_Z)) & 0xFFFF)
        for arr in (prev_input, input_):
            arr["p"][p]["buttons"] = np.uint16(arr["p"][p]["buttons"] & clear_mask)
            arr["p"][p]["l"] = 0
            arr["p"][p]["r"] = 0

    out_without_lr_lane = _run_one_step(
        ds,
        1330,
        seed_mutator=force_pre_hitlag_tech_timer,
        input_mutator=clear_live_lr_lane,
    )
    assert int(out_without_lr_lane["action_id"][p]) == ACT_PASSIVE_STAND_F


@pytest.mark.integration
def test_damageflytop_downbound_floor_loss_publishes_callback_substep_x_ldw_2556() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[2556]["ref_t1"]

    out = _run_one_step(ds, 2556)
    assert int(ds.samples[2556]["seed_t"]["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(ds.samples[2556]["seed_t"]["seed_prev_action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_FALL
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)

    def remove_damageflytop_entry_proof(seed) -> None:
        seed["seed_prev_action_id"][p] = ACT_DAMAGE_FLY_N

    out_without_entry_owner = _run_one_step(ds, 2556, seed_mutator=remove_damageflytop_entry_proof)
    assert int(out_without_entry_owner["action_id"][p]) == ACT_FALL
    assert float(out_without_entry_owner["pos_x"][p]) != pytest.approx(
        float(ref["pos_x"][p]), abs=0.1
    )


@pytest.mark.integration
def test_catch_entry_preserves_source_kb_velocity_lane_ldw_2915() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[2915]["ref_t1"]

    out = _run_one_step(ds, 2915)
    assert int(ds.samples[2915]["seed_t"]["action_id"][p]) == 0x00B2  # GuardOn
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_CATCH
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]))
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)

    def clear_kb_lane(seed) -> None:
        seed["speed_x_attack"][p] = 0.0

    out_without_kb = _run_one_step(ds, 2915, seed_mutator=clear_kb_lane)
    assert int(out_without_kb["action_id"][p]) == ACT_CATCH
    assert float(out_without_kb["speed_x_attack"][p]) == pytest.approx(0.0)
    assert float(out_without_kb["pos_x"][p]) != pytest.approx(float(ref["pos_x"][p]), abs=0.1)


@pytest.mark.integration
def test_damagefly_473cc_uses_callback_entry_last_pos_ldw_3075() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[3075]["ref_t1"]

    out = _run_rollout(ds, 2952, 3075)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=5e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jump_escapeair_uses_frame_start_mpcollprev_for_landing_ldw_3710() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[3710]["ref_t1"]

    direct = _run_one_step(ds, 3710)
    assert int(direct["action_id"][p]) == ACT_ESCAPE_AIR

    out = _run_rollout(ds, 3660, 3710)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_ldw_rollout_prefix_reaches_final_damageflytop_boundary_cleanly() -> None:
    ds = _dataset()
    p = 0
    ref = ds.samples[3095]["ref_t1"]

    out = _run_rollout(ds, 0, 3095)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 7
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 81
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-4)
