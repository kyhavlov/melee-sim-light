from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
SM_ESCAPE_AIR = 44
SM_LANDING_FALL_SPECIAL = 36


def _step_bytes(ds, seed, prev_input, input_t) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _rollout_outputs(ds, start_record: int, stop_record: int) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples = ds.samples
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes = (
            samples[start_record : start_record + 1]["seed_t"]
            .view(np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        binding.reseed_seed_rollout(handle, seed_bytes)
        out: dict[int, np.void] = {}
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        for record in range(start_record, stop_record + 1):
            prev_input = (
                samples[record : record + 1]["prev_input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            input_t = (
                samples[record : record + 1]["input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_bytes)
            out[record + 1] = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        return out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_escapeair_sustained_left_ledge_floor_lands_on_shallow_root_crossing() -> None:
    # EscapeAir_Coll delegates to ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. With
    # CollData_X130_Locked active, vanilla can resolve a shallow sustained EscapeAir root crossing
    # on FD's ledge floor even though the lite sim's raw SSANIM bottom point remains above floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0
    start_record = 6147
    target_record = 6163
    row = samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["action_frame"][p]) == 4
    assert int(row["seed_t"]["animation_index"][p]) == SM_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 4
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING_FALL_SPECIAL

    out = _step_bytes(ds, row["seed_t"], row["prev_input_t"], row["input_t"])
    ref = row["ref_t1"]
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "jumps_left",
        "hurtbox_state",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )

    rollout = _rollout_outputs(ds, start_record, target_record)
    out_rollout = rollout[target_record + 1]
    for field in ("action_id", "on_ground", "ground_id", "jumps_left", "instance_id"):
        assert int(out_rollout[field][p]) == int(ref[field][p]), field
    assert float(out_rollout["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_escapeair_floorhug_and_already_below_ledge_rows_do_not_land_early() -> None:
    # Negative controls for the same EscapeAir_Coll bridge:
    # - PJO 4645 is already floor-hugging near the floor bias with only sub-bias vertical drift.
    # - HIS 4313 is already below the left ledge floor at frame start.
    # Neither is a frame-local root crossing from above the floor into the persisted line.
    root = Path(__file__).resolve().parents[1]
    cases = (
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl",
            4645,
            0,
        ),
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            4313,
            0,
        ),
    )
    for dataset_path, record, p in cases:
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR

        out = _step_bytes(ds, row["seed_t"], row["prev_input_t"], row["input_t"])
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (dataset_path.name, record, field)


@pytest.mark.integration
def test_kneebend_horizontal_escapeair_entry_floorhug_stays_airborne() -> None:
    # Fresh KneeBend -> Jump -> EscapeAir can publish the horizontal airdodge entry at FD's floor
    # bias without immediately entering LandingFallSpecial. This covers the generic resting-contact
    # fallback as well as the disruptive rollout first mismatch from the IAT AttackAirLw landing
    # cluster.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{
    #   ftCo_Jump_Enter,ftCo_Jump_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_80099A9C,ftCo_EscapeAir_Coll}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0
    start_record = 5783
    entry_record = 5806
    sustained_record = 5829
    row = samples[entry_record]
    assert int(row["seed_t"]["action_id"][p]) == 24  # KneeBend
    assert int(row["seed_t"]["action_frame"][p]) == 4
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["animation_index"][p]) == SM_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0
    assert float(row["ref_t1"]["speed_y_self"][p]) == pytest.approx(0.0, abs=1e-7)

    out = _step_bytes(ds, row["seed_t"], row["prev_input_t"], row["input_t"])
    ref = row["ref_t1"]
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "jumps_left",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(0.0, abs=1e-7)

    rollout = _rollout_outputs(ds, start_record, sustained_record)
    out_rollout = rollout[entry_record + 1]
    for field in ("action_id", "on_ground", "ground_id", "jumps_left", "instance_id"):
        assert int(out_rollout[field][p]) == int(ref[field][p]), field
    assert float(out_rollout["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    sustained_ref = samples[sustained_record]["ref_t1"]
    sustained_out = rollout[sustained_record + 1]
    assert int(sustained_ref["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(sustained_ref["on_ground"][p]) == 0
    for field in ("action_id", "on_ground", "ground_id", "jumps_left", "instance_id"):
        assert int(sustained_out[field][p]) == int(sustained_ref[field][p]), field
    assert float(sustained_out["pos_y"][p]) == pytest.approx(
        float(sustained_ref["pos_y"][p]), abs=1e-6
    )


@pytest.mark.integration
def test_escapeair_late_below_ledge_floor_continuations_keep_source_landing_boundary() -> None:
    # Late locked EscapeAir rows that are still horizontally over the ledge floor can resolve
    # LandingFallSpecial after the prior CollData snapshot was already below the floor.
    root = Path(__file__).resolve().parents[1]
    positives = (
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl",
            6433,
            1,
        ),
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            3100,
            0,
        ),
    )
    for dataset_path, record, p in positives:
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
        assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][p]) < 0.0

        out = _step_bytes(ds, row["seed_t"], row["prev_input_t"], row["input_t"])
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (dataset_path.name, record, field)
