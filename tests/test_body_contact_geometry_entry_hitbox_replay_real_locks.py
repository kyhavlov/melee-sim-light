from __future__ import annotations

import pytest
from pathlib import Path

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("DistinctCaringCobra.msl", 9272, 1),
        ("TubbyCurlyHerring.msl", 3186, 1),
    ],
)
def test_enable_edge_tiplog_phantom_rows_do_not_enter_damage(dataset_name: str, record: int, defender: int) -> None:
    # Enable-edge BODY phantom/tip-log lock:
    # - The selected HitCapsule overlaps by less than p_ftCommonData->x7A8.
    # - Vanilla starts hitlag through ftColl_80076ED8's tip-log lane but does not enter a damage
    #   motion state or write hitstun.
    # - The runtime branch is decomp-shaped and uses the collision matrix helper, not a replay row
    #   or post-admission admission bridge.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8,ftColl_8007AD18}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_sustained_attackairn_edge_near_x7a8_still_enters_damage_qgd_8332() -> None:
    # Negative sentinel for the enable-edge phantom subset. QGD:8332 is a sustained AttackAirN
    # capsule near the x7A8 boundary; vanilla enters DamageAir2, so the tip-log subset must not
    # broaden into a generic overlap-margin suppression.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 8332)
    defender = 0
    assert int(ref["action_id"][defender]) == 85  # DamageAir2
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
