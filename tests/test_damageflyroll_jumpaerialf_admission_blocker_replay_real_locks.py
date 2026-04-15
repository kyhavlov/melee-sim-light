from __future__ import annotations

import csv
import os
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


def _assert_fields_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in (
        "action_id",
        "action_frame",
        "on_ground",
        "hitlag",
        "hitstun",
        "pos_y",
        "speed_y_self",
        "speed_ground_x_self",
        "speed_air_x_self",
    ):
        out_v = out_row[field][p]
        ref_v = ref_row[field][p]
        if field == "pos_y" or field.startswith("speed_"):
            assert float(out_v) == pytest.approx(float(ref_v), abs=1e-6), field
        else:
            assert int(out_v) == int(ref_v), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


def _trace_site_count(trace_path: Path, site_id: int) -> int:
    calls = 0
    with trace_path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        for row in reader:
            if int(row["site_id"]) == int(site_id):
                calls += int(row["call_count"])
    return int(calls)


@pytest.mark.integration
def test_damageflyroll_jumpaerialf_attackairb_carry_target_and_controls_are_replay_exact() -> None:
    # Replay-real lock for the remaining weak-bair / JumpAerialF admission family:
    # - ftCo_8008DCE0 block_33 evaluates the airborne DamageFlyRoll gate on the defender pre-action.
    # - Keep the extra pre-gate RNG carry narrow to the actual JumpAerialF/B <- AttackAirB severe
    #   damage-entry family and verify that target+-1 plus a broader non-hit JumpAerialF control
    #   stay stable.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = 5747
    victim = 1
    attacker = 0
    rows = (
        (target_record - 1, 0),
        (target_record, 1),
        (target_record + 1, 0),
        (5753, 0),
    )
    for rec, _ in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for fix lock: record={rec}"

    seed_t = samples[target_record]["seed_t"]
    ref_t1 = samples[target_record]["ref_t1"]
    assert int(seed_t["action_id"][victim]) == 27  # ftCo_MS_JumpAerialF
    assert int(seed_t["action_frame"][victim]) == 1
    assert int(seed_t["action_id"][attacker]) == 67  # ftCo_MS_AttackAirB
    assert int(seed_t["action_frame"][attacker]) == 13
    assert int(ref_t1["action_id"][victim]) == 91  # ftCo_MS_DamageFlyRoll
    assert int(seed_t["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0

    prev_trace_env = os.environ.get("MSL_RNG_TRACE_PATH")
    try:
        for rec, expect_site7 in rows:
            trace_path = root / f"reports/triage/qgd_jumpaerialf_attackairb_carry_{rec}.tsv"
            os.environ["MSL_RNG_TRACE_PATH"] = str(trace_path)
            _, ref_row, out_row = _run_one_step_row(dataset_path, rec, victim)

            assert int(_trace_site_count(trace_path, 7)) == int(expect_site7), rec
            _assert_fields_match_ref(out_row=out_row, ref_row=ref_row, p=victim)
            if rec == target_record:
                assert int(out_row["action_id"][victim]) == 91
                assert int(ref_row["action_id"][victim]) == 91

        # Explicit broader-family negative control:
        # - same JumpAerialF <- AttackAirB carry shape, but no damage entry this frame.
        neg_rel = (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl"
        )
        neg_path = root / neg_rel
        if not neg_path.exists():
            pytest.skip(f"missing local dataset: {neg_rel}")
        neg_record = 6490
        neg_victim = 0
        trace_path = root / f"reports/triage/agn_jumpaerialf_attackairb_carry_{neg_record}.tsv"
        os.environ["MSL_RNG_TRACE_PATH"] = str(trace_path)
        neg_seed, neg_ref, neg_out = _run_one_step_row(neg_path, neg_record, neg_victim)
        assert int(neg_seed["action_id"][neg_victim]) == 27
        assert int(neg_seed["action_frame"][neg_victim]) == 1
        assert int(neg_seed["action_id"][1]) == 67
        assert int(neg_seed["action_frame"][1]) == 16
        assert int(_trace_site_count(trace_path, 7)) == 0
        _assert_fields_match_ref(out_row=neg_out, ref_row=neg_ref, p=neg_victim)
        assert int(neg_out["action_id"][neg_victim]) == int(neg_ref["action_id"][neg_victim]) == 27
    finally:
        if prev_trace_env is None:
            os.environ.pop("MSL_RNG_TRACE_PATH", None)
        else:
            os.environ["MSL_RNG_TRACE_PATH"] = prev_trace_env
