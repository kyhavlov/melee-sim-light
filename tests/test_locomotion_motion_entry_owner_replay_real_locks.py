from __future__ import annotations

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
            note="JumpF -> SpecialAirLwStart stays off the broad special-boundary instance lane",
        ),
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/GracefulAttachedTurtle.msl",
            record=6425,
            port=1,
            note="Landing -> SpecialLwStart stays off the broad special-boundary instance lane",
        ),
        _Case(
            dataset_rel=f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.msl",
            record=124,
            port=0,
            note="JumpF -> SpecialAirNStart stays off generic special-entry instance overrides",
        ),
    ],
)
def test_special_boundary_entries_do_not_use_motion_entry_override(case: _Case) -> None:
    # Negative guard for the rejected broad special-boundary expansion. Direct special entries keep
    # normal ft_800895E0/x2073 + plAttack_80037B08 runtime ownership unless a specific source-backed
    # callback lane owns an override, such as SpecialN Loop -> Loop restart below.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[case.record]
    p = case.port
    assert int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) == 0, case.note


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

    def count_lanes(rel: str) -> tuple[int, int, int, int, int, set[tuple[int, int]]]:
        turn_count = 0
        locomotion_motion_count = 0
        specialn_loop_count = 0
        match_flow_count = 0
        invalid_motion = 0
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
                        if is_specialn_loop_restart:
                            specialn_loop_count += 1
                        elif is_locomotion_entry:
                            locomotion_motion_count += 1
                        elif is_match_flow_entry:
                            match_flow_count += 1
                        else:
                            invalid_motion += 1
        return (
            turn_count,
            locomotion_motion_count,
            specialn_loop_count,
            match_flow_count,
            invalid_motion,
            turn_transitions,
        )

    (
        primary_turn,
        primary_locomotion_motion,
        primary_specialn,
        primary_match_flow,
        primary_invalid,
        primary_turn_transitions,
    ) = count_lanes(_PRIMARY_VALID)
    (
        aggregate_turn,
        aggregate_locomotion_motion,
        aggregate_specialn,
        aggregate_match_flow,
        aggregate_invalid,
        aggregate_turn_transitions,
    ) = count_lanes("datasets/aggregate_recent/replays/validation")

    assert primary_turn == 8
    assert primary_locomotion_motion == 121
    assert primary_specialn == 29
    assert primary_match_flow == 54
    assert primary_invalid == 0
    assert primary_turn_transitions == {(18, 24)}
    assert aggregate_turn == 50
    assert aggregate_locomotion_motion == 597
    assert aggregate_specialn == 88
    assert aggregate_match_flow == 196
    assert aggregate_invalid == 0
    assert aggregate_turn_transitions == {(18, 24)}
