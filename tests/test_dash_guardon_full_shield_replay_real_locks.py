from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


_CASES = (
    _Case(record=9341, note="GAT full-shield Dash family nearby negative control"),
    _Case(record=9342, note="GAT full-shield Dash target row"),
    _Case(record=9343, note="GAT full-shield Dash target+1 residual row"),
    _Case(record=9344, note="GAT full-shield Dash next-hit negative control"),
)


@pytest.mark.integration
def test_dash_full_shield_guardsetoff_hitlag_does_not_rehit_same_laser() -> None:
    # Replay-real lock for the ongoing shield-hit continuation frame:
    # - the prior Dash -> GuardOn family row already entered GuardSetOff and active shield hitlag,
    # - the same live laser must not rehurt the same shield again on the next teacher-forced step,
    # - item HitCapsule shield inserts own this continuation in vanilla; reseed must preserve it.
    # refs/melee/src/melee/it/itcoll.c::it_8027146C
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_HitShield
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 5281
    row = ds.rows[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][0]) == 181
    assert int(seed["hitlag"][0]) > 0
    assert int(seed["items"][0]["exists"]) == 1
    assert int(ref["items"][0]["exists"]) == 1
    assert float(ref["shield_hp"][0]) == pytest.approx(float(seed["shield_hp"][0]))

    _seed_row, out_row, ref_row = _step_one_row(dataset_path, record)

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_row[field][0]) == int(ref_row[field][0]), (
            f"record={record} field={field} expected={int(ref_row[field][0])} got={int(out_row[field][0])}"
        )
    assert float(out_row["shield_hp"][0]) == pytest.approx(float(ref_row["shield_hp"][0]), abs=1e-3)
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_row["items"][0][field]) == int(ref_row["items"][0][field]), (
            f"record={record} item field={field} expected={int(ref_row['items'][0][field])} "
            f"got={int(out_row['items'][0][field])}"
        )


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_dash_full_shield_guardon_family_rows(case: _Case) -> None:
    # Replay-real lock for the fresh Dash -> GuardOn full-shield snapshot suppression:
    # - Dash IASA late branch enters GuardOn via ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
    # - On the fresh no-submotion GuardOn snapshot, item shield precedence must not consume the
    #   overlapping laser before the guard admission frame becomes replay-visible.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800923B4,ftCo_800924C0}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = int(case.record)
    p = 0
    assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 9342:
        assert int(seed["action_id"][p]) == 20, case.note  # Dash
        assert int(seed["action_frame"][p]) == 10, case.note
        assert int(seed["animation_index"][p]) == 12, case.note
        assert float(seed["shield_hp"][p]) == pytest.approx(60.0), case.note
        assert int(seed["items"][0]["exists"]) == 1, case.note
        assert int(ref["action_id"][p]) == 178, case.note  # GuardOn
        assert int(ref["action_frame"][p]) == -1, case.note
        assert int(ref["animation_index"][p]) == 0xFFFFFFFF, case.note
        assert int(ref["hitlag"][p]) == 0, case.note
        assert int(ref["items"][0]["exists"]) == 1, case.note

    seed_row, out_row, ref_row = _step_one_row(dataset_path, record)

    if record == 9343:
        # GuardOn -> GuardReflect followup owner:
        # - GuardOn_Anim drains shield before GuardOn_IASA consumes the LR edge into GuardReflect,
        # - the laser then resolves through BODY damage, while guard-reflect timer bits remain
        #   visible on the damage post-frame.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        #   ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        assert int(seed_row["action_id"][p]) == 178, case.note
        assert int(seed_row["items"][0]["exists"]) == 1, case.note
        assert int(seed_row["seed_prev_action_id"][p]) == 20, case.note
        assert int(seed_row["guard_reflect_timer_x14"][p]) == 0, case.note
        assert int(seed_row["guard_reflect_timer_x18"][p]) == 0, case.note

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "facing", "jumps_left", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )

    assert float(out_row["shield_hp"][p]) == pytest.approx(float(ref_row["shield_hp"][p]), abs=1e-3)
    assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out_row["items"][0][field]) == int(ref_row["items"][0][field]), (
            f"{case.note}: item field={field} expected={int(ref_row['items'][0][field])} "
            f"got={int(out_row['items'][0][field])}"
        )
