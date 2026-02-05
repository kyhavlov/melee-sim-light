from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import read_dataset


def _run_one_step_timebase(
    *,
    binding: object,
    seed_bytes: np.ndarray,
    prev_input_bytes: np.ndarray,
    input_bytes: np.ndarray,
    num_players: int,
    use_pre_combat: bool,
) -> np.ndarray:
    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        if use_pre_combat:
            binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        else:
            binding.step_input(handle, prev_input_bytes, input_bytes)
        return binding.debug_timebase(handle, 0)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_debug_step_input_pre_combat_matches_step_timebase_when_no_hit() -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    rows = ds.samples
    num_players = int(ds.header["num_players"])
    assert num_players == 2

    speed = rows["seed_t"]["frame_speed_mul_f32"][:, :num_players].astype(np.float32)
    no_seed_hit = (rows["seed_t"]["hitlag"][:, :num_players] == 0).all(axis=1) & (
        rows["seed_t"]["hitstun"][:, :num_players] == 0
    ).all(axis=1)
    no_ref_hit = (rows["ref_t1"]["hitlag"][:, :num_players] == 0).all(axis=1) & (
        rows["ref_t1"]["hitstun"][:, :num_players] == 0
    ).all(axis=1)
    has_anim_progress = (np.abs(speed) > np.float32(1.0e-6)).any(axis=1)
    candidate = np.where(no_seed_hit & no_ref_hit & has_anim_progress)[0]
    if candidate.size == 0:
        pytest.skip("no suitable no-hit candidate row with non-zero frame_speed_mul_f32")

    rec = int(candidate[0])
    row = rows[rec : rec + 1]

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    tb_pre = _run_one_step_timebase(
        binding=msl_binding,
        seed_bytes=seed_bytes,
        prev_input_bytes=prev_input_bytes,
        input_bytes=input_bytes,
        num_players=num_players,
        use_pre_combat=True,
    )
    tb_full = _run_one_step_timebase(
        binding=msl_binding,
        seed_bytes=seed_bytes,
        prev_input_bytes=prev_input_bytes,
        input_bytes=input_bytes,
        num_players=num_players,
        use_pre_combat=False,
    )

    np.testing.assert_allclose(tb_pre[:num_players, :6], tb_full[:num_players, :6], rtol=0.0, atol=1.0e-6)

    seed_anim = row["seed_t"]["anim_frame_f32"][0, :num_players].astype(np.float32)
    assert np.any(np.abs(tb_pre[:num_players, 3] - seed_anim) > np.float32(1.0e-6))
