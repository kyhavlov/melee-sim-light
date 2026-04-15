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
    # Population guard for the two non-causal replay-facing lanes introduced by this owner pass.
    # These counts are over validation datasets only; debug datasets are regenerated for test
    # compatibility but are not part of the acceptance audit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    owner_actions = {14, 15, 16, 17, 18, 19, 20, 21, 22, 24}

    def count_lanes(rel: str) -> tuple[int, int, int, set[tuple[int, int]]]:
      turn_count = 0
      motion_count = 0
      invalid_motion = 0
      turn_transitions: set[tuple[int, int]] = set()
      for dataset_path in (root / rel).glob("**/*.msl"):
        ds = read_dataset(str(dataset_path))
        for row in ds.samples:
          for p in range(int(ds.header["num_players"])):
            if int(row["seed_t"]["turn_kneebend_facing_override_u8"][p]) != 0:
              turn_count += 1
              turn_transitions.add((int(row["seed_t"]["action_id"][p]), int(row["ref_t1"]["action_id"][p])))
            if int(row["seed_t"]["motion_entry_instance_id_override_u16"][p]) != 0:
              motion_count += 1
              sa = int(row["seed_t"]["action_id"][p])
              ra = int(row["ref_t1"]["action_id"][p])
              if sa not in owner_actions or ra not in owner_actions:
                invalid_motion += 1
      return turn_count, motion_count, invalid_motion, turn_transitions

    primary_turn, primary_motion, primary_invalid, primary_turn_transitions = count_lanes(_PRIMARY_VALID)
    aggregate_turn, aggregate_motion, aggregate_invalid, aggregate_turn_transitions = count_lanes(
        "datasets/aggregate_recent/replays/validation"
    )

    assert primary_turn == 8
    assert primary_motion == 121
    assert primary_invalid == 0
    assert primary_turn_transitions == {(18, 24)}
    assert aggregate_turn == 50
    assert aggregate_motion == 597
    assert aggregate_invalid == 0
    assert aggregate_turn_transitions == {(18, 24)}
