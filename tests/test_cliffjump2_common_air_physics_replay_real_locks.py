from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    ref_action_frame: int
    ref_vx: float
    ref_vy: float


_AGG_VALID = "datasets/aggregate_recent/replays/validation/aggregate_recent"
_PRIMARY_CARDINAL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"

_CASES = [
    _Case(
        f"{_AGG_VALID}/PositiveRevolvingHyena.msl",
        4377,
        1,
        260,  # CliffJumpSlow1
        261,  # CliffJumpSlow2 same-frame handoff, x0 gate skips ft_80084DB0
        1,
        -1.0,
        3.9000000953674316,
    ),
    _Case(
        f"{_AGG_VALID}/PositiveRevolvingHyena.msl",
        4378,
        1,
        261,  # steady CliffJumpSlow2
        261,
        2,
        -0.9800000190734863,
        3.7300000190734863,
    ),
    _Case(
        f"{_PRIMARY_CARDINAL}/QuerulousGrandDinosaur.msl",
        9041,
        1,
        262,  # CliffJumpQuick1
        263,  # CliffJumpQuick2 same-frame handoff, x0 gate skips ft_80084DB0
        1,
        -1.100000023841858,
        4.0,
    ),
    _Case(
        f"{_PRIMARY_CARDINAL}/QuerulousGrandDinosaur.msl",
        9042,
        1,
        263,  # steady CliffJumpQuick2
        263,
        2,
        -1.0800000429153442,
        3.7699999809265137,
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES)
def test_cliffjump2_common_air_helper_x0_gate_replay_real(case: _Case) -> None:
    # Decomp ownership lock for CliffJump2 physics:
    # - ftCo_8009B2F8 enters CliffJump2 and immediately ticks the new motion, so visible
    #   action_frame is already 1 on the handoff row.
    # - ftCo_CliffJump2_Phys still skips ft_80084DB0 on that first physics callback through the
    #   mv.co.cliffjump.x0 gate, then runs common-air physics on steady CliffJump2 frames.
    # - Both slow and quick variants use the same ftCo_CliffJump2_Phys callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::{
    #   ftCo_8009B2F8,ftCo_CliffJump2_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(
        dataset_path, case.record, case.p, rng_damage_fly_roll_gate=True
    )
    assert int(seed_row["action_id"][case.p]) == case.seed_action
    assert int(ref_row["action_id"][case.p]) == case.ref_action
    assert int(ref_row["action_frame"][case.p]) == case.ref_action_frame

    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out_row[field][case.p]) == int(ref_row[field][case.p]), field
    assert float(out_row["speed_air_x_self"][case.p]) == pytest.approx(case.ref_vx, abs=1e-6)
    assert float(out_row["speed_y_self"][case.p]) == pytest.approx(case.ref_vy, abs=1e-6)
    assert float(out_row["speed_air_x_self"][case.p]) == pytest.approx(
        float(ref_row["speed_air_x_self"][case.p]), abs=1e-6
    )
    assert float(out_row["speed_y_self"][case.p]) == pytest.approx(
        float(ref_row["speed_y_self"][case.p]), abs=1e-6
    )
    assert float(out_row["pos_x"][case.p]) == pytest.approx(float(ref_row["pos_x"][case.p]), abs=1e-6)
    assert float(out_row["pos_y"][case.p]) == pytest.approx(float(ref_row["pos_y"][case.p]), abs=1e-6)
