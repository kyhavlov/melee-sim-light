from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _ReboundStopEntryCase:
    records: tuple[int, int, int]
    rebound_port: int
    seed_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ReboundStopEntryCase(
            records=(2731, 2732, 2733),
            rebound_port=1,
            seed_action=44,
            note="QGD jab clank -> ReboundStop no-submotion entry",
        ),
        _ReboundStopEntryCase(
            records=(2989, 2990, 2991),
            rebound_port=0,
            seed_action=50,
            note="QGD dash-attack clank -> ReboundStop no-submotion entry (p0)",
        ),
        _ReboundStopEntryCase(
            records=(2989, 2990, 2991),
            rebound_port=1,
            seed_action=44,
            note="QGD jab clank -> ReboundStop no-submotion entry (p1)",
        ),
    ],
)
def test_reboundstop_entry_no_submotion_target_pm1_rows_are_replay_exact(
    case: _ReboundStopEntryCase,
) -> None:
    # Replay-real lock for the clank-owned ReboundStop entry snapshot:
    # - ftCo_80099D9C enters ReboundStop.
    # - The destination replay-visible entry row keeps no submotion (animation_index=-1) and
    #   action_frame=-1 until ReboundStop_Anim consumes into Rebound on the first !hitlag callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    #   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in case.records:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = case.records[1]
    seed_t = samples[target]["seed_t"]
    ref_t1 = samples[target]["ref_t1"]
    p = int(case.rebound_port)

    assert int(seed_t["action_id"][p]) == int(case.seed_action), case.note
    assert int(seed_t["hitlag"][p]) == 0, case.note
    assert int(ref_t1["action_id"][p]) == 237, case.note  # ReboundStop
    assert int(ref_t1["action_frame"][p]) == -1, case.note
    assert int(ref_t1["animation_index"][p]) == 0xFFFFFFFF, case.note
    assert int(ref_t1["hitlag"][p]) > 0, case.note

    for rec in case.records:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=rec,
            p=p,
        )
