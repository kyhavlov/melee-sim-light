from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_turn_iasa_no_spurious_dash_regression import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset

ACT_FALL = 29
ACT_LANDING = 42
NO_GROUND = 0xFFFF

WWS = "datasets/marth/replays/validation/marth/WellWornSmallGoshawk.msl"
DSG = "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"


def _run_one_step(dataset_rel: str, record: int) -> tuple[object, np.ndarray, np.void]:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    path = root / dataset_rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(path))
    if record >= int(ds.samples.shape[0]):
        pytest.skip(f"dataset too short for regression check: {dataset_rel} rec={record}")

    row = ds.samples[record : record + 1]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

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

    return ds, row, out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize("record", (5727, 5728, 5729))
def test_fall_stale_same_static_platform_carry_does_not_snap_up_from_below(record: int) -> None:
    # Source owner:
    # Fall_Coll -> ft_800831CC -> mpColl_80047E14 must first let mpColl_80044628_Floor
    # accept a callback-local bottom sweep before mpColl_80044838_Floor can publish a soft
    # platform root projection. These WWS rows already have both callback-local roots below
    # Dream Land's left static platform while CollData carries that same stale platform id, so
    # vanilla remains airborne instead of reusing the carried id as a fresh floor acceptance.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    _, row, out = _run_one_step(WWS, record)
    player = 1

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][player]) == ACT_FALL
    assert int(seed["seed_prev_action_id"][player]) == ACT_FALL
    assert int(seed["ground_id"][player]) == int(ref["ground_id"][player])
    assert int(ref["action_id"][player]) == ACT_FALL
    assert int(ref["on_ground"][player]) == 0

    assert int(out["action_id"][player]) == ACT_FALL
    assert int(out["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-5)


@pytest.mark.integration
@pytest.mark.parametrize(("record", "player"), ((72, 0), (83, 1), (4790, 1), (2051, 0), (3780, 1)))
def test_fall_static_platform_crossing_without_same_stale_carry_still_lands(record: int, player: int) -> None:
    # Adjacent controls: these official Dream Land rows also run common Fall_Coll against a static
    # soft platform, but either the callback-local sweep crosses from above or the carried floor id
    # is absent/different from the accepted platform. They must keep the ordinary Fall landing path.
    _, row, out = _run_one_step(WWS, record)
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][player]) == ACT_FALL
    assert int(ref["action_id"][player]) == ACT_LANDING
    assert int(ref["on_ground"][player]) == 1
    assert (
        int(seed["ground_id"][player]) == NO_GROUND
        or int(seed["ground_id"][player]) != int(ref["ground_id"][player])
    )

    assert int(out["action_id"][player]) == ACT_LANDING
    assert int(out["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-4)


@pytest.mark.integration
def test_fastfall_same_static_platform_carry_stays_on_bottom_sweep_landing_path() -> None:
    # Adjacent aggregate control: DSG has the same carried-platform id shape as the WWS Marth rows,
    # but Fall is in fastfall. Keep this on the ordinary source bottom-sweep publication path rather
    # than borrowing the non-fastfall stale-carry rejection.
    _, row, out = _run_one_step(DSG, 200)
    player = 1
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][player]) == ACT_FALL
    assert int(seed["seed_prev_action_id"][player]) == ACT_FALL
    assert int(seed["fall_fast"][player]) == 1
    assert int(seed["ground_id"][player]) == int(ref["ground_id"][player])
    assert int(ref["action_id"][player]) == ACT_LANDING
    assert int(ref["on_ground"][player]) == 1

    assert int(out["action_id"][player]) == ACT_LANDING
    assert int(out["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-4)
