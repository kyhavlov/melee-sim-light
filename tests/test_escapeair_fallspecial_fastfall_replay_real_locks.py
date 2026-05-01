from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _rollout_outputs(ds, start_record: int, stop_record: int) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    samples = ds.samples
    seed_bytes = (
        samples[start_record : start_record + 1]["seed_t"]
        .view(np.uint8)
        .reshape(1, seed_stride)
        .copy()
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, stop_record + 1):
            prev_input = (
                samples[record : record + 1]["prev_input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            input_t = (
                samples[record : record + 1]["input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            out[record + 1] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    return out


@pytest.mark.integration
def test_escapeair_anim_end_fallspecial_preserves_fastfall_until_landing() -> None:
    # ftCo_EscapeAir_Anim enters FallSpecial through ftCo_80096900, whose inline0 uses
    # Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall). The fastfall bit and fastfall y velocity
    # persist on the FallSpecial entry row, then the later LandingFallSpecial ground transition
    # clears them.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 1
    start_record = 9883
    fallspecial_record = 9897
    landing_record = 9898

    assert int(samples[start_record]["seed_t"]["action_id"][p]) == 236  # EscapeAir
    assert int(samples[start_record]["seed_t"]["state_flags"][p, 1]) & 0x08

    outputs = _rollout_outputs(ds, start_record, landing_record)
    out_fallspecial = outputs[fallspecial_record]
    ref_fallspecial = samples[fallspecial_record - 1]["ref_t1"]
    assert int(ref_fallspecial["action_id"][p]) == 35  # FallSpecial
    assert int(ref_fallspecial["on_ground"][p]) == 0
    assert int(ref_fallspecial["state_flags"][p, 1]) & 0x08
    assert int(out_fallspecial["action_id"][p]) == int(ref_fallspecial["action_id"][p])
    assert int(out_fallspecial["on_ground"][p]) == int(ref_fallspecial["on_ground"][p])
    assert int(out_fallspecial["state_flags"][p, 1]) == int(ref_fallspecial["state_flags"][p, 1])
    assert float(out_fallspecial["speed_y_self"][p]) == pytest.approx(
        float(ref_fallspecial["speed_y_self"][p]), abs=1e-6
    )
    assert float(out_fallspecial["pos_y"][p]) == pytest.approx(
        float(ref_fallspecial["pos_y"][p]), abs=1e-5
    )

    out_landing = outputs[landing_record]
    ref_landing = samples[landing_record - 1]["ref_t1"]
    assert int(ref_landing["action_id"][p]) == 43  # LandingFallSpecial
    assert int(ref_landing["on_ground"][p]) == 1
    assert (int(ref_landing["state_flags"][p, 1]) & 0x08) == 0
    assert int(out_landing["action_id"][p]) == int(ref_landing["action_id"][p])
    assert int(out_landing["on_ground"][p]) == int(ref_landing["on_ground"][p])
    assert int(out_landing["action_frame"][p]) == int(ref_landing["action_frame"][p])
    assert int(out_landing["state_flags"][p, 1]) == int(ref_landing["state_flags"][p, 1])
