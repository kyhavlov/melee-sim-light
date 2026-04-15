from __future__ import annotations

from pathlib import Path

import pytest

from tools.eval.damageflyroll_rng_blocker_report import (
    DEFAULT_CASES,
    RngRowObservation,
    _fighter_8006cda4_phase_family_details,
    build_summary,
    observe_case,
)


def test_build_summary_identifies_blockers_and_controls() -> None:
    rows = [
        RngRowObservation(
            dataset="a.msl",
            record=10,
            p=0,
            role="resolved_control",
            note="resolved",
            seed_action_id=67,
            ref_action_id=91,
            out_action_id=91,
            attacker_seed_action_id=67,
            attacker_seed_action_frame=3,
            seed_last_hit_by=1,
            seed_frame_pre_random_seed=123,
            site1_count=1,
            site1_roll=0.94,
            site1_roll_window=(0.94, 0.10, 0.01, 0.97),
            phase_advance_to_lt_threshold=1,
            modeled_pre_gate_site_counts=(0, 0, 0, 1, 0, 0),
            requires_unmodeled_pre_gate_consumer=True,
            compatible_fighter_8006cda4_total_consumes=(1,),
            fighter_8006cda4_compatible_families=("x197c_x418", "held_item_primary_x418"),
            roll_threshold=0.30,
        ),
        RngRowObservation(
            dataset="b.msl",
            record=20,
            p=1,
            role="blocker",
            note="blocker",
            seed_action_id=90,
            ref_action_id=91,
            out_action_id=88,
            attacker_seed_action_id=67,
            attacker_seed_action_frame=5,
            seed_last_hit_by=0,
            seed_frame_pre_random_seed=456,
            site1_count=1,
            site1_roll=0.26,
            site1_roll_window=(0.26, 0.74, 0.28, 0.45),
            phase_advance_to_lt_threshold=2,
            modeled_pre_gate_site_counts=(0, 0, 0, 0, 0, 0),
            requires_unmodeled_pre_gate_consumer=True,
            compatible_fighter_8006cda4_total_consumes=(2,),
            fighter_8006cda4_compatible_families=("held_item_primary_x418_plus_x197c_x418", "held_item_type3_x418_plus_x41c"),
            roll_threshold=0.30,
        ),
        RngRowObservation(
            dataset="c.msl",
            record=21,
            p=1,
            role="positive_control",
            note="control+",
            seed_action_id=91,
            ref_action_id=91,
            out_action_id=91,
            attacker_seed_action_id=-1,
            attacker_seed_action_frame=-1,
            seed_last_hit_by=6,
            seed_frame_pre_random_seed=789,
            site1_count=1,
            site1_roll=0.26,
            site1_roll_window=(0.26, 0.74, 0.28, 0.45),
            phase_advance_to_lt_threshold=0,
            modeled_pre_gate_site_counts=(0, 0, 0),
            requires_unmodeled_pre_gate_consumer=False,
            compatible_fighter_8006cda4_total_consumes=tuple(),
            fighter_8006cda4_compatible_families=tuple(),
            roll_threshold=0.30,
        ),
        RngRowObservation(
            dataset="d.msl",
            record=22,
            p=1,
            role="negative_control",
            note="control-",
            seed_action_id=91,
            ref_action_id=91,
            out_action_id=91,
            attacker_seed_action_id=-1,
            attacker_seed_action_frame=-1,
            seed_last_hit_by=6,
            seed_frame_pre_random_seed=999,
            site1_count=0,
            site1_roll=None,
            site1_roll_window=tuple(),
            phase_advance_to_lt_threshold=None,
            modeled_pre_gate_site_counts=(0, 0, 0, 0, 0, 0),
            requires_unmodeled_pre_gate_consumer=False,
            compatible_fighter_8006cda4_total_consumes=tuple(),
            fighter_8006cda4_compatible_families=tuple(),
            roll_threshold=0.30,
        ),
    ]
    summary = build_summary(rows)
    assert summary["case_count"] == 4
    assert len(summary["blocker_rows"]) == 1
    assert set(summary["blocker_phase_groups"]) == {"phase_plus_2"}
    assert len(summary["unmodeled_pre_gate_blockers"]) == 1
    assert len(summary["resolved_controls"]) == 1
    assert summary["consume_critical_schema_gap_fields"] == [
        "fighter.ftCo_8008E984_guard",
        "fighter.item_gobj_presence",
        "fighter.item_hold_is_nonheavy",
        "fighter.item_hold_projectile_empty",
        "fighter.item_hold_subtype3",
        "fighter.x197C_presence",
        "fighter.x2220_b3",
        "fighter.x2220_b4",
        "fighter.x2226_b2",
    ]
    assert summary["consume_side_effect_only_fields"] == ["fighter.x1978_presence"]
    assert set(summary["phase_minimal_seed_families"]) == {"phase_plus_1", "phase_plus_2"}
    assert [family["family"] for family in summary["phase_minimal_seed_families"]["phase_plus_2"]] == [
        "held_item_primary_x418_plus_x197c_x418",
        "held_item_type3_x418_plus_x41c",
    ]
    assert len(summary["positive_controls"]) == 1
    assert len(summary["negative_controls"]) == 1
    assert "One Fighter_8006CDA4 pre-gate consumer still remains unmodeled" in summary["blocker"]
    assert "Fighter_8006CDA4" in summary["blocker"]
    assert "x1978 is side-effect-only" in summary["blocker"]


