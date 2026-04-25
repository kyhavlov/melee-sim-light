from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_CARDINAL = "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p_defender"),
    [
        ("GracefulAttachedTurtle.msl", 5088, 0),
        ("GracefulAttachedTurtle.msl", 6012, 0),
        ("QuerulousGrandDinosaur.msl", 9911, 0),
        ("AttachedGoodNaturedGuanaco.msl", 2259, 0),
    ],
)
def test_reciprocal_body_hit_uses_precombat_stale_damage_and_received_hitlag(
    dataset_name: str, record: int, p_defender: int
) -> None:
    # Reciprocal BODY hits can mutate the later attacker into Damage* before its own hit is applied
    # by this simplified sequential pass. The BODY damage owner must use the pre-combat HitCapsule
    # attack id for staling, and a received KB hit must overwrite same-frame deal-hitlag.
    #
    # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
    # refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CARDINAL / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    assert float(out_row["percent"][p_defender]) == pytest.approx(float(ref_row["percent"][p_defender]))
    assert int(out_row["hitlag"][p_defender]) == int(ref_row["hitlag"][p_defender])
    assert int(out_row["hitstun"][p_defender]) == int(ref_row["hitstun"][p_defender])
    assert int(out_row["action_id"][p_defender]) == int(ref_row["action_id"][p_defender])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p_defender"),
    [
        (
            "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            815,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            11469,
            1,
        ),
    ],
)
def test_reciprocal_body_hit_received_hitlag_survives_later_outgoing_hit(
    dataset_rel: str, record: int, p_defender: int
) -> None:
    # These rows process the subject's received BODY hit before its own same-frame outgoing aerial
    # hit. The outgoing deal-hitlag lane must not overwrite the received-KB hitlag.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    assert int(out_row["action_id"][p_defender]) == int(ref_row["action_id"][p_defender])
    assert int(out_row["hitlag"][p_defender]) == int(ref_row["hitlag"][p_defender])
    assert int(out_row["hitstun"][p_defender]) == int(ref_row["hitstun"][p_defender])


@pytest.mark.integration
def test_reciprocal_body_hit_stale_owner_negative_extra_contact_stays_outside_slice() -> None:
    # TBK:5247 is still an extra BODY contact/geometry residual, not stale-damage or reciprocal
    # received-hitlag ownership. This lock prevents the reciprocal-hit patch from being used as a
    # broad extra-contact suppressor.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CARDINAL / "TreasuredBackKangaroo.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, 5247, 0)
    assert int(ref_row["hitlag"][0]) == 0
    assert int(out_row["hitlag"][0]) > 0
    assert float(out_row["percent"][0]) > float(ref_row["percent"][0])
