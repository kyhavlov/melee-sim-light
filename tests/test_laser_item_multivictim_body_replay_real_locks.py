from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


DATASET_REL = "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"
RECORD_MULTI_VICTIM_LASER = 9048
P0_ATTACKER_HIT_BY_LASER = 0
P1_ALREADY_IN_DAMAGE_HIT_BY_LASER = 1
LASER_ITEM_SLOT = 1
LASER_INSTANCE_ID = 3219

ACT_DAMAGE_LW_1 = 81
ACT_DAMAGE_AIR_1 = 84
ACT_DAMAGE_FLY_HI = 87


def _step_row(row: np.ndarray, *, num_players: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_falco_laser_body_damage_dealt_callback_hits_multiple_victims_before_destroy() -> None:
    # Item BODY source owner:
    # - it_802706D0/it_8026FAC4 records laser BODY hits during the item collision pass.
    # - Item_8026A294 consumes the laser's dmg_dealt callback after collision, so the first BODY
    #   hit does not destroy the projectile before later fighters in the same pass are tested.
    # - it_80272460 computes the staled HitCapsule.damage before those per-victim contacts; the
    #   first victim's stale-queue insert must not stale the same laser for the second victim.
    # refs/melee/src/melee/it/item.c::{Item_80269C5C,Item_8026A294,OnGiveDamageThink}
    # refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_8026FAC4,it_80272460}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / DATASET_REL
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {DATASET_REL}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[RECORD_MULTI_VICTIM_LASER : RECORD_MULTI_VICTIM_LASER + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["items"][LASER_ITEM_SLOT]["instance_id"]) == LASER_INSTANCE_ID
    assert int(seed["hitlag"][P0_ATTACKER_HIT_BY_LASER]) == 6
    assert int(seed["hitlag"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == 6

    out = _step_row(row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][P0_ATTACKER_HIT_BY_LASER]) == ACT_DAMAGE_LW_1
    assert int(out["hitlag"][P0_ATTACKER_HIT_BY_LASER]) == 3
    assert int(out["last_hit_by"][P0_ATTACKER_HIT_BY_LASER]) == 3
    assert int(out["instance_hit_by"][P0_ATTACKER_HIT_BY_LASER]) == LASER_INSTANCE_ID
    assert float(out["percent"][P0_ATTACKER_HIT_BY_LASER]) == pytest.approx(
        float(ref["percent"][P0_ATTACKER_HIT_BY_LASER]), abs=1.0e-5
    )

    assert int(out["action_id"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == ACT_DAMAGE_AIR_1
    assert int(out["hitlag"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == 3
    assert int(out["last_hit_by"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == 3
    assert int(out["instance_hit_by"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == LASER_INSTANCE_ID
    assert float(out["percent"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == pytest.approx(
        float(ref["percent"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]), abs=1.0e-5
    )


@pytest.mark.integration
def test_multivictim_laser_body_still_respects_item_hitlist_victim_ring() -> None:
    # Negative boundary for the deferred destroy owner: keeping the item alive through the collision
    # pass must not bypass the per-HitCapsule victims_1 ring from lbColl_80008688.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / DATASET_REL
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {DATASET_REL}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[RECORD_MULTI_VICTIM_LASER : RECORD_MULTI_VICTIM_LASER + 1].copy()
    seed = row["seed_t"][0]
    seed["item_hitlist_victim_port"][LASER_ITEM_SLOT] = np.uint8(P1_ALREADY_IN_DAMAGE_HIT_BY_LASER)
    seed["item_hitlist_victim_cd"][LASER_ITEM_SLOT] = np.uint8(16)
    seed["item_hitlist_victim_hitbox_mask"][LASER_ITEM_SLOT] = np.uint8(0x0F)
    seed["item_hitlist_victim_iid"][LASER_ITEM_SLOT] = np.uint16(
        seed["instance_id"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]
    )

    out = _step_row(row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][P0_ATTACKER_HIT_BY_LASER]) == ACT_DAMAGE_LW_1
    assert int(out["instance_hit_by"][P0_ATTACKER_HIT_BY_LASER]) == LASER_INSTANCE_ID
    assert int(out["action_id"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) == ACT_DAMAGE_FLY_HI
    assert int(out["instance_hit_by"][P1_ALREADY_IN_DAMAGE_HIT_BY_LASER]) != LASER_INSTANCE_ID
