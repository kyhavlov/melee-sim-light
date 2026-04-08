from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

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

    return row["seed_t"][0], row["ref_t1"][0], out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _assert_f32_bits_match(got: float, exp: float, *, record: int, p: int, field: str) -> None:
    got_bits = int(np.float32(got).view(np.uint32))
    exp_bits = int(np.float32(exp).view(np.uint32))
    assert got_bits == exp_bits, (
        f"record={record} p={p} field={field} expected={exp} (0x{exp_bits:08x}) "
        f"got {got} (0x{got_bits:08x})"
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [5119, 5120, 5121])
def test_grounded_friction_only_vertical_speed_target_and_adjacent_rows_stay_replay_exact(record: int) -> None:
    # Replay-real locks for the grounded friction-only vertical-speed lane:
    # - grounded ft_80084F3C states should not carry stale airborne speed_y_self while on the floor.
    # - the target Landing -> Fall release row must leave the floor with zero self Y speed, while
    #   adjacent Landing entry / Fall continuation rows stay exact.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 1
    seed, ref, out = _run_one_step_row(dataset_path, record)

    if record == 5120:
        assert int(seed["action_id"][p]) == 42
        assert int(ref["action_id"][p]) == 29
        assert int(seed["on_ground"][p]) == 1
        assert int(ref["on_ground"][p]) == 0
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"record={record} p={p} field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 1e-6, (
        f"record={record} p={p} field=pos_y expected~={float(ref['pos_y'][p])} "
        f"got={float(out['pos_y'][p])}"
    )
    _assert_f32_bits_match(
        float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]), record=record, p=p, field="speed_y_self"
    )


@pytest.mark.integration
def test_grounded_friction_only_vertical_speed_negative_control_guardreflect_stays_replay_exact() -> None:
    # Explicit over-broad negative control:
    # GuardReflect also routes through grounded ft_80084F3C, but this lane must not perturb its
    # steady no-hit snapshot rows.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 11084
    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)
    assert int(seed["action_id"][p]) == 182
    assert int(ref["action_id"][p]) == 182

    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"record={record} p={p} field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )
    _assert_f32_bits_match(float(out["pos_y"][p]), float(ref["pos_y"][p]), record=record, p=p, field="pos_y")
    _assert_f32_bits_match(
        float(out["speed_y_self"][p]), float(ref["speed_y_self"][p]), record=record, p=p, field="speed_y_self"
    )
