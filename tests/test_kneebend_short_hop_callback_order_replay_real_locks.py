from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, ref, out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_kneebend_takeoff_frame_release_keeps_full_jump_dcc_9255() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Source ordering lock:
    # - ftCo_KneeBend_Anim enters JumpF before ftCo_KneeBend_IASA can call
    #   ftCo_KneeBend_Check_ShortHop on the same frame.
    # - Releasing X/Y on the Anim-owned takeoff frame is therefore too late to convert the jump
    #   into a short hop; only an earlier latch or seeded latch may do that.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{
    #   ftCo_KneeBend_Anim,ftCo_KneeBend_IASA,ftCo_KneeBend_Check_ShortHop
    # }
    seed, ref, out = _step_one_row(dataset_path, 9255)
    p = 0

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 2
    assert int(seed["kneebend_is_short_hop"][p]) == 0
    assert int(seed["kneebend_jump_input"][p]) == 3  # JumpInput_XY

    assert int(ref["action_id"][p]) in (25, 26)  # JumpF/B
    assert int(ref["action_frame"][p]) == 0
    assert np.isclose(float(ref["speed_y_self"][p]), 3.680000066757202)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]))
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]))


@pytest.mark.integration
def test_kneebend_seeded_short_hop_still_uses_hop_velocity_his_154() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Negative boundary: the takeoff-frame latch suppression must not erase a real short-hop bit
    # that was already produced by an earlier KneeBend_IASA frame and carried in the seed.
    seed, ref, out = _step_one_row(dataset_path, 154)
    p = 0

    assert int(seed["action_id"][p]) == 24  # KneeBend
    assert int(seed["action_frame"][p]) == 2
    assert int(seed["kneebend_is_short_hop"][p]) == 1
    assert int(seed["kneebend_jump_input"][p]) == 3  # JumpInput_XY

    assert int(ref["action_id"][p]) in (25, 26)  # JumpF/B
    assert int(ref["action_frame"][p]) == 0
    assert np.isclose(float(ref["speed_y_self"][p]), 2.0999999046325684)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert np.isclose(float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]))
    assert np.isclose(float(out["pos_y"][p]), float(ref["pos_y"][p]))
