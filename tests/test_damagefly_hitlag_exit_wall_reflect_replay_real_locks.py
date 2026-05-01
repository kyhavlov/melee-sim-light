from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_GAT = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "GracefulAttachedTurtle.msl"
)


def _field_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    row = samples[record : record + 1]
    return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _run_one_step(dataset_path: Path, record: int, *, seed_mutator=None) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_t = samples[record : record + 1]["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed_t)
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(
            handle,
            _field_bytes(samples, record, "prev_input_t", input_stride),
            _field_bytes(samples, record, "input_t", input_stride),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return seed_t[0], samples[record]["ref_t1"], out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()


def _run_rollout(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start, "seed_t", seed_stride))
        for record in range(start, stop + 1):
            binding.step_input(
                handle,
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
            binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return samples[stop]["ref_t1"], out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()


@pytest.mark.integration
def test_damageflyroll_hitlag_exit_asdi_wall_hug_enters_flyreflectwall() -> None:
    # DamageFlyRoll hitlag exit owner:
    # - Fighter_8006A1BC runs ftCo_Damage_OnExitHitlag before Phys/Coll.
    # - ft_80081DD4 preserves the pre-ASDI CollData.prev_pos sweep root while cur_pos has already
    #   consumed ASDI and DI.
    # - DamageFlyRoll_Coll then sees Collide_LeftWallHug and ftCo_800C15F4 enters FlyReflectWall.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{ftCo_800C15F4,ftCo_800C18A8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _GAT
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_GAT}")

    p = 0
    seed, ref, out = _run_one_step(dataset_path, 3113)
    assert int(seed["action_id"][p]) == 91  # DamageFlyRoll
    assert int(seed["hitlag"][p]) == 1
    assert int(ref["action_id"][p]) == 247  # FlyReflectWall
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]))
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]))

    ref_roll, out_roll = _run_rollout(dataset_path, 3107, 3113)
    assert int(out_roll["action_id"][p]) == int(ref_roll["action_id"][p]) == 247
    assert float(out_roll["speed_x_attack"][p]) == pytest.approx(float(ref_roll["speed_x_attack"][p]))
    assert float(out_roll["speed_y_attack"][p]) == pytest.approx(float(ref_roll["speed_y_attack"][p]))


@pytest.mark.integration
def test_damageflyroll_hitlag_exit_wall_reflect_requires_knockback_threshold() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _GAT
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_GAT}")

    def clear_horizontal_kb(seed_t: np.ndarray) -> None:
        seed_t["speed_x_attack"][:, 0] = np.float32(0.0)

    _seed, _ref, out = _run_one_step(dataset_path, 3113, seed_mutator=clear_horizontal_kb)
    assert int(out["action_id"][0]) == 91  # DamageFlyRoll, not FlyReflectWall.

