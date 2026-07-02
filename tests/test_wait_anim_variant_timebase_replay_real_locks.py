from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


DEMO2 = Path("replays/validation/sheik/sheik_demo_game_2.slpz")


@pytest.mark.integration
def test_wait_anim_end_gate_uses_current_variant_not_wait1_default_demo2() -> None:
    # Marth Wait variant 3 is still live at frame 89 because its extracted AObj end frame is 95.
    # The Wait_Anim RNG chooser runs only after the current visible variant finishes; using the
    # default Wait1_0 end frame (90) restarts action_frame five frames early.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Anim
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,ftCo_8008A6D8,getAnimID}
    # data/anims/marth.tracks.bin::ftCo_SM_Wait2 end_frame=95
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    player = 1
    seed, ref, out = _run_one_step_row(root / DEMO2, 384, player)

    assert int(seed["action_id"][player]) == 14
    assert int(seed["animation_index"][player]) == 3
    assert int(seed["action_frame"][player]) == 89
    assert int(ref["action_id"][player]) == 14
    assert int(ref["animation_index"][player]) == 3
    assert int(ref["action_frame"][player]) == 90
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["animation_index"][player]) == int(ref["animation_index"][player])
    assert int(out["action_frame"][player]) == int(ref["action_frame"][player])


@pytest.mark.integration
def test_wait_anim_end_gate_still_restarts_wait1_at_its_own_end_frame() -> None:
    # Adjacent negative for the variant end gate: if the same seeded row is made Wait1_0, frame 89
    # reaches Wait1_0's extracted end_frame=90 and should restart through the ordinary Wait_Anim RNG
    # path instead of preserving the long Wait2 age.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    player = 1

    def force_wait1(seed_t: np.ndarray) -> None:
        seed_t["animation_index"][0, player] = np.uint32(2)

    _seed, _ref, out = _run_one_step_row(root / DEMO2, 384, player, seed_mutator=force_wait1)

    assert int(out["action_id"][player]) == 14
    assert int(out["action_frame"][player]) == 0
