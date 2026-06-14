from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _rollout_rows(dataset_path: Path, *, start: int, stop: int) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > stop

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        return np.array(samples_u8[record : record + 1, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out: dict[int, tuple[np.void, np.void]] = {}
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(handle, field_bytes(start, seed_off, seed_stride))
        for record in range(start, stop + 1):
            binding.step_input(
                handle,
                field_bytes(record, prev_input_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            if record in (start, stop):
                binding.write_compare(handle, out_bytes)
                out[record] = (
                    samples["ref_t1"][record].copy(),
                    out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(),
                )
    finally:
        binding.destroy(handle)
    return out


@pytest.mark.integration
def test_marth_attackairn_post_clear_second_band_keeps_empty_hitcapsules_wws() -> None:
    # Marth NAir has an opening multi-hit band, a clear_hitboxes command, then a later strong
    # create_hitbox band in the same hit_group. A rollout seed inside the second band can still
    # expose the legacy dense group latch from the first hit, but source ftAction_8007121C already
    # ran clear_hitboxes -> ftColl_800768A0 for the second band, so the concrete HitCapsule lists
    # are empty until a live contact inserts the victim.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_80008688}
    # data/scripts/marth.bin (MSLFTSC1 ftCo_SM_AttackAirN clear at frame 8, create at frame 15)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth/WellWornSmallGoshawk.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start = 12152
    hit_record = 12155
    attacker = 1
    defender = 0
    seed = samples[start]["seed_t"]
    assert int(seed["char_id"][attacker]) == 18  # Marth
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed["action_frame"][attacker]) == 17
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        seed["instance_id"][defender]
    )
    assert all(int(v) == 0 for v in seed["combat_hitlist_hb_valid"][attacker])

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        return np.array(
            samples_u8[record : record + 1, off : off + stride],
            dtype=np.uint8,
            order="C",
            copy=True,
        )

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, field_bytes(hit_record, seed_off, seed_stride))
        binding.debug_step_input_pre_combat(
            handle,
            field_bytes(hit_record, prev_input_off, input_stride),
            field_bytes(hit_record, input_off, input_stride),
        )
        for hb in range(4):
            assert binding.debug_hitlist_fighter_contains(handle, 0, attacker, hb, defender) == 0
    finally:
        binding.destroy(handle)

    rows = _rollout_rows(dataset_path, start=start, stop=hit_record)
    ref_hit, out_hit = rows[hit_record]
    assert int(ref_hit["action_id"][defender]) == 88  # DamageFlyN from NAir second band.
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
            assert int(out_hit[field][p]) == int(ref_hit[field][p]), f"p={p} field={field}"
    assert float(out_hit["percent"][defender]) == pytest.approx(float(ref_hit["percent"][defender]))


@pytest.mark.integration
def test_marth_attackairn_post_clear_second_band_carries_post_contact_victim_pfz() -> None:
    # Marth NAir's second band is a post-clear create band, so pre-contact rows own an empty
    # HitCapsule list. After the second-band BODY hit lands, lbColl_80008688 inserts the victim in
    # victims_1 across the same hit_group; lbColl_8000ACFC then suppresses repeat BODY contacts until
    # the script changes/clears the slot. The victim can leave DamageFly hitlag and enter DownBound
    # while the same NAir band is still active; Slippi-visible hitlag/hitstun are then zero, but
    # `last_hit_by` + `instance_hit_by` still prove the same source-owned victim object.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # data/scripts/marth.bin (MSLFTSC1 ftCo_SM_AttackAirN clear at frame 8, create at frame 15)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth/ParallelFamiliarZebra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    attacker = 1
    defender = 0
    for record in (6381, 6382, 6383):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    hitlag_tail = samples[6381]["seed_t"]
    assert int(hitlag_tail["char_id"][attacker]) == 18  # Marth
    assert int(hitlag_tail["action_id"][attacker]) == 65  # AttackAirN
    assert int(hitlag_tail["action_frame"][attacker]) == 18
    assert int(hitlag_tail["action_id"][defender]) == 88  # DamageFlyN hitlag tail.
    assert int(hitlag_tail["hitlag"][defender]) == 1
    assert int(hitlag_tail["instance_hit_by"][defender]) == int(hitlag_tail["instance_id"][attacker])

    downbound_tail = samples[6382]["seed_t"]
    assert int(downbound_tail["action_id"][attacker]) == 65  # AttackAirN still active.
    assert int(downbound_tail["action_id"][defender]) == 183  # ftCo_MS_DownBoundU.
    assert int(downbound_tail["seed_prev_action_id"][defender]) == 88  # DamageFlyN -> DownBoundU.
    assert int(downbound_tail["hitlag"][defender]) == 0
    assert int(downbound_tail["hitstun"][defender]) == 0
    assert int(downbound_tail["instance_hit_by"][defender]) == int(downbound_tail["instance_id"][attacker])

    for record in (6381, 6382, 6383):
        rows = _rollout_rows(dataset_path, start=record, stop=record)
        ref, out = rows[record]
        for p in (attacker, defender):
            for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
                assert int(out[field][p]) == int(ref[field][p]), f"record={record} p={p} field={field}"
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_marth_attackairn_clear_create_boundary_allows_second_band_rehit_ldg() -> None:
    # LDG has Marth NAir hit Fox with the first weak band, then the script emits clear_hitboxes at
    # frame 8 and a second create_hitbox band at frame 15. The later hb3 BODY overlap at frame 21 is
    # a legal second-band hit; same-source dense hitlist provenance from the first band must not be
    # rematerialized after the source clear/create boundary.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # data/scripts/marth.bin (MSLFTSC1 ftCo_SM_AttackAirN clear at frame 8, create at frame 15)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth/LoudDullGoat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start = 2629
    hit_record = 3346
    attacker = 1
    defender = 0
    seed = samples[hit_record]["seed_t"]
    assert int(seed["char_id"][attacker]) == 18  # Marth
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed["action_frame"][attacker]) == 20
    assert int(seed["action_id"][defender]) == 42  # Landing after the earlier first-band hit.
    assert int(seed["instance_hit_by"][defender]) == int(seed["instance_id"][attacker])

    rows = _rollout_rows(dataset_path, start=start, stop=hit_record)
    ref_hit, out_hit = rows[hit_record]
    assert int(ref_hit["action_id"][defender]) == 80  # DamageN3 from NAir second band.
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
            assert int(out_hit[field][p]) == int(ref_hit[field][p]), f"p={p} field={field}"
    assert float(out_hit["percent"][defender]) == pytest.approx(float(ref_hit["percent"][defender]))


