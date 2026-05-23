from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


DATASET_REL = "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"
RECORD_MULTI_VICTIM_LASER = 9048
P0_ATTACKER_HIT_BY_LASER = 0
P1_ALREADY_IN_DAMAGE_HIT_BY_LASER = 1
LASER_ITEM_SLOT = 1
LASER_INSTANCE_ID = 3219
SELFPLAY_182447_SLP = Path("replays/validation/aggregate_recent/Game_20260515T182447_frozenps.slpz")
EWT_SLP = Path("replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz")

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


def _rollout_rows(ds, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    samples = ds.samples
    assert 0 <= start_record <= target_record < int(samples.shape[0])
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(samples.shape[0], sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_bytes.view(COMPARE_DTYPE).reshape(-1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_view[0].copy()
    finally:
        binding.destroy(handle)

    return samples["ref_t1"][target_record].copy(), out


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


@pytest.mark.integration
def test_laser_fighter_hitcapsule_contact_registers_item_victim_before_later_body() -> None:
    # Runtime live item victims_1 registration owner:
    # - Game_20260515T182447 rec 1184..1190 has p1's laser cross p0's active AttackAirLw
    #   HitCapsule before a later BODY-sized overlap.
    # - In vanilla, ftColl_8007925C takes the fighter-HitCapsule/item-HitCapsule catch_path before
    #   ShieldDesc/BODY and ftColl_80077970 -> it_8026FAC4 registers p0 in the laser HitCapsule's
    #   victims_1 list. The later BODY pass must therefore stay suppressed while the laser persists.
    # - This is a free-running runtime hitlist owner, not a replay seed reconstruction shortcut.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_802706D0}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {SELFPLAY_182447_SLP}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref, out = _rollout_rows(ds, 1184, 1190)

    assert int(ref["action_id"][0]) == 69  # AttackAirLw, still vulnerable and undamaged.
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 0
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0]) == 0
    assert int(out["instance_hit_by"][0]) == int(ref["instance_hit_by"][0])
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1.0e-5)


@pytest.mark.integration
def test_falco_laser_flinching_body_not_suppressed_by_attackairlw_hitcapsule_contact() -> None:
    # Negative boundary for the zero-KB fighter-HitCapsule/item-HitCapsule victims_1 lane:
    # - EWT rec5477 has a Falco laser crossing Fox AttackAirLw HitCapsules and then taking the
    #   ordinary flinching BODY path on the same row.
    # - The retained victims_1 registration is restricted by generated MSLLASR1 zero_kb_damage_class data,
    #   so Fox zero-KB shots can suppress later BODY without causing Falco laser hits to disappear.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_802706D0}
    # data/items/lasers.bin::MSLLASR1 zero_kb_damage_class/state1_zero_kb_damage_class
    root = Path(__file__).resolve().parents[1]
    slp_path = root / EWT_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {EWT_SLP}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    row = ds.samples[5477:5478]
    out = _step_row(row, num_players=int(ds.header["num_players"]))
    ref = row["ref_t1"][0]

    assert int(row["seed_t"][0]["action_id"][0]) == 69  # AttackAirLw.
    assert int(row["seed_t"][0]["items"][0]["type"]) == 55  # Falco laser.
    assert int(ref["action_id"][0]) == ACT_DAMAGE_AIR_1
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0])
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0])
    assert int(out["instance_hit_by"][0]) == int(ref["instance_hit_by"][0])
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1.0e-5)
