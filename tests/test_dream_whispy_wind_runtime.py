from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.known_data_artifacts import dream_whispy_metadata


STAGE_DREAM_LAND_N64 = 28


def _step_seed(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
        cur_input = np.zeros((1,), dtype=INPUT_DTYPE)
        prev_input_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        cur_input_bytes = np.frombuffer(cur_input.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes.copy())
        binding.step_input(handle, prev_input_bytes.copy(), cur_input_bytes.copy())
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes.copy())
        binding.step_input(handle, prev_input_bytes.copy(), input_bytes.copy())
        binding.write_compare(handle, out_bytes)
        return row["seed_t"][0], row["ref_t1"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


def _base_seed(x: float, y: float, wind_dir: int, *, valid: bool = True) -> np.ndarray:
    ds = read_dataset(
        "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed[0] = ds.samples[890]["seed_t"]
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["stage_dream_whispy_wind_dir_u8"] = np.uint8(wind_dir)
    seed["stage_dream_whispy_wind_valid_u8"] = np.uint8(1 if valid else 0)
    seed["items"][0, :]["owner"] = np.int8(-1)
    return seed


def test_dream_whispy_wind_direction_magnitude_and_rect_gate() -> None:
    params = dream_whispy_metadata(Path("data"))
    x = (params.right_rect_left + params.right_rect_right) * 0.5
    y = (params.rect_bottom + params.rect_top) * 0.5

    inside_right = _base_seed(x, y, 2)
    no_wind = _base_seed(x, y, 2, valid=False)
    right_out = _step_seed(inside_right)
    no_wind_out = _step_seed(no_wind)
    assert float(right_out["pos_x"][0] - no_wind_out["pos_x"][0]) == pytest.approx(
        params.wind_speed, abs=1e-6
    )

    left_x = (params.left_rect_left + params.left_rect_right) * 0.5
    inside_left = _base_seed(left_x, y, 1)
    no_left = _base_seed(left_x, y, 1, valid=False)
    left_out = _step_seed(inside_left)
    no_left_out = _step_seed(no_left)
    assert float(left_out["pos_x"][0] - no_left_out["pos_x"][0]) == pytest.approx(
        -params.wind_speed, abs=1e-6
    )

    outside = _base_seed(params.right_rect_right + 1.0, y, 2)
    outside_no_wind = _base_seed(params.right_rect_right + 1.0, y, 2, valid=False)
    assert float(_step_seed(outside)["pos_x"][0]) == pytest.approx(
        float(_step_seed(outside_no_wind)["pos_x"][0])
    )


def test_dream_whispy_replay_seed_wind_matches_representative_row() -> None:
    dataset_path = Path(
        "datasets/aggregate_recent/replays/validation/dream_land_recent/FlippantEnchantedHorse.msl"
    )
    seed, ref, out = _step_one_row(dataset_path, 890)
    assert int(seed["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(seed["stage_dream_whispy_wind_dir_u8"]) == 2
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]))
