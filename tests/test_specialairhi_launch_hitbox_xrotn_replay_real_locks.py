from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _dataset_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )


def _aggregate_fsp_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"


def _aggregate_his_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"


def _aggregate_pec_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"


def _run_row(binding, row: np.ndarray) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _run_rollout_to_record(binding, samples: np.ndarray, start_record: int, target_record: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_specialairhi_launch_hitbox_xrotn_qgd_target_window_stays_replay_exact() -> None:
    # Replay-real lock for the recent-suite SpecialAirHi launch-contact family.
    #
    # Decomp:
    # - SpecialAirHi launch rotates FtPart_XRotN by `2*pi - rotateModel`.
    # - rotateModel is written from launch velocity / collision response.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
    #
    # Runtime scope:
    # - Only launch-hit hitboxes attached under FtPart_XRotN need this local-chain rotation.
    # - Victim hurtcaps already match replay-real rows without an extra correction.
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    # target-1 / target / target+1 around the first real recent-suite front door
    for record in (9388, 9389, 9390):
        row = samples[record : record + 1]
        got = _run_row(binding, row)
        np.testing.assert_array_equal(
            got["action_id"], row["ref_t1"]["action_id"][0], err_msg=f"record={record} action_id"
        )
        np.testing.assert_array_equal(
            got["hitlag"], row["ref_t1"]["hitlag"][0], err_msg=f"record={record} hitlag"
        )
        np.testing.assert_allclose(
            got["percent"], row["ref_t1"]["percent"][0], atol=1e-6, err_msg=f"record={record} percent"
        )
        np.testing.assert_allclose(
            got["pos_x"], row["ref_t1"]["pos_x"][0], atol=1e-6, err_msg=f"record={record} pos_x"
        )
        np.testing.assert_allclose(
            got["pos_y"], row["ref_t1"]["pos_y"][0], atol=1e-6, err_msg=f"record={record} pos_y"
        )


@pytest.mark.integration
def test_specialairhi_launch_hitbox_xrotn_qgd_target_precombat_hitbox_matches_probe() -> None:
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9389:9390]
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        hitboxes, count = binding.hitboxes_world_full(handle, 0, 0)

        assert int(count) == 1
        # Vanilla probe from reports/triage/qgd9389_live_probe.json frame 9267:
        # raw `pos` is stored as [z, y, x] in that legacy probe format. Radius
        # uses the extracted ftAction scale literal from GALE01, not exact 1/256.
        np.testing.assert_allclose(
            hitboxes[0][:4],
            np.asarray([76.5973663, 24.2701416, -1.0097059, 3.9997439], dtype=np.float32),
            atol=1e-5,
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_specialairhi_dense_hitlist_stale_gap_admits_qgd_launch_body() -> None:
    # SpecialAirHi dense-hitlist/live-HitCapsule boundary:
    # - Earlier stale dense group fallback may still be present in the compatibility seed lane.
    # - SpecialHi launch materialization must not turn that coarse lane into a current HitCapsule
    #   victim ring when its stored victim instance differs from the live victim instance and no
    #   same-source accepted-hit proof exists.
    # - Native admits the real launch BODY contact at 9389; stale dense group state must not
    #   suppress the live launch capsule.
    # Decomp owner: HitCapsule victim lists, copied/cleared by ftColl_800768A0 and consulted by
    # lbColl_8000ACFC; SpecialHi charge/launch gaps must not preserve stale dense lineage as if it
    # were a source-owned active capsule.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    defender = 1
    attacker = 0
    start_record = 9382
    target_record = 9389

    assert int(samples["seed_t"]["combat_hitlist_cd"][start_record, attacker, 0, defender]) == 0xFFFF
    assert int(samples["seed_t"]["combat_hitlist_cd"][target_record, attacker, 0, defender]) == 0
    assert int(samples["seed_t"]["combat_hitlist_hb_valid"][target_record, attacker, 0]) == 1
    assert int(samples["seed_t"]["combat_hitlist_hb_cd"][target_record, attacker, 0, defender]) == 0

    got = _run_rollout_to_record(binding, samples, start_record, target_record)
    ref = samples["ref_t1"][target_record]

    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop in native.
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "hitlag", "hitstun"):
        np.testing.assert_array_equal(got[field], ref[field], err_msg=f"field={field}")
    for field in ("percent", "speed_x_attack", "speed_y_attack"):
        np.testing.assert_allclose(got[field], ref[field], atol=1e-6, err_msg=f"field={field}")


