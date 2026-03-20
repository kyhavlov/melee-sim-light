from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tools.eval.dataset import read_dataset


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
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

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
        # Residual row is intentionally kept explicit for this family: the current lane fixes the
        # fresh GuardOn admission frame, but the follow-up hit-ownership row is not yet corrected.
        assert int(seed_row["action_id"][p]) == 178, case.note
        assert int(seed_row["items"][0]["exists"]) == 1, case.note
        assert int(out_row["action_id"][p]) == 182, case.note
        assert int(out_row["action_frame"][p]) == -2, case.note
        assert int(out_row["animation_index"][p]) == 0xFFFFFFFF, case.note
        assert int(out_row["hitlag"][p]) == 0, case.note
        assert int(out_row["hitstun"][p]) == 0, case.note
        assert int(out_row["items"][0]["exists"]) == 1, case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [52, 0, 0, 112, 0], case.note
        assert [int(x) for x in ref_row["state_flags"][p]] == [36, 48, 0, 98, 0], case.note
        return

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "facing", "jumps_left", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )

    assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out_row["items"][0][field]) == int(ref_row["items"][0][field]), (
            f"{case.note}: item field={field} expected={int(ref_row['items'][0][field])} "
            f"got={int(out_row['items'][0][field])}"
        )
