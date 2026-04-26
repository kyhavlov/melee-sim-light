from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(ds_path: Path, record: int) -> tuple[np.void, np.void]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = samples[record : record + 1]
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
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

    return samples["ref_t1"][record].copy(), out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_rollout_records(ds_path: Path, start_record: int, records: tuple[int, ...]) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(records)
    assert start_record <= min(records)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    out: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed(handle, seed_bytes)
        for record in range(start_record, max(records) + 1):
            prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out[record] = (
                    samples["ref_t1"][record].copy(),
                    out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                )
    finally:
        binding.destroy(handle)

    return out


@pytest.mark.integration
def test_attackhi4_post_contact_hitlag_seed_suppresses_same_window_rehit_rollout() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    # Replay-real positive:
    # p1 AttackHi4 hits p0 on the create-frame BODY callback and both fighters enter hitlag.
    # Reseeding at rec=2115 starts inside that same hitlag window while the pose frame still equals
    # the create frame. The dense seed hitlist is the post-contact lbColl_80008688 victims_1 state
    # and must survive the enable-edge materialization so rollout does not rehit at hitlag exit.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    rows = _run_rollout_records(ds_path, start_record=2115, records=(2123, 2124))
    for record in (2123, 2124):
        ref, out = rows[record]
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "pos_y"):
            assert out[field][0] == ref[field][0], f"record={record} field={field}"


@pytest.mark.integration
def test_attackhi4_fresh_create_frame_still_admits_initial_body_hit() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    # Replay-real negative/control: before the accepted BODY hit, there is no active-hitlag
    # post-contact seed owner, so the create-frame hit must still be admitted normally.
    ref, out = _run_one_step(ds_path, 2114)
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by"):
        assert out[field][0] == ref[field][0], f"field={field}"


@pytest.mark.integration
def test_attackhi4_post_contact_hitlag_bridge_requires_body_source_proof() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    record = 2115
    attacker = 1
    victim = 0
    assert int(ds.samples.shape[0]) > record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    def materialized_hitlists(*, instance_hit_by: int | None, hitstun: int | None, clear_cd: bool) -> list[int]:
        row = ds.samples[record : record + 1].copy()
        seed = row["seed_t"]
        if instance_hit_by is not None:
            seed["instance_hit_by"][0, victim] = np.uint16(instance_hit_by)
        if hitstun is not None:
            seed["hitstun"][0, victim] = np.uint16(hitstun)
        if clear_cd:
            seed["combat_hitlist_cd"][0, attacker, :, victim] = np.uint16(0)

        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride)
        )
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

        handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
        try:
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
            return [
                int(binding.debug_hitlist_fighter_contains(handle, 0, attacker, hb_id, victim))
                for hb_id in range(4)
            ]
        finally:
            binding.destroy(handle)

    attacker_iid = int(ds.samples["seed_t"][record]["instance_id"][attacker])
    victim_hitstun = int(ds.samples["seed_t"][record]["hitstun"][victim])

    assert materialized_hitlists(instance_hit_by=None, hitstun=None, clear_cd=False) == [1, 1, 0, 0]
    assert materialized_hitlists(instance_hit_by=attacker_iid + 17, hitstun=None, clear_cd=False) == [
        0,
        0,
        0,
        0,
    ]
    assert materialized_hitlists(instance_hit_by=None, hitstun=0, clear_cd=False) == [0, 0, 0, 0]
    assert victim_hitstun > 0
    assert materialized_hitlists(instance_hit_by=None, hitstun=None, clear_cd=True) == [0, 0, 0, 0]