@pytest.mark.integration
def test_specialairhi_dense_hitlist_keeps_same_victim_fsp_suppression() -> None:
    # Negative for the QGD stale-instance bridge. FavorableSuperficialPig has the same SpecialHi
    # action family and a dense group entry, but that entry names the current victim instance.
    # Rollout materialization must keep the HitCapsule victim ring and must not fabricate a re-hit.
    #
    # Decomp owner: lbColl_8000ACFC checks current HitCapsule victims_1 provenance. The
    # compatibility dense seed may be skipped only when its stored victim identity is stale, not
    # merely because this is a replay rollout or SpecialHi action.
    dataset_path = _aggregate_fsp_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    attacker = 0
    defender = 1
    record = 1516

    assert int(samples["seed_t"]["action_id"][record, attacker]) == 356
    assert int(samples["seed_t"]["combat_hitlist_cd"][record, attacker, 0, defender]) == 0xFFFF
    assert int(samples["seed_t"]["combat_hitlist_victim_iid"][record, attacker, 0, defender]) == int(
        samples["seed_t"]["instance_id"][record, defender]
    )

    got = _run_rollout_to_record(binding, samples, record, record)
    ref = samples["ref_t1"][record]

    assert int(got["action_id"][defender]) == int(ref["action_id"][defender]) == 90
    assert int(got["action_frame"][defender]) == int(ref["action_frame"][defender]) == 12
    assert int(got["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert int(got["hitstun"][defender]) == int(ref["hitstun"][defender]) == 29
    np.testing.assert_allclose(got["percent"][defender], ref["percent"][defender], atol=1e-6)


@pytest.mark.integration
def test_specialairhi_dense_hitlist_same_victim_without_source_admits_qgd_body() -> None:
    # Same-victim SpecialHi dense fallback:
    # - QGD carries a dense SpecialHi group seed naming the current victim instance, but the victim
    #   is not in an accepted-hit episode from this attacker: hitstun/hitlag are clear and
    #   instance_hit_by points elsewhere.
    # - A coarse replay-derived group lane without current damage-source provenance cannot stand in
    #   for that HitVictim pointer, so the live BODY contact remains admissible.
    # Decomp owner: HitCapsule.victims_1 is written by accepted contact through ftColl_80076808 /
    # lbColl_80008688 and tested by lbColl_8000ACFC.
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    attacker = 1
    defender = 0
    start_record = 5856
    target_record = 5868

    assert int(samples["seed_t"]["action_id"][start_record, attacker]) == 356
    assert int(samples["seed_t"]["combat_hitlist_cd"][start_record, attacker, 0, defender]) == 0xFFFF
    assert int(samples["seed_t"]["combat_hitlist_victim_iid"][start_record, attacker, 0, defender]) == int(
        samples["seed_t"]["instance_id"][start_record, defender]
    )
    assert int(samples["seed_t"]["instance_hit_by"][start_record, defender]) != int(
        samples["seed_t"]["instance_id"][start_record, attacker]
    )
    assert int(samples["seed_t"]["hitlag"][start_record, defender]) == 0
    assert int(samples["seed_t"]["hitstun"][start_record, defender]) == 0

    got = _run_rollout_to_record(binding, samples, start_record, target_record)
    ref = samples["ref_t1"][target_record]

    assert int(ref["action_id"][defender]) == 90
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "hitlag", "hitstun"):
        np.testing.assert_array_equal(got[field], ref[field], err_msg=f"field={field}")
    for field in ("percent", "speed_x_attack", "speed_y_attack"):
        np.testing.assert_allclose(got[field], ref[field], atol=1e-6, err_msg=f"field={field}")


@pytest.mark.integration
def test_specialairhi_dense_hitlist_same_victim_without_source_admits_his_body() -> None:
    # HIS mirrors the same source boundary on an exact one-step seed:
    # - p1 SpecialAirHi has a stale same-victim dense group seed.
    # - p0 is in AttackLw3 with no accepted-hit episode from p1, so the dense fallback cannot
    #   suppress the live launch BODY contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    dataset_path = _aggregate_his_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    record = 8513
    attacker = 1
    defender = 0

    assert int(samples["seed_t"]["action_id"][record, attacker]) == 356
    assert int(samples["seed_t"]["action_id"][record, defender]) == 57
    assert int(samples["seed_t"]["combat_hitlist_cd"][record, attacker, 0, defender]) == 0xFFFF
    assert int(samples["seed_t"]["combat_hitlist_victim_iid"][record, attacker, 0, defender]) == int(
        samples["seed_t"]["instance_id"][record, defender]
    )
    assert int(samples["seed_t"]["instance_hit_by"][record, defender]) != int(
        samples["seed_t"]["instance_id"][record, attacker]
    )
    assert int(samples["seed_t"]["hitlag"][record, defender]) == 0
    assert int(samples["seed_t"]["hitstun"][record, defender]) == 0

    got = _run_row(binding, samples[record : record + 1])
    ref = samples["ref_t1"][record]

    assert int(ref["action_id"][defender]) == 90
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "hitlag", "hitstun"):
        np.testing.assert_array_equal(got[field], ref[field], err_msg=f"field={field}")
    for field in ("percent", "speed_x_attack", "speed_y_attack"):
        np.testing.assert_allclose(got[field], ref[field], atol=1e-6, err_msg=f"field={field}")


