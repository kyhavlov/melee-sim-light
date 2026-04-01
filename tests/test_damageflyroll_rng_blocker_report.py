from __future__ import annotations

from pathlib import Path

import pytest

from tools.eval.damageflyroll_rng_blocker_report import (
    DEFAULT_CASES,
    RngRowObservation,
    build_summary,
    observe_case,
)


def test_build_summary_identifies_blockers_and_controls() -> None:
    rows = [
        RngRowObservation(
            dataset="a.msl",
            record=10,
            p=0,
            role="blocker",
            note="blocker",
            seed_action_id=67,
            ref_action_id=91,
            out_action_id=87,
            attacker_seed_action_id=67,
            attacker_seed_action_frame=3,
            seed_last_hit_by=1,
            seed_frame_pre_random_seed=123,
            site1_count=1,
            site1_roll=0.94,
            site1_roll_window=(0.94, 0.10, 0.01, 0.97),
            phase_advance_to_lt_threshold=1,
            modeled_pre_gate_site_counts=(0, 0, 0),
            requires_unmodeled_pre_gate_consumer=True,
            roll_threshold=0.30,
        ),
        RngRowObservation(
            dataset="b.msl",
            record=20,
            p=1,
            role="positive_control",
            note="control+",
            seed_action_id=90,
            ref_action_id=91,
            out_action_id=91,
            attacker_seed_action_id=67,
            attacker_seed_action_frame=5,
            seed_last_hit_by=0,
            seed_frame_pre_random_seed=456,
            site1_count=1,
            site1_roll=0.26,
            site1_roll_window=(0.26, 0.74, 0.28, 0.45),
            phase_advance_to_lt_threshold=0,
            modeled_pre_gate_site_counts=(0, 0, 0),
            requires_unmodeled_pre_gate_consumer=False,
            roll_threshold=0.30,
        ),
        RngRowObservation(
            dataset="c.msl",
            record=21,
            p=1,
            role="negative_control",
            note="control-",
            seed_action_id=91,
            ref_action_id=91,
            out_action_id=91,
            attacker_seed_action_id=-1,
            attacker_seed_action_frame=-1,
            seed_last_hit_by=6,
            seed_frame_pre_random_seed=789,
            site1_count=0,
            site1_roll=None,
            site1_roll_window=tuple(),
            phase_advance_to_lt_threshold=None,
            modeled_pre_gate_site_counts=(0, 0, 0),
            requires_unmodeled_pre_gate_consumer=False,
            roll_threshold=0.30,
        ),
    ]
    summary = build_summary(rows)
    assert summary["case_count"] == 3
    assert len(summary["blocker_rows"]) == 1
    assert set(summary["blocker_phase_groups"]) == {"phase_plus_1"}
    assert len(summary["unmodeled_pre_gate_blockers"]) == 1
    assert len(summary["positive_controls"]) == 1
    assert len(summary["negative_controls"]) == 1
    assert "upstream RNG-consumer ownership" in summary["blocker"]
    assert "different pre-gate phase advances" in summary["blocker"]
    assert "next runtime lane needs a new upstream consumer owner" in summary["blocker"]


@pytest.mark.integration
def test_damageflyroll_rng_blocker_cases_and_controls_match_replay_real_trace_shape() -> None:
    # Replay-real blocker evidence for the kept triage lane:
    # - ftCo_8008DCE0 block_33 owns the severe-airborne DamageFlyRoll HSD_Randf gate.
    # - A blocker row is only in-scope here if the runtime already consumes the site-1 RNG pulse on
    #   the clean baseline; otherwise it is an admission blocker, not an RNG-stream blocker.
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
    assert blocker_keys == {
        ("AttachedGoodNaturedGuanaco.msl", 2694, 0),
        ("GracefulAttachedTurtle.msl", 5717, 0),
        ("TreasuredBackKangaroo.msl", 6929, 1),
    }

    for obs in summary["blocker_rows"]:
        assert int(obs["site1_count"]) == 1, obs["note"]
        assert int(obs["ref_action_id"]) == 91, obs["note"]  # DamageFlyRoll
        assert int(obs["out_action_id"]) in (87, 88), obs["note"]
        assert float(obs["site1_roll"]) >= float(obs["roll_threshold"]), obs["note"]
        assert int(obs["phase_advance_to_lt_threshold"]) in (1, 2), obs["note"]
        assert len(obs["site1_roll_window"]) == 4, obs["note"]
        assert tuple(obs["modeled_pre_gate_site_counts"]) == (0, 0, 0), obs["note"]
        assert bool(obs["requires_unmodeled_pre_gate_consumer"]), obs["note"]

    phase_by_key = {
        (obs["dataset"], int(obs["record"]), int(obs["p"])): int(obs["phase_advance_to_lt_threshold"])
        for obs in summary["blocker_rows"]
    }
    assert phase_by_key == {
        ("AttachedGoodNaturedGuanaco.msl", 2694, 0): 1,
        ("GracefulAttachedTurtle.msl", 5717, 0): 2,
        ("TreasuredBackKangaroo.msl", 6929, 1): 2,
    }
    assert set(summary["blocker_phase_groups"]) == {"phase_plus_1", "phase_plus_2"}
    assert {
        (obs["dataset"], int(obs["record"]), int(obs["p"]))
        for obs in summary["unmodeled_pre_gate_blockers"]
    } == blocker_keys

    assert len(summary["positive_controls"]) == 1
    pos = summary["positive_controls"][0]
    assert (pos["dataset"], int(pos["record"]), int(pos["p"])) == ("AttachedGoodNaturedGuanaco.msl", 6020, 0)
    assert int(pos["site1_count"]) == 1
    assert int(pos["ref_action_id"]) == int(pos["out_action_id"]) == 91
    assert float(pos["site1_roll"]) < float(pos["roll_threshold"])
    assert int(pos["phase_advance_to_lt_threshold"]) == 0
    assert pos["site1_roll_window"][0] == pytest.approx(0.26458740234375)
    assert tuple(pos["modeled_pre_gate_site_counts"]) == (0, 0, 0)
    assert not bool(pos["requires_unmodeled_pre_gate_consumer"])

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
        assert tuple(obs["modeled_pre_gate_site_counts"]) == (0, 0, 0)
        assert not bool(obs["requires_unmodeled_pre_gate_consumer"])
