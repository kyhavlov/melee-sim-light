from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_hitlist_clears_on_death_respawn_instance_id_mismatch_allows_hit() -> None:
    # Regression lock: when the victim is in a death/respawn motion state, treat an instance-id
    # proxy mismatch as "new victim" and clear the hitlist entry (decomp hitlists key by victim
    # pointer equality).
    #
    # This test is intentionally synthetic: it uses a real dataset record where the victim is in a
    # death motion state, then injects a stale hitlist entry and forces a simple hitbox↔hurtcap
    # overlap via debug primitives. The hit must be accepted; otherwise the stale entry would
    # suppress it.
    #
    # NOTE: If we later implement stricter combat gating that forbids taking hits during Dead*/Rebirth*
    # states, this test may need to be updated to assert hitlist clearing via a different observable
    # (or a different record/state where hits are still processed).
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    record = 3137
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    # Use p=0 as the victim (DeadDown) and p=1 as the attacker.
    victim = 0
    attacker = 1
    hit_group = 0

    row = samples[record : record + 1]
    seed_t = row["seed_t"].copy()

    # Sanity: victim is in a death motion state in this dataset record.
    assert int(seed_t["action_id"][0, victim]) == 0x0000  # MSL_ACT_DEAD_DOWN

    # Inject a stale, non-empty hitlist entry with a mismatched victim instance-id proxy.
    victim_iid = int(seed_t["instance_id"][0, victim]) & 0xFFFF
    stale_iid = (victim_iid + 1) & 0xFFFF
    seed_t["combat_hitlist_cd"][0, attacker, hit_group, victim] = 0xFFFF  # MSL_HITLIST_CD_INDEFINITE
    seed_t["combat_hitlist_victim_iid"][0, attacker, hit_group, victim] = stale_iid

    assert int(seed_t["combat_hitlist_cd"][0, attacker, hit_group, victim]) == 0xFFFF
    assert int(seed_t["combat_hitlist_victim_iid"][0, attacker, hit_group, victim]) != victim_iid

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        binding.reseed_seed(handle, seed_bytes)

        # Force a minimal attacker hitbox ↔ victim hurtcap overlap for combat resolution.
        binding.debug_clear_hitboxes_world(handle, 0, attacker)
        binding.debug_clear_hurtcaps_world(handle, 0, victim)

        # combat_resolve() applies a per-fighter translation shift of (prev_pos - pos) to already
        # computed world primitives. On reseed, prev_pos_* is 0, so choose coordinates that cancel
        # this shift and overlap at the origin in the combat pass.
        ax = float(seed_t["pos_x"][0, attacker])
        ay = float(seed_t["pos_y"][0, attacker])
        az = float(seed_t["pos_z"][0, attacker])
        vx = float(seed_t["pos_x"][0, victim])
        vy = float(seed_t["pos_y"][0, victim])
        vz = float(seed_t["pos_z"][0, victim])

        hitbox_flags = (1 << 9) | (1 << 10)  # MSL_HITBOX_FLAG_HIT_GROUNDED | MSL_HITBOX_FLAG_HIT_AERIAL
        binding.debug_set_hitbox_world(handle, 0, attacker, 0, ax, ay, az, 5.0, 10.0, 1)
        binding.debug_set_hitbox_flags(handle, 0, attacker, 0, hitbox_flags)
        binding.debug_set_hurtcap_world(handle, 0, victim, 0, vx, vy, vz, vx, vy, vz, 5.0)

        binding.debug_combat_resolve(handle)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_hitlag_victim = int(out["hitlag"][0, victim])
        got_hitlag_attacker = int(out["hitlag"][0, attacker])

        assert got_hitlag_victim > 0 or got_hitlag_attacker > 0, (
            f"record=3137 expected combat hit to be accepted (hitlist cleared on death/respawn); "
            f"got victim_hitlag={got_hitlag_victim} attacker_hitlag={got_hitlag_attacker}"
        )
    finally:
        binding.destroy(handle)
