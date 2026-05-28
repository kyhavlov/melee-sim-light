from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_DASH = 20
STAGE_FD = 32


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
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "BlondHardHippopotamus.msl",
            6673,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            829,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            11238,
            1,
        ),
    ],
)
def test_grounded_dash_entry_uses_connected_floor_projection_result(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for grounded seam floor-index ownership.
    #
    # Source owner:
    # - Dash_Coll delegates to ft_800844EC -> ft_80082708.
    # - ft_80082708 runs mpColl_8004B108 on the current CollData.
    # - mpLib_8004DD90_Floor may traverse connected prev/next floor links and returns the accepted
    #   floor line. Same-frame Dash entry must keep that returned floor.index rather than forcing
    #   the pre-entry seed line at legal-stage seams.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
    # refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["ref_t1"]["action_id"][p]) == ACT_DASH
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) != int(row["ref_t1"]["ground_id"][p])

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            2340,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            4985,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            739,
            0,
        ),
    ],
)
def test_whispy_wind_grounded_root_crossing_flat_seam_refreshes_floor_index(
    dataset_rel: str, record: int, p: int
) -> None:
    # Dream Land Whispy wind is a stage displacement after the main grounded floor pass. If wind
    # moves the root across a connected flat floor seam, the post-frame CollData.floor index follows
    # the final root through the same mpLib_8004DD90_Floor prev/next traversal instead of keeping
    # the earlier ECB-bottom projection segment.
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_GetWindOffsetVec
    # refs/melee/src/melee/gr/groldpupupu.c::fn_802112F4
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    # data/stages/bin/grop.bin::MSLSTG01 floor prev/next links
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) != int(row["ref_t1"]["ground_id"][p])
    assert int(row["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_grounded_dash_entry_does_not_rewrite_floor_without_connected_seam() -> None:
    # Synthetic negative: the retained owner consumes the mpLib connected-floor projection result.
    # A center-stage FD Dash entry with no seam traversal keeps the current floor.index.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 6673
    p = 1

    def center_floor(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["action_id"][0, p] = np.uint16(ACT_DASH)
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_DASH)
        seed["on_ground"][0, p] = np.uint8(1)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(0.0001)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(0.0001)

    out = _run_one_step(ds, record, seed_mutator=center_floor)
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == 1
