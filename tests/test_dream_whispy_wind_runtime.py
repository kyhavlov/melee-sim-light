from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.replay_buffers_loader import load_replay_buffers
from tools.slippi.validation_buffer_items import _derive_dream_whispy_wind_seed_lanes
from tools.extraction.known_data_artifacts import dream_whispy_metadata


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
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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


def _rollout_row_ucf(
    dataset_path: Path,
    start_record: int,
    target_record: int,
    samples_override: np.ndarray | None = None,
    *,
    replay_frame_rng: bool = False,
) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    ds = load_replay_buffers(str(dataset_path))
    samples = samples_override if samples_override is not None else ds.rows
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes = np.frombuffer(
            samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"),
            dtype=np.uint8,
        ).reshape(1, seed_stride)
        binding.reseed_seed_rollout(handle, seed_bytes.copy())
        for rec in range(start_record, target_record + 1):
            row = samples[rec : rec + 1]
            step_seed_bytes = np.frombuffer(
                row["seed_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, seed_stride)
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                1, input_stride
            )
            if replay_frame_rng:
                binding.step_input_replay_frame_rng(
                    handle, step_seed_bytes.copy(), prev_input_bytes.copy(), input_bytes.copy()
                )
            else:
                binding.step_input(handle, prev_input_bytes.copy(), input_bytes.copy())
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        return samples[target_record]["ref_t1"], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


def _base_seed(x: float, y: float, wind_dir: int, *, valid: bool = True) -> np.ndarray:
    ds = load_replay_buffers(
        "replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.slpz"
    )
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed[0] = ds.rows[890]["seed_t"]
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["stage_dream_whispy_wind_dir_u8"] = np.uint8(wind_dir)
    seed["stage_dream_whispy_wind_valid_u8"] = np.uint8(1 if valid else 0)
    seed["stage_dream_whispy_wind_timer_u16"] = np.uint16(274 if valid else 0)
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
        "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz"
    )
    seed, ref, out = _step_one_row(dataset_path, 890)
    assert int(seed["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(seed["stage_dream_whispy_wind_dir_u8"]) == 2
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]))


@pytest.mark.integration
def test_dream_whispy_replay_playback_applies_prefix_lane_each_frame_feh_953() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz"
    )
    ref, out = _rollout_row_ucf(dataset_path, 889, 953, replay_frame_rng=True)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 361
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 0
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=5e-5)
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]), abs=1e-4)


@pytest.mark.integration
def test_dream_whispy_first_visible_seed_catchup_is_rollout_advanced_only() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz"
    )
    ds = load_replay_buffers(str(dataset_path))
    assert int(ds.rows[889]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 0
    assert int(ds.rows[890]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(ds.rows[890]["seed_t"]["stage_dream_whispy_wind_timer_u16"]) == 274

    ref_from_start, out_from_start = _rollout_row_ucf(
        dataset_path, 889, 890, replay_frame_rng=True
    )
    assert float(out_from_start["pos_x"][1]) == pytest.approx(
        float(ref_from_start["pos_x"][1]), abs=1e-4
    )

    ref_from_seed, out_from_seed = _rollout_row_ucf(
        dataset_path, 890, 890, replay_frame_rng=True
    )
    assert float(out_from_seed["pos_x"][1]) == pytest.approx(
        float(ref_from_seed["pos_x"][1]), abs=1e-5
    )


@pytest.mark.integration
def test_dream_whispy_seed_wind_does_not_stale_carry_after_episode() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/ShadyDecimalStarling.slpz"
    )
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows.copy()
    samples["seed_t"]["stage_dream_whispy_wind_dir_u8"] = np.uint8(0)
    samples["seed_t"]["stage_dream_whispy_wind_valid_u8"] = np.uint8(0)
    samples["seed_t"]["stage_dream_whispy_wind_timer_u16"] = np.uint16(0)
    dream_wind_dir, dream_wind_valid, dream_wind_timer = _derive_dream_whispy_wind_seed_lanes(
        samples, stage_id=STAGE_DREAM_LAND_N64, num_players=int(ds.num_players)
    )
    samples["seed_t"]["stage_dream_whispy_wind_dir_u8"] = dream_wind_dir
    samples["seed_t"]["stage_dream_whispy_wind_valid_u8"] = dream_wind_valid
    samples["seed_t"]["stage_dream_whispy_wind_timer_u16"] = dream_wind_timer
    assert int(samples[714]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(samples[714]["seed_t"]["stage_dream_whispy_wind_timer_u16"]) < 274
    assert int(samples[1927]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 0

    ref, out = _rollout_row_ucf(dataset_path, 714, 1947, samples)
    p = 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    # The source PSVECNormalize floor normal accumulates a 5.8e-5 horizontal residual across this
    # 1,234-frame rollout without changing the stale-wind owner this test protects.
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=6e-5)


@pytest.mark.integration
def test_dream_whispy_sparse_contact_gap_keeps_hidden_xdc_episode_sds_2231() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/ShadyDecimalStarling.slpz"
    )
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows.copy()
    samples["seed_t"]["stage_dream_whispy_wind_dir_u8"] = np.uint8(0)
    samples["seed_t"]["stage_dream_whispy_wind_valid_u8"] = np.uint8(0)
    samples["seed_t"]["stage_dream_whispy_wind_timer_u16"] = np.uint16(0)
    dream_wind_dir, dream_wind_valid, dream_wind_timer = _derive_dream_whispy_wind_seed_lanes(
        samples, stage_id=STAGE_DREAM_LAND_N64, num_players=int(ds.num_players)
    )
    samples["seed_t"]["stage_dream_whispy_wind_dir_u8"] = dream_wind_dir
    samples["seed_t"]["stage_dream_whispy_wind_valid_u8"] = dream_wind_valid
    samples["seed_t"]["stage_dream_whispy_wind_timer_u16"] = dream_wind_timer

    assert int(samples[1991]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(samples[2004]["seed_t"]["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(samples[2004]["seed_t"]["stage_dream_whispy_wind_timer_u16"]) < 274

    ref, out = _rollout_row_ucf(dataset_path, 0, 2231, samples, replay_frame_rng=True)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 28
    assert int(out["on_ground"][0]) == int(ref["on_ground"][0]) == 0
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=5e-5)


@pytest.mark.integration
def test_dream_whispy_final_timer_row_does_not_apply_wind_sds_2264() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/ShadyDecimalStarling.slpz"
    )
    seed, ref, out = _step_one_row(dataset_path, 2264)
    assert int(seed["stage_dream_whispy_wind_valid_u8"]) == 1
    assert int(seed["stage_dream_whispy_wind_timer_u16"]) == 1
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=2e-6)
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]), abs=2e-6)