def test_fighter_8006cda4_phase_family_details_capture_minimal_seed_fields() -> None:
    phase1 = _fighter_8006cda4_phase_family_details(1)
    assert [family["family"] for family in phase1] == [
        "x197c_x418",
        "held_item_primary_x418",
    ]
    assert phase1[0]["consume_count"] == 1
    assert phase1[0]["consume_critical_fields"] == (
        "fighter.x197C_presence",
        "fighter.x2226_b2",
    )
    assert phase1[1]["side_effect_only_fields"] == ("fighter.x1978_presence",)

    phase2 = _fighter_8006cda4_phase_family_details(2)
    assert [family["family"] for family in phase2] == [
        "held_item_primary_x418_plus_x197c_x418",
        "held_item_type3_x418_plus_x41c",
    ]
    assert phase2[1]["consume_count"] == 2
    assert phase2[1]["consume_critical_fields"] == (
        "fighter.item_gobj_presence",
        "fighter.item_hold_is_nonheavy",
        "fighter.item_hold_subtype3",
        "fighter.item_hold_projectile_empty",
        "fighter.x2220_b3",
        "fighter.x2220_b4",
        "fighter.ftCo_8008E984_guard",
        "fighter.x2226_b2",
    )


@pytest.mark.integration
def test_damageflyroll_rng_blocker_cases_and_controls_match_replay_real_trace_shape() -> None:
    # Replay-real closure evidence for the explicit Fighter_8006CDA4 pre-gate consume-count lane:
    # - ftCo_8008DCE0 block_33 owns the severe-airborne DamageFlyRoll HSD_Randf gate.
    # - The replay-facing closure is an explicit consume-count seed lane because Slippi does not
    #   expose the hidden Fighter_8006CDA4 held-item/x197C owner inputs directly.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
    root = Path(__file__).resolve().parents[1]
    dataset_paths = [root / case.dataset_rel for case in DEFAULT_CASES]
    missing = [str(path.relative_to(root)) for path in dataset_paths if not path.exists()]
    if missing:
        pytest.skip(f"missing local datasets: {', '.join(missing)}")

    observations = [observe_case(case) for case in DEFAULT_CASES]
    summary = build_summary(observations)

    blocker_keys = {(obs["dataset"], int(obs["record"]), int(obs["p"])) for obs in summary["blocker_rows"]}
    assert blocker_keys == set()

    resolved_keys = {(obs["dataset"], int(obs["record"]), int(obs["p"])) for obs in summary["resolved_controls"]}
    assert resolved_keys == {
        ("AttachedGoodNaturedGuanaco.msl", 2694, 0),
        ("GracefulAttachedTurtle.msl", 5717, 0),
        ("TreasuredBackKangaroo.msl", 6929, 1),
    }

    for obs in summary["resolved_controls"]:
        assert int(obs["site1_count"]) == 1, obs["note"]
        assert int(obs["ref_action_id"]) == int(obs["out_action_id"]) == 91, obs["note"]
        assert int(obs["phase_advance_to_lt_threshold"]) in (1, 2), obs["note"]
    tbk = next(
        obs
        for obs in summary["resolved_controls"]
        if (obs["dataset"], int(obs["record"]), int(obs["p"])) == ("TreasuredBackKangaroo.msl", 6929, 1)
    )
    assert float(tbk["site1_roll"]) >= float(tbk["roll_threshold"])
    assert int(tbk["phase_advance_to_lt_threshold"]) == 2
    assert len(tbk["site1_roll_window"]) == 4
    assert tuple(tbk["modeled_pre_gate_site_counts"]) == (0, 0, 0, 1, 1, 0)
    assert not bool(tbk["requires_unmodeled_pre_gate_consumer"])
    assert tuple(tbk["compatible_fighter_8006cda4_total_consumes"]) == (2,)
    assert tuple(tbk["fighter_8006cda4_compatible_families"]) == (
        "held_item_primary_x418_plus_x197c_x418",
        "held_item_type3_x418_plus_x41c",
    )

    assert set(summary["blocker_phase_groups"]) == set()
    assert [family["family"] for family in summary["phase_minimal_seed_families"]["phase_plus_2"]] == [
        "held_item_primary_x418_plus_x197c_x418",
        "held_item_type3_x418_plus_x41c",
    ]
    assert summary["consume_critical_schema_gap_fields"] == [
        "fighter.ftCo_8008E984_guard",
        "fighter.item_gobj_presence",
        "fighter.item_hold_is_nonheavy",
        "fighter.item_hold_projectile_empty",
        "fighter.item_hold_subtype3",
        "fighter.x197C_presence",
        "fighter.x2220_b3",
        "fighter.x2220_b4",
        "fighter.x2226_b2",
    ]
    assert summary["consume_side_effect_only_fields"] == ["fighter.x1978_presence"]
    assert summary["unmodeled_pre_gate_blockers"] == []

    assert len(summary["positive_controls"]) == 1
    pos = summary["positive_controls"][0]
    assert (pos["dataset"], int(pos["record"]), int(pos["p"])) == ("AttachedGoodNaturedGuanaco.msl", 6020, 0)
    assert int(pos["site1_count"]) == 1
    assert int(pos["ref_action_id"]) == int(pos["out_action_id"]) == 91
    assert float(pos["site1_roll"]) < float(pos["roll_threshold"])
    assert int(pos["phase_advance_to_lt_threshold"]) == 0
    assert pos["site1_roll_window"][0] == pytest.approx(0.26458740234375)
    assert tuple(pos["modeled_pre_gate_site_counts"]) == (0, 0, 0, 0, 0, 0)
    assert not bool(pos["requires_unmodeled_pre_gate_consumer"])
    assert tuple(pos["compatible_fighter_8006cda4_total_consumes"]) == tuple()
    assert tuple(pos["fighter_8006cda4_compatible_families"]) == tuple()

    neg_keys = {(obs["dataset"], int(obs["record"]), int(obs["p"])) for obs in summary["negative_controls"]}
    assert neg_keys == {
        ("AttachedGoodNaturedGuanaco.msl", 6019, 0),
        ("AttachedGoodNaturedGuanaco.msl", 6021, 0),
        ("GracefulAttachedTurtle.msl", 5716, 0),
        ("GracefulAttachedTurtle.msl", 5718, 0),
        ("TreasuredBackKangaroo.msl", 6928, 1),
        ("TreasuredBackKangaroo.msl", 6930, 1),
    }
    for obs in summary["negative_controls"]:
        assert int(obs["site1_count"]) == 0
        assert int(obs["ref_action_id"]) == int(obs["out_action_id"])
        assert obs["phase_advance_to_lt_threshold"] is None
        assert tuple(obs["site1_roll_window"]) == tuple()
        assert tuple(obs["modeled_pre_gate_site_counts"]) == (0, 0, 0, 0, 0, 0)
        assert not bool(obs["requires_unmodeled_pre_gate_consumer"])
        assert tuple(obs["compatible_fighter_8006cda4_total_consumes"]) == tuple()
        assert tuple(obs["fighter_8006cda4_compatible_families"]) == tuple()
