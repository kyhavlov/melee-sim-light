from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_FX_SPECIAL_AIR_LW_START = 0x016D
ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E
ACT_FX_SPECIAL_AIR_LW_TURN = 0x0171


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def test_aerial_shine_loop_applies_reflector_fall_phys() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 717)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_start_delay_does_not_apply_reflector_fall_early() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 715)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_start_to_loop_handoff_does_not_double_tick_fall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 716)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_start_to_turn_handoff_does_not_double_tick_fall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 4738)
    p = 1
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_TURN

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_TURN
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
