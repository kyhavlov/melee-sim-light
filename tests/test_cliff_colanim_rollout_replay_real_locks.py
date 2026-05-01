from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


def _rollout_one_row(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(target_record), f"dataset too short for record={target_record}"

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_row = None
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
            if record == target_record:
                out_row = out_view[0].copy()
                break
        assert out_row is not None
        return samples["seed_t"][start_record].copy(), samples["ref_t1"][target_record].copy(), out_row
    finally:
        binding.destroy(handle)


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(record), f"dataset too short for record={record}"

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_bytes.view(COMPARE_DTYPE).reshape(1)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(
            handle, samples_u8[record : record + 1, seed_off : seed_off + seed_stride].copy()
        )
        binding.step_input(
            handle,
            samples_u8[record : record + 1, prev_input_off : prev_input_off + input_stride].copy(),
            samples_u8[record : record + 1, input_off : input_off + input_stride].copy(),
        )
        binding.write_compare(handle, out_bytes)
        return samples["seed_t"][record].copy(), samples["ref_t1"][record].copy(), out_view[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_cliff_x1990_seed_does_not_promote_unproven_damage_x1994_rollout() -> None:
    # PPA rollout lock for ledge colanim ownership:
    # - Fox enters CliffCatch with a replay-history x1990 ledge intangible timer.
    # - Earlier versions also carried a stale damage x1994 seed candidate from prior laser
    #   hitlag-exit derivation; seed history now clears that stale x1994 at the earlier visible
    #   vulnerable non-damage row.
    # - The source CliffCatch/CliffWait entry path only calls ftColl_8007B760(..., x49C), so the
    #   unproven x1994 lane must not be queued behind ledge x1990.
    # - When CliffAttackQuick reaches Falco's AttackAirN BODY contact, Fox is vulnerable and enters
    #   DamageAir3; carrying the stale x1994 yields attacker-only hitlag and misses this rollout hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B760,ftColl_8007B7A4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    start_record = 2360
    target_record = 2404
    p = 0
    attacker = 1

    seed, ref, out = _rollout_one_row(dataset_path, start_record, target_record)
    assert int(seed["action_id"][p]) == 252  # CliffCatch
    assert int(seed["colanim_timer_x1990"][p]) > 0
    assert int(seed["colanim_timer_x1994"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 86  # DamageAir3
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 6
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 21
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p]) == 0
    assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["action_id"][attacker]) == int(ref["action_id"][attacker])


@pytest.mark.integration
def test_cliffjump_x1990_expiry_does_not_promote_stale_x1994_hvg_5101() -> None:
    # HVG lock for the same ledge colanim owner on CliffJumpQuick1/2:
    # - The replay-history seed carries an x1994 candidate underneath ledge x1990 during
    #   CliffJumpQuick2, but the cliff source path is still x1990-only (`ftColl_8007B760`).
    # - When x1990 expires on this row, visible hurtbox_state must clear to vulnerable instead of
    #   falling through to an unproven x1994 invincible-contact status.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::{ftCo_8009B1B8,ftCo_8009B2F8}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B760,ftColl_8007B7A4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5101
    p = 1
    seed, ref, out = _step_one_row(dataset_path, record)

    assert int(seed["action_id"][p]) == 263  # CliffJumpQuick2
    assert int(seed["colanim_hit_status_x198c"][p]) == 2
    assert int(seed["colanim_timer_x1990"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) > 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 263
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 10
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p]) == 0
