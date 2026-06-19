from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

_ATTACK_DASH = 50
_CATCH_DASH = 214


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


@pytest.mark.integration
def test_attackdash_into_catchdash_clamps_ground_velocity_to_terminal() -> None:
    # Entering CatchDash from AttackDash goes through ftCo_800D8C54 -> Fighter_ChangeMotionState, which
    # clamps gr_vel to co_attrs.dash_run_terminal_velocity (the previous root-motion AttackDash exits
    # into the non-root-motion CatchDash) -- same clamp the Dash_IASA -> CatchDash path already applied.
    # Without it the full AttackDash ground speed carried into CatchDash and the fighter slid far past
    # the source, accumulating large rollout position drift. This locks the one-step transition: a
    # high-speed AttackDash frame that transitions to CatchDash must land on the source pos_x and a
    # ground speed no greater than the dash/run terminal.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D8C54,ftCo_CatchDash_Phys}
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    ds = read_dataset(str(path))
    samples = ds.samples
    num_players = int(ds.header["num_players"])
    n = int(samples.shape[0])

    binding = importlib.import_module("msl_binding")

    # Find an AttackDash frame whose seed ground speed clearly exceeds the dash/run terminal and whose
    # reference next state is CatchDash -- i.e. the over-speed transition the clamp must catch.
    found = []
    for j in range(n):
        seed = samples[j]["seed_t"]
        ref = samples[j]["ref_t1"]
        for p in range(num_players):
            if (
                int(seed["action_id"][p]) == _ATTACK_DASH
                and int(ref["action_id"][p]) == _CATCH_DASH
                and abs(float(seed["speed_ground_x_self"][p])) > 3.0
            ):
                found.append((j, p))
    assert found, "no over-speed AttackDash->CatchDash transition in dataset"

    j, p = found[0]
    ref = samples[j]["ref_t1"]
    out = _one_step(binding, num_players, samples[j : j + 1])
    # The transition lands exactly on the source (no slide-past): pos_x and ground speed match ref, and
    # the resulting speed magnitude is bounded by the dash/run terminal (it was clamped, not preserved).
    assert int(out["action_id"][p]) == _CATCH_DASH
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-2), (
        j,
        float(out["pos_x"][p]),
        float(ref["pos_x"][p]),
    )
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-3
    )
    # And it is strictly slower than the pre-transition over-speed AttackDash velocity (clamp happened).
    assert abs(float(out["speed_ground_x_self"][p])) < abs(
        float(samples[j]["seed_t"]["speed_ground_x_self"][p])
    )


@pytest.mark.integration
def test_attackdash_into_catchdash_no_drift_over_grab_freerun() -> None:
    # Adjacent rollout lock: free-running across the AttackDash->CatchDash transition and the CatchDash
    # itself must track the source (no accumulating position drift), since the velocity is clamped at
    # entry and only sheds friction afterward.
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    ds = read_dataset(str(path))
    samples = ds.samples
    num_players = int(ds.header["num_players"])
    n = int(samples.shape[0])
    binding = importlib.import_module("msl_binding")

    # Locate an AttackDash->CatchDash transition and free-run the surrounding window.
    start = None
    for j in range(n - 40):
        seed = samples[j]["seed_t"]
        ref = samples[j]["ref_t1"]
        if (
            int(seed["action_id"][0]) == _ATTACK_DASH
            and int(ref["action_id"][0]) == _CATCH_DASH
            and abs(float(seed["speed_ground_x_self"][0])) > 3.0
        ):
            start = j
            break
    if start is None:
        pytest.skip("no over-speed AttackDash->CatchDash transition in dataset")

    sizes = binding.sizes()
    ss = int(sizes["seed"])
    ins = int(sizes["input"])
    cs = int(sizes["compare"])
    su8 = samples.view(np.uint8).reshape(n, samples.dtype.itemsize)
    so = samples.dtype.fields["seed_t"][1]
    po = samples.dtype.fields["prev_input_t"][1]
    io = samples.dtype.fields["input_t"][1]
    ref = samples["ref_t1"]
    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        sb = np.empty((1, ss), dtype=np.uint8)
        sb[0, :] = su8[start, so : so + ss]
        binding.reseed_seed_rollout(handle, sb)
        max_drift = 0.0
        ob = np.zeros((1, cs), dtype=np.uint8)
        for j in range(start, min(start + 25, n)):
            pb = su8[j, po : po + ins].reshape(1, ins).copy()
            ib = su8[j, io : io + ins].reshape(1, ins).copy()
            sb[0, :] = su8[j, so : so + ss]
            binding.step_input_replay_frame_rng(handle, sb, pb, ib)
            binding.write_compare(handle, ob)
            o = ob.view(COMPARE_DTYPE).reshape((1,))[0]
            max_drift = max(max_drift, abs(float(o["pos_x"][0]) - float(ref[j]["pos_x"][0])))
    finally:
        binding.destroy(handle)
    # Before the fix the per-frame drift was ~3.0 and grew unbounded across the grab; locked well under
    # a unit here.
    assert max_drift < 1.0, max_drift
