from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.eval.streaming_validation import _load_binding


_AGG_FSP = Path(
    "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
)

ACT_RUN_BRAKE = 23
ACT_TURN_RUN = 19


def _byte_views(ds):
    views = replay_buffer_byte_views(ds)
    return views.seed_t, views.prev_input_t, views.input_t


def _rollout_row(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), samples["ref_t1"][stop]


@pytest.mark.integration
def test_turnrun_cmd1_freeze_handles_runbrake_preserved_anim_start_rollout() -> None:
    # FSP 9664 rollout hits RunBrake -> TurnRun with TurnRun entered at preserved anim_start=11.
    # The TurnRun script cmd_var[1] frame has already been crossed by the visible animation frame,
    # but source only consumes the freeze after a steady TurnRun Anim callback. The first TurnRun
    # row must still advance 12->13, then the next row freezes at 13 until the pivot.
    #
    # Decomp/data:
    # - ftCo_TurnRun_Enter preserves RunBrake cur_anim_frame via fn_800C9CEC.
    # - ftCo_TurnRun_Anim consumes script-owned cmd_vars[1], sets anim rate 0, and later flips.
    # - data/scripts/{fox,falco}.bin MSLFTSC1 contains ftCo_SM_TurnRun set_cmd_var(idx=1).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::{
    #   fn_800C9CEC,ftCo_TurnRun_Enter,ftCo_TurnRun_Anim}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _AGG_FSP
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    start = 9664
    p = 1

    seed_first_turnrun = ds.rows["seed_t"][9669]
    assert int(seed_first_turnrun["action_id"][p]) == ACT_TURN_RUN
    assert int(seed_first_turnrun["action_frame"][p]) == 12
    assert int(seed_first_turnrun["seed_prev_action_id"][p]) == ACT_RUN_BRAKE

    out_first, ref_first = _rollout_row(dataset_path, start, 9669)
    assert int(out_first["action_id"][p]) == int(ref_first["action_id"][p]) == ACT_TURN_RUN
    assert int(out_first["action_frame"][p]) == int(ref_first["action_frame"][p]) == 13
    assert float(out_first["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_first["speed_ground_x_self"][p]), abs=1e-7
    )

    out_freeze, ref_freeze = _rollout_row(dataset_path, start, 9670)
    assert int(out_freeze["action_id"][p]) == int(ref_freeze["action_id"][p]) == ACT_TURN_RUN
    assert int(out_freeze["action_frame"][p]) == int(ref_freeze["action_frame"][p]) == 13
    assert float(out_freeze["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_freeze["speed_ground_x_self"][p]), abs=1e-7
    )

    out_pivot, ref_pivot = _rollout_row(dataset_path, start, 9683)
    assert int(out_pivot["action_id"][p]) == int(ref_pivot["action_id"][p]) == ACT_TURN_RUN
    assert int(out_pivot["action_frame"][p]) == int(ref_pivot["action_frame"][p]) == 13
    assert int(out_pivot["facing"][p]) == int(ref_pivot["facing"][p]) == 0
