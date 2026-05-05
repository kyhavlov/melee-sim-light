from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
SM_ESCAPE_AIR = 44
SM_LANDING_FALL_SPECIAL = 36


def _run_one_step(ds, record: int, *, seed_mutator=None) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])

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


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            2565,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            6243,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            2771,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            6328,
            1,
        ),
    ],
)
def test_locked_escapeair_platform_start_lifetime_enters_landing_fall_special(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for platform-stage EscapeAir over soft platforms.
    #
    # Source owner:
    # - EscapeAir_Coll delegates through ft_80082C74 -> ft_80081D0C.
    # - mpColl_80043754 owns the callback-local substep result.
    # - With CollData_X130_Locked active, mpColl_80046904 can consume a platform floor result via
    #   mpColl_80044838_Floor(ignore_bottom=true), projecting from the fighter root while the locked
    #   bottom point is still zeroed.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in (
        "action_id",
        "animation_index",
        "action_frame",
        "on_ground",
        "ground_id",
        "jumps_left",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            3936,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            4812,
            0,
        ),
    ],
)
def test_locked_escapeair_first_platform_crossing_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls: the first post-physics root crossing under a soft platform does
    # not yet have the callback-local platform result lifetime consumed by the retained owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_locked_escapeair_platform_consumer_does_not_broaden_fd_or_hard_floor() -> None:
    # Synthetic negative controls using a replay-real platform row:
    # - FD/cardinal has no soft platform line to consume.
    # - a hard-floor-only pose above Battlefield main floor is not admitted by the platform helper.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 2565
    p = 1

    def fd_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(0xFFFF)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(24.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(26.0)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0

    def hard_floor_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(31)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(3.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(5.0)

    out_hard_floor = _run_one_step(ds, record, seed_mutator=hard_floor_mutation)
    assert int(out_hard_floor["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_hard_floor["on_ground"][p]) == 0
