from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


@dataclass(frozen=True)
class _ThrowHiFirstPulseCarryCase:
    record: int
    thrower_port: int
    item_slot: int
    note: str


_BHH_AGG_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiFirstPulseCarryCase(
            record=527,
            thrower_port=1,
            item_slot=1,
            note="left-facing ThrowHi first pulse carries on the non-projectile X side",
        ),
        _ThrowHiFirstPulseCarryCase(
            record=1206,
            thrower_port=0,
            item_slot=1,
            note="mirrored ThrowHi first pulse carries on the non-projectile X side",
        ),
    ],
    ids=lambda c: f"rec{c.record}-p{c.thrower_port}",
)
def test_throwhi_first_pulse_carries_when_same_owner_victim_is_off_projectile_side(
    case: _ThrowHiFirstPulseCarryCase,
) -> None:
    # Replay-real locks for the ThrowHi first-pulse BODY carry lane:
    # - ftAction_80071974 emits throw_flags_b0 from set_throw_spawn_projectile,
    # - ftFx_Throw_Anim consumes it to spawn the throw-side laser,
    # - if the already-damaged victim is on the non-projectile X side of that first-pulse segment,
    #   the fresh article must persist and must not create false combo/source bookkeeping.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, int(case.record), int(case.thrower_port))
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    item_slot = int(case.item_slot)

    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 17, case.note
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note
    projectile_vx = float(ref["items"][item_slot]["vel_x"])
    projectile_side_x = (float(seed["pos_x"][victim]) - float(seed["pos_x"][thrower])) * projectile_vx
    assert projectile_vx != pytest.approx(0.0), case.note
    assert projectile_side_x <= 0.0, case.note

    assert int(ref["items"][item_slot]["exists"]) == 1, case.note
    assert int(ref["items"][item_slot]["type"]) == 54, case.note
    assert int(out["items"][item_slot]["exists"]) == 1, case.note
    assert int(out["items"][item_slot]["type"]) == 54, case.note
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]), case.note


def test_throwhi_first_pulse_front_side_contact_still_consumes_and_counts_combo() -> None:
    # Negative sentinel: the carry lane must not suppress a front-side first-pulse BODY contact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, 480, 1)
    thrower = 1
    victim = 0
    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 17
    front_side_dx = (float(seed["pos_x"][victim]) - float(seed["pos_x"][thrower])) * (
        1.0 if int(seed["facing"][thrower]) else -1.0
    )
    assert front_side_dx > 0.0
    assert int(ref["items"][1]["exists"]) == 0
    assert int(out["items"][1]["exists"]) == 0
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 2


def test_throwhi_mid_pulse_front_side_contact_is_not_first_pulse_carry() -> None:
    # Adjacent negative sentinel: frame-20/mid-pulse rows remain outside the first-pulse carry.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, 937, 1)
    thrower = 1

    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 18
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(ref["items"][1]["exists"]) == 0
    assert int(out["items"][1]["exists"]) == 1
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 2
