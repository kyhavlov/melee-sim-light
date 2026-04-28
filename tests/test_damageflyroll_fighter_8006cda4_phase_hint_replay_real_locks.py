from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_pre_combat_debug_row,
    _run_rollout_window_rows_with_trace,
    _assert_transition_identity_lock_fields_match_ref,
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _DamageFlyRoll8006CDA4Case:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_seed_count: int
    expect_action_id: int
    note: str
    expect_hb0_enable_edge: int | None = None


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2694,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackAirB carry now lands DamageFlyRoll (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=5717,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume grounded ThrownF hitlag carry now lands DamageFlyRoll (GAT)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6020,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="DamageFlyTop <- AttackAirB positive control remains DamageFlyRoll without early create-window carry (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6929,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume DamageFlyTop <- AttackAirB carry now lands DamageFlyRoll (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=11134,
            victim_port=1,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume LandingAirLw carry now lands DamageFlyRoll (GAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=5391,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackLw3 carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=1338,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume Fox SpecialLwEnd carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=2933,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=87,
            note="SpecialLwEnd entry control does not consume before the DamageFlyHi branch (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            target_record=11154,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=87,
            note="late LandingAirLw double-consume carry now lands DamageFlyHi (IAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=6002,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="LandingAirLw entry double-consume carry now lands DamageFlyRoll (PPA aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4065,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="AttackHi4 carry is admitted without an extra Fighter_8006CDA4 consume (BHH aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=4024,
            victim_port=0,
            expect_seed_count=3,
            expect_action_id=91,
            note="early AttackAirB create-edge full Fighter_8006CDA4 path lands DamageFlyRoll (PPA aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=1033,
            victim_port=0,
            expect_seed_count=4,
            expect_action_id=91,
            note="early AttackAirB DamageFlyTop uses explicit zero-consume frame-start roll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2635,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=87,
            note="early AttackAirB DamageFlyTop consumes once to avoid false DamageFlyRoll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6380,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="early AttackAirB DamageFlyTop consumes twice to reach DamageFlyRoll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7875,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="late victim DamageFlyTop still uses early AttackAirB two-consume stream phase",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7185,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="SpecialHiFall <- AttackAirB enable-edge admits DamageFlyRoll (PPA aggregate)",
            expect_hb0_enable_edge=1,
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7019,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=88,
            note="SpecialHiFall <- steady AttackAirB contact does not admit DamageFlyRoll (PPA aggregate)",
            expect_hb0_enable_edge=0,
        ),
    ],
)
def test_fighter_8006cda4_pre_gate_consume_count_replay_real_locks(
    case: _DamageFlyRoll8006CDA4Case,
) -> None:
    # Replay-real seed and one-step locks for the explicit Fighter_8006CDA4 pre-gate stream-phase
    # owner.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[int(case.target_record)]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    victim = int(case.victim_port)

    assert (
        int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == int(case.expect_seed_count)
    ), case.note

    if int(case.expect_seed_count) == 1 and int(seed["action_id"][victim]) != 90:
        assert int(seed["action_id"][victim]) in {57, 67, 74, 363}, case.note
        assert int(seed["hitlag"][victim]) == 0, case.note
    if int(case.expect_seed_count) == 1 and int(seed["action_id"][victim]) == 90:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) == 4, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 239:
        assert int(seed["action_id"][victim]) == 239, case.note  # ThrownF
        assert int(seed["hitlag"][victim]) > 0, case.note
        assert (int(seed["state_flags"][victim, 1]) & 0x10) != 0, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 90:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 90, case.note  # DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) >= 3, case.note
    if int(case.expect_seed_count) == 3:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 90, case.note  # DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) == 3, case.note
    if int(case.expect_seed_count) == 4:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 90, case.note  # DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) in (3, 4), case.note
    if case.expect_hb0_enable_edge is not None:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 358, case.note  # Fox SpecialHiFall
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        _, _, _, timing = _run_pre_combat_debug_row(dataset_path, int(case.target_record), attacker, 0)
        assert int(timing["enable_edge"]) == int(case.expect_hb0_enable_edge), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, int(case.target_record), victim)
    assert int(out_row["action_id"][victim]) == int(case.expect_action_id), case.note
    if int(case.expect_action_id) == int(ref["action_id"][victim]):
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=int(case.target_record),
                p=p,
            )


@pytest.mark.integration
def test_gat_top_f26_rollout_advances_replay_frame_rng_clock_to_delayed_damageflyroll() -> None:
    # Replay-reseeded validation rollouts must advance Slippi's frame-start RNG clock instead of
    # freezing it on the seed row. The selected GAT F26 cluster starts far before the eventual
    # AttackAirB hit; the direct one-step at 8633 is exact, but rollout only reaches the
    # ftCo_8008DCE0 DamageFlyRoll branch if the frame-start seed has advanced from the 8586 seed row.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/slippi-ssbm-asm/Recording/SendGamePreFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=8586,
        window_records=(8632, 8633, 8634),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_gat8586_replay_frame_clock_rollout.tsv",
    )
    for rec in (8632, 8633, 8634):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 8633 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[8633]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_tbk_damageflytop_segment_carries_fighter_8006cda4_stream_phase_to_delayed_hit() -> None:
    # The TBK F26 cluster seeds long before the eventual AttackAirB contact. The hidden
    # Fighter_8006CDA4 held-item branch state belongs to the victim's DamageFlyTop segment, so the
    # replay seed must carry the pending pre-gate stream phase from the segment start; runtime C must
    # not rediscover it from the later AttackAirB action-frame shape.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[3870]["seed_t"]
    assert int(start_seed["action_id"][1]) == 90
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 2

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=3870,
        window_records=(3906, 3907, 3908),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_tbk3870_damageflytop_segment_rollout.tsv",
    )
    for rec in (3906, 3907, 3908):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 3907 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[3907]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_damageflytop_f26_runtime_maps_raw_source_port_before_attacker_lookup() -> None:
    # Runtime source-owner lanes store raw Slippi source port, not compact local slot. A non-compact
    # source_port0 mapping must still find the local attacker before applying the F26 stream-phase
    # gate; treating last_hit_by=2 as local slot 2 would reject this two-player row.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def _noncompact_ports(seed_t):
        seed_t["source_port0"][0, 0] = 2
        seed_t["source_port0"][0, 1] = 3
        seed_t["last_hit_by"][0, 1] = 2

    _, ref_row, out_row = _run_one_step_row(dataset_path, 3907, 1, seed_mutator=_noncompact_ports)
    assert int(ref_row["action_id"][1]) == 91
    assert int(out_row["action_id"][1]) == 91