@pytest.mark.integration
def test_specialairhi_dense_hitlist_same_source_downbound_suppresses_pec_rehit() -> None:
    # Negative control for the SpecialHi dense filter:
    # - p0 has already been hit by p1 SpecialAirHi and has landed in DownBoundU with hitlag/hitstun
    #   clear, but `instance_hit_by`/`last_hit_by` still prove same-source accepted-hit ownership.
    # - The dense group seed is therefore a real HitVictim suppression latch, not an inactive-gap
    #   stale entry, and must block the repeated SpecialHi BODY contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    dataset_path = _aggregate_pec_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    record = 6905
    attacker = 1
    defender = 0

    assert int(samples["seed_t"]["action_id"][record, attacker]) == 356
    assert int(samples["seed_t"]["action_id"][record, defender]) == 183
    assert int(samples["seed_t"]["combat_hitlist_cd"][record, attacker, 0, defender]) != 0
    assert int(samples["seed_t"]["instance_hit_by"][record, defender]) == int(
        samples["seed_t"]["instance_id"][record, attacker]
    )
    assert int(samples["seed_t"]["last_hit_by"][record, defender]) == int(
        samples["seed_t"]["source_port0"][record, attacker]
    )
    assert int(samples["seed_t"]["hitlag"][record, defender]) == 0
    assert int(samples["seed_t"]["hitstun"][record, defender]) == 0

    got = _run_row(binding, samples[record : record + 1])
    ref = samples["ref_t1"][record]

    assert int(ref["action_id"][defender]) == 183
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "hitlag", "hitstun"):
        np.testing.assert_array_equal(got[field], ref[field], err_msg=f"field={field}")
    for field in ("percent", "speed_x_attack", "speed_y_attack"):
        np.testing.assert_allclose(got[field], ref[field], atol=1e-6, err_msg=f"field={field}")


@pytest.mark.integration
def test_specialairhi_dense_hitlist_stale_gap_does_not_clear_real_qgd_body_seed() -> None:
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    attacker = 0
    defender = 1

    # The stale inactive-gap bridge is ignored at runtime, but the real accepted hit still
    # populates the compatibility dense seed lane on the following frame.
    assert int(samples["seed_t"]["combat_hitlist_cd"][9390, attacker, 0, defender]) == 0xFFFF
    assert int(samples["seed_t"]["combat_hitlist_hb_valid"][9390, attacker, 0]) == 0
    assert int(samples["seed_t"]["combat_hitlist_hb_cd"][9390, attacker, 0, defender]) == 0
