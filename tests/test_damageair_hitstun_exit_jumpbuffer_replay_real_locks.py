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
def test_damageair_hitstun_exit_jumpbuffer_window_is_replay_exact() -> None:
    # Replay-real lock for airborne DamageAir2 hitstun-exit IASA before a same-frame hit:
    # - ftCo_Damage_IASA forwards into ftCo_Fall_IASA_Inner once x221C_b6 clears,
    # - when mv.co.damage.x14 is active, it first injects XY and can enter JumpAerial via
    #   ftCo_800CB870 before the later hit overwrites the action state.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local replay artifacts")

    p = 1
    for rec in (608, 609, 610, 611):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
        assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
        assert float(out_row["pos_y"][p]) == pytest.approx(float(ref_row["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageair_hitstun_exit_jumpbuffer_stale_tap_buffer_does_not_spuriously_jump() -> None:
    # Negative control for the kept DamageAir hitstun-exit jump-buffer subset:
    # - doIasa refreshes mv.co.damage.x14 from the current damage timer on qualifying jump-input
    #   frames while x221C_b6 is set,
    # - so a large x14 on the first post-hitstun DamageAir IASA frame means the seed bridge is
    #   carrying an old tap rather than a recently refreshed exit intent.
    # Keep stale carried taps from spuriously entering JumpAerial on the post-hitstun rows here.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local replay artifacts")

    p = 0
    for rec in (1579, 1580, 1581):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
