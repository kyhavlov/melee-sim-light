from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def test_terminal_x1990_colanim_guard_keeps_fox_laser_alive() -> None:
    # Replay-real lock for terminal x1990 item BODY ownership:
    # - Fighter_8006A360 decrements x1990 from 1 and clears visible x198C for t+1.
    # - The same item BODY pass still follows ftColl_8007925C's x1988/x198C collision-status gate,
    #   so the live Fox laser must not consume on the terminal hidden-status frame.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 6544, 0)
    p = 0
    slot = 0

    assert int(seed["items"][slot]["type"]) == 54  # Fox laser
    assert int(seed["colanim_hit_status_x198c"][p]) == 2
    assert int(seed["colanim_timer_x1990"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) == 0
    assert int(ref["hurtbox_state"][p]) == 0

    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), field
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 20  # Dash
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0


def test_terminal_x1990_guard_does_not_block_disabled_contact_rows() -> None:
    # Negative sentinel for the terminal x1990 guard: disabled-hurtcap item contact without the
    # terminal x1990/x198C shape still consumes the laser through the retained disabled-contact lane.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 4757, 1)
    p = 1
    slot = 0

    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(seed["hurtbox_state"][p]) == 1
    assert int(seed["colanim_timer_x1990"][p]) != 1
    assert int(ref["items"][slot]["exists"]) == 0
    assert int(out["items"][slot]["exists"]) == 0


def test_terminal_x1990_x1994_carry_allows_falco_laser_body_item_consume() -> None:
    # Replay-real lock for terminal x1990 + x1994 carry:
    # - Fighter_8006A360 expires x1990 from 1 while x1994 remains live, moving x198C from 2 to 1.
    # - ftColl_8007925C blocks item BODY on x1988/x198C==2 only, so the same item pass may consume
    #   the laser even though Slippi's merged seed hurtbox_state is still 2.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 8054, 0)
    p = 0
    slot = 1

    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(seed["colanim_hit_status_x198c"][p]) == 2
    assert int(seed["colanim_timer_x1990"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) > 0

    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), field
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][p]) == int(ref[field][p]), field


def test_nonterminal_x1990_x1994_carry_keeps_falco_laser_alive() -> None:
    # Adjacent negative: x1990 still has more than one tick, so x198C remains 2 through the item
    # pass and the shot must not consume as a terminal carry.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 8053, 0)
    p = 0
    slot = 1

    assert int(seed["items"][slot]["type"]) == 55
    assert int(seed["colanim_timer_x1990"][p]) > 1
    assert int(seed["colanim_timer_x1994"][p]) > 0
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["exists"]) == 1
