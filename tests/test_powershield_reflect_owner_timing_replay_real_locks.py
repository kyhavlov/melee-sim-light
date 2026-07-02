from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


_STRICT_FIELDS = (
    "action_id",
    "action_frame",
    "animation_index",
    "hitlag",
    "hitstun",
    "instance_id",
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    spawn_id: int
    item_type: int
    note: str


@dataclass(frozen=True)
class _SpawnFrameCase:
    dataset_rel: str
    target_record: int
    item_slot: int
    reflected_owner: int
    note: str


@dataclass(frozen=True)
class _AgedCommitCase:
    dataset_rel: str
    target_record: int
    spawn_id: int
    item_type: int
    reflected_owner: int
    note: str


def _find_item_slot_by_key(items, *, spawn_id: int, item_type: int) -> int:
    for i in range(len(items)):
        it = items[i]
        if int(it["exists"]) != 1:
            continue
        if int(it["spawn_id"]) != int(spawn_id):
            continue
        if int(it["type"]) != int(item_type):
            continue
        return i
    return -1


def _assert_strict_transition_fields_match_ref_all_players(*, out_row, ref_row, record: int) -> None:
    num_players = int(ref_row["num_players"])
    for p in range(num_players):
        for field in _STRICT_FIELDS:
            got = int(out_row[field][p])
            exp = int(ref_row[field][p])
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        got_sf = out_row["state_flags"][p].tolist()
        exp_sf = ref_row["state_flags"][p].tolist()
        assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=6207,
            spawn_id=149,
            item_type=55,
            note="powershield owner timing B",
        ),
    ],
)
def test_guardon_followup_powershield_reflect_commits_owner_xda8_target_pm1_both_players(
    case: _Case,
) -> None:
    # Replay-real lock for GuardOn -> GuardReflect follow-up reflect ownership:
    # - ftCo_8009388C installs GuardReflect and ReflectDesc from GuardOn IASA.
    # - ftColl_80077464 writes the reflect snapshot and Item_80269F14 consumes owner/xDA8 before
    #   Slippi's post-frame item record.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, out, ref = _step_one_row(dataset_path, record)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)

    seed_t, out_t, ref_t = _step_one_row(dataset_path, case.target_record)
    slot_seed = _find_item_slot_by_key(seed_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_out = _find_item_slot_by_key(out_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_ref = _find_item_slot_by_key(ref_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert slot_seed >= 0 and slot_out >= 0 and slot_ref >= 0, f"{case.note}: target row item key missing"

    owner_seed = int(seed_t["items"][slot_seed]["owner"])
    owner_out = int(out_t["items"][slot_out]["owner"])
    owner_ref = int(ref_t["items"][slot_ref]["owner"])
    assert owner_out != owner_seed, f"{case.note}: target owner should transfer away from seed={owner_seed}"
    assert owner_out == owner_ref, f"{case.note}: target owner expected ref={owner_ref}, got {owner_out}"
    assert int(out_t["items"][slot_out]["instance_id"]) == int(ref_t["items"][slot_ref]["instance_id"])

    assert int(out_t["items"][slot_out]["misc2"]) == int(ref_t["items"][slot_ref]["misc2"])
    assert int(out_t["items"][slot_out]["misc3"]) == int(ref_t["items"][slot_ref]["misc3"])


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _AgedCommitCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=4828,
            spawn_id=125,
            item_type=55,
            reflected_owner=0,
            note="aged powershield reflect owner transfer A",
        ),
        _AgedCommitCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.slpz"
            ),
            target_record=7448,
            spawn_id=142,
            item_type=55,
            reflected_owner=0,
            note="aged powershield reflect owner transfer C",
        ),
    ],
)
def test_aged_powershield_reflect_commits_owner_xda8(case: _AgedCommitCase) -> None:
    # Replay-real locks for aged GuardReflect transfer rows:
    # - ftColl_80077464 writes owner/xDA8 when the laser overlaps the GuardReflect ReflectDesc.
    # - Item_80269F14 consumes the snapshot before Slippi's post-frame item record.
    # - GuardOn follow-up rows with no seed-visible x14/x18 have their own lock above.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    _, out_t, ref_t = _step_one_row(dataset_path, case.target_record)
    slot_out = _find_item_slot_by_key(out_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_ref = _find_item_slot_by_key(ref_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert slot_out >= 0 and slot_ref >= 0, f"{case.note}: target row item key missing"
    assert int(ref_t["items"][slot_ref]["owner"]) == int(case.reflected_owner), case.note
    assert int(ref_t["items"][slot_ref]["instance_id"]) == int(
        ref_t["instance_id"][case.reflected_owner]
    ), case.note

    for field in ("exists", "type", "state", "owner", "instance_id", "timer", "spawn_id"):
        got = out_t["items"][slot_out][field]
        exp = ref_t["items"][slot_ref][field]
        assert got == exp, f"{case.note}: item {field} expected={exp} got={got}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=2274,
            spawn_id=65,
            item_type=55,
            note="aged powershield no-transfer high-lane A",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=2274,
            spawn_id=65,
            item_type=55,
            note="aged powershield broadphase-only ReflectDesc row does not transfer",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=2275,
            spawn_id=65,
            item_type=55,
            note="aged powershield no-transfer high-lane B",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=9479,
            spawn_id=202,
            item_type=55,
            note="aged powershield no-transfer shield-bounce lane",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "DistinctCaringCobra.slpz"
            ),
            target_record=352,
            spawn_id=11,
            item_type=55,
            note="aged GuardReflect timer carry without x221B shield descriptor does not transfer A",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.slpz"
            ),
            target_record=10921,
            spawn_id=125,
            item_type=55,
            note="aged GuardReflect timer carry without x221B shield descriptor does not transfer B",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.slpz"
            ),
            target_record=7384,
            spawn_id=166,
            item_type=55,
            note="aged GuardReflect shield-bounce source owner blocks reflect transfer",
        ),
    ],
)
def test_aged_powershield_reflect_does_not_broad_transfer_controls(case: _Case) -> None:
    # Negative sentinels for the rejected broad same-frame/aged transfer. These GuardReflect rows
    # keep the original item owner in replay and must not take the aged owner/xDA8 commit path.
    # Decomp owner:
    # - `fp+0x221B_b0` is the live ShieldDesc bit; ftColl only checks shield collision while that
    #   descriptor is active.
    # - DCC/TCH rows carry GuardReflect timer bits but not the shield descriptor, so they remain on
    #   item shield/hit ownership instead of `ftColl_80077464` reflect-owner transfer.
    # - GAT high-lane rows can pass lbColl's 20x broadphase but miss the exact ReflectDesc.x2A8
    #   result; broadphase alone is not source authority to transfer owner/xDA8.
    # - MAJ shield-bounce rows already expose Item_80269DC8's ShieldBounced owner; that hidden
    #   callback result has precedence over reconstructing a reflected-owner snapshot from visible
    #   GuardReflect timers.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077464}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    # refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed_t, out_t, ref_t = _step_one_row(dataset_path, case.target_record)
    slot_seed = _find_item_slot_by_key(seed_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_out = _find_item_slot_by_key(out_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    slot_ref = _find_item_slot_by_key(ref_t["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert slot_seed >= 0 and slot_out >= 0 and slot_ref >= 0, f"{case.note}: target item missing"
    assert int(out_t["items"][slot_out]["owner"]) == int(ref_t["items"][slot_ref]["owner"])
    assert int(out_t["items"][slot_out]["owner"]) == int(seed_t["items"][slot_seed]["owner"])
    assert int(out_t["items"][slot_out]["instance_id"]) == int(
        ref_t["items"][slot_ref]["instance_id"]
    )


@pytest.mark.integration
def test_non_dash_same_frame_guardreflect_contact_does_not_transfer_owner_xda8() -> None:
    # Negative for the rejected broad same-frame ReflectDesc-overlap owner transfer:
    # Run/locomotion-origin GuardReflect can expose same-frame shield contact while keeping the
    # incoming laser on ShieldBounced/item-owner state. Immediate owner/xDA8 transfer is restricted
    # to the Dash-terminal scalar phase.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    seed_t, out_t, ref_t = _step_one_row(dataset_path, 6314)
    p = 0
    slot = 0
    assert int(seed_t["action_id"][p]) == 182
    assert int(seed_t["seed_prev_action_id"][p]) == 21
    assert int(seed_t["guard_reflect_timer_x14"][p]) == 2
    assert int(ref_t["action_id"][p]) == 182

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
    for field in ("exists", "type", "state", "owner", "instance_id", "direction", "vel_x"):
        assert out_t["items"][slot][field] == ref_t["items"][slot][field], field
    assert int(out_t["items"][slot]["owner"]) == int(seed_t["items"][slot]["owner"])
    assert int(out_t["items"][slot]["instance_id"]) == int(seed_t["items"][slot]["instance_id"])


@pytest.mark.integration
def test_final_x14_guardreflect_hitshield_handoff_destroys_laser() -> None:
    # Replay-real lock for final-x14 GuardReflect HitShield handoff:
    # - ftCo_GuardReflect_Anim ticks the reflect window before the row reaches normal shield-hit
    #   ownership.
    # - Item_80269DC8 routes non-bounce shield contacts to itFoxLaser_Logic94_HitShield, which
    #   destroys the projectile and enters GuardSetOff/hitlag.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local replay: TreasuredBackKangaroo.slpz")

    # Adjacent previous row is still inside the active reflect window and keeps the laser alive.
    _seed_prev, out_prev, ref_prev = _step_one_row(dataset_path, 2322)
    slot_prev_out = _find_item_slot_by_key(out_prev["items"], spawn_id=40, item_type=55)
    slot_prev_ref = _find_item_slot_by_key(ref_prev["items"], spawn_id=40, item_type=55)
    assert slot_prev_out >= 0 and slot_prev_ref >= 0
    assert int(out_prev["items"][slot_prev_out]["exists"]) == int(ref_prev["items"][slot_prev_ref]["exists"]) == 1

    seed_t, out_t, ref_t = _step_one_row(dataset_path, 2323)
    assert int(seed_t["guard_reflect_timer_x14"][0]) == 1
    assert int(seed_t["guard_reflect_timer_x18"][0]) == 3
    assert int(ref_t["action_id"][0]) == 181  # GuardSetOff
    assert int(ref_t["hitlag"][0]) == 3
    assert _find_item_slot_by_key(out_t["items"], spawn_id=40, item_type=55) < 0
    assert _find_item_slot_by_key(ref_t["items"], spawn_id=40, item_type=55) < 0


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SpawnFrameCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.slpz"
            ),
            target_record=428,
            item_slot=1,
            reflected_owner=1,
            note="AGG spawn-frame powershield reflect owner transfer A",
        ),
        _SpawnFrameCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.slpz"
            ),
            target_record=3345,
            item_slot=1,
            reflected_owner=1,
            note="AGG spawn-frame powershield reflect owner transfer B",
        ),
    ],
)
def test_spawn_frame_powershield_reflect_commits_owner_xda8(case: _SpawnFrameCase) -> None:
    # Replay-real locks for spawn-frame SpecialN projectile reflect:
    # - A newly spawned laser can overlap GuardReflect in the same item logic pass.
    # - ftColl_80077464 writes the reflect snapshot and Item_80269F14 consumes owner/xDA8 before
    #   Slippi's post-frame item record.
    # - Older reflected lasers stay covered by test_powershield_reflect_owner_timing... and remain
    #   on the staged-owner lane.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed_t, out_t, ref_t = _step_one_row(dataset_path, case.target_record)
    slot = int(case.item_slot)
    owner = int(case.reflected_owner)

    assert int(seed_t["items"][slot]["exists"]) == 0, case.note
    assert int(ref_t["items"][slot]["exists"]) == 1, case.note
    assert int(ref_t["items"][slot]["owner"]) == owner, case.note
    assert int(ref_t["items"][slot]["instance_id"]) == int(ref_t["instance_id"][owner]), case.note

    for field in ("exists", "type", "state", "owner", "instance_id", "timer", "spawn_id"):
        got = out_t["items"][slot][field]
        exp = ref_t["items"][slot][field]
        assert got == exp, f"{case.note}: item {field} expected={exp} got={got}"
