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
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2367,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="AttackAirN pre-action carries double Fighter_8006CDA4 stream phase (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2752,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="active-hitlag DamageFlyN <- ThrowHi state1 laser reaches DamageFlyRoll gate (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            target_record=10207,
            victim_port=0,
            expect_seed_count=3,
            expect_action_id=91,
            note="AttackAirN pre-action carries triple Fighter_8006CDA4 stream phase (PRH)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            target_record=4968,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="AttackAirN pre-action follows same-frame reciprocal gate in the global RNG stream (DCC)",
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
        assert int(seed["action_id"][victim]) in {57, 65, 67, 74, 363}, case.note
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
        assert int(seed["action_id"][victim]) in {65, 90}, case.note  # AttackAirN / DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert int(seed["hitstun"][victim]) > 0, case.note
        else:
            assert int(seed["hitstun"][victim]) == 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert attacker in (0, 1), case.note
            assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
            assert int(seed["action_frame"][attacker]) == 3, case.note
    if int(case.expect_seed_count) == 4:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) in {65, 90}, case.note  # AttackAirN / DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert int(seed["hitstun"][victim]) > 0, case.note
        else:
            assert int(seed["hitstun"][victim]) == 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert attacker in (0, 1), case.note
    if int(case.expect_seed_count) in {2, 3} and int(seed["action_id"][victim]) == 65:
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) == 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
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
def test_throwhi_capture_episode_rollout_clock_reaches_delayed_damageflyroll_gate() -> None:
    # Replay-real rollout lock for the TBK capture -> ThrowHi -> throw-laser -> DamageFlyRoll
    # episode selected from the F26 disruptive cluster.
    #
    # Source owner chain:
    # - CatchAttack/CaptureDamageHi can enter ThrowHi/ThrownHi through the common throw owner.
    # - ftCo_ThrowHi_Anim runs ftFx_Throw_Anim and keeps the victim-weight throw rate alive after
    #   release; command-active ThrowHi frame crossings serialize throw-side state1 lasers.
    # - The later item BODY hit reaches ftCo_8008DCE0's DamageFlyRoll RNG gate, which needs the
    #   replay frame-start RNG clock during validation rollout.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD398,ftCo_ThrowHi_Anim,ftCo_800DD724}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 2712
    for rec in (start_record, 2743, 2748, 2751, 2752, 2754):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    start_seed = samples[start_record]["seed_t"]
    assert int(start_seed["action_id"][0]) == 225  # CaptureDamageHi
    assert int(start_seed["grab_owner_port"][0]) == 1
    assert int(start_seed["action_id"][1]) == 217  # CatchAttack

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=start_record,
        window_records=(2743, 2748, 2751, 2752, 2754),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/tbk_throwhi_capture_damageflyroll_clock_lock.tsv",
    )

    ref_2743, out_2743, site_2743 = rows[2743]
    assert site_2743 == 0
    for row in (ref_2743, out_2743):
        state1_count = sum(
            1
            for item in row["items"]
            if int(item["exists"]) != 0
            and int(item["type"]) == 55  # Falco laser shot
            and int(item["state"]) == 1
            and int(item["owner"]) == 1
        )
        assert state1_count == 2

    for rec in (2748, 2751, 2754):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 221
        assert int(out_row["action_frame"][1]) == int(ref_row["action_frame"][1])

    ref_2752, out_2752, site_2752 = rows[2752]
    assert site_2752 == 1
    assert int(out_2752["action_id"][0]) == int(ref_2752["action_id"][0]) == 91
    assert int(out_2752["animation_index"][0]) == int(ref_2752["animation_index"][0])
    assert int(out_2752["hitlag"][0]) == int(ref_2752["hitlag"][0])


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
def test_prh_damageflytop_segment_carries_zero_consume_gate_marker_to_delayed_hit() -> None:
    # The PRH F26 cluster seeds before a DamageFlyTop segment whose later AttackAirB hit uses the
    # source-proven zero-consume DamageFlyRoll gate marker. Marker 4 must carry gate-admission
    # provenance across the same-source DamageFlyTop segment, but it still does not advance the RNG
    # stream before the HSD_Randf gate.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[1593]["seed_t"]
    target_seed = ds.samples[1612]["seed_t"]
    assert int(start_seed["action_id"][1]) == 90
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 4
    assert int(target_seed["action_id"][1]) == 90
    assert int(target_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 4

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=1593,
        window_records=(1611, 1612, 1613),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh1593_damageflytop_zero_consume_rollout.tsv",
    )
    for rec in (1611, 1612, 1613):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 1612 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[1612]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_tbk_damageflyroll_live_xrotn_pose_selects_late_attackairb_height() -> None:
    # DamageFlyRoll live XRotN hurtcap owner:
    # - ftCo_8008DCE0 enters DamageFlyRoll and immediately calls inlineA1, rotating FtPart_XRotN
    #   from current self+KB velocity.
    # - ftCo_DamageFlyRoll_Phys calls doFlyRoll before/after physics, keeping that live XRotN
    #   owner current for ftColl_80076ED8 BODY hurtcap selection.
    # - The adjacent rows prove this is not a broad late-BAir admission: the same AttackAirB
    #   hitboxes stay suppressed until the real high-hurtcap frame, which enters DamageFlyHi.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008DCE0,inlineA1,doFlyRoll,ftCo_DamageFlyRoll_Phys}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    for rec in (6993, 6994):
        seed, ref_row, out_row = _run_one_step_row(dataset_path, rec, 1)
        assert int(seed["action_id"][1]) == 91  # DamageFlyRoll
        assert int(seed["action_id"][0]) == 67  # AttackAirB
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 91
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 0
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])

    seed, ref_row, out_row = _run_one_step_row(dataset_path, 6995, 1)
    assert int(seed["action_id"][1]) == 91  # DamageFlyRoll
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 87  # DamageFlyHi
    assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 5
    assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1]) == 45


