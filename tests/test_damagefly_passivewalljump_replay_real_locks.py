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


@pytest.mark.integration
def test_passivewall_entry_uses_source_wall_anchor_and_clears_kb_distinctcaringcobra_lock() -> None:
    # Replay-real lock for PassiveWall entry placement after DamageFlyTop wall-tech:
    # - ftCo_800C1E64 snapshots the outgoing wall ECB side before the PassiveWall motion change,
    #   then ft_80081F2C runs the target-state 0xA wall projection.
    # - ftCommon_8007E2FC clears attack/self velocities on entry.
    # - The adjacent pre-Hug row proves the clear/projection is not a broad DamageFly shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: DistinctCaringCobra.msl")

    pre_record = 4809
    entry_record = 4810
    p = 1

    _seed, ref_pre, out_pre = _run_one_step_row(dataset_path, pre_record, p)
    assert int(ref_pre["action_id"][p]) == 90  # DamageFlyTop
    assert int(out_pre["action_id"][p]) == int(ref_pre["action_id"][p])
    assert float(out_pre["speed_x_attack"][p]) == pytest.approx(float(ref_pre["speed_x_attack"][p]), abs=1e-6)
    assert float(out_pre["speed_y_attack"][p]) == pytest.approx(float(ref_pre["speed_y_attack"][p]), abs=1e-6)

    seed, ref_entry, out_entry = _run_one_step_row(dataset_path, entry_record, p)
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["mpcoll_wall_kind_seed_u8"][p]) == 1
    assert int(seed["mpcoll_wall_id_seed_u16"][p]) == 13
    assert int(ref_entry["action_id"][p]) == 202  # PassiveWall

    for field in ("action_id", "animation_index", "action_frame", "facing", "hitstun"):
        assert int(out_entry[field][p]) == int(ref_entry[field][p]), (
            f"field={field} expected={int(ref_entry[field][p])} got={int(out_entry[field][p])}"
        )
    for field in ("pos_x", "pos_y", "speed_x_attack", "speed_y_attack", "speed_air_x_self", "speed_y_self"):
        assert float(out_entry[field][p]) == pytest.approx(float(ref_entry[field][p]), abs=1e-5), (
            f"field={field} expected={float(ref_entry[field][p])} got={float(out_entry[field][p])}"
        )


@pytest.mark.integration
def test_passivewalljump_terminal_x1990_clears_hurtbox_state_hvg_lock() -> None:
    # Replay-real lock for PassiveWallJump x198C/x1990 terminal ownership:
    # - ftCo_800C1E64 starts x1990 via ftColl_8007B760(..., p_ftCommonData->x764) on entry.
    # - Fighter_8006A360 decrements x1990 every frame even while mv.co.passivewall.timer freezes
    #   action_frame; the terminal x1990=1 row clears x198C before the post-frame snapshot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: HilariousVillainousGiraffe.msl")

    p = 0
    pre_record = 4680
    terminal_record = 4681
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > terminal_record

    pre_seed = samples[pre_record]["seed_t"]
    assert int(pre_seed["action_id"][p]) == 203  # PassiveWallJump
    assert int(pre_seed["action_frame"][p]) == 7
    assert int(pre_seed["colanim_timer_x1990"][p]) == 2
    _, pre_ref, pre_out = _run_one_step_row(dataset_path, pre_record, p)
    assert int(pre_out["hurtbox_state"][p]) == int(pre_ref["hurtbox_state"][p]) == 2

    seed = samples[terminal_record]["seed_t"]
    ref = samples[terminal_record]["ref_t1"]
    assert int(seed["action_id"][p]) == 203  # PassiveWallJump
    assert int(seed["action_frame"][p]) == 8
    assert int(seed["colanim_timer_x1990"][p]) == 1
    assert int(seed["colanim_hit_status_x198c"][p]) == 2
    assert int(ref["action_id"][p]) == 203
    assert int(ref["action_frame"][p]) == 9
    assert int(ref["hurtbox_state"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, terminal_record, p)
    for field in ("action_id", "action_frame", "animation_index", "hurtbox_state"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )


@pytest.mark.integration
def test_common_air_walljump_hidden_phase_seed_qgd_replay_real_lock() -> None:
    # Replay-real positive/negative controls for the common-air walljump hidden phase seed:
    # - Slippi exposes neither `fp->wall_jump_input_timer` nor `fp->x2110_walljumpWallSide`.
    # - The seed lane reconstructs only the late FD wall/underside hidden timer phase; runtime still
    #   requires ftWallJump stick-away and x670 freshness before entering PassiveWallJump.
    # - Earlier generic wall-hug rows keep sentinel seed state and must not be admitted broadly.
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    # data/stages/final_destination.json
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
    target = 9218
    for record in (627, 635, target):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(seed["action_frame"][p]) == 17
    assert int(seed["walljump_input_timer"][p]) == 9
    assert int(seed["walljump_wall_side_i8"][p]) == -1
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump

    _, ref_row, out_row = _run_one_step_row(dataset_path, target, p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]) == 203
    assert int(out_row["animation_index"][p]) == int(ref_row["animation_index"][p]) == 203

    for record in (627, 635):
        seed = samples[record]["seed_t"]
        assert int(seed["action_id"][p]) == 29  # Fall
        assert int(seed["walljump_input_timer"][p]) == 254
        assert int(seed["walljump_wall_side_i8"][p]) == 0
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        assert int(ref_row["action_id"][p]) != 203
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p])
