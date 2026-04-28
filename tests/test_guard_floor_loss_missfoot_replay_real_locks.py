from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_HVG = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "HilariousVillainousGiraffe.msl"
)


@pytest.mark.integration
def test_landing_terminal_guardon_floor_loss_enters_missfoot_replay_real_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _HVG
    if not ds_path.exists():
        pytest.skip(f"missing local dataset artifact: {ds_path}")

    seed, ref, out = _run_one_step_row(ds_path, 520, 1)

    # Source path:
    # - LandingFallSpecial_Anim -> ft_8008A2BC enters Wait at terminal anim.
    # - The same frame's input callback can enter GuardOn from held L.
    # - GuardOn_Coll -> ft_800845B4 routes ledge-slip floor loss to ftCo_8009F39C.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800845B4
    assert int(seed["action_id"][1]) == 43  # LandingFallSpecial
    assert int(seed["landing_fallspecial_allow_interrupt"][1]) == 0
    assert int(ref["action_id"][1]) == 251  # MissFoot
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert int(out["animation_index"][1]) == int(ref["animation_index"][1])
    assert int(out["action_frame"][1]) == int(ref["action_frame"][1])
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]), abs=1e-6)
    assert float(out["pos_y"][1]) == pytest.approx(float(ref["pos_y"][1]), abs=1e-6)
    assert float(out["speed_air_x_self"][1]) == pytest.approx(
        float(ref["speed_air_x_self"][1]), abs=1e-6
    )
    assert float(out["speed_y_self"][1]) == pytest.approx(float(ref["speed_y_self"][1]), abs=1e-6)
    assert int(out["on_ground"][1]) == 0


@pytest.mark.integration
def test_landing_terminal_floor_loss_without_guard_entry_does_not_missfoot() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _HVG
    if not ds_path.exists():
        pytest.skip(f"missing local dataset artifact: {ds_path}")

    def clear_shield(seed_t):
        seed_t["shield_hp"][0, 1] = 0.0

    seed, _ref, out = _run_one_step_row(ds_path, 520, 1, seed_mutator=clear_shield)

    # Mutated boundary: the same ledge floor loss with no possible GuardOn entry must take the
    # plain Landing/Wait floor-loss fallback, not the guard-only MissFoot branch.
    assert int(seed["action_id"][1]) == 43  # LandingFallSpecial
    assert float(seed["shield_hp"][1]) == 0.0
    assert int(out["action_id"][1]) != 251  # MissFoot
    assert int(out["action_id"][1]) == 29  # Fall


@pytest.mark.integration
@pytest.mark.parametrize("record", [519, 521])
def test_guard_floor_loss_missfoot_adjacent_rows_remain_exact(record: int) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _HVG
    if not ds_path.exists():
        pytest.skip(f"missing local dataset artifact: {ds_path}")

    _seed, ref, out = _run_one_step_row(ds_path, record, 1)
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert int(out["animation_index"][1]) == int(ref["animation_index"][1])
    assert int(out["action_frame"][1]) == int(ref["action_frame"][1])
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]), abs=1e-6)
    assert float(out["pos_y"][1]) == pytest.approx(float(ref["pos_y"][1]), abs=1e-6)
