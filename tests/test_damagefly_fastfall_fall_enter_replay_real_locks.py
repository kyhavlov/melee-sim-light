from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_FALL = 29
ACT_DAMAGE_FLY_TOP = 90


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


def _aggregate_dataset_path(root: Path, name: str) -> Path:
    dataset_rel = f"replays/validation/aggregate_recent/{name}.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    return dataset_path


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
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

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_max = max(target_records)
    targets = set(target_records)
    assert int(samples.shape[0]) > target_max

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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
def test_damageflytop_fastfall_survives_damagefall_iasa_fall_enter_rollout() -> None:
    # Replay-real lock for DCC rec=6407 -> rec=6437 p0:
    # - DamageFlyTop latches fall_fast after hitstun clears via ftCo_DamageFly_Phys -> ft_80084DB0.
    # - The later DamageFly_Anim -> ftCo_80090780 -> DamageFall handoff and same-frame
    #   DamageFall_IASA -> ftCo_Fall_Enter both use Ft_MF_KeepFastFall, so the Fall row keeps
    #   fp->fall_fast and the already-fastfall terminal velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Phys,ftCo_DamageFly_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{ftCo_80090780,ftCo_DamageFall_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset_path(root, "DistinctCaringCobra")

    p = 0
    out_by_record = _run_rollout_records(dataset_path, 6407, (6433, 6437, 6438))

    ref, out = out_by_record[6433]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["state_flags"][p][1]) == int(ref["state_flags"][p][1]) == 0x08
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)

    ref, out = out_by_record[6437]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_FALL
    assert int(out["state_flags"][p][1]) == int(ref["state_flags"][p][1]) == 0x08
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    ref, out = out_by_record[6438]
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["state_flags"][p][1]) == int(ref["state_flags"][p][1]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflytop_fall_enter_does_not_create_fastfall_without_source_latch() -> None:
    # Negative control: BHH rec=2545 p1 has the same DamageFlyTop -> Fall shape but no source
    # fall_fast latch, so the KeepFastFall paths must preserve zero rather than creating x221A bit
    # 0x08 from action family alone.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset_path(root, "BlondHardHippopotamus")

    p = 1
    seed, ref, out = _run_one_step(dataset_path, 2545)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["fall_fast"][p]) == 0
    assert int(seed["state_flags"][p][1]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_FALL
    assert int(out["state_flags"][p][1]) == int(ref["state_flags"][p][1]) == 0
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)
