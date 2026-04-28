from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_DAMAGE_FLY_HI = 0x0057
ACT_PASSIVE = 0x00C7


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
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


def _run_rollout_records(
    dataset_path: Path, start_record: int, target_records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_max = max(target_records)
    targets = set(target_records)
    assert int(samples.shape[0]) > target_max

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_max + 1):
            row = samples[record : record + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(
                row["input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if record in targets:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                out_by_record[record] = (samples[record]["ref_t1"].copy(), out)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damageflyhi_rollout_uses_prior_sweep_root_for_floor_tech_handoff() -> None:
    # Replay-real lock for AGN rec=5196 -> rec=5208 p0:
    # - direct one-step at rec=5208 already lands into Passive because the replay seed carries the
    #   previous floor-sweep root.
    # - rollout must promote the prior frame's pre-physics root for the next floor sweep; using the
    #   current root as the sweep start misses the floor and stays in DamageFlyHi.
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpCheckFloor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    p = 0
    seed, ref, out = _run_one_step(dataset_path, 5208)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_HI
    assert int(seed["floor_sweep_prev_pos_valid_u8"][p]) == 1
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) == pytest.approx(-1.881044864654541)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_PASSIVE
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1

    by_record = _run_rollout_records(dataset_path, 5196, (5207, 5208))
    ref_5207, out_5207 = by_record[5207]
    assert int(out_5207["action_id"][p]) == int(ref_5207["action_id"][p]) == ACT_DAMAGE_FLY_HI
    assert int(out_5207["on_ground"][p]) == int(ref_5207["on_ground"][p]) == 0
    assert float(out_5207["pos_y"][p]) == pytest.approx(float(ref_5207["pos_y"][p]), abs=1e-6)

    ref_5208, out_5208 = by_record[5208]
    assert int(out_5208["action_id"][p]) == int(ref_5208["action_id"][p]) == ACT_PASSIVE
    assert int(out_5208["on_ground"][p]) == int(ref_5208["on_ground"][p]) == 1
    assert int(out_5208["hitstun"][p]) == int(ref_5208["hitstun"][p]) == 0
    assert float(out_5208["pos_y"][p]) == pytest.approx(float(ref_5208["pos_y"][p]), abs=1e-6)
