from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_AGG_VALID = "datasets/aggregate_recent/replays/validation/aggregate_recent"
_AGG_CARDINAL = "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent"
_PRIMARY_VALID = "datasets/fox_falco_fd_ucf084_recent/replays/validation"


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    port: int
    note: str
    all_players: bool = False


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG_VALID}/DistinctCaringCobra.msl",
            record=3280,
            port=1,
            note="Turn first-tick jump inherits hidden facing_after",
        ),
        _Case(
            dataset_rel=f"{_AGG_VALID}/ImpassionedAlarmedTarsier.msl",
            record=6353,
            port=0,
            note="Turn first-tick jump hidden facing_after mirror case",
        ),
    ],
)
def test_turn_kneebend_hidden_facing_lane_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for the narrow Turn->KneeBend hidden-facing lane:
    # - ftCo_Turn_IASA exposes mv.co.turn.facing_after during its early selector phase.
    # - The first replay-visible KneeBend row is the first place Slippi exposes which facing won.
    # - The explicit lane is Turn-only and must not alter the general facing owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["seed_t"]["action_id"][p]) == 18, case.note  # Turn
    assert int(row["seed_t"]["action_frame"][p]) == 1, case.note
    assert int(row["ref_t1"]["action_id"][p]) == 24, case.note  # KneeBend
    assert int(row["seed_t"]["turn_kneebend_facing_override_u8"][p]) in (1, 2), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=case.record, p=p)
    assert int(out_row["facing"][p]) == int(ref_row["facing"][p]), case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG_VALID}/BlondHardHippopotamus.msl",
            record=88,
            port=0,
            note="Dash->KneeBend does not populate the Turn hidden-facing lane",
        ),
        _Case(
            dataset_rel=f"{_AGG_VALID}/DistinctCaringCobra.msl",
            record=121,
            port=1,
            note="later Turn->KneeBend phase does not consume the first-tick Turn lane",
        ),
    ],
)
def test_turn_kneebend_hidden_facing_lane_negative_locks(case: _Case) -> None:
    # Negative locks for the Turn-only hidden-facing lane:
    # - non-Turn KneeBend entries must not populate it;
    # - later Turn phases must not consume it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["ref_t1"]["action_id"][p]) == 24, case.note  # KneeBend
    assert int(row["seed_t"]["turn_kneebend_facing_override_u8"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=case.record, p=p)
    assert int(out_row["facing"][p]) == int(ref_row["facing"][p]), case.note


def test_turnrun_exit_downstick_feeds_wait_squat_selector_replay_real_lock() -> None:
    # Replay-real lock for TurnRun anim-end ownership:
    # ftCo_TurnRun_Anim routes through fn_800CA644; when the hidden exit microphase rejects Run,
    # ft_8008A2BC enters Wait and same-frame Wait_IASA owns the Squat selector.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_VALID}/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 9690
    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 19  # TurnRun
    assert int(row["ref_t1"]["action_id"][p]) == 39  # Squat
    assert int(row["input_t"]["p"][p]["main_y"]) <= -73

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


def test_turnrun_anim_end_run_gate_uses_pre_input_then_wait_dash_replay_real_lock() -> None:
    # Replay-real lock for TurnRun anim-end input-phase ownership:
    # - ftCo_TurnRun_Anim calls fn_800CA644 in the Anim callback phase.
    # - fn_800CA644 reads the pre-input fp->input.lstick snapshot, so a current-frame opposite
    #   dash flick can fail the Run gate, route through ft_8008A2BC -> Wait, and then let
    #   destination Wait_IASA consume the current input as Dash.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_Spaghetti_8006AD10}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 2641
    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 19  # TurnRun
    assert int(row["seed_t"]["action_frame"][p]) == 19
    assert int(row["prev_input_t"]["p"][p]["main_x"]) > 0
    assert int(row["input_t"]["p"][p]["main_x"]) < 0
    assert int(row["ref_t1"]["action_id"][p]) == 20  # Dash

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


def _run_turnrun_rollout_records(
    dataset_path: Path,
    *,
    start_record: int,
    target_records: tuple[int, ...],
    ucf_enabled: bool = False,
    ucf_cardinals_1_0_enabled: bool = False,
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_max = max(target_records)
    assert int(samples.shape[0]) > target_max, f"dataset too short for record={target_max}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        samples[start_record : start_record + 1]["seed_t"]
        .copy()
        .reshape((1,))
        .view(np.uint8)
        .reshape((1, seed_stride))
        .copy()
    )
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    targets = set(target_records)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=int(ucf_enabled),
        ucf_cardinals_1_0_enabled=int(ucf_cardinals_1_0_enabled),
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_max + 1):
            row = samples[record : record + 1]
            prev_input_bytes = (
                row["prev_input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
            )
            input_bytes = row["input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare)
            if record in targets:
                out_by_record[record] = (
                    samples[record]["ref_t1"].copy(),
                    out_compare.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
                )
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_turnrun_midstate_pause_flips_and_resumes_after_ground_speed_stops_replay_real_lock() -> None:
    # Replay-real lock for the mid-state TurnRun pause owner:
    # - ftCo_TurnRun_Anim freezes rate when hidden cmd_vars[1] first fires.
    # - On the next Anim callback, if mv.co.walk.middle_anim_frame * gr_vel <= 0.01, it restores
    #   rate and flips facing.
    # - MSLFTSC1 has no decoded common-action event for ftCo_SM_TurnRun today, so this stays a
    #   narrow decomp callback model rather than a generated script-table query.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::{
    #   ftCo_TurnRun_Enter,ftCo_TurnRun_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_VALID}/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 1

    pre_stop = ds.samples[9682]
    assert int(pre_stop["seed_t"]["action_id"][p]) == 19  # TurnRun
    assert int(pre_stop["seed_t"]["action_frame"][p]) == 13
    assert float(pre_stop["seed_t"]["speed_ground_x_self"][p]) != pytest.approx(0.0)
    _, pre_ref, pre_out = _run_one_step_row(dataset_path, 9682, p)
    _assert_transition_lock_fields_match_ref(out_row=pre_out, ref_row=pre_ref, record=9682, p=p)
    assert int(pre_out["facing"][p]) == int(pre_ref["facing"][p]) == 1

    flip_seed = ds.samples[9683]
    assert int(flip_seed["seed_t"]["action_id"][p]) == 19
    assert int(flip_seed["seed_t"]["action_frame"][p]) == 13
    assert int(flip_seed["seed_t"]["facing"][p]) == 1
    assert int(flip_seed["seed_t"]["facing_dir1"][p]) == 1
    assert float(flip_seed["seed_t"]["speed_ground_x_self"][p]) == pytest.approx(0.0)
    _, flip_ref, flip_out = _run_one_step_row(dataset_path, 9683, p)
    _assert_transition_lock_fields_match_ref(out_row=flip_out, ref_row=flip_ref, record=9683, p=p)
    assert int(flip_out["action_frame"][p]) == int(flip_ref["action_frame"][p]) == 13
    assert int(flip_out["facing"][p]) == int(flip_ref["facing"][p]) == 0

    resume_seed = ds.samples[9684]
    assert int(resume_seed["seed_t"]["action_id"][p]) == 19
    assert int(resume_seed["seed_t"]["action_frame"][p]) == 13
    assert int(resume_seed["seed_t"]["facing"][p]) == 0
    assert int(resume_seed["seed_t"]["facing_dir1"][p]) == 1
    _, resume_ref, resume_out = _run_one_step_row(dataset_path, 9684, p)
    _assert_transition_lock_fields_match_ref(out_row=resume_out, ref_row=resume_ref, record=9684, p=p)
    assert int(resume_out["action_frame"][p]) == int(resume_ref["action_frame"][p]) == 14
    assert int(resume_out["facing"][p]) == int(resume_ref["facing"][p]) == 0

    post_flip_accel_seed = ds.samples[9687]
    assert int(post_flip_accel_seed["seed_t"]["action_id"][p]) == 19
    assert int(post_flip_accel_seed["seed_t"]["facing"][p]) == 0
    assert int(post_flip_accel_seed["seed_t"]["facing_dir1"][p]) == 1
    assert float(post_flip_accel_seed["seed_t"]["speed_ground_x_self"][p]) == pytest.approx(0.0)
    _, accel_ref, accel_out = _run_one_step_row(dataset_path, 9687, p)
    _assert_transition_lock_fields_match_ref(out_row=accel_out, ref_row=accel_ref, record=9687, p=p)
    assert float(accel_out["speed_ground_x_self"][p]) == pytest.approx(
        float(accel_ref["speed_ground_x_self"][p])
    )
    assert float(accel_out["speed_air_x_self"][p]) == pytest.approx(
        float(accel_ref["speed_air_x_self"][p])
    )

    rollout = _run_turnrun_rollout_records(
        dataset_path,
        start_record=9671,
        target_records=(9683, 9684, 9688),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    for record, (ref_row, out_row) in rollout.items():
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
        assert int(out_row["facing"][p]) == int(ref_row["facing"][p]), f"record={record}"
        assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
            float(ref_row["speed_ground_x_self"][p])
        ), f"record={record}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            record=3898,
            port=1,
            note="Battlefield TurnRun cmd1 pivot with post-rate x14 hidden latch",
        ),
        _Case(
            dataset_rel=f"{_AGG_VALID}/FavorableSuperficialPig.msl",
            record=2385,
            port=0,
            note="FD TurnRun cmd1 pivot at zero ground speed",
        ),
        _Case(
            dataset_rel=f"{_AGG_VALID}/ImpassionedAlarmedTarsier.msl",
            record=7755,
            port=1,
            note="FD TurnRun cmd1 pivot mirror at zero ground speed",
        ),
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/QuerulousGrandDinosaur.msl",
            record=2344,
            port=0,
            note="cardinal TurnRun cmd1 pivot after command frame",
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            record=8111,
            port=0,
            note="Pokemon Stadium TurnRun cmd1 pivot on transformed floor",
        ),
        _Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            record=9978,
            port=0,
            note="Pokemon Stadium later TurnRun cmd1 pivot on transformed floor",
        ),
    ],
)
def test_turnrun_cmd1_hidden_latch_flips_when_pivot_condition_already_satisfied(
    case: _Case,
) -> None:
    # Replay-real locks for TurnRun hidden x14 latch ownership:
    # - ftCo_TurnRun_Anim arms x14 when cmd_vars[1] is first consumed.
    # - A later Anim callback restores rate and flips facing once accel_mul * gr_vel <= 0.01.
    # - Slippi one-step seeds expose frame_speed_mul but not x14, so the runtime must use the
    #   source pivot predicate rather than treating every script-active steady row as first-arm.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["seed_t"]["action_id"][p]) == 19, case.note  # TurnRun
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == 19, case.note
    assert int(row["seed_t"]["facing"][p]) != int(row["ref_t1"]["facing"][p]), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=case.record, p=p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]) == 19, case.note
    assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
    assert int(out_row["facing"][p]) == int(ref_row["facing"][p]), case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG_VALID}/HilariousVillainousGiraffe.msl",
            record=760,
            port=0,
            note="same-frame p0/p1 motion-entry counter order swap",
        ),
        _Case(
            dataset_rel=f"{_AGG_VALID}/ImpassionedAlarmedTarsier.msl",
            record=4148,
            port=1,
            note="sticky same-frame motion-entry override across chained entry",
        ),
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/TreasuredBackKangaroo.msl",
            record=187,
            port=1,
            note="hidden prior global counter consumer before KneeBend entry",
        ),
    ],
)
def test_motion_entry_instance_order_lane_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for the explicit same-frame plAttack_80037B08 order lane.
    # The normal ft_800895E0/x2073 path remains runtime-owned; this lane only supplies the
    # replay-visible fp->x2088 value when same-frame global consumers are hidden from Slippi's
    # post-frame seed surface.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) == int(
        row["ref_t1"]["instance_id"][p]
    ), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    players = range(int(row["seed_t"]["num_players"])) if case.all_players else (p,)
    for player in players:
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=case.record,
            p=player,
        )


