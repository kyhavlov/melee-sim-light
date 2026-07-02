from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _rollout_row(dataset_path: Path, *, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert 0 <= start_record <= target_record < int(samples.shape[0])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            input_bytes[0, :] = np.frombuffer(
                samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return samples[target_record]["ref_t1"], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_escapeair_terminal_fallspecial_landing_rollout_keeps_allow_interrupt_clear() -> None:
    # EscapeAir_Anim can enter FallSpecial immediately before floor collision. That FallSpecial
    # source still carries allow_interrupt=false, so the following LandingFallSpecial must not
    # consume held shield through Landing_IASA.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_EscapeAir_Anim,ftCo_80099D70}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ref, out = _rollout_row(dataset_path, start_record=3543, target_record=3574)

    player = 0
    assert int(ref["action_id"][player]) == 43  # LandingFallSpecial
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["action_frame"][player]) == int(ref["action_frame"][player])
    assert int(out["animation_index"][player]) == int(ref["animation_index"][player])
    assert int(out["instance_id"][player]) == int(ref["instance_id"][player])
    assert int(out["state_flags"][player][1]) == int(ref["state_flags"][player][1])
