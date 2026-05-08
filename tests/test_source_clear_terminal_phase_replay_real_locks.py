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


@dataclass(frozen=True)
class _SourceClearTerminalPhaseCase:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_owner: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2143,
            victim_port=0,
            expect_owner=1,
            note="ref 1->out 6 residual cleanup (AGN)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=1670,
            victim_port=1,
            expect_owner=0,
            note="ref 0->out 6 residual cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=1936,
            victim_port=0,
            expect_owner=1,
            note="ref 1->out 6 residual cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=10537,
            victim_port=1,
            expect_owner=0,
            note="ref 0->out 6 residual cleanup (GAT late)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=1905,
            victim_port=0,
            expect_owner=1,
            note="Wait followup terminal cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=5208,
            victim_port=0,
            expect_owner=1,
            note="EscapeF followup terminal cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6501,
            victim_port=0,
            expect_owner=1,
            note="SpecialSEnd followup terminal cleanup (AGN)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=8198,
            victim_port=1,
            expect_owner=0,
            note="SpecialSEnd followup terminal cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=531,
            victim_port=1,
            expect_owner=0,
            note="SpecialSEnd zero-combo terminal cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=1974,
            victim_port=0,
            expect_owner=1,
            note="AttackAirN terminal followup cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=4477,
            victim_port=0,
            expect_owner=1,
            note="AttackAirLw terminal followup cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=5442,
            victim_port=1,
            expect_owner=0,
            note="Guard hold terminal followup cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=9591,
            victim_port=1,
            expect_owner=0,
            note="EscapeAir terminal followup cleanup (QGD)",
        ),
    ],
)
def test_source_clear_terminal_phase_target_pm1_both_players_strict_lock(
    case: _SourceClearTerminalPhaseCase,
) -> None:
    # Replay-real target+/-1 strict lock for x18C8 terminal clear phase bridge in src/timers.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # - refs/melee/src/melee/ft/types.h (fp+0x221F b3 gate; dmg.x18C8 countdown)
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    victim = int(case.victim_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # Terminal x18C8 row with modeled defer-phase bridge active.
    assert int(seed["source_clear_timer_x18c8"][victim]) == 1, case.note
    assert int(seed["source_clear_owner_set_phase"][victim]) == 1, case.note
    assert int(seed["source_clear_terminal_phase"][victim]) == 1, case.note
    assert int(seed["last_hit_by"][victim]) == int(case.expect_owner), case.note
    assert int(ref["last_hit_by"][victim]) == int(case.expect_owner), case.note
    # x221F_b3 gate must be clear on the terminal tick for decomp timer ownership path.
    assert (int(seed["state_flags"][victim, 4]) & 0x10) == 0, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
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


def _run_rollout_compare_row(dataset_path: Path, start_record: int, compare_record: int) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert start_record <= compare_record
    assert int(samples.shape[0]) > compare_record, f"dataset too short for compare row: record={compare_record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
    seed_bytes = seed_bytes.copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, compare_record + 1):
            prev_input_bytes = np.frombuffer(
                samples[record : record + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            )
            input_bytes = np.frombuffer(samples[record : record + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
            binding.step_input(
                handle,
                prev_input_bytes.copy().reshape(1, input_stride),
                input_bytes.copy().reshape(1, input_stride),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[compare_record]["ref_t1"]
    return out, ref


@pytest.mark.integration
def test_source_clear_terminal_phase_attackairn_without_terminal_lane_still_clears_tch() -> None:
    # Negative control: not every owner-set terminal x18C8 row retains source attribution.
    # TCH:441 is AttackAirN with timer==1 and owner-set phase, but the generated terminal seed lane
    # is clear, so Fighter_8006A360's ordinary terminal clear still publishes source 6.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 441
    victim = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][victim]) == 0x0041
    assert int(seed["source_clear_timer_x18c8"][victim]) == 1
    assert int(seed["source_clear_owner_set_phase"][victim]) == 1
    assert int(seed["source_clear_terminal_phase"][victim]) == 0

    _, ref, out = _run_one_step_row(dataset_path, record, victim)
    assert int(out["last_hit_by"][victim]) == int(ref["last_hit_by"][victim]) == 6


@pytest.mark.integration
def test_source_clear_downed_recovery_terminal_parks_owner_in_rollout_cdo() -> None:
    # CDO:12020 rolls out through an x18C8 terminal tick inherited from Dash/Jump into
    # PassiveStandF. Vanilla parks last_hit_by with the timer inactive instead of clearing source
    # at the terminal row; free rollout must reconstruct that downed/passive owner without the
    # one-step-only source_clear_terminal_phase seed lane.
    #
    # Decomp/data refs:
    # - refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c
    # - refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_PassiveStandF callback row)
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start_record = 12020
    terminal_record = 12024
    compare_record = 12025
    victim = 1
    terminal_seed = ds.samples[terminal_record]["seed_t"]
    assert int(terminal_seed["action_id"][victim]) == 0x00C8
    assert int(terminal_seed["source_clear_timer_x18c8"][victim]) == 1
    assert int(terminal_seed["source_clear_terminal_phase"][victim]) == 1
    assert int(ds.samples[start_record]["seed_t"]["source_clear_terminal_phase"][victim]) == 0

    out, ref = _run_rollout_compare_row(dataset_path, start_record, compare_record)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 0x00C8
    assert int(out["last_hit_by"][victim]) == int(ref["last_hit_by"][victim]) == 0
