from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_DCC = Path("datasets/aggregate_recent/replays/debug/fd_mixed_recent/DistinctCaringCobra.msl")
_HVG = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl")
_TCH = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl")
_QGD = Path("datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl")
_PTE = Path(
    "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
)
_GAT_DOUBLES = Path("datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl")


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


def _run_rollout_to_record(binding: object, samples: np.ndarray, start: int, target: int, *, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = (
        np.frombuffer(samples[start:start + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = (
                np.frombuffer(samples[record:record + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(samples[record:record + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_grounded_shine_start_terminal_damagefly_uses_dense_victim_seed() -> None:
    # DCC rec=6431 has Falco entering grounded SpecialLwStart while Fox is in terminal
    # DamageFlyTop hitstun. Replay-history extraction carries a dense group-0 HitCapsule victim
    # entry for the live Fox object plus an x198C=1/x1994 proof, so the immediate Shine Start BODY
    # overlap is a stale lbColl_8000ACFC repeat and must not rehit.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _DCC
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[6431:6432]
    attacker = 1
    defender = 0

    assert int(row["seed_t"]["action_id"][0, attacker]) == 39
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 360
    assert int(row["seed_t"]["action_id"][0, defender]) == 90
    assert int(row["seed_t"]["hitstun"][0, defender]) == 2
    assert int(row["seed_t"]["combat_hitlist_cd"][0, attacker, 0, defender]) == 0xFFFF
    assert int(row["seed_t"]["colanim_hit_status_x198c"][0, defender]) == 1
    assert int(row["seed_t"]["colanim_timer_x1994"][0, defender]) != 0

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert float(out["percent"][defender]) == pytest.approx(float(row["ref_t1"]["percent"][0, defender]))
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])


@pytest.mark.integration
def test_grounded_shine_start_dense_seed_requires_explicit_victim_proof() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _DCC
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[6431:6432].copy()
    attacker = 1
    defender = 0
    row["seed_t"]["combat_hitlist_cd"][0, attacker, 0, defender] = 0
    row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, 0, defender] = 0

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    assert int(out["hitlag"][attacker]) > 0
    assert int(out["hitlag"][defender]) > 0
    assert float(out["percent"][defender]) > float(row["seed_t"]["percent"][0, defender])


@pytest.mark.integration
def test_late_slot_grounded_shine_start_entry_does_not_hit_earlier_turn_defender() -> None:
    # DCC rec=3864 has p1 entering grounded SpecialLwStart while p0 is an earlier-slot Turn
    # defender that has already flipped its internal Turn facing. Vanilla does not apply the frame-0
    # Shine BODY hit here: ftColl_80078C70 walks fighter pairs in entity order, so the late
    # entry-created HitCapsule can miss the earlier fighter's collision-pair phase during this
    # internal-facing microphase.
    #
    # HVG:5200/TCH:3376 below prove this is not a broad later-slot Shine-vs-Turn suppressor.
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim_Inner
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _DCC
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[3864:3865]
    attacker = 1
    defender = 0

    assert int(row["seed_t"]["action_id"][0, attacker]) == 39  # Squat
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 360  # grounded SpecialLwStart
    assert int(row["seed_t"]["action_id"][0, defender]) == 18  # Turn
    assert int(row["seed_t"]["turn_has_turned"][0, defender]) == 1
    assert int(row["ref_t1"]["action_id"][0, defender]) == 18
    assert float(row["ref_t1"]["percent"][0, defender]) == pytest.approx(
        float(row["seed_t"]["percent"][0, defender])
    )

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert float(out["percent"][defender]) == pytest.approx(float(row["ref_t1"]["percent"][0, defender]))
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "seed_action"),
    (
        (_HVG, 5200, 39),
        (_TCH, 3376, 40),
    ),
)
def test_late_slot_grounded_shine_start_still_hits_pre_turn_internal_facing_controls(
    dataset_rel: Path, record: int, seed_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / dataset_rel
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[record:record + 1]
    attacker = 1
    defender = 0

    assert int(row["seed_t"]["action_id"][0, attacker]) == seed_action
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 360  # grounded SpecialLwStart
    assert int(row["seed_t"]["action_id"][0, defender]) == 18  # Turn
    assert int(row["seed_t"]["turn_has_turned"][0, defender]) == 0
    assert int(row["ref_t1"]["action_id"][0, defender]) == 90  # DamageFlyTop
    assert float(row["ref_t1"]["percent"][0, defender]) > float(row["seed_t"]["percent"][0, defender])

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert float(out["percent"][defender]) == pytest.approx(float(row["ref_t1"]["percent"][0, defender]))
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])


@pytest.mark.integration
def test_aerial_shine_start_terminal_damagefly_still_hits_qgd_235() -> None:
    # QGD rec=235 is the nearby control: Fox enters aerial SpecialLwStart and vanilla does connect
    # the BODY hit on a DamageFlyTop victim with the same dense/x198C seed shape. The dense bridge is
    # therefore grounded-SpecialLwStart only; aerial entry remains on the normal BODY path.
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _QGD
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[235:236]
    attacker = 0
    defender = 1

    assert int(row["ref_t1"]["action_id"][0, attacker]) == 365
    assert int(row["seed_t"]["action_id"][0, defender]) == 90
    assert int(row["seed_t"]["combat_hitlist_cd"][0, attacker, 0, defender]) == 0xFFFF

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])


@pytest.mark.integration
def test_aerial_shine_start_late_slot_grounded_attack_entry_uses_dense_victim_seed() -> None:
    # PTE rec=5419 has p1 entering aerial SpecialLwStart on the same frame p0 enters grounded
    # AttackHi3. The later-slot Shine hitbox does not damage the earlier-slot grounded attack entry
    # during this collision-pair phase; QGD:235 remains the aerial-DamageFly control where the same
    # aerial Shine entry does hit.
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PTE
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[5419:5420]
    attacker = 1
    defender = 0

    assert int(row["seed_t"]["action_id"][0, attacker]) == 24  # KneeBend
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 365  # aerial SpecialLwStart
    assert int(row["seed_t"]["action_id"][0, defender]) == 42  # Landing
    assert int(row["ref_t1"]["action_id"][0, defender]) == 56  # AttackHi3
    assert int(row["seed_t"]["combat_hitlist_cd"][0, attacker, 0, defender]) == 0xFFFF
    assert int(row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, 0, defender]) != int(
        row["seed_t"]["instance_id"][0, defender]
    )

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert float(out["percent"][defender]) == pytest.approx(float(row["ref_t1"]["percent"][0, defender]))
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])


