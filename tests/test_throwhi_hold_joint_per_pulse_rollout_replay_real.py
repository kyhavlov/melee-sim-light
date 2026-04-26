from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


def _rollout_rows(dataset_path: Path, start_record: int, end_record_inclusive: int):
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed(handle, seed_bytes)
        rows = {}
        for record in range(start_record, end_record_inclusive + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            rows[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return samples, rows
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throwhi_rollout_recomputes_hold_joint_vector_for_each_pulse() -> None:
    # GAT rollout-real lock for per-pulse ThrowHi laser vectors:
    # - ftFx_Throw_Anim consumes each throw_flags_b0 pulse and recomputes
    #   atan2f(FtGetHoldJoint - ItGetHoldJoint) before calling it_8029C6CC.
    # - A stale latest-shot velocity proxy makes frame-20/24 reuse the frame-18 vector and sends
    #   the later lasers right/up instead of the replay's rotated hold-joint vectors.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    # data/moves/fox.json moves["ftCo_SM_ThrowHi"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    samples, rows = _rollout_rows(dataset_path, start_record=8416, end_record_inclusive=8425)
    seed = samples["seed_t"][8416]
    assert int(seed["char_id"][0]) == 1  # Fox
    assert int(seed["action_id"][0]) == 221  # ThrowHi
    assert int(seed["action_id"][1]) == 90  # DamageFlyTop
    assert float(seed["frame_speed_mul_f32"][0]) == pytest.approx(1.25)

    out20, ref20 = rows[8421]
    out24, ref24 = rows[8425]
    first_pulse = out20["items"][1]
    second_pulse = out20["items"][2]
    third_pulse = out24["items"][2]
    ref_second = ref20["items"][2]
    ref_third = ref24["items"][2]

    assert int(second_pulse["exists"]) == 1
    assert int(third_pulse["exists"]) == 1
    assert float(second_pulse["vel_x"]) != pytest.approx(float(first_pulse["vel_x"]), abs=0.1)
    assert float(second_pulse["pos_x"]) == pytest.approx(float(ref_second["pos_x"]), abs=1e-4)
    assert float(second_pulse["pos_y"]) == pytest.approx(float(ref_second["pos_y"]), abs=1e-4)
    assert float(second_pulse["vel_x"]) == pytest.approx(float(ref_second["vel_x"]), abs=1e-4)
    assert float(second_pulse["vel_y"]) == pytest.approx(float(ref_second["vel_y"]), abs=1e-4)

    assert float(third_pulse["pos_x"]) == pytest.approx(float(ref_third["pos_x"]), abs=1e-4)
    assert float(third_pulse["pos_y"]) == pytest.approx(float(ref_third["pos_y"]), abs=1e-4)
    assert float(third_pulse["vel_x"]) == pytest.approx(float(ref_third["vel_x"]), abs=1e-4)
    assert float(third_pulse["vel_y"]) == pytest.approx(float(ref_third["vel_y"]), abs=1e-4)
