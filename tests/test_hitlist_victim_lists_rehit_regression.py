from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_hitlist_victim_list_is_updated_on_real_hit_record_1036() -> None:
    # Integration lock for decomp-shaped victim list bookkeeping (HitCapsule victims_1):
    # - When a hit is applied, the attacker should register the victim into victims_1 via
    #   lbColl_80008688, shared across same hit_group hitboxes (inlineB0 / ftColl_80076808).
    #
    # Decomp anchors:
    # - Gate: refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    # - Insert: refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    # - Share across same group: refs/melee/src/melee/ft/ftcoll.c::inlineB0 and ::ftColl_80076808
    #
    # Record provenance:
    # - Picked from the suite dataset by scanning for a record where the replay reference
    #   (`ref_t1.hitlag`) indicates a real hit AND the current simulator also produces that hit.
    # - This avoids locking in a sim-only hit while still exercising the HitCapsule victim list
    #   update path.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {expected_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 1036
    assert int(samples.shape[0]) > record, "replay too short for regression check"
    row = samples[record : record + 1]

    ref_t1 = row["ref_t1"].reshape(-1)
    victim = 0
    attacker = 1
    assert int(ref_t1["hitlag"][0, victim]) > 0, "expected replay reference to record a real hit (hitlag > 0)"
    assert int(ref_t1["last_hit_by"][0, victim]) == attacker, "expected replay reference attacker identity"

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        assert int(out["hitlag"][0, victim]) > 0, "expected the simulator to apply the same replay-real hit"
        assert int(out["last_hit_by"][0, victim]) == attacker, "expected attacker identity to match replay reference"

        # Victim should be registered into attacker hitbox victim list(s) (victims_1).
        present_any = False
        for hb_id in range(4):
            if int(binding.debug_hitlist_fighter_contains(handle, 0, attacker, hb_id, victim)) != 0:
                present_any = True
                break
        assert present_any, "expected victim port to be present in attacker hitbox victims_1 after hit"
    finally:
        binding.destroy(handle)
