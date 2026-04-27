from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_PRH = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"


@dataclass(frozen=True)
class _Case:
    record: int
    port: int
    note: str
    seed_action: int
    ref_action: int
    ref_action_frame: int
    ref_animation: int
    ref_on_ground: int
    ref_shield_hp: float
    ref_vy: float


_CASES = (
    _Case(
        record=11464,
        port=0,
        note="last Guard hold-drain frame before ShieldBreakFly",
        seed_action=179,
        ref_action=179,
        ref_action_frame=-1,
        ref_animation=0xFFFFFFFF,
        ref_on_ground=1,
        ref_shield_hp=0.08003824949264526,
        ref_vy=0.0,
    ),
    _Case(
        record=11465,
        port=0,
        note="Guard hold-drain crosses zero and enters ShieldBreakFly",
        seed_action=179,
        ref_action=205,
        ref_action_frame=1,
        ref_animation=286,
        ref_on_ground=0,
        ref_shield_hp=0.07000000029802322,
        ref_vy=3.129999876022339,
    ),
    _Case(
        record=11560,
        port=0,
        note="ShieldBreakStandU animation end enters Furafura",
        seed_action=209,
        ref_action=211,
        ref_action_frame=0,
        ref_animation=205,
        ref_on_ground=1,
        ref_shield_hp=30.06999969482422,
        ref_vy=0.0,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}_p{c.port}")
def test_shield_hold_drain_enters_shieldbreakfly_only_on_depletion(case: _Case) -> None:
    # Replay-real boundary for held-shield depletion:
    # - ftCo_800925A4 drains shield HP during Guard/GuardOn Anim.
    # - When HP crosses below zero, ftCo_80098B20 enters ShieldBreakFly and ftCo_ShieldBreakFly_Phys
    #   runs the same frame through ft_80084EEC, applying gravity to co_attrs.x94.
    # - The preceding row stays ordinary Guard, so this is not a broad low-shield action shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::{
    #   ftCo_80098B20,ftCo_ShieldBreakFly_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PRH
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_PRH}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.port)
    p = case.port
    assert int(seed_row["action_id"][p]) == case.seed_action, case.note
    assert int(ref_row["action_id"][p]) == case.ref_action, case.note
    assert int(ref_row["action_frame"][p]) == case.ref_action_frame, case.note
    assert int(ref_row["animation_index"][p]) == case.ref_animation, case.note
    assert int(ref_row["on_ground"][p]) == case.ref_on_ground, case.note
    assert float(ref_row["shield_hp"][p]) == pytest.approx(case.ref_shield_hp, abs=1e-7)

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "hitlag",
        "hitstun",
    ):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )
    for field in ("shield_hp", "pos_y", "speed_y_self", "speed_air_x_self"):
        assert float(out_row[field][p]) == pytest.approx(float(ref_row[field][p]), abs=1e-6), (
            f"{case.note}: field={field} expected={float(ref_row[field][p]):.8f} "
            f"got={float(out_row[field][p]):.8f}"
        )
    assert float(out_row["speed_y_self"][p]) == pytest.approx(case.ref_vy, abs=1e-6)