@pytest.mark.integration
def test_tbk_damageflyroll_live_xrotn_rollout_waits_for_late_attackairb_height() -> None:
    # Rollout lock for the same TBK F26 disruptive row. Starting at the disruptive seed frame must
    # not admit the late BAir BODY hit early, but must still reach the DamageFlyHi transition once
    # the live DamageFlyRoll XRotN pose and hitbox frame align.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=6941,
        window_records=(6993, 6994, 6995, 6996),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/tbk6941_damageflyroll_live_xrotn_rollout.tsv",
    )
    for rec in (6993, 6994):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 91
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 0
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])

    for rec, expected_hitlag in ((6995, 5), (6996, 4)):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 87
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == expected_hitlag
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1]) == 45


@pytest.mark.integration
def test_tbk_attackairn_segment_carries_fighter_8006cda4_stream_phase_to_delayed_hit() -> None:
    # This TBK segment seeds on the contiguous AttackAirN <- AttackAirLw damage-entry episode.
    # The explicit Fighter_8006CDA4 stream phase must survive until ftCo_8008DCE0 consumes it, but
    # must not be carried backward across unrelated grounded/action/source boundaries.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
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
    start_seed = ds.samples[2366]["seed_t"]
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 2

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=2366,
        window_records=(2366, 2367, 2368, 2378, 2399),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_tbk2366_attackairn_segment_rollout.tsv",
    )
    for rec in (2366, 2367, 2368, 2378, 2399):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 2367 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"


