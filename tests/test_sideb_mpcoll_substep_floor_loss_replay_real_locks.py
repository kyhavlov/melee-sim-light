from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset_window


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/fountain_of_dreams.json",
        "data/stages/dream_land_n64.json",
        "data/motion_state/owners/fox.bin",
        "data/motion_state/owners/falco.bin",
        "data/characters/fox.json",
        "data/characters/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset_window(str(dataset_path), record, record + 1)
    row = ds.samples[0:1]

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed(handle, row["seed_t"].view("u1").reshape(1, seed_stride).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
            row["input_t"].view("u1").reshape(1, input_stride).copy(),
        )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    return row["seed_t"][0].copy(), row["ref_t1"][0].copy(), out


def test_fod_grounded_sideb_floor_loss_publishes_mpcoll_substep() -> None:
    # Replay-real positive:
    # - Fox grounded Side-B main traverses FoD floor segment 3 by more than mpColl's 6-unit
    #   callback threshold.
    # - Source `ftFx_SpecialS_Coll -> ft_80082708 -> mpColl_8004B108` runs
    #   `mpColl_80043754` substeps; `mpColl_8004ACE4` stops when the carried floor projection
    #   first fails and publishes that intermediate cur_pos before GroundToAir enters
    #   SpecialAirS.
    # - The integrated ground velocity remains the full TransN-derived speed; only the collision
    #   callback root is the substep stop point.
    #
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialS_Coll,ftFx_SpecialS_GroundToAir}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_8004ACE4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _step_one_row(dataset_path, 7115)
    p = 0
    assert int(seed["action_id"][p]) == 348
    assert int(seed["on_ground"][p]) == 1
    assert int(ref["action_id"][p]) == 351
    assert int(ref["on_ground"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-5
    )


def test_dream_land_aerial_sideb_wall_envelope_matches_entry_and_end_window() -> None:
    # Replay-real positive:
    # - Falco aerial Side-B Start ends into SpecialAirS while its main-state ECB expands into Dream
    #   Land's left wall, then the Main -> End window keeps using the same airborne mpColl envelope.
    # - Source `ftFx_SpecialAirS{Start,,End}_Coll -> ft_CheckGroundAndLedge` runs
    #   `mpColl_800473CC` / `mpColl_800471F8`, both of which dispatch the full
    #   `mpColl_80046904` wall envelope before floor/ledge consumers.
    # - This lock proves Side-B owns the same wall-envelope family as ft_80081D0C airborne
    #   callbacks; the FoD control below proves the owner does not clamp ordinary airborne root
    #   motion when no wall envelope is active.
    #
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class_bits
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800473CC,mpColl_800471F8,mpColl_80046904,
    #   mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        / "FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    for record in range(7692, 7700):
        seed, ref, out = _step_one_row(dataset_path, record)
        assert int(seed["action_id"][p]) in (350, 351, 352)
        assert int(ref["action_id"][p]) in (351, 352)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
        assert float(out["speed_air_x_self"][p]) == pytest.approx(
            float(ref["speed_air_x_self"][p]), abs=1e-5
        )


def test_fod_aerial_sideb_after_floor_loss_keeps_full_airborne_root_step() -> None:
    # Replay-real negative:
    # the next row is already SpecialAirS. It does not run the grounded
    # `ftFx_SpecialS_Coll -> ft_80082708` callback, so the retained substep floor-loss owner must
    # not clamp its ordinary airborne root movement.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _step_one_row(dataset_path, 7116)
    p = 0
    assert int(seed["action_id"][p]) == 351
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["action_id"][p]) == 351
    assert int(ref["on_ground"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-5
    )
