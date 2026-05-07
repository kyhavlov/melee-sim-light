from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_sample(sample: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=int(sample["seed_t"]["num_players"][0]), ucf_enabled=1)
    try:
        msl_binding.reseed_seed(handle, sample["seed_t"].view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(
            handle,
            sample["prev_input_t"].view(np.uint8).reshape((1, input_stride)),
            sample["input_t"].view(np.uint8).reshape((1, input_stride)),
        )
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _step_record(path: str, rec: int) -> np.void:
    ds = read_dataset(path)
    return _step_sample(ds.samples[rec : rec + 1].copy())


def test_escapeair_airjump_locked_desired_bottom_lands_on_late_yoshi_platform_rows() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((149, 1), (2389, 0)):
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x002B
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
        assert float(out["pos_y"][player]) == float(ref["pos_y"][player])


def test_escapeair_late_yoshi_platform_rows_need_locked_desired_bottom_lane() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((149, 1), (2389, 0)):
        sample = ds.samples[rec : rec + 1].copy()
        seed = sample["seed_t"][0]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1
        sample["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][0, player] = np.uint8(0)

        out = _step_sample(sample)

        assert int(out["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == 0


def test_escapeair_locked_desired_bottom_waits_for_source_bottom_sweep() -> None:
    rows = (
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            148,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
            7364,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/ShadyDecimalStarling.msl",
            7521,
            1,
        ),
    )

    for path, rec, player in rows:
        ds = read_dataset(path)
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0


def test_escapeair_locked_desired_bottom_final_writeback_needs_bottom_sweep() -> None:
    path = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    ds = read_dataset(path)

    for rec, player in ((147, 1), (1983, 0), (1984, 0)):
        seed = ds.samples["seed_t"][rec]
        assert int(seed["action_id"][player]) == 0x00EC
        assert int(seed["ecb_lock_bottom_rel_y_valid_u8"][player]) == 1

        out = _step_record(path, rec)
        ref = ds.samples["ref_t1"][rec]

        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 0x00EC
        assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