def test_ordinary_single_motion_entry_uses_x2073_without_instance_override() -> None:
    # Ordinary single-fighter grounded motion entry stays on the normal ft_800895E0/x2073 path.
    # The explicit override lane is only for hidden same-frame global counter order.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 88
    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 15  # WalkSlow
    assert int(row["ref_t1"]["action_id"][p]) == 20  # Dash
    assert int(row["seed_t"]["instance_id_counter"]) == int(row["ref_t1"]["instance_id"][p])
    assert int(row["seed_t"]["instance_id_x2073"][p]) != 0
    assert int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl",
            record=609,
            port=0,
            note="JumpF -> SpecialAirLwStart keeps hidden same-frame instance order",
        ),
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl",
            record=124,
            port=0,
            note="JumpF -> SpecialAirNStart keeps hidden same-frame instance order",
        ),
    ],
)
def test_special_boundary_entries_keep_hidden_same_frame_instance_order(case: _Case) -> None:
    # Replay-real lock for the seed-only same-frame plAttack_80037B08 order lane. Direct special
    # entries still use runtime ft_800895E0/x2073 ownership, but when the replay-visible post-frame id
    # proves a hidden same-frame counter order, the seed lane supplies that final fp->x2088 value.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) == int(
        row["ref_t1"]["instance_id"][p]
    ), case.note


