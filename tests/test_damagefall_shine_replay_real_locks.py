from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _DamageFallShineCase:
    dataset_rel: str
    port: int
    target_minus_1: int
    target: int
    target_plus_1: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFallShineCase(
            dataset_rel=(
                "replays/validation/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
            ),
            port=0,
            target_minus_1=3798,
            target=3799,
            target_plus_1=3800,
            note="QGD DamageFall -> SpecialAirLwStart target family",
        ),
        _DamageFallShineCase(
            dataset_rel=(
                "replays/validation/"
                "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
            ),
            port=0,
            target_minus_1=1757,
            target=1758,
            target_plus_1=1759,
            note="TBK DamageFall -> SpecialAirLwStart target family",
        ),
    ],
)
def test_damagefall_bedge_down_enters_aerial_shine_target_family(case: _DamageFallShineCase) -> None:
    # Replay-real lock for DamageFall_IASA -> ftCo_SpecialAir_CheckInput -> SpecialAirLw entry.
    # - DamageFall IASA delegates into the common aerial special dispatcher.
    # - Fox/Falco Down-B routes through ftFx_SpecialAirLw_Enter, which enters the aerial shine
    #   startup motion state and clears KeepFastFall-owned carry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = int(case.port)
    for record in (case.target_minus_1, case.target, case.target_plus_1):
        assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    target_row = samples[case.target : case.target + 1]
    target_seed = target_row["seed_t"][0]
    target_ref = target_row["ref_t1"][0]
    assert int(target_seed["action_id"][p]) == 38, case.note  # DamageFall
    assert int(target_ref["action_id"][p]) == 365, case.note  # ftFx_MS_SpecialAirLwStart
    assert int(target_ref["action_frame"][p]) == 1, case.note
    assert int(target_ref["animation_index"][p]) == 317, case.note
    assert int(target_seed["hurtbox_state"][p]) == 0, case.note
    assert int(target_ref["hurtbox_state"][p]) == 2, case.note
    assert int(target_seed["state_flags"][p, 1]) & 0x08, case.note
    assert (int(target_ref["state_flags"][p, 1]) & 0x08) == 0, case.note

    improved_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "instance_id",
        "state_flags",
    )
    stable_fields = ("on_ground", "jumps_left")

    for record in (case.target_minus_1, case.target, case.target_plus_1):
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
