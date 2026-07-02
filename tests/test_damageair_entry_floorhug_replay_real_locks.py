from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


HIS = "replays/validation/aggregate_recent/HungryImportantSnake.slpz"


def _rollout_rows(
    dataset_path: Path, start_record: int, targets: tuple[int, ...]
) -> tuple[object, dict[int, np.void]]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert targets
    assert start_record <= min(targets) <= max(targets) < int(samples.shape[0])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    out_by_record: dict[int, np.void] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        target_set = set(targets)
        for record in range(start_record, max(targets) + 1):
            prev_input_bytes[0, :] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            input_bytes[0, :] = np.frombuffer(
                samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in target_set:
                binding.write_compare(handle, out_bytes)
                out_by_record[record] = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    return ds, out_by_record


@pytest.mark.integration
def test_fresh_damageair_entry_and_frozen_hitlag_do_not_snap_root_to_floor_his_8101() -> None:
    # HIS rollout lock:
    # - p1 enters DamageAir3 from AttackAirN under BODY hitlag near FD floor.
    # - The entry frame publishes the pre-ProcessHit root after AttackAirN_Coll, not a later
    #   persisted floor-id snap.
    # - The following active-hitlag rows hold a downward stick, but damage entry has reset
    #   x670/x671 to 0xFE. Without a new ftCo_Damage_OnEveryHitlag SDI consume, the floorhug bridge
    #   must not re-project the frozen DamageAir root to floor bias.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procMap,Fighter_ProcessHit_8006D1EC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / HIS
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {HIS}")

    ds, out_by_record = _rollout_rows(dataset_path, 8080, (8101, 8102, 8103))
    p = 1

    seed_8101 = ds.rows[8101]["seed_t"]
    assert int(seed_8101["action_id"][p]) == 65  # AttackAirN
    assert int(seed_8101["hitlag"][p]) == 0
    assert float(seed_8101["pos_y"][p]) == pytest.approx(1.6300973892211914, abs=1e-6)

    ref_8101 = ds.rows[8101]["ref_t1"]
    out_8101 = out_by_record[8101]
    assert int(ref_8101["action_id"][p]) == 86  # DamageAir3
    assert int(out_8101["action_id"][p]) == 86
    assert int(out_8101["hitlag"][p]) == int(ref_8101["hitlag"][p]) == 8
    assert float(out_8101["pos_y"][p]) == pytest.approx(float(ref_8101["pos_y"][p]), abs=1e-6)
    assert float(out_8101["pos_y"][p]) == pytest.approx(-1.7699027061462402, abs=1e-6)

    seed_8102 = ds.rows[8102]["seed_t"]
    assert int(seed_8102["action_id"][p]) == 86
    assert int(seed_8102["tilt_timer_x"][p]) == 0xFE
    assert int(seed_8102["tilt_timer_y"][p]) == 0xFE
    assert int(ds.rows[8102]["input_t"]["p"]["main_y"][p]) < -100

    for record, expected_hitlag in ((8102, 7), (8103, 6)):
        ref = ds.rows[record]["ref_t1"]
        out = out_by_record[record]
        assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 86
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == expected_hitlag
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(-1.7699027061462402, abs=1e-6)

