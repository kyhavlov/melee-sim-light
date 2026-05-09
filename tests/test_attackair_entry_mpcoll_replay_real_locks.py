from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_CDO_POKEMON_STADIUM = (
    "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
    "CornyDelayedOkapi.msl"
)


def test_jumpaerial_to_attackair_entry_tick_feeds_same_frame_left_wall_collision() -> None:
    # Replay-real lock for AttackAir entry timebase before mpColl:
    # - CDO:8825 is an airborne JumpAerialF row near Pokemon Stadium's left wall.
    # - Current c-stick left enters AttackAirB from JumpAerial IASA.
    # - Vanilla immediately runs ftAnim_8006EBA4 in ftCo_AttackAir_EnterFromMsid before
    #   AttackAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
    # - The first AttackAir ECB frame must therefore feed the same-frame left-wall projection.
    #   Without that source phase, the sim stays about 0.142 units too far right while velocities
    #   and the action transition already match.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
    #   ftCo_AttackAir_EnterFromMsid,ftCo_AttackAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CDO_POKEMON_STADIUM
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_CDO_POKEMON_STADIUM}")

    seed, ref, out = _run_one_step_row(dataset_path, 8825, 1)
    p = 1

    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 67  # AttackAirB
    assert int(ref["action_frame"][p]) == int(out["action_frame"][p]) == 1
    assert int(ref["on_ground"][p]) == int(out["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]), abs=1e-6)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)


def test_jumpaerial_left_wall_adjacent_non_attackair_control_stays_on_source_state() -> None:
    # Adjacent negative: one row earlier there is no AttackAir IASA entry, so the JumpAerialF
    # collision/timebase owner must remain untouched and replay-exact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CDO_POKEMON_STADIUM
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_CDO_POKEMON_STADIUM}")

    seed, ref, out = _run_one_step_row(dataset_path, 8824, 1)
    p = 1

    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 27
    assert int(ref["on_ground"][p]) == int(out["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
