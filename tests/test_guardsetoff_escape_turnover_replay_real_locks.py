from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


def _assert_branch_identity_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


@pytest.mark.integration
def test_guardsetoff_escape_turnover_window_is_replay_exact() -> None:
    # Replay-real lock for the GuardSetOff->EscapeN turnover drift:
    # - ftCommon_8007E0E4 / ftCommon_8007DD7C apply grounded fighter-overlap nudge via
    #   `p_ftCommonData->x450` before Fighter_procUpdate position integration.
    # - This exact window exercises the missing horizontal nudge during steady GuardSetOff and the
    #   same-proc GuardSetOff->EscapeN turnover.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset artifacts")

    p = 0
    for rec in (9414, 9415, 9416, 9417, 9418):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
        assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
        assert float(out_row["pos_y"][p]) == pytest.approx(float(ref_row["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_guardsetoff_guard_escape_n_same_callback_nudge_does_not_stale_carry_ppa() -> None:
    # PPA locks the GuardSetOff_Anim -> Guard -> EscapeN same-callback nudge owner:
    # - rec5364 starts as Guard with seed_prev GuardSetOff, then enters EscapeN after the common
    #   overlap nudge pass; the nudge is still owned by the GuardSetOff callback window.
    # - rec5365 is the next ordinary EscapeN row. It must not receive another GuardSetOff nudge
    #   from stale seed_prev provenance.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{ftCo_8009980C,ftCo_800998EC}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset artifacts")

    p = 1
    seed, ref_row, out_row = _run_one_step_row(dataset_path, 5364, p)
    assert int(seed["action_id"][p]) == 179  # Guard
    assert int(seed["seed_prev_action_id"][p]) == 181  # GuardSetOff
    assert int(ref_row["action_id"][p]) == 235  # EscapeN
    _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
    assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)

    seed_next, ref_next, out_next = _run_one_step_row(dataset_path, 5365, p)
    assert int(seed_next["action_id"][p]) == 235
    assert int(seed_next["seed_prev_action_id"][p]) == 179
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=p)
    assert float(out_next["pos_x"][p]) == pytest.approx(float(ref_next["pos_x"][p]), abs=1e-6)
