from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_BASE_REL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_REQUIRED_ARTIFACTS = (
    "data/moves/fox.json",
    "data/moves/falco.json",
    "data/scripts/fox.bin",
    "data/scripts/falco.bin",
    "data/hurtcaps/fox.bin",
    "data/hurtcaps/falco.bin",
)


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
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
        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        ref = row["ref_t1"].reshape(-1)[0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_replay_capturepulledhi_entry_clears_velocity_lanes_record_2691_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    victim = 0
    record = 2691
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay-real lock: Catch connect transitions victim SpecialAirS -> CapturePulledHi.
    # The captured victim's self/attack velocity lanes should match replay on entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18,ftCo_CapturePulledHi_Phys}
    assert int(row["seed_t"]["action_id"][0, victim]) == 351
    assert int(row["ref_t1"]["action_id"][0, victim]) == 223

    out, ref = _run_record(dataset_path, record)

    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert float(out["speed_air_x_self"][0, victim]) == pytest.approx(
        float(ref["speed_air_x_self"][victim]), abs=1e-6
    )
    assert float(out["speed_ground_x_self"][0, victim]) == pytest.approx(
        float(ref["speed_ground_x_self"][victim]), abs=1e-6
    )
    assert float(out["speed_y_self"][0, victim]) == pytest.approx(
        float(ref["speed_y_self"][victim]), abs=1e-6
    )
    assert float(out["speed_x_attack"][0, victim]) == pytest.approx(
        float(ref["speed_x_attack"][victim]), abs=1e-6
    )
    assert float(out["speed_y_attack"][0, victim]) == pytest.approx(
        float(ref["speed_y_attack"][victim]), abs=1e-6
    )


@pytest.mark.integration
def test_replay_capturepulledhi_entry_adjacent_context_controls_records_2690_2692() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE_REL}/TreasuredBackKangaroo.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    victim = 0
    ds = read_dataset(str(dataset_path))

    # Adjacent context controls: verify entry-frame zeroing does not bleed to surrounding rows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,ftCo_CapturePulledHi_Phys}
    row_before = ds.samples[2690:2691]
    assert int(row_before["seed_t"]["action_id"][0, victim]) == 351
    assert int(row_before["ref_t1"]["action_id"][0, victim]) == 351
    out_before, ref_before = _run_record(dataset_path, 2690)
    assert float(out_before["speed_air_x_self"][0, victim]) == pytest.approx(
        float(ref_before["speed_air_x_self"][victim]), abs=1e-6
    )
    assert float(out_before["speed_air_x_self"][0, victim]) > 1.0

    row_after = ds.samples[2692:2693]
    assert int(row_after["seed_t"]["action_id"][0, victim]) == 223
    assert int(row_after["ref_t1"]["action_id"][0, victim]) == 223
    out_after, ref_after = _run_record(dataset_path, 2692)
    assert float(out_after["speed_air_x_self"][0, victim]) == pytest.approx(
        float(ref_after["speed_air_x_self"][victim]), abs=1e-6
    )


@pytest.mark.integration
def test_replay_capturepulledhi_entry_applies_airborne_capture_delta_record_6468_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    _skip_if_required_artifacts_missing(root)

    victim = 0
    owner = 1
    record = 6468
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    # Replay-real lock: Catch -> CatchPull connects against an airborne DamageFlyTop victim and
    # fn_800DAADC immediately applies the capture-anchor minus victim-XRotN delta after
    # CapturePulledHi entry.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   fn_800DAADC,fn_800DAC78}
    assert int(row["seed_t"]["action_id"][0, owner]) == 212
    assert int(row["seed_t"]["action_id"][0, victim]) == 90
    assert int(row["ref_t1"]["action_id"][0, owner]) == 213
    assert int(row["ref_t1"]["action_id"][0, victim]) == 223

    out, ref = _run_record(dataset_path, record)

    assert int(out["action_id"][0, victim]) == int(ref["action_id"][victim])
    assert float(out["pos_x"][0, victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=1e-5)
    assert float(out["pos_y"][0, victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=1e-5)
