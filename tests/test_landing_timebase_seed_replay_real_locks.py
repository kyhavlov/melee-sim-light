from __future__ import annotations

from pathlib import Path

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


ACT_LANDING_FALL_SPECIAL = 43
ACT_LANDING_AIR_F = 71
ACT_LANDING_AIR_B = 72
ACT_SK_SPECIAL_AIR_HI = 360


def _dataset(name: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    path = root / "datasets/sheik/replays/validation/sheik" / name
    if not path.exists():
        import pytest

        pytest.skip(f"missing local dataset: {path}")
    return path


def test_sheik_vanish_landingfallspecial_frame0_rederives_vanish_lag_shb_8811() -> None:
    # Source owner:
    # - ftSk_SpecialAirHi_Coll lands through Sheik's Vanish landing helper.
    # - Slippi can expose the transient entry frame_speed on the frame-0 LandingFallSpecial row,
    #   but the next source Anim tick consumes the steady da->x2EC Vanish landing lag rate.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{ftSk_SpecialAirHi_Coll,fn_80112ED8}
    # data/characters/sheik.json::sheik_vanish_landing_lag_frames
    seed, ref, out = _run_one_step_row(_dataset("SnarlingHelplessBeaver.msl"), 8811, 0)

    assert int(seed["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(seed["seed_prev_action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert int(seed["action_frame"][0]) == 0
    assert int(ref["action_frame"][0]) == 1
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0])


def test_sheik_vanish_landingfallspecial_steady_row_keeps_source_rate_shb_8812() -> None:
    seed, ref, out = _run_one_step_row(_dataset("SnarlingHelplessBeaver.msl"), 8812, 0)

    assert int(seed["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(seed["seed_prev_action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(seed["action_frame"][0]) == 1
    assert int(ref["action_frame"][0]) == 2
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0])


def test_landingair_frame0_uses_explicit_lcancel_result_not_stale_lr_timer_rrr_8856() -> None:
    # Source owner:
    # - LandingAir entry divides lag only when fp->x67F is inside the source L-cancel window.
    # - Slippi exposes the entry result through l_cancel; stale lr_press_timer can remain nonzero
    #   on the first LandingAir row and must not re-open the divide branch in replay reseeds.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    seed, ref, out = _run_one_step_row(_dataset("RuralReasonableRat.msl"), 8856, 0)

    assert int(seed["action_id"][0]) == ACT_LANDING_AIR_F
    assert int(seed["action_frame"][0]) == 0
    assert int(seed["l_cancel"][0]) != 1
    assert int(seed["lr_press_timer"][0]) < 10
    assert int(ref["action_frame"][0]) == 1
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0])


def test_landingair_frame0_stale_lr_timer_control_covers_backair_pme_9525() -> None:
    seed, ref, out = _run_one_step_row(_dataset("PaleMajorEchidna.msl"), 9525, 0)

    assert int(seed["action_id"][0]) == ACT_LANDING_AIR_B
    assert int(seed["action_frame"][0]) == 0
    assert int(seed["l_cancel"][0]) != 1
    assert int(seed["lr_press_timer"][0]) < 10
    assert int(ref["action_frame"][0]) == 1
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0])
