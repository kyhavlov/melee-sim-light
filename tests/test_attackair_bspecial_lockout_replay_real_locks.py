from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


@pytest.mark.integration
def test_attackair_do_iasa_does_not_route_to_aerial_side_special_llw_4546() -> None:
    # AttackAir DO_IASA is gated by script-owned allow_interrupt, but its branch list does not
    # include ftCo_SpecialAir_CheckInput. A B+side edge during Falco NAir IASA must therefore stay
    # in AttackAirN instead of entering SpecialAirSStart.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::DO_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/battlefield_recent/LoyalDishonestWren.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 1
    seed, out, ref = _step_one_row(dataset_path, 4546)

    assert int(seed["action_id"][p]) == 65  # AttackAirN
    assert int(seed["action_frame"][p]) == 47
    assert int(ref["action_id"][p]) == 65
    assert int(ref["action_frame"][p]) == 48
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fall_still_routes_to_aerial_side_special_tch_9798() -> None:
    # Negative control for the owner boundary: ordinary Fall IASA does call
    # ftCo_SpecialAir_CheckInput, so the AttackAir lockout must not suppress non-AttackAir aerial
    # side-special entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0
    seed, out, ref = _step_one_row(dataset_path, 9798)

    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(ref["action_id"][p]) == 350  # SpecialAirSStart
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
