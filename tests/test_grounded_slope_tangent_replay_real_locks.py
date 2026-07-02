from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.slpz",
            2321,
            1,
        ),
        (
            "replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.slpz",
            5275,
            0,
        ),
        (
            "replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.slpz",
            1160,
            1,
        ),
        (
            "replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.slpz",
            4794,
            0,
        ),
    ],
)
def test_grounded_slope_tangent_projects_ground_velocity(dataset_rel: str, record: int, p: int) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / dataset_rel
    if not ds_path.exists():
        pytest.skip(f"missing local replay dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(ds_path, record, p)
    assert int(seed["on_ground"][p]) == 1
    assert int(seed["ground_id"][p]) != int(ref["ground_id"][p]) or float(ref["speed_y_self"][p]) != 0.0
    assert float(ref["speed_y_self"][p]) != 0.0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]), abs=1e-5)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-5)
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)


@pytest.mark.integration
def test_flat_grounded_velocity_does_not_gain_vertical_component() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = (
        root
        / "replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.slpz"
    )
    if not ds_path.exists():
        pytest.skip("missing local replay dataset: dream_land_recent/FlippantEnchantedHorse.slpz")

    _seed, ref, out = _run_one_step_row(ds_path, 2340, 1)
    assert float(ref["speed_ground_x_self"][1]) == 0.0
    assert float(ref["speed_y_self"][1]) == 0.0
    assert float(out["speed_y_self"][1]) == 0.0
