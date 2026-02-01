from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_damageair3_does_not_spuriously_land_record_1028() -> None:
    # Regression lock: in GracefulAttachedTurtle, record 1028 previously grounded (and entered Landing)
    # even though the Slippi reference remains in DamageAir3.
    #
    # Root cause: our ISO-derived SSANIM01 subset (data/anims/*.bin) omitted ftCo_Submotion ids
    # DamageAir2/DamageAir3 (175/176), so ECB sampling fell back to 0 (missing table entry) and floor
    # collision grounded too early.
    #
    # Decomp source of these submotion ids:
    # - refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`
    # (see tools/extraction/build_data.py FTCO_SM_DAMAGEAIR{2,3})
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    # Integration policy: skip if required ISO-derived data artifacts are missing locally.
    required = [
        "data/ecb/fox_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/falco_extents.bin",
    ]
    missing = [p for p in required if not (root / p).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 1028
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == 86  # ftCo_MS_DamageAir3
    assert int(row["seed_t"]["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])

    expected_action = int(row["ref_t1"]["action_id"][0, p])
    expected_anim = int(row["ref_t1"]["animation_index"][0, p])

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, p])
        got_anim = int(out["animation_index"][0, p])

        assert got_action == expected_action, (
            f"record=1028 p=0 expected action_id={expected_action}, got {got_action}"
        )
        assert got_anim == expected_anim, (
            f"record=1028 p=0 expected animation_index={expected_anim}, got {got_anim}"
        )
    finally:
        binding.destroy(handle)
