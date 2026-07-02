from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@pytest.mark.integration
def test_fox_illusion_ground_to_air_clears_skipped_x221c_u16_y_opcode52() -> None:
    # Source owner:
    # - opcode 52 writes fp->x221C_u16_y through ftAction_80072C6C / ft_8008A1B8,
    # - Fighter_ChangeMotionState clears x221C_u16_y unless Ft_MF_Unk24 is supplied,
    # - Fox Illusion ground->air uses FTFOX_SPECIALS_COLL_FLAG and preserves the current
    #   animation/action frame, so SpecialAirS frame-0 opcode-52 is not replayed after the reset.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80072C6C
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A1B8
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_GroundToAir
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset = (
        root
        / "replays/validation/fountain_of_dreams_recent/"
        / "ElatedWearyTermite.slpz"
    )
    if not dataset.exists():
        pytest.skip(f"missing local replay: {dataset.relative_to(root)}")

    # Positive control: ordinary grounded SpecialS entry starts at frame 0, crosses the opcode-52
    # command, and publishes the visible fp+0x221C high-byte bit0.
    _, ref_entry, out_entry = _run_one_step_row(dataset, 7114, 0)
    assert int(ref_entry["action_id"][0]) == 348  # Fox SpecialS
    assert int(ref_entry["action_frame"][0]) == 0
    assert int(ref_entry["state_flags"][0][3]) & 0x01
    _assert_transition_lock_fields_match_ref(out_row=out_entry, ref_row=ref_entry, record=7114, p=0)

    # Boundary: the next collision step changes to SpecialAirS at frame 1. Vanilla has already
    # cleared x221C_u16_y and does not replay SpecialAirS's frame-0 opcode-52 command.
    for rec in (7115, 7116, 7117):
        _, ref_row, out_row = _run_one_step_row(dataset, rec, 0)
        assert int(ref_row["action_id"][0]) == 351  # Fox SpecialAirS
        assert (int(ref_row["state_flags"][0][3]) & 0x01) == 0
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=0)
