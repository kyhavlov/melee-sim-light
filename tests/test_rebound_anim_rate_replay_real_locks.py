from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@pytest.mark.integration
def test_rebound_anim_rate_qgd_family_target_and_controls() -> None:
    # Rebound anim-speed + friction ownership lock:
    # - ftCo_80099D9C derives rebound x0 / anim_start from the clank-owned x191C lane.
    # - ftCo_80099E44 enters Rebound with that anim speed.
    # - Rebound_Phys applies the grounded ft_80084F3C friction path on subsequent Rebound frames.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    #   ftCo_80099D9C,ftCo_80099E44,ftCo_Rebound_Phys}
    # refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    rebound_p = 1
    negative_control = 2735
    family_rows = (2736, 2737, 2738)

    seed_2737, ref_2737, _ = _run_one_step_row(dataset_path, 2737, rebound_p)
    assert int(seed_2737["action_id"][rebound_p]) == 238
    assert int(seed_2737["action_frame"][rebound_p]) == 0
    assert int(ref_2737["action_id"][rebound_p]) == 238
    assert int(ref_2737["action_frame"][rebound_p]) == 3
    assert int(ref_2737["animation_index"][rebound_p]) == 45

    for record in (negative_control,) + family_rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, rebound_p)

        # Target row: frame-0 Rebound should use the decomp-owned anim speed and first-step friction.
        if record == 2737:
            assert int(out_row["action_id"][rebound_p]) == int(ref_row["action_id"][rebound_p]) == 238
            assert int(out_row["action_frame"][rebound_p]) == int(ref_row["action_frame"][rebound_p]) == 3
            assert int(out_row["animation_index"][rebound_p]) == int(ref_row["animation_index"][rebound_p]) == 45
            assert float(out_row["speed_ground_x_self"][rebound_p]) == pytest.approx(
                float(ref_row["speed_ground_x_self"][rebound_p]), abs=2e-6
            )
            assert float(out_row["pos_x"][rebound_p]) == pytest.approx(
                float(ref_row["pos_x"][rebound_p]), abs=2e-6
            )
            assert int(out_row["on_ground"][rebound_p]) == int(ref_row["on_ground"][rebound_p]) == 1
            assert int(out_row["facing"][rebound_p]) == int(ref_row["facing"][rebound_p]) == 1
            continue

        # Nearby controls / flanks should stay stable on the rebound-owned fields.
        assert int(out_row["action_id"][rebound_p]) == int(ref_row["action_id"][rebound_p])
        assert int(out_row["action_frame"][rebound_p]) == int(ref_row["action_frame"][rebound_p])
        assert int(out_row["animation_index"][rebound_p]) == int(ref_row["animation_index"][rebound_p])
        if record != 2736:
            assert float(out_row["speed_ground_x_self"][rebound_p]) == pytest.approx(
                float(ref_row["speed_ground_x_self"][rebound_p]), abs=2e-6
            )
            assert float(out_row["pos_x"][rebound_p]) == pytest.approx(
                float(ref_row["pos_x"][rebound_p]), abs=2e-6
            )
        assert int(out_row["on_ground"][rebound_p]) == int(ref_row["on_ground"][rebound_p])
        assert int(out_row["facing"][rebound_p]) == int(ref_row["facing"][rebound_p])
