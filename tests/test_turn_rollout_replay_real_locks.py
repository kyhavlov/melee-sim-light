from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


_AGG_HVG = Path(
    "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
)

ACT_DAMAGE_N_2 = 79
ACT_TURN = 18
ACT_WALK_SLOW = 15
ACT_WAIT = 14


def _byte_views(ds):
    samples = ds.samples
    stride = int(samples.dtype.itemsize)
    u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), stride)
    return (
        u8,
        int(samples.dtype.fields["seed_t"][1]),
        int(samples.dtype.fields["prev_input_t"][1]),
        int(samples.dtype.fields["input_t"][1]),
    )


def _rollout_window(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = _load_binding()
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8, seed_off, prev_input_off, input_off = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[
                record : record + 1, input_off : input_off + input_stride
            ].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), samples["ref_t1"][stop]


@pytest.mark.integration
def test_damage_exit_basic_turn_delays_visible_flip_until_source_countdown_expires() -> None:
    # HVG 6695 rollout enters Basic Turn from DamageN2 -> Wait_IASA after hitstun clears. Source
    # stores mv.co.turn.frames_to_turn at entry, calls ChangeMotionState, then immediately advances
    # the destination animation; the Turn_Anim countdown is not consumed until the next fighter proc.
    # This lock protects both the delayed visible facing flip and the common player-overlap nudge
    # while the fighter is still in Turn.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #   ftCo_Turn_Enter_Basic,ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _AGG_HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][6695]
    assert int(seed["action_id"][0]) == ACT_DAMAGE_N_2

    out_turn, ref_turn = _rollout_window(dataset_path, 6695, 6706)
    assert int(out_turn["action_id"][0]) == int(ref_turn["action_id"][0]) == ACT_TURN
    assert int(out_turn["action_frame"][0]) == int(ref_turn["action_frame"][0]) == 7
    assert int(out_turn["facing"][0]) == int(ref_turn["facing"][0]) == 1
    assert float(out_turn["pos_x"][0]) == pytest.approx(float(ref_turn["pos_x"][0]), abs=1e-6)
    assert float(out_turn["speed_ground_x_self"][0]) == pytest.approx(
        float(ref_turn["speed_ground_x_self"][0]), abs=1e-6
    )

    out_walk, ref_walk = _rollout_window(dataset_path, 6695, 6711)
    assert int(out_walk["action_id"][0]) == int(ref_walk["action_id"][0]) == ACT_WALK_SLOW
    assert int(out_walk["facing"][0]) == int(ref_walk["facing"][0]) == 1
    assert float(out_walk["pos_x"][0]) == pytest.approx(float(ref_walk["pos_x"][0]), abs=1e-6)

    out_wait, ref_wait = _rollout_window(dataset_path, 6695, 6712)
    assert int(out_wait["action_id"][0]) == int(ref_wait["action_id"][0]) == ACT_WAIT
    assert int(out_wait["facing"][0]) == int(ref_wait["facing"][0]) == 1
    assert float(out_wait["pos_x"][0]) == pytest.approx(float(ref_wait["pos_x"][0]), abs=1e-6)
