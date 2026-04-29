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


@pytest.mark.integration
def test_cliff_x1990_seed_does_not_promote_unproven_damage_x1994_rollout() -> None:
    # PPA rollout lock for ledge colanim ownership:
    # - Fox enters CliffCatch with a replay-history x1990 ledge intangible timer plus a stale
    #   damage x1994 seed candidate from the prior laser hitlag-exit derivation.
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
    assert int(seed["colanim_timer_x1994"][p]) > 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 86  # DamageAir3
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 6
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 21
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p]) == 0
    assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["action_id"][attacker]) == int(ref["action_id"][attacker])