@pytest.mark.integration
def test_aerial_shine_start_late_slot_active_damagefly_uses_pair_phase_owner() -> None:
    # GAT-doubles rec=3154 has p1 platform-pass into aerial SpecialLwStart while p0 is an
    # earlier-slot active DamageFlyN victim. The ref keeps p0 in the prior damage episode and p1
    # out of hitlag for this frame. This is the same ftColl entity-order owner as the grounded
    # attack-entry lane above, but the victim family is DAMAGE_*_COLL from MSLMSO01 rather than a
    # grounded attack entry.
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLw_Enter}
    # data/motion_state/owners/{fox,falco}.bin (MSLMSO01 DAMAGE_*_COLL classes)
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _GAT_DOUBLES
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    row = ds.samples[3154:3155]
    attacker = 1
    defender = 0

    assert int(row["seed_t"]["action_id"][0, attacker]) == 39  # Squat
    assert int(row["ref_t1"]["action_id"][0, attacker]) == 365  # aerial SpecialLwStart
    assert int(row["ref_t1"]["animation_index"][0, attacker]) == 317  # platform-pass start submotion
    assert int(row["seed_t"]["action_id"][0, defender]) == 88  # DamageFlyN
    assert int(row["seed_t"]["hitstun"][0, defender]) != 0
    assert int(row["ref_t1"]["instance_hit_by"][0, defender]) == int(
        row["seed_t"]["instance_hit_by"][0, defender]
    )

    out = _run_one_step(binding, row, num_players=int(ds.header["num_players"]))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
        assert int(out[field][defender]) == int(row["ref_t1"][field][0, defender]), field
    assert int(out["hitlag"][attacker]) == int(row["ref_t1"]["hitlag"][0, attacker])
    assert float(out["percent"][defender]) == pytest.approx(float(row["ref_t1"]["percent"][0, defender]))


@pytest.mark.integration
def test_aerial_shine_start_late_slot_grounded_attack_entry_rollout_uses_pair_phase_owner() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PTE
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    start = 5412
    target = 5419
    attacker = 1
    defender = 0

    assert int(ds.samples[start]["seed_t"]["action_id"][defender]) == 67  # AttackAirB
    assert int(ds.samples[target]["ref_t1"]["action_id"][attacker]) == 365
    assert int(ds.samples[target]["ref_t1"]["action_id"][defender]) == 56

    out = _run_rollout_to_record(
        binding, ds.samples, start, target, num_players=int(ds.header["num_players"])
    )
    for field in ("action_id", "action_frame", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ds.samples[target]["ref_t1"][field][defender]), field
    assert float(out["percent"][defender]) == pytest.approx(
        float(ds.samples[target]["ref_t1"]["percent"][defender])
    )
    assert int(out["hitlag"][attacker]) == int(ds.samples[target]["ref_t1"]["hitlag"][attacker])
