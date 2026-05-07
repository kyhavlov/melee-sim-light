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
