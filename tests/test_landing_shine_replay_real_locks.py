from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _LandingShineCase:
    record: int
    seed_action_id: int
    ref_action_id: int
    ref_action_frame: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _LandingShineCase(
            record=6996,
            seed_action_id=42,
            ref_action_id=42,
            ref_action_frame=18,
            note="AGG Landing shine family negative control",
        ),
        _LandingShineCase(
            record=6997,
            seed_action_id=42,
            ref_action_id=42,
            ref_action_frame=19,
            note="AGG Landing shine family target-1 row",
        ),
        _LandingShineCase(
            record=6998,
            seed_action_id=42,
            ref_action_id=360,
            ref_action_frame=1,
            note="AGG Landing -> SpecialLwStart target row",
        ),
        _LandingShineCase(
            record=6999,
            seed_action_id=360,
            ref_action_id=360,
            ref_action_frame=1,
            note="AGG Landing shine family target+1 row",
        ),
    ],
)
def test_landing_iasa_enters_ground_shine_agg_replay_real_lock(case: _LandingShineCase) -> None:
    # Replay-real lock for Landing_IASA -> ftCo_800D68C0 -> Fox/Falco SpecialLw entry.
    # - Landing_IASA dispatches grounded specials after the landing-lag gate.
    # - Shine entry uses ftFx_SpecialLw_Enter, which creates the grounded startup motion and entry
    #   hitlag/hurtbox ownership on the transition frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == int(case.seed_action_id), case.note
    assert int(ref["action_id"][p]) == int(case.ref_action_id), case.note
    assert int(ref["action_frame"][p]) == int(case.ref_action_frame), case.note

    if record == 6998:
        assert int(seed["animation_index"][p]) == 35, case.note
        assert int(ref["animation_index"][p]) == 313, case.note
        assert int(seed["hurtbox_state"][p]) == 0, case.note
        assert int(ref["hurtbox_state"][p]) == 2, case.note
        assert int(seed["hitlag"][p]) == 0, case.note
        assert int(ref["hitlag"][p]) == 5, case.note
        assert int(seed["state_flags"][p, 1]) == 0, case.note
        assert int(ref["state_flags"][p, 1]) == 0x20, case.note

    improved_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "instance_id",
        "hitlag",
        "state_flags",
    )
    stable_fields = ("on_ground", "jumps_left", "hitstun")

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in improved_fields + stable_fields:
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
