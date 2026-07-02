from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views

_SK_SPECIAL_AIR_HI = 360  # MSL_ACT_SK_SPECIAL_AIR_HI (Vanish air travel)
_LANDING_FALL_SPECIAL = 43  # MSL_ACT_LANDING_FALL_SPECIAL
_WAIT = 14


def _load_demo():
    root = Path(__file__).resolve().parents[1]
    rel = "replays/validation/sheik/sheik_demo_game.slpz"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local replay: {rel}")
    return load_replay_buffers(str(path))


@pytest.mark.integration
def test_sheik_vanish_landing_lag_spans_full_window() -> None:
    # Sheik's Vanish (ftSk_SpecialAirHi) lands through ftCo_LandingFallSpecial_Enter with
    # landing_lag = ftSeakAttributes x5C (sheik_vanish_landing_lag_frames, 30). The fixed-length
    # LandingFallSpecial submotion (~9 frames) is stretched across that 30-frame window. The af==0
    # re-derivation in anim_timebase cannot see the Vanish source (prev_action is already
    # LandingFallSpecial by then), so without preserving the live entry rate it would replay the
    # animation at ~3.3x and end the lag after ~9 frames instead of ~30. Free-run a real Vanish
    # landing and assert the LandingFallSpecial state persists well past the natural animation end,
    # tracking the reference (which holds it for the full lag) rather than exiting early to Wait.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    ds = _load_demo()
    samples = ds.rows
    num_players = int(ds.num_players)
    n = int(samples.shape[0])
    seed = samples["seed_t"]
    ref = samples["ref_t1"]

    # Locate a Vanish-air -> LandingFallSpecial landing for Sheik (player 0) whose reference keeps
    # the LandingFallSpecial state for many frames (the long Vanish lag, not a short generic one).
    start = None
    for j in range(1, n - 35):
        if (
            int(seed[j - 1]["action_id"][0]) == _SK_SPECIAL_AIR_HI
            and int(seed[j]["action_id"][0]) == _LANDING_FALL_SPECIAL
            and int(seed[j]["action_frame"][0]) == 0
            and int(ref[j + 20]["action_id"][0]) == _LANDING_FALL_SPECIAL
        ):
            start = j
            break
    if start is None:
        pytest.skip("no long Vanish-air -> LandingFallSpecial landing in dataset")

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    ss = int(sizes["seed"])
    ins = int(sizes["input"])
    cs = int(sizes["compare"])
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    handle = binding.init(batch_size=1, num_players=num_players, ucf_enabled=1,
                          ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed_rollout(handle, seed_u8[start - 4, :ss].reshape(1, ss).copy())
        ob = np.zeros((1, cs), dtype=np.uint8)
        end = start + 25
        matched_window = True
        sim_left_early = False
        for j in range(start - 4, end):
            binding.step_input_replay_frame_rng(
                handle,
                seed_u8[j, :ss].reshape(1, ss).copy(),
                prev_input_u8[j, :ins].reshape(1, ins).copy(),
                input_u8[j, :ins].reshape(1, ins).copy(),
            )
            binding.write_compare(handle, ob)
            out = ob.view(COMPARE_DTYPE).reshape((1,))[0]
            # In the window where the reference is still in LandingFallSpecial, the sim must be too.
            if start <= j < end and int(ref[j]["action_id"][0]) == _LANDING_FALL_SPECIAL:
                if int(out["action_id"][0]) != _LANDING_FALL_SPECIAL:
                    matched_window = False
                    if int(out["action_id"][0]) == _WAIT:
                        sim_left_early = True
    finally:
        binding.destroy(handle)

    assert matched_window and not sim_left_early, (
        "sim exited Sheik Vanish LandingFallSpecial before the reference; the 30-frame Vanish "
        "landing lag was not preserved (rederivation clobbered the stretched rate)"
    )
