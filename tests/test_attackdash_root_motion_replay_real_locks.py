from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            2400,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            2401,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            2403,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            2404,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            879,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            10189,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            9565,
            0,
        ),
    ],
)
def test_attackdash_phys_replay_real_root_motion_family(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real lock for the kept AttackDash Phys owner:
    # - ftCo_AttackDash_Phys calls ft_80085030 with the current facing dir.
    # - ft_80085030 uses SSANIM01 TransN root motion when the current motion has it, otherwise
    #   falls back to `p_ftCommonData->x50 * gr_friction`.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_SM_AttackDash
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)

    assert int(seed["action_id"][p]) == 50
    assert int(ref["action_id"][p]) == 50
    assert int(out["action_id"][p]) == 50

    for field in ("action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"{dataset_rel} record={record} p={p} field={field} "
            f"expected={int(ref[field][p])} got={int(out[field][p])}"
        )

    for field in ("pos_x", "speed_ground_x_self", "speed_air_x_self"):
        assert float(out[field][p]) == pytest.approx(float(ref[field][p]), abs=2e-6), (
            f"{dataset_rel} record={record} p={p} field={field} "
            f"expected={float(ref[field][p])} got={float(out[field][p])}"
        )


@pytest.mark.integration
def test_attackdash_tbk2402_adjacent_contact_cluster_exact() -> None:
    # Historical adjacent-blocker row for the kept AttackDash movement lane:
    # - seed carries explicit defender internals x198C=1 / x1994=62 while the visible merged
    #   hurtbox_state stays 0.
    # - broken output used to keep attacker motion exact but still report an invincible-only BODY
    #   contact, i.e. defender out action/hitlag/hitstun/percent = 90/6/55/112.31999969482422 and
    #   attacker out hitlag = 6 at this row.
    # - kept fix bundle now makes the entire row exact without reopening the surrounding
    #   AttackDash root-motion family.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 2402
    attacker = 1
    defender = 0
    seed, ref, out = _run_one_step_row(dataset_path, record, attacker)

    assert int(seed["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed["colanim_timer_x1994"][defender]) == 62
    assert int(seed["action_id"][attacker]) == 50
    assert int(ref["action_id"][attacker]) == 50
    assert int(out["action_id"][attacker]) == 50
    for field in ("action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][attacker]) == int(ref[field][attacker]), field
    for field in ("pos_x", "speed_ground_x_self", "speed_air_x_self"):
        assert float(out[field][attacker]) == pytest.approx(float(ref[field][attacker]), abs=2e-6), field

    assert int(seed["action_id"][defender]) == 191
    assert int(ref["action_id"][defender]) == 191
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), field
    for field in ("pos_x", "percent"):
        assert float(out[field][defender]) == pytest.approx(float(ref[field][defender]), abs=2e-6), field
