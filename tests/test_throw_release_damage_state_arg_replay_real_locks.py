from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


ACT_THROW_F = 219
ACT_THROW_B = 220
ACT_THROW_LW = 222
ACT_THROWN_F = 239
ACT_THROWN_B = 240
ACT_THROWN_LW = 242
ACT_DAMAGE_AIR_3 = 86
ACT_DAMAGE_FLY_N = 88
ACT_DAMAGE_FLY_TOP = 90


@dataclass(frozen=True)
class _Case:
    dataset: str
    record: int
    thrower: int
    victim: int


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case("InternalPowerlessWallaby.slpz", 7365, 1, 0),
        _Case("ExtraLargeScaryHornet.slpz", 456, 0, 1),
        _Case("VigorousRelievedLlama.slpz", 5436, 0, 1),
    ],
)
def test_throwlw_release_forces_damageflytop_state_arg(case: _Case) -> None:
    # Low-throw release damage-state argument:
    # ftCo_800DD724 calls ftCo_800DE7C0(victim, thrower, fp->motion_id == ThrowLw). For ThrowLw,
    # calcKnockbackAngle(true) returns 90, and ftCo_8008DCE0 treats a non--1 arg as tumble
    # severity before forcing the final motion id to that arg. These low-percent official rows
    # therefore enter DamageFlyTop, not DamageAir3, even though percent/hitstun are otherwise
    # aligned.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE7C0,calcKnockbackAngle}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth" / case.dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.record]
    assert int(row["seed_t"]["action_id"][case.thrower]) == ACT_THROW_LW
    assert int(row["seed_t"]["action_id"][case.victim]) == ACT_THROWN_LW
    assert int(row["seed_t"]["grab_owner_port"][case.victim]) == case.thrower
    assert int(row["ref_t1"]["action_id"][case.victim]) == ACT_DAMAGE_FLY_TOP

    _, ref, out = _run_one_step_row(dataset_path, case.record, case.victim)
    for field in ("action_id", "animation_index", "hitstun", "hitlag"):
        assert int(out[field][case.victim]) == int(ref[field][case.victim]), field
    assert float(out["percent"][case.victim]) == pytest.approx(float(ref["percent"][case.victim]))
    assert float(out["pos_y"][case.victim]) == pytest.approx(float(ref["pos_y"][case.victim]))


@pytest.mark.integration
@pytest.mark.parametrize(
    "case,throw_action,thrown_action,expected_action",
    [
        (_Case("MetallicUniqueGrouse.slpz", 236, 1, 0), ACT_THROW_F, ACT_THROWN_F, ACT_DAMAGE_AIR_3),
        (_Case("LoudDullGoat.slpz", 252, 1, 0), ACT_THROW_B, ACT_THROWN_B, ACT_DAMAGE_FLY_N),
    ],
)
def test_non_low_throw_release_does_not_force_damageflytop_arg(
    case: _Case, throw_action: int, thrown_action: int, expected_action: int
) -> None:
    # Adjacent negative: ftCo_800DE7C0 receives arg1=-1 for non-low throws, so the final damage
    # motion state stays owned by the ordinary ftCo_8008DCE0 severity/angle path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth" / case.dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.record]
    assert int(row["seed_t"]["action_id"][case.thrower]) == throw_action
    assert int(row["seed_t"]["action_id"][case.victim]) == thrown_action
    assert int(row["ref_t1"]["action_id"][case.victim]) == expected_action

    _, ref, out = _run_one_step_row(dataset_path, case.record, case.victim)
    assert int(out["action_id"][case.victim]) == int(ref["action_id"][case.victim])
    assert int(out["action_id"][case.victim]) == expected_action
    assert int(out["animation_index"][case.victim]) == int(ref["animation_index"][case.victim])
