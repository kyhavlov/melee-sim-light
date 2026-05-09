from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_DSG = Path("datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl")


def _run_one_step(binding: object, row: np.ndarray, *, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_rollout_to_record(
    binding: object, samples: np.ndarray, start: int, target: int, *, num_players: int
) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = (
        np.frombuffer(samples[start : start + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            row = samples[record : record + 1]
            prev_input_bytes = (
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_wait_shield_entry_laser_contact_uses_live_shielddesc() -> None:
    # DSG:10535 has Fox in Wait pressing analog shield as a Falco laser reaches the collision-time
    # ShieldDesc. Vanilla routes the item through HitShield/GuardSetOff and destroys the laser;
    # the simulator must not first leave the fresh GuardOn entry on the capped steady-shield lane.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450,ftCo_80092C54}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076CBC}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_HitShield}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _DSG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_DSG}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[10535:10536]
    defender = 0
    slot = 0

    assert int(row["seed_t"]["action_id"][0, defender]) == 14  # Wait
    assert int(row["seed_t"]["items"][0, slot]["type"]) == 55  # Falco laser
    assert int(row["ref_t1"]["action_id"][0, defender]) == 181  # GuardSetOff
    assert int(row["ref_t1"]["items"][0, slot]["exists"]) == 0

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "animation_index", "action_frame", "hitlag", "state_flags"):
        np.testing.assert_array_equal(out[field][defender], row["ref_t1"][field][0, defender])
    assert float(out["shield_hp"][defender]) == pytest.approx(
        float(row["ref_t1"]["shield_hp"][0, defender]), abs=1.0e-3
    )
    assert int(out["items"][slot]["exists"]) == 0


@pytest.mark.integration
def test_laser_before_wait_shield_entry_contact_stays_alive() -> None:
    # Adjacent negative: one row before the ShieldDesc contact, the same laser is still alive and
    # Fox remains in Wait. This protects the full item HitCapsule scale lane from becoming a broad
    # early projectile destroy.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _DSG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_DSG}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[10534:10535]
    defender = 0
    slot = 0

    assert int(row["seed_t"]["action_id"][0, defender]) == 14
    assert int(row["ref_t1"]["action_id"][0, defender]) == 14
    assert int(row["ref_t1"]["items"][0, slot]["exists"]) == 1

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    assert int(out["action_id"][defender]) == 14
    assert int(out["hitlag"][defender]) == 0
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["type"]) == 55


@pytest.mark.integration
def test_wait_shield_entry_laser_contact_rollout_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _DSG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_DSG}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    defender = 0
    slot = 0

    out = _run_rollout_to_record(binding, samples, 10498, 10535, num_players=int(ds.header["num_players"]))
    ref = samples[10535]["ref_t1"]
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]), abs=1.0e-3)
    assert int(out["items"][slot]["exists"]) == 0
