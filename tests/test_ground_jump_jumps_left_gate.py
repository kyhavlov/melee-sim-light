from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

_KNEE_BEND = 24  # MSL_ACT_KNEE_BEND (the grounded jumpsquat = ground jump)


def _one_step(binding, num_players, row):
    sizes = binding.sizes()
    ss = int(sizes["seed"])
    ins = int(sizes["input"])
    cs = int(sizes["compare"])
    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, row["seed_t"].view(np.uint8).reshape((1, ss)).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view(np.uint8).reshape((1, ins)).copy(),
            row["input_t"].view(np.uint8).reshape((1, ins)).copy(),
        )
        ob = np.zeros((1, cs), dtype=np.uint8)
        binding.write_compare(handle, ob)
        return ob.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _load_demo():
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    return read_dataset(str(path))


@pytest.mark.integration
def test_grounded_jump_is_available_with_zero_jumps_left() -> None:
    # The grounded jump enters KneeBend (the jumpsquat) unconditionally: ftCo_Jump_CheckInput calls
    # ftCo_KneeBend_Enter on any jump input WITHOUT a jump-count gate -- only the air jump
    # (ftCo_JumpAerial) checks the jump counter. A fighter who spent both jumps in a Vanish recovery
    # and then landed has jumps_left==0 while grounded, yet must still be able to ground-jump.
    # Before the fix the sim gated KneeBend on jumps_left>0, so these grounded jumps were dropped.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
    ds = _load_demo()
    samples = ds.samples
    num_players = int(ds.header["num_players"])
    n = int(samples.shape[0])
    binding = importlib.import_module("msl_binding")

    found = []
    for j in range(n):
        seed = samples[j]["seed_t"]
        ref = samples[j]["ref_t1"]
        for p in range(num_players):
            if (
                int(seed["on_ground"][p]) == 1
                and int(seed["jumps_left"][p]) == 0
                and int(ref["action_id"][p]) == _KNEE_BEND
                and int(seed["action_id"][p]) != _KNEE_BEND
            ):
                found.append((j, p))
    assert found, "no grounded jumps_left==0 -> KneeBend transition in dataset"

    for j, p in found:
        out = _one_step(binding, num_players, samples[j : j + 1])
        assert int(out["action_id"][p]) == _KNEE_BEND, (
            j,
            p,
            int(out["action_id"][p]),
        )


@pytest.mark.integration
def test_airborne_jump_still_requires_jumps_left_adjacent_negative() -> None:
    # Adjacent negative: the ground-jump relaxation must NOT leak into the air jump. The fix keeps
    # jumps_left>0 as the airborne fallback (`jumps_left>0 || on_ground`), so an AIRBORNE fighter with
    # jumps_left==0 must never spuriously enter the grounded KneeBend. Scan every airborne
    # jumps_left==0 frame whose reference does not enter KneeBend and assert the sim does not either.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c (air jump gates on x1968_jumpsUsed)
    ds = _load_demo()
    samples = ds.samples
    num_players = int(ds.header["num_players"])
    n = int(samples.shape[0])
    binding = importlib.import_module("msl_binding")

    checked = 0
    for j in range(n):
        seed = samples[j]["seed_t"]
        ref = samples[j]["ref_t1"]
        airborne_zero = [
            p
            for p in range(num_players)
            if int(seed["on_ground"][p]) == 0
            and int(seed["jumps_left"][p]) == 0
            and int(ref["action_id"][p]) != _KNEE_BEND
        ]
        if not airborne_zero:
            continue
        out = _one_step(binding, num_players, samples[j : j + 1])
        for p in airborne_zero:
            assert int(out["action_id"][p]) != _KNEE_BEND, (j, p)
            checked += 1
    assert checked > 0, "no airborne jumps_left==0 frames to guard the air-jump gate"