@pytest.mark.integration
def test_specialn_loop_restart_uses_only_ft80089824_callback_lane() -> None:
    # Positive/negative lock for the only special-owned instance override kept here:
    # SpecialN Loop -> same SpecialN Loop with action_frame reset. This is tied to
    # ftFx_SpecialNLoop_Anim installing ftFx_SpecialN_OnChangeAction, which calls ft_80089824.
    # It is not a generic SpecialN Start/End or broad special-entry override.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,ftFx_SpecialN_OnChangeAction}
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    restart = ds.samples[2218]
    p = 0
    assert int(restart["seed_t"]["action_id"][p]) == 0x0159
    assert int(restart["ref_t1"]["action_id"][p]) == 0x0159
    assert int(restart["ref_t1"]["action_frame"][p]) == 0
    assert int(restart["seed_t"]["motion_entry_instance_id_override_u16"][p]) == int(
        restart["ref_t1"]["instance_id"][p]
    )

    steady_loop = ds.samples[2217]
    assert int(steady_loop["seed_t"]["action_id"][p]) == 0x0159
    assert int(steady_loop["ref_t1"]["action_id"][p]) == 0x0159
    assert int(steady_loop["seed_t"]["motion_entry_instance_id_override_u16"][p]) == 0

    start_to_loop = ds.samples[100]
    assert int(start_to_loop["seed_t"]["action_id"][p]) == 0x0158
    assert int(start_to_loop["ref_t1"]["action_id"][p]) == 0x0159
    assert int(start_to_loop["seed_t"]["motion_entry_instance_id_override_u16"][p]) == 0


