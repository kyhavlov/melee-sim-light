from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_damagefly_wall_tech_entry_qgd_replay_real_lock() -> None:
    # Replay-real lock for the kept DamageFlyN -> PassiveWallJump wall-tech subset:
    # - DamageFly_Coll tries ftCo_800C1D38 before floor-tech / DownBound callbacks.
    # - ftCo_800C1D38 upgrades to PassiveWallJump when ftCo_800C1E0C is true.
    # - ftCo_800C1E64 clears hitstun ownership and applies ftColl_8007B760(..., x764) on entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E0C,ftCo_800C1E64}
    # data/common/ft_common_data.json: colanim_passivewall_x1990_frames
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0
    negative_control = 8406
    target_minus_1 = 8403
    target = 8404
    target_plus_1 = 8405
    for record in (negative_control, target_minus_1, target, target_plus_1):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["action_frame"][p]) == 12
    assert int(seed["hurtbox_state"][p]) == 0
    assert int(seed["hitstun"][p]) == 21
    assert int(seed["x680"][p]) == 18
    assert int(seed["x684"][p]) == 119
    assert int(seed["x67E"][p]) == 76
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump
    assert int(ref["action_frame"][p]) == 0
    assert int(ref["animation_index"][p]) == 203
    assert int(ref["hurtbox_state"][p]) == 2
    assert int(ref["hitstun"][p]) == 0

    locked_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "hitstun",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
        "state_flags",
    )
    for record in (negative_control, target_minus_1, target, target_plus_1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in locked_fields:
            got = out_row[field][p]
            want = ref_row[field][p]
            if field == "state_flags":
                assert list(map(int, got)) == list(map(int, want)), (
                    f"record={record} p={p} field={field} expected={list(map(int, want))} "
                    f"got={list(map(int, got))}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p={p} field={field} expected={int(want)} got={int(got)}"
                )


@pytest.mark.integration
def test_passivewalljump_timer_hold_and_launch_qgd_replay_real_lock() -> None:
    # Replay-real lock for the full PassiveWallJump startup/launch owner:
    # - ftCo_800C1E64 seeds `mv.co.passivewall.timer = p_ftCommonData->x760`.
    # - ftCo_PassiveWall_Anim decrements that hidden timer each frame, keeps animation frozen while
    #   it is nonzero, then applies wall-jump launch velocity when it reaches 0.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    locked_fields = ("action_id", "action_frame", "pos_x", "pos_y", "speed_air_x_self", "speed_y_self")
    for record in range(8405, 8414):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, 0)
        for field in locked_fields:
            got = out_row[field][0]
            want = ref_row[field][0]
            if field.startswith("pos_") or field.startswith("speed_"):
                assert float(got) == pytest.approx(float(want), abs=1e-4), (
                    f"record={record} p=0 field={field} expected={float(want)} got={float(got)}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p=0 field={field} expected={int(want)} got={int(got)}"
                )


@pytest.mark.integration
def test_passivewall_timer_allows_specialairs_after_hold_distinctcaringcobra_lock() -> None:
    # Replay-real lock for PassiveWall startup hold on the non-jump wall-tech branch:
    # - the startup timer still freezes PassiveWall action_frame at 0,
    # - once the hidden timer reaches 0, grounded/airborne IASA can take the current-frame
    #   SpecialAir path (`350` here) instead of remaining frozen indefinitely.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim,ftCo_PassiveWall_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    for record in range(4811, 4816):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, 1)
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]), (
            f"record={record} p=1 expected_action={int(ref_row['action_id'][1])} "
            f"got={int(out_row['action_id'][1])}"
        )
        assert int(out_row["action_frame"][1]) == int(ref_row["action_frame"][1]), (
            f"record={record} p=1 expected_action_frame={int(ref_row['action_frame'][1])} "
            f"got={int(out_row['action_frame'][1])}"
        )