@pytest.mark.integration
def test_prh_damageflytop_damagefall_iasa_carries_attackairn_stream_phase_to_delayed_hit() -> None:
    # This PRH F26 segment seeds during DamageFlyTop, exits hitstun through DamageFall_IASA into
    # AttackAirN, then takes the next AttackAirB hit through ftCo_8008DCE0. The hidden
    # Fighter_8006CDA4 stream phase belongs to the same source-owned damage episode and must
    # survive the one-frame DamageFall IASA handoff into AttackAirN.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFly_IASA,ftCo_8008DCE0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[10169]["seed_t"]
    damagefall_seed = ds.samples[10206]["seed_t"]
    target_seed = ds.samples[10207]["seed_t"]
    assert int(start_seed["action_id"][0]) == 90  # DamageFlyTop
    assert int(start_seed["hitstun"][0]) > 0
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3
    assert int(damagefall_seed["action_id"][0]) == 38  # DamageFall
    assert int(damagefall_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3
    assert int(target_seed["action_id"][0]) == 65  # AttackAirN
    assert int(target_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10169,
        window_records=(10205, 10206, 10207, 10208),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh10169_damagefall_attackairn_stream_rollout.tsv",
    )
    for rec in (10205, 10206, 10207, 10208):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 10207 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[10207]
    assert int(out_target["action_id"][0]) == int(ref_target["action_id"][0]) == 91


@pytest.mark.integration
def test_damagefall_iasa_stream_phase_does_not_arm_unproven_damageflytop_controls() -> None:
    # Negative boundary for the same source-family: DamageFlyTop rows with AttackAirB nearby do not
    # receive the explicit stream lane unless a later replay-proven DamageFlyRoll gate identifies
    # the hidden Fighter_8006CDA4 phase. This keeps ordinary DamageFlyN controls outside the bridge.
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
    for rec in (1656, 1657, 1658):
        seed = ds.samples[rec]["seed_t"]
        assert int(seed["action_id"][0]) == 90  # DamageFlyTop
        assert int(seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, 1658, 0)
    assert int(ref_row["action_id"][0]) == 88  # DamageFlyN, not DamageFlyRoll.
    assert int(out_row["action_id"][0]) == 88


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


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_uses_live_rollout_rng_stream_his_1598() -> None:
    # HIS rollout lock for ftCo_8008DCE0's generic severe-airborne DamageFlyRoll gate:
    # p1 is still in SpecialAirHi when p0's AttackAirB hits. SpecialAirHi is not excluded by
    # Fighter_8006CDA4 or ftCo_8008DCE0, so the runtime rollout must admit the gate and consume the
    # live RNG stream. The teacher-forced one-step seed remains a separate hidden stream-phase
    # problem; this lock protects the free-running source owner that caused the HIS best/max red.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HungryImportantSnake.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1598]["seed_t"]
    ref = ds.samples[1598]["ref_t1"]
    assert int(seed["action_id"][1]) == 356  # SpecialAirHi
    assert int(seed["action_frame"][1]) == 15
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(ref["action_id"][1]) == 91  # DamageFlyRoll

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(1597, 1598, 1599),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/his1598_specialairhi_damageflyroll_rollout.tsv",
    )
    for rec in (1597, 1598, 1599):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 1598 else 0), (
            f"unexpected DamageFlyRoll gate pulse at {rec}"
        )


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_rejects_teacher_forced_seed_phase_agg_2864() -> None:
    # Replay-real one-step negative for the SpecialAirHi slice of ftCo_8008DCE0:
    # SpecialAirHi is admitted when a free-running rollout owns the RNG clock, but a teacher-forced
    # one-step seed does not expose enough hidden HSD_Randf stream phase. The generic
    # Fighter_8006CDA4 pre-gate consume-count lane is not sufficient to turn this seeded row into a
    # DamageFlyRoll.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2864]["seed_t"]
    ref = ds.samples[2864]["ref_t1"]
    p = 0
    assert int(seed["action_id"][p]) == 356  # SpecialAirHi
    assert int(ref["action_id"][p]) == 87  # DamageFlyHi, not DamageFlyRoll

    _, ref_row, out_row = _run_one_step_row(dataset_path, 2864, p)
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=2864, p=q)


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_rejects_exact_rollout_reseed_phase_agg_2864() -> None:
    # Replay-real rollout negative for the same SpecialAirHi owner: `reseed_seed_rollout()` exposes
    # frame-indexed rollout ownership, but a rollout that starts exactly on the hit row has not
    # advanced the hidden HSD_Randf stream beyond the seed frame. It must match the one-step
    # teacher-forced behavior and reject the DamageFlyRoll gate.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=2864,
        window_records=(2864,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/agg2864_specialairhi_exact_reseed_rollout.tsv",
    )
    ref_row, out_row, site1_count = rows[2864]
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=2864, p=q)
    assert site1_count == 0, f"unexpected DamageFlyRoll gate pulse at exact reseed row"


@pytest.mark.integration
def test_damageflyn_without_stream_phase_rejects_exact_reseed_damageflyroll_prh_8390() -> None:
    # DamageFlyN/Lw remain source-eligible for ftCo_8008DCE0's DamageFlyRoll gate, but visible
    # damage-state shape alone does not reconstruct the hidden HSD_Randf phase. PRH 8390 has no
    # Fighter_8006CDA4 pre-gate stream lane, so both one-step and exact rollout reseed must keep
    # the vanilla DamageFlyLw result instead of manufacturing DamageFlyRoll.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[8390]["seed_t"]
    ref = ds.samples[8390]["ref_t1"]
    p = 1
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][p]) == 0
    assert int(ref["action_id"][p]) == 89  # DamageFlyLw, not DamageFlyRoll

    _, ref_row, out_row = _run_one_step_row(dataset_path, 8390, p)
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=8390, p=q)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=8390,
        window_records=(8390,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh8390_damageflyn_exact_reseed_rollout.tsv",
    )
    ref_row, out_row, site1_count = rows[8390]
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=8390, p=q)
    assert site1_count == 0, f"unexpected DamageFlyRoll gate pulse at exact reseed row"