@pytest.mark.integration
def test_marth_attackairf_same_action_reentry_clears_stale_dense_hitlist_mug() -> None:
    # Marth Fair can IASA into a fresh Fair while Slippi still exposes the same action id/submotion.
    # Source still calls ftCo_AttackAir_EnterFromMsid -> Fighter_ChangeMotionState with entry-call
    # flags that do not include Ft_MF_SkipHit, then ftAnim_8006EBA4 runs the new Fair script.
    # The fresh first create edge owns an empty x914 HitCapsule list; a stale dense seed from the
    # previous Fair hit must not be lazily materialized into victims_1 before the new hit checks.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_800768A0}
    # data/scripts/marth.bin (MSLFTSC1 ftCo_SM_AttackAirF first create_hitbox frame 4)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth/MetallicUniqueGrouse.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start = 696
    hit_record = 697
    attacker = 1
    defender = 0
    seed = samples[start]["seed_t"]
    assert int(seed["char_id"][attacker]) == 18  # Marth
    assert int(seed["action_id"][attacker]) == 66  # AttackAirF
    assert int(seed["action_frame"][attacker]) == 3
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        seed["instance_id"][defender]
    )
    assert all(int(v) == 0 for v in seed["combat_hitlist_hb_valid"][attacker])

    rows = _rollout_rows(dataset_path, start=start, stop=hit_record)
    ref_first, out_first = rows[start]
    assert int(ref_first["action_id"][defender]) == 88  # DamageFlyN continues.
    assert int(out_first["action_id"][defender]) == int(ref_first["action_id"][defender])
    assert int(out_first["hitlag"][defender]) == 0
    assert int(out_first["instance_id"][defender]) == int(ref_first["instance_id"][defender])

    ref_hit, out_hit = rows[hit_record]
    assert int(ref_hit["action_id"][defender]) == 89  # DamageFlyHi from the fresh Fair.
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_id", "instance_hit_by"):
            assert int(out_hit[field][p]) == int(ref_hit[field][p]), f"p={p} field={field}"
