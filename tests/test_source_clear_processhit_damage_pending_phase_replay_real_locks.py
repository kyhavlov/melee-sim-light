from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _SourceClearProcessHitCase:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_seed: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SourceClearProcessHitCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=5922,
            victim_port=0,
            expect_seed=0,
            note="negative control: landing-air row stays matched (GAT)",
        ),
        _SourceClearProcessHitCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=6113,
            victim_port=0,
            expect_seed=0,
            note="negative control: special-air-n loop source owner stays matched (GAT)",
        ),
        _SourceClearProcessHitCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=6137,
            victim_port=1,
            expect_seed=0,
            note="negative control: mirrored special-air-n loop source owner stays matched (GAT)",
        ),
    ],
)
def test_source_clear_processhit_damage_pending_phase_target_pm1_both_players_strict_lock(
    case: _SourceClearProcessHitCase,
) -> None:
    # Replay-real negative controls for the foundational ProcessHit source-owner clear seed lane in
    # src/timers.c. While producer logic is intentionally runtime-neutral, matched rows must remain
    # matched and the explicit seed lane must stay inactive.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    victim = int(case.victim_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    assert int(seed["source_clear_processhit_damage_pending_phase"][victim]) == int(case.expect_seed), case.note
    assert int(seed["source_clear_timer_x18c8"][victim]) > 0, case.note
    assert int(seed["source_clear_owner_set_phase"][victim]) == 1, case.note
    assert (int(seed["state_flags"][victim, 4]) & 0x10) == 0, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, victim)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
            got_last_hit_by = int(out_row["last_hit_by"][p])
            exp_last_hit_by = int(ref_row["last_hit_by"][p])
            assert (
                got_last_hit_by == exp_last_hit_by
            ), f"record={rec} p={p} field=last_hit_by expected={exp_last_hit_by} got={got_last_hit_by}"