def test_motion_entry_instance_override_clears_after_one_step() -> None:
    # The override lane is one-step replay-facing state. If it is seeded on a non-entry row, it must
    # clear before a later rollout transition can consume it.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 86
    p = 1
    row0 = ds.samples[record]
    row1 = ds.samples[record + 1]
    assert int(row0["seed_t"]["action_id"][p]) == int(row0["ref_t1"]["action_id"][p])
    assert int(row1["seed_t"]["action_id"][p]) != int(row1["ref_t1"]["action_id"][p])
    assert int(row1["seed_t"]["motion_entry_instance_id_override_u16"][p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed0 = row0["seed_t"].copy().reshape((1,))
    seed0["motion_entry_instance_id_override_u16"][0, p] = np.uint16(50000)
    seed0_bytes = seed0.view(np.uint8).reshape((1, seed_stride)).copy()
    prev0_bytes = row0["prev_input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
    inp0_bytes = row0["input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
    prev1_bytes = row1["prev_input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
    inp1_bytes = row1["input_t"].copy().reshape((1,)).view(np.uint8).reshape((1, input_stride)).copy()
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed0_bytes)
        binding.step_input(handle, prev0_bytes, inp0_bytes)
        binding.step_input(handle, prev1_bytes, inp1_bytes)
        binding.write_compare(handle, out_compare)
    finally:
        binding.destroy(handle)

    out_row = out_compare.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(out_row["action_id"][p]) == int(row1["ref_t1"]["action_id"][p])
    assert int(out_row["instance_id"][p]) == int(row1["ref_t1"]["instance_id"][p])


def test_noncausal_locomotion_lane_population_stays_narrow() -> None:
    # Population guard for the non-causal replay-facing lanes introduced by these owner passes.
    # These counts are over validation datasets only; debug datasets are regenerated for test
    # compatibility but are not part of the acceptance audit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    owner_actions = {14, 15, 16, 17, 18, 19, 20, 21, 22, 24}

    specialn_loop_actions = {0x0156, 0x0159}
    match_flow_entry_actions = {0, 1, 2, 12, 13, 29}
    match_flow_source_actions = {4, 12, 13}
    guard_collision_actions = {178, 179, 180, 181, 182}

    def hidden_order_family(action_id: int) -> str:
        if 14 <= action_id <= 24:
            return "locomotion"
        if 25 <= action_id <= 43:
            return "jump_landing"
        if 44 <= action_id <= 69:
            return "attack"
        if 75 <= action_id <= 91:
            return "damage"
        if 178 <= action_id <= 182:
            return "guard"
        if 212 <= action_id <= 227:
            return "grab_capture"
        if 235 <= action_id <= 255:
            return "cliff"
        if 344 <= action_id <= 369:
            return "fox_falco_special"
        return "other"

    def count_lanes(
        rel: str,
    ) -> tuple[
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        Counter[str],
        Counter[int],
        set[tuple[int, int]],
    ]:
        turn_count = 0
        locomotion_motion_count = 0
        specialn_loop_count = 0
        match_flow_count = 0
        guard_collision_count = 0
        hidden_order_count = 0
        attacklw3_runtime_count = 0
        hidden_order_families: Counter[str] = Counter()
        hidden_order_ref_actions: Counter[int] = Counter()
        turn_transitions: set[tuple[int, int]] = set()
        for dataset_path in (root / rel).glob("**/*.msl"):
            ds = read_dataset(str(dataset_path))
            for row in ds.samples:
                num_players = int(ds.header["num_players"])
                for p in range(num_players):
                    if int(row["seed_t"]["turn_kneebend_facing_override_u8"][p]) != 0:
                        turn_count += 1
                        turn_transitions.add(
                            (int(row["seed_t"]["action_id"][p]), int(row["ref_t1"]["action_id"][p]))
                        )
                    if int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) != 0:
                        sa = int(row["seed_t"]["action_id"][p])
                        ra = int(row["ref_t1"]["action_id"][p])
                        raf = int(row["ref_t1"]["action_frame"][p])
                        is_specialn_loop_restart = sa == ra and ra in specialn_loop_actions and raf == 0
                        is_locomotion_entry = (
                            not is_specialn_loop_restart
                            and sa in owner_actions
                            and ra in owner_actions
                        )
                        # Match-flow entry/exit states consume the same plAttack_80037B08 stream.
                        # Keep this classification tied to decomp-defined Dead/Rebirth/Fall
                        # motion-state handoffs, not to arbitrary action boundaries.
                        # refs/melee/src/melee/ft/ft_0D4D.c::{
                        #   ftCo_800D4FF4,ftCo_RebirthWait_Anim,ftCo_RebirthWait_IASA}
                        # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{
                        #   ft_800895E0,ft_80089824}
                        is_match_flow_entry = (
                            not is_specialn_loop_restart
                            and not is_locomotion_entry
                            and (ra in match_flow_entry_actions or sa in match_flow_source_actions)
                        )
                        # Guard collision rows use the same explicit replay-facing instance-id lane:
                        # shield hits enter GuardSetOff through ftCo_80092F2C, and Guard/GuardOn/
                        # GuardReflect/GuardOff handoffs use Fighter_ChangeMotionState in the same
                        # global plAttack_80037B08 stream as the opponent's same-frame entry.
                        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
                        #   ftCo_80092F2C,ftCo_Guard_Anim}
                        # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
                        # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
                        is_guard_collision_entry = (
                            not is_specialn_loop_restart
                            and not is_locomotion_entry
                            and not is_match_flow_entry
                            and (sa in guard_collision_actions or ra in guard_collision_actions)
                        )
                        if is_specialn_loop_restart:
                            specialn_loop_count += 1
                        elif is_locomotion_entry:
                            locomotion_motion_count += 1
                        elif is_match_flow_entry:
                            match_flow_count += 1
                        elif is_guard_collision_entry:
                            guard_collision_count += 1
                        elif ra == 57:
                            attacklw3_runtime_count += 1
                        else:
                            # General hidden same-frame counter order lane: Slippi exposes the t+1
                            # post-frame fp->x2088 value, but not HSD proc order or hidden
                            # same-frame counter consumers. This is a non-causal teacher-forced
                            # seed surface, not a live-runtime gameplay rule.
                            # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
                            # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
                            hidden_order_count += 1
                            hidden_order_families[hidden_order_family(ra)] += 1
                            hidden_order_ref_actions[ra] += 1
        return (
            turn_count,
            locomotion_motion_count,
            specialn_loop_count,
            match_flow_count,
            guard_collision_count,
            hidden_order_count,
            attacklw3_runtime_count,
            hidden_order_families,
            hidden_order_ref_actions,
            turn_transitions,
        )

    (
        primary_turn,
        primary_locomotion_motion,
        primary_specialn,
        primary_match_flow,
        primary_guard_collision,
        primary_hidden_order,
        primary_attacklw3_runtime,
        primary_hidden_order_families,
        primary_hidden_order_ref_actions,
        primary_turn_transitions,
    ) = count_lanes(_PRIMARY_VALID)
    (
        aggregate_turn,
        aggregate_locomotion_motion,
        aggregate_specialn,
        aggregate_match_flow,
        aggregate_guard_collision,
        aggregate_hidden_order,
        aggregate_attacklw3_runtime,
        aggregate_hidden_order_families,
        aggregate_hidden_order_ref_actions,
        aggregate_turn_transitions,
    ) = count_lanes("datasets/aggregate_recent/replays/validation")

    assert primary_turn == 8
    assert primary_locomotion_motion == 121
    assert primary_specialn == 29
    assert primary_match_flow == 54
    assert primary_guard_collision == 164
    assert primary_hidden_order == 1034
    assert primary_attacklw3_runtime == 0
    assert primary_hidden_order_families == {
        "attack": 33,
        "cliff": 67,
        "damage": 103,
        "fox_falco_special": 105,
        "grab_capture": 257,
        "jump_landing": 256,
        "locomotion": 199,
        "other": 14,
    }
    assert dict(primary_hidden_order_ref_actions.most_common(25)) == {
        43: 91,
        360: 69,
        39: 63,
        227: 60,
        216: 60,
        20: 59,
        18: 57,
        213: 44,
        42: 40,
        90: 35,
        221: 34,
        241: 34,
        25: 32,
        226: 29,
        14: 25,
        15: 23,
        88: 19,
        16: 18,
        365: 17,
        24: 14,
        69: 13,
        27: 12,
        344: 10,
        26: 10,
        67: 8,
    }
    assert primary_turn_transitions == {(18, 24)}
    # Aggregate population moves with the current canonical aggregate .msl set. This is a direct
    # seed-lane fixture census over checked-in datasets, not runtime output; validation_report_diff
    # must stay unchanged for behavior-neutral runtime refactors.
    assert aggregate_turn == 87
    assert aggregate_locomotion_motion == 1182
    assert aggregate_specialn == 125
    assert aggregate_match_flow == 398
    assert aggregate_guard_collision == 1242
    assert aggregate_hidden_order == 7543
    assert aggregate_attacklw3_runtime == 0
    assert aggregate_hidden_order_families == {
        "attack": 446,
        "cliff": 371,
        "damage": 736,
        "fox_falco_special": 772,
        "grab_capture": 979,
        "jump_landing": 2494,
        "locomotion": 1625,
        "other": 120,
    }
    assert dict(aggregate_hidden_order_ref_actions.most_common(25)) == {
        43: 933,
        39: 633,
        18: 469,
        360: 468,
        20: 401,
        25: 340,
        42: 305,
        90: 255,
        24: 211,
        14: 205,
        15: 193,
        216: 191,
        213: 189,
        227: 185,
        88: 174,
        241: 153,
        221: 153,
        365: 149,
        27: 130,
        226: 129,
        16: 105,
        69: 99,
        67: 99,
        344: 99,
        65: 91,
    }
    assert aggregate_turn_transitions == {(18, 24)}
