from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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


def test_fallspecial_landing_controls_do_not_require_the_prephysics_helper() -> None:
    # FallSpecial_Coll uses ft_80083090 -> mpColl_80047E14. The retained prephysics bottom-sweep
    # helper is a source path, not a final-publication veto: ordinary replay-real FallSpecial floor
    # contacts must still reach LandingFallSpecial through the shared floor projection path.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
    rows = (
        ("datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl", 3368, 1),
        ("datasets/aggregate_recent/replays/validation/dream_land_recent/FlippantEnchantedHorse.msl", 1965, 0),
    )

    for path, rec, player in rows:
        ds = read_dataset(path)
        seed = ds.samples["seed_t"][rec]
        ref = ds.samples["ref_t1"][rec]
        assert int(seed["action_id"][player]) == 0x0023
        assert int(ref["action_id"][player]) == 0x002B

        out = _step_record(path, rec)

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x002B
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
        assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)


def test_fallspecial_lands_when_callback_visible_bottom_sweep_hits() -> None:
    # Control: once the carried CollData previous root to callback-visible root sweep crosses the
    # floor, FallSpecial_Coll enters LandingFallSpecial through ftCo_80096D28.
    path = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    rec = 4321
    player = 1
    ds = read_dataset(path)
    seed = ds.samples["seed_t"][rec]
    ref = ds.samples["ref_t1"][rec]
    assert int(seed["action_id"][player]) == 0x0023
    assert int(ref["action_id"][player]) == 0x002B

    out = _step_record(path, rec)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x002B
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)


def test_fallspecial_connected_hard_floor_root_projection_uses_source_prev_endpoint_ipw() -> None:
    # IPW:1240 carries FoD center hard floor 5 while the current FallSpecial root has drifted over
    # connected ledge hard floor 3. Vanilla's ft_80083090/mpColl_80047E14 callback can publish
    # LandingFallSpecial through mpColl_80044838_Floor even though the loaded FallSpecial ECB bottom
    # is still above the ledge floor. The owner is the source-owned CollData.prev_pos/floor.index
    # endpoint; clearing that endpoint must not synthesize the landing from visible root state.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80083090
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    path = "datasets/aggregate_recent/replays/validation/marth/InternalPowerlessWallaby.msl"
    rec = 1240
    player = 1
    ds = read_dataset(path)
    sample = ds.samples[rec : rec + 1].copy()
    seed = sample["seed_t"][0]
    ref = sample["ref_t1"][0]
    assert int(seed["action_id"][player]) == 0x0023
    assert int(seed["ground_id"][player]) == 5
    assert int(seed["floor_sweep_prev_pos_valid_u8"][player]) == 1
    assert int(ref["action_id"][player]) == 0x002B
    assert int(ref["ground_id"][player]) == 3

    out = _step_sample(sample)
    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x002B
    assert int(out["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == 3
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)

    no_source_endpoint = sample.copy()
    no_source_endpoint["seed_t"]["floor_sweep_prev_pos_valid_u8"][0, player] = np.uint8(0)
    out_no_source = _step_sample(no_source_endpoint)
    assert int(out_no_source["action_id"][player]) == 0x0023
    assert int(out_no_source["on_ground"][player]) == 0
    assert int(out_no_source["ground_id"][player]) == 5

    prior = ds.samples[rec - 1 : rec].copy()
    prior_seed = prior["seed_t"][0]
    prior_ref = prior["ref_t1"][0]
    assert int(prior_seed["action_id"][player]) == 0x0023
    assert int(prior_seed["action_frame"][player]) == 2
    assert int(prior_ref["action_id"][player]) == 0x0023
    out_prior = _step_sample(prior)
    assert int(out_prior["action_id"][player]) == 0x0023
    assert int(out_prior["on_ground"][player]) == 0


def test_fallspecial_connected_hard_floor_root_projection_rejects_frame_start_fastfall_pfz() -> None:
    # PFZ:5199 carries Battlefield main hard floor 1 and the root drifts beyond the connected right
    # ledge floor. The seed/frame-start fp->fall_fast latch remains source evidence for
    # FallSpecial_Coll even if the mutable latch is cleared before this helper runs; vanilla stays
    # airborne rather than publishing LandingFallSpecial from the connected same-height ledge.
    #
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    path = "datasets/aggregate_recent/replays/validation/marth/ParallelFamiliarZebra.msl"
    rec = 5199
    player = 1
    ds = read_dataset(path)
    sample = ds.samples[rec : rec + 1].copy()
    seed = sample["seed_t"][0]
    ref = sample["ref_t1"][0]
    assert int(seed["action_id"][player]) == 0x0023
    assert int(seed["ground_id"][player]) == 1
    assert int(seed["fall_fast"][player]) == 1
    assert int(ref["action_id"][player]) == 0x0023
    assert int(ref["on_ground"][player]) == 0

    out = _step_sample(sample)

    assert int(out["action_id"][player]) == 0x0023
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == 1


def test_fallspecial_current_ecb_owner_does_not_land_first_sustained_frame_pec() -> None:
    # PEC:6952 is the first sustained FallSpecial callback after entry. Its current ECB bottom is
    # near the Yoshi floor, but vanilla keeps FallSpecial airborne for this frame and lands on the
    # following callback. The retained current-ECB owner starts after this first sustained frame.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    rec = 6952
    player = 1
    ds = read_dataset(path)
    seed = ds.samples["seed_t"][rec]
    ref = ds.samples["ref_t1"][rec]
    assert int(seed["action_id"][player]) == 0x0023
    assert int(seed["action_frame"][player]) == 1
    assert int(ref["action_id"][player]) == 0x0023

    out = _step_record(path, rec)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x0023
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)


def test_fallspecial_same_floor_early_root_crossing_stays_airborne_dcc() -> None:
    # Same carried-floor early FallSpecial root crossings stay airborne; adjacent floor/seam
    # handoffs remain covered by the positive controls above.
    path = "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    rec = 8771
    player = 1
    ds = read_dataset(path)
    seed = ds.samples["seed_t"][rec]
    ref = ds.samples["ref_t1"][rec]
    assert int(seed["action_id"][player]) == 0x0023
    assert int(seed["ground_id"][player]) == 1
    assert int(ref["action_id"][player]) == 0x0023

    out = _step_record(path, rec)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x0023
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)
