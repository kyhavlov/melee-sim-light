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
        / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

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


@pytest.mark.integration
def test_attackairlw_early_invincible_contact_starts_attacker_hitlag() -> None:
    # Replay-real lock for the early half of the F29 AttackAirLw invincible-contact split:
    # - the defender is still visible invincible AttackAirLw,
    # - vanilla still admits attacker-side BODY contact hitlag on this earlier body frame,
    # - later AttackAirLw/AttackHi3 invincible rows remain covered by the no-hit lock above.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirLw"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 7935
    attacker = 1
    defender = 0
    seed, ref, out = _run_one_step_row(dataset_path, record, attacker)

    assert int(seed["action_id"][attacker]) == 69
    assert int(seed["action_id"][defender]) == 69
    assert int(seed["on_ground"][attacker]) == 0
    assert int(seed["on_ground"][defender]) == 0
    assert int(seed["hurtbox_state"][defender]) == 1
    assert int(seed["action_frame"][defender]) < 10

    assert int(out["action_id"][attacker]) == int(ref["action_id"][attacker])
    assert int(out["action_frame"][attacker]) == int(ref["action_frame"][attacker])
    assert int(out["animation_index"][attacker]) == int(ref["animation_index"][attacker])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker])
    assert list(out["state_flags"][attacker]) == list(ref["state_flags"][attacker])
