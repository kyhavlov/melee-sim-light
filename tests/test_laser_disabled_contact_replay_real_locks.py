from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


@dataclass(frozen=True)
class _DisabledContactCase:
    dataset_rel: str
    record: int
    item_slot: int
    defender_port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DisabledContactCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=4757,
            item_slot=0,
            defender_port=1,
            note="Falco laser clears on disabled hurtcaps without applying fighter damage.",
        ),
        _DisabledContactCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=4778,
            item_slot=0,
            defender_port=1,
            note="Follow-up disabled-hurtcap laser contact also clears without damage.",
        ),
    ],
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.record}-item{c.item_slot}",
)
def test_laser_disabled_hurtcaps_consume_item_without_damage(case: _DisabledContactCase) -> None:
    # Replay-real locks for the disabled-hit-status item contact lane:
    # - hurtbox_state=1 maps to `HurtCapsule_Disabled`,
    # - item BODY geometry can consume the laser,
    # - Fighter_ProcessHit damage/hitlag/hitstun must not run.
    # refs/melee/src/melee/lb/forward.h::HurtCapsuleState
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, int(case.record), int(case.defender_port))
    p = int(case.defender_port)
    item_slot = int(case.item_slot)

    assert int(seed["hurtbox_state"][p]) == 1, case.note
    assert int(seed["items"][item_slot]["exists"]) == 1, case.note
    assert int(ref["items"][item_slot]["exists"]) == 0, case.note
    assert int(out["items"][item_slot]["exists"]) == 0, case.note
    assert int(out["action_id"][p]) == int(ref["action_id"][p]), case.note
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0, case.note
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0, case.note
    assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p]), case.note


def test_vulnerable_zero_kb_laser_body_hit_still_applies_damage() -> None:
    # Negative sentinel: the disabled-contact lane must not suppress ordinary vulnerable BODY hits.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 197, 1)
    p = 1

    assert int(seed["hurtbox_state"][p]) == 0
    assert float(ref["percent"][p]) > float(seed["percent"][p])
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6)
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0


def test_passive_hidden_colanim_keeps_fox_laser_alive_without_damage() -> None:
    # Replay-real lock for the narrow Passive hidden-colanim item BODY guard:
    # - Passive uses Ft_MF_KeepColAnimHitStatus, so hidden collision status can still reject item
    #   BODY contact while Slippi's visible hurtbox_state is transitioning.
    # - This retained slice is Fox-laser-only; Falco-laser Passive contacts are covered by the
    #   negative sentinel below and remain on the normal consume path.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_Passive
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 6197, 1)
    p = 1
    slot = 0

    assert int(seed["action_id"][p]) == 199  # Passive
    assert int(seed["hurtbox_state"][p]) == 2
    assert int(seed["items"][slot]["type"]) == 54  # Fox laser
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), field
    assert int(out["combo_count"][0]) == int(ref["combo_count"][0])
    assert int(out["last_attack_landed"][0]) == int(ref["last_attack_landed"][0])
    assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
    assert int(out["last_hit_by"][p]) == int(ref["last_hit_by"][p])


def test_passive_hidden_colanim_guard_does_not_keep_falco_laser_alive() -> None:
    # Negative sentinel: the Passive hidden-colanim guard is intentionally scoped away from Falco
    # type-55 lasers, whose replay rows still consume normally.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 3886, 1)
    p = 1
    slot = 0

    assert int(seed["action_id"][p]) == 199  # Passive
    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(ref["items"][slot]["exists"]) == 0
    assert int(out["items"][slot]["exists"]) == 0
