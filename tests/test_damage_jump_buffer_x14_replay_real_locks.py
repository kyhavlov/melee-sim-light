from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


TCH = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl")


def _skip_if_dataset_missing(ds_path: Path) -> None:
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")


def _run_one_step_row(ds_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    _skip_if_dataset_missing(ds_path)
    ds = read_dataset(str(ds_path))
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


@pytest.mark.integration
def test_damage_air_tap_jump_x14_refresh_uses_x671_window_replay_real() -> None:
    # TCH 11852 is the top F08d rollout island's direct owner:
    # - an earlier XY press left a stale high x14 snapshot (38),
    # - the later UCF-processed tap-jump crosses the threshold on x671=1,
    # - doIasa refreshes x14 into the <=x1D0 window before terminal Damage_IASA enters JumpAerialF.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
    seed, ref, out = _run_one_step_row(TCH, 11852)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["damage_jump_buffer_x14"][p]) == 17
    assert int(seed["tilt_timer_y"][p]) == 254

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 27  # JumpAerialF
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_damage_air_tap_jump_x14_does_not_refresh_below_threshold_replay_real() -> None:
    # The first x671=0 frame in the same TCH sequence is below tap-jump threshold after UCF diagonal
    # adjustment, so the stale high x14 remains inert and must not enter JumpAerialF early.
    seed, ref, out = _run_one_step_row(TCH, 11834)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["tilt_timer_y"][p]) == 0
    assert int(seed["damage_jump_buffer_x14"][p]) == 38

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 86
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 1
