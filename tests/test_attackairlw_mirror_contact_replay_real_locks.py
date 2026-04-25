from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "defender_action", "defender_on_ground"),
    [
        (7943, 69, 0),
        (7944, 69, 0),
        (11465, 56, 1),
        (11468, 56, 1),
    ],
)
def test_attackairlw_invincible_contact_does_not_start_attacker_hitlag(
    record: int, defender_action: int, defender_on_ground: int
) -> None:
    # Replay-real lock for the retained F29 AttackAirLw invincible-contact bridge:
    # - attacker is airborne AttackAirLw,
    # - defender is still in visible invincible collision status,
    # - vanilla does not start the attacker's BODY hitlag on this overlap row.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirLw"].events
    # data/moves/{fox,falco}.json moves["ftCo_SM_AttackHi3"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 1
    defender = 0
    seed, ref, out = _run_one_step_row(dataset_path, record, attacker)

    assert int(seed["action_id"][attacker]) == 69
    assert int(seed["action_id"][defender]) == defender_action
    assert int(seed["on_ground"][attacker]) == 0
    assert int(seed["on_ground"][defender]) == defender_on_ground
    assert int(seed["hurtbox_state"][defender]) == 1

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "state_flags"):
        if field == "state_flags":
            assert list(out[field][attacker]) == list(ref[field][attacker]), field
        else:
            assert int(out[field][attacker]) == int(ref[field][attacker]), field
