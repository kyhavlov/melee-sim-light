from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


PJO = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"


def _rollout_rows(
    dataset_path: Path, start_record: int, targets: tuple[int, ...]
) -> tuple[object, dict[int, np.void]]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert targets
    assert start_record <= min(targets) <= max(targets) < int(samples.shape[0])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    out_by_record: dict[int, np.void] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        target_set = set(targets)
        for record in range(start_record, max(targets) + 1):
            prev_input_bytes[0, :] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            input_bytes[0, :] = np.frombuffer(
                samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in target_set:
                binding.write_compare(handle, out_bytes)
                out_by_record[record] = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    return ds, out_by_record


@pytest.mark.integration
def test_escapeair_landing_does_not_leak_stale_damage_x1994_under_cliff_invuln() -> None:
    # Replay-history seed provenance lock for PJO:
    # Damage exit x1994 is no longer source-proven after a visible vulnerable non-damage row.
    # Do not carry that stale x1994 underneath later CliffWait x1990 and EscapeAir x1988, otherwise
    # it leaks as invincible hurtbox_state when EscapeAir lands into LandingFallSpecial.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / PJO
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {PJO}")

    ds, out_by_record = _rollout_rows(dataset_path, 2718, (2735, 2738))
    p = 1

    seed = ds.samples[2718]["seed_t"]
    assert int(seed["action_id"][p]) == 236  # EscapeAir
    assert int(seed["hurtbox_state"][p]) == 2
    assert int(seed["colanim_timer_x1990"][p]) > 0
    assert int(seed["colanim_timer_x1994"][p]) == 0

    ref_2735 = ds.samples[2735]["ref_t1"]
    out_2735 = out_by_record[2735]
    assert int(ref_2735["action_id"][p]) == 236
    assert int(out_2735["action_id"][p]) == 236
    assert int(out_2735["hurtbox_state"][p]) == int(ref_2735["hurtbox_state"][p]) == 2

    ref_2738 = ds.samples[2738]["ref_t1"]
    out_2738 = out_by_record[2738]
    assert int(ref_2738["action_id"][p]) == 43  # LandingFallSpecial
    assert int(out_2738["action_id"][p]) == 43
    assert int(out_2738["hurtbox_state"][p]) == int(ref_2738["hurtbox_state"][p]) == 0
