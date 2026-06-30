from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE
from tests.replay_dataset_loader import load_replay_dataset as read_dataset


def _step_sample(sample: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=int(sample["seed_t"]["num_players"][0]), ucf_enabled=1)
    try:
        msl_binding.reseed_seed(handle, sample["seed_t"].view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(
            handle,
            sample["prev_input_t"].view(np.uint8).reshape((1, input_stride)),
            sample["input_t"].view(np.uint8).reshape((1, input_stride)),
        )
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _step_record(path: str, rec: int) -> np.void:
    ds = read_dataset(path)
    return _step_sample(ds.samples[rec : rec + 1].copy())


def _rollout_to_record(path: str, start_rec: int, target_rec: int) -> np.void:
    import msl_binding

    ds = read_dataset(path)
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(samples.shape[0], sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input = np.empty((1, input_stride), dtype=np.uint8)
    cur_input = np.empty((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=1,
        num_players=int(samples["seed_t"][start_rec]["num_players"]),
        ucf_enabled=1,
    )
    try:
        seed[0, :] = samples_u8[start_rec, seed_off : seed_off + seed_stride]
        msl_binding.reseed_seed_rollout(handle, seed)
        for rec in range(start_rec, target_rec + 1):
            prev_input[0, :] = samples_u8[
                rec, prev_input_off : prev_input_off + input_stride
            ]
            cur_input[0, :] = samples_u8[rec, input_off : input_off + input_stride]
            msl_binding.step_input(handle, prev_input, cur_input)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_escapeair_airjump_locked_desired_bottom_lands_on_late_yoshi_platform_rows() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((149, 1), (2389, 0)):
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x002B
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
        assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-6)


def test_escapeair_late_yoshi_platform_rows_need_locked_desired_bottom_lane() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((149, 1), (2389, 0)):
        sample = ds.samples[rec : rec + 1].copy()
        seed = sample["seed_t"][0]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1
        sample["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][0, player] = np.uint8(0)

        out = _step_sample(sample)

        assert int(out["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == 0


def test_escapeair_locked_desired_bottom_waits_for_source_bottom_sweep() -> None:
    rows = (
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            148,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            7364,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/ShadyDecimalStarling.msl",
            7521,
            1,
        ),
    )

    for path, rec, player in rows:
        ds = read_dataset(path)
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
        assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-6)

def test_escapeair_locked_desired_bottom_final_writeback_needs_bottom_sweep() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((147, 1), (1983, 0), (1984, 0)):
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0


def test_escapeair_locked_missing_desired_bottom_keeps_already_below_floor_airborne() -> None:
    # Locked EscapeAir rows whose replay seed lacks the preserved desired-bottom owner cannot use an
    # upward root projection if the callback-local previous root was already below the carried
    # floor. These are cleanup locks for current-tree replay regressions on Yoshi and Pokemon
    # Stadium; true above->floor EscapeAir landings remain covered by the existing positives.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
    rows = (
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            6998,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            7388,
            0,
        ),
    )

    for path, rec, player in rows:
        ds = read_dataset(path)
        seed = ds.samples["seed_t"][rec]
        ref = ds.samples["ref_t1"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_timer"][player]) != 0
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 0
        assert int(ref["action_id"][player]) == 0x00EC
        assert int(ref["on_ground"][player]) == 0

        out = _step_record(path, rec)

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
        assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-6)


def test_sustained_escapeair_locked_bottom_lands_on_static_hard_floor_rollout() -> None:
    # EscapeAir_Coll consumes the live CollData_X130 locked bottom through
    # ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. That source floor producer is generic for
    # static hard floors; it is not limited to height-platform stages.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
    path = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    start_rec = 3639
    target_rec = 5224
    player = 1
    ds = read_dataset(path)
    row = ds.samples[target_rec]
    assert int(row["seed_t"]["action_id"][player]) == 0x00EC
    assert int(row["seed_t"]["seed_prev_action_id"][player]) == 0x00EC
    assert int(row["ref_t1"]["action_id"][player]) == 0x002B
    assert int(row["ref_t1"]["on_ground"][player]) == 1

    out = _rollout_to_record(path, start_rec, target_rec)
    ref = row["ref_t1"]

    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][player]) == int(ref[field][player]), field
    assert abs(float(out["pos_y"][player]) - float(ref["pos_y"][player])) <= 2e-7


def test_escapeair_unlocked_direct_reseed_does_not_borrow_locked_floor_sweep() -> None:
    # Direct replay-seeded EscapeAir rows with no active CollData lock use their current/desired ECB
    # state directly. They must not borrow the early locked hard-floor handoff that runtime
    # JumpAerial -> EscapeAir rollouts need for the FD floor chain.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
    rows = (
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.msl",
            1829,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            2082,
            0,
        ),
    )

    for path, rec, player in rows:
        ds = read_dataset(path)
        seed = ds.samples["seed_t"][rec]
        ref = ds.samples["ref_t1"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_timer"][player]) == 0
        assert int(ref["action_id"][player]) == 0x00EC
        assert int(ref["on_ground"][player]) == 0

        out = _step_record(path, rec)

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
