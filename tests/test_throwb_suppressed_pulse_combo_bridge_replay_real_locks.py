from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    thrower_p: int
    note: str


@dataclass(frozen=True)
class _ThrowBCallbackCase:
    dataset_rel: str
    target_record: int
    thrower_p: int
    item_slot: int
    note: str


@dataclass(frozen=True)
class _ThrowBHitCase:
    dataset_rel: str
    target_record: int
    thrower_p: int
    victim_p: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=9960,
            thrower_p=1,
            note="ThrowB pulse15 suppressed-pulse combo bookkeeping bridge",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=9962,
            thrower_p=1,
            note="ThrowB pulse17 suppressed-pulse combo bookkeeping bridge",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.slpz"
            ),
            target_record=8293,
            thrower_p=1,
            note="ThrowB non-terminal stale-crossing combo bookkeeping bridge",
        ),
    ],
)
def test_throwb_suppressed_pulse_combo_bridge_target_pm1(case: _Case) -> None:
    # Replay-real lock for the kept ThrowB bookkeeping bridge:
    # - Throw-side projectile pulses are one-shot script events consumed in ftFx_Throw_Anim.
    # - Confirmed throw-side item hits route combo tracking through ftColl_8007646C -> ftColl_800763C0.
    # - When the pulse is suppressed in an ongoing same-owner throw-laser hitstun context, keep the
    #   attacker-side combo bookkeeping aligned for the uniquely-owned victim.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target = int(case.target_record)
    rows = (target - 1, target, target + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    seed = samples[target]["seed_t"]
    thrower = int(case.thrower_p)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 220, case.note  # ThrowB
    assert int(seed["combo_count"][thrower]) > 0, case.note
    assert int(seed["throw_pulse_consumed"][thrower]) == 0, case.note
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, thrower)
        for field in ("combo_count", "last_attack_landed"):
            assert int(out_row[field][thrower]) == int(ref_row[field][thrower]), (
                f"{case.note}: record={rec} field={field} "
                f"expected={int(ref_row[field][thrower])} got={int(out_row[field][thrower])}"
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowBCallbackCase(
            dataset_rel="replays/validation/aggregate_recent/PutridJoyousOryx.slpz",
            target_record=3287,
            thrower_p=0,
            item_slot=1,
            note="Fox ThrowB first-command callback consumes article and advances combo",
        ),
        _ThrowBCallbackCase(
            dataset_rel="replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz",
            target_record=2121,
            thrower_p=1,
            item_slot=1,
            note="Falco ThrowB terminal callback consumes article and advances combo",
        ),
        _ThrowBCallbackCase(
            dataset_rel="replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz",
            target_record=8380,
            thrower_p=0,
            item_slot=1,
            note="Falco ThrowB startup carry keeps article BODY-eligible",
        ),
        _ThrowBCallbackCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.slpz"
            ),
            target_record=2116,
            thrower_p=1,
            item_slot=1,
            note="Falco ThrowB startup same-frame hb0 callback destroys article",
        ),
        _ThrowBCallbackCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.slpz"
            ),
            target_record=8093,
            thrower_p=1,
            item_slot=1,
            note="Falco ThrowB startup same-frame hb0 callback destroys later article",
        ),
        _ThrowBCallbackCase(
            dataset_rel="replays/validation/aggregate_recent/TubbyCurlyHerring.slpz",
            target_record=5094,
            thrower_p=1,
            item_slot=1,
            note="Falco ThrowB startup same-frame hb0 callback destroys TCH article",
        ),
        _ThrowBCallbackCase(
            dataset_rel="replays/validation/aggregate_recent/TubbyCurlyHerring.slpz",
            target_record=9499,
            thrower_p=1,
            item_slot=1,
            note="Falco ThrowB startup non-laser-body landed identity carries article",
        ),
        _ThrowBCallbackCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=2522,
            thrower_p=1,
            item_slot=1,
            note="primary terminal control remains before ThrowB callback consume phase",
        ),
    ],
)
def test_throwb_callback_phase_item_and_scoreboard_locks(case: _ThrowBCallbackCase) -> None:
    # Replay-real locks for the ThrowB callback-phase owner:
    # - ftAction emits one throw_flags_b0 pulse and ftFx_Throw_Anim spawns via it_8029C6CC.
    # - Item BODY callback/source bookkeeping is owned by it_8026FAC4 / it_80272460 plus
    #   ftColl_8007646C -> ftColl_800763C0.
    # - Startup carry rows seed per-HitCapsule victim state; terminal consume rows suppress the
    #   live article and advance only item-domain combo bookkeeping. Startup same-frame callback rows
    #   run hb0 BODY/give_damage/destroy before post-frame serialization.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target = int(case.target_record)
    assert int(samples.shape[0]) > target, f"replay too short for lock row: record={target}"

    seed = samples[target]["seed_t"]
    thrower = int(case.thrower_p)
    assert int(seed["action_id"][thrower]) == 220, case.note  # ThrowB

    _, ref_row, out_row = _run_one_step_row(dataset_path, target, thrower)
    assert int(out_row["combo_count"][thrower]) == int(ref_row["combo_count"][thrower]), case.note
    slot = int(case.item_slot)
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_row["items"][slot][field]) == int(ref_row["items"][slot][field]), (
            f"{case.note}: item{slot}.{field} expected={int(ref_row['items'][slot][field])} "
            f"got={int(out_row['items'][slot][field])}"
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowBHitCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=9960,
            thrower_p=1,
            victim_p=0,
            note="Falco ThrowB frame-15 pending command with no live shot spawns and hits",
        ),
        _ThrowBHitCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=9962,
            thrower_p=1,
            victim_p=0,
            note="Falco ThrowB crossed frame-18 command with no live shot spawns and hits",
        ),
    ],
)
def test_throwb_no_live_shot_command_spawns_body_hit(case: _ThrowBHitCase) -> None:
    # Replay-real lock for ftFx_Throw_Anim command authority:
    # - A set_throw_spawn_projectile pulse with only the blaster gun live must still call
    #   it_8029C6CC; same-source victim hitstun is not enough to suppress the article.
    # - The new article is then immediately owned by it_8029C4D4/it_8026FAC4 BODY collision, which
    #   applies the hit and clears the shot before post-frame serialization.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    target = int(case.target_record)
    assert int(ds.rows.shape[0]) > target, f"replay too short for lock row: record={target}"
    seed = ds.rows[target]["seed_t"]
    thrower = int(case.thrower_p)
    victim = int(case.victim_p)
    assert int(seed["action_id"][thrower]) == 220, case.note  # ThrowB
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target, thrower)
    for field in ("action_id", "hitlag", "hitstun", "last_hit_by"):
        assert int(out_row[field][victim]) == int(ref_row[field][victim]), (
            f"{case.note}: victim {field} expected={int(ref_row[field][victim])} "
            f"got={int(out_row[field][victim])}"
        )
    assert float(out_row["percent"][victim]) == pytest.approx(float(ref_row["percent"][victim]))
    assert int(out_row["combo_count"][thrower]) == int(ref_row["combo_count"][thrower]), case.note
