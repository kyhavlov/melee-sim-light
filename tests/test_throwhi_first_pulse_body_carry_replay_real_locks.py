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
    "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz"
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
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

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
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

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


@pytest.mark.parametrize("record", [4266, 8123])
def test_throwhi_first_pulse_same_frame_spawn_body_destroy_event_rows(record: int) -> None:
    # Event-probe-backed same-frame state1 laser lifecycle:
    # - BHH:4266 and BHH:8123 emit the frame-18 it_8029C6CC spawn request.
    # - The item then runs hb0 BODY hitlist / give-damage / destroy before Slippi post-frame item
    #   serialization, so no item remains even though the command spawned a state1 article.
    # - Per-HitCapsule victim rings must stay per-hitbox: seeded hb2 carry state cannot suppress the
    #   hb0 BODY callback that owns the same-frame destroy and combo increment.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 0)
    thrower = 0
    victim = 1
    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 17
    assert int(seed["throw_command_pending_pulse_frame"][thrower]) == 18
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 0
    assert int(seed["hitstun"][victim]) > 0
    assert int(seed["last_hit_by"][victim]) == thrower

    assert int(out["items"][1]["exists"]) == int(ref["items"][1]["exists"]) == 0
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 2
    assert int(out["last_attack_landed"][thrower]) == int(ref["last_attack_landed"][thrower]) == 55


def test_throwhi_mid_pulse_front_side_contact_uses_callback_phase() -> None:
    # Adjacent sentinel: the frame-18 crossed-prev/front-side row is not a first-pulse carry; the
    # later same-character callback phase owns the consume and must match replay.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, 937, 1)
    thrower = 1

    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 18
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(ref["items"][1]["exists"]) == 0
    assert int(out["items"][1]["exists"]) == 0
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 2


def test_throwhi_crossed_prev_first_pulse_carry_keeps_replay_article() -> None:
    # Positive sentinel for the crossed-prev continuation of the first-pulse carry lane:
    # BHH:9419 seeds the live state1 article one post-frame after the frame-18 command. The v10
    # item-hitlist dump shows the victim-ring callback has already represented this article, so the
    # item must carry instead of being consumed by another BODY callback.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, 9419, 0)
    thrower = 0
    victim = 1
    item_slot = 1

    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 18
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(seed["hitstun"][victim]) > 0
    assert int(seed["last_hit_by"][victim]) == thrower
    projectile_vx = float(seed["items"][item_slot]["vel_x"])
    projectile_side_x = (float(seed["pos_x"][victim]) - float(seed["pos_x"][thrower])) * projectile_vx
    assert projectile_side_x <= 0.0

    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out["items"][item_slot][field]) == int(ref["items"][item_slot][field])
    assert int(ref["items"][item_slot]["exists"]) == 1
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 1


def test_throwhi_frame18_callback_clear_does_not_create_extra_article_or_combo() -> None:
    # Replay-real lock for the narrow ThrowHi state1 callback-clear lane:
    # - ftFx_Throw_Anim has already spawned the frame-18 throw-side state1 laser.
    # - it_8029C4D4 / it_80272460 own item BODY collision and the item callback clears the fresh
    #   article without advancing false combo bookkeeping on this same-owner hitstun row.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BHH_AGG_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BHH_AGG_DATASET}")

    seed, ref, out = _run_one_step_row(dataset_path, 9419, 0)
    thrower = 0
    victim = 1
    item_slot = 2

    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 18
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(seed["hitstun"][victim]) > 0
    assert int(seed["last_hit_by"][victim]) == thrower
    assert int(seed["instance_hit_by"][victim]) != 0

    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out["items"][item_slot][field]) == int(ref["items"][item_slot][field])
    assert int(ref["items"][item_slot]["exists"]) == 0
    assert int(ref["items"][item_slot]["owner"]) == -1
    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower]) == 1
