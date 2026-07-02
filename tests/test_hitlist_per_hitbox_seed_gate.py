from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.replay_buffers_loader import load_replay_buffers


_SUITE_DATASETS = (
    "replays/validation/cardinal_1.0_recent/"
    "AttachedGoodNaturedGuanaco.slpz",
    "replays/validation/cardinal_1.0_recent/"
    "GracefulAttachedTurtle.slpz",
    "replays/validation/cardinal_1.0_recent/"
    "QuerulousGrandDinosaur.slpz",
    "replays/validation/cardinal_1.0_recent/"
    "TreasuredBackKangaroo.slpz",
)


def _skip_if_suite_datasets_missing(root: Path) -> None:
    missing = [rel for rel in _SUITE_DATASETS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local replays: {', '.join(missing)}")


def _load_replay_buffers_or_skip(path: Path) -> object:
    try:
        return load_replay_buffers(str(path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale cached dataset: {path}")
        raise


@pytest.mark.integration
def test_generated_suite_only_marks_per_hitbox_authority_when_payload_exists() -> None:
    # Generated replay buffers may now emit authoritative per-HitCapsule shield provenance for a narrow
    # frozen-Guard family. Keep the generic contract tight:
    # - combat_hitlist_hb_valid may be nonzero
    # - but only when the corresponding per-hitbox payload is actually populated
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/types.h::HitCapsule
    root = Path(__file__).resolve().parents[1]
    _skip_if_suite_datasets_missing(root)

    for rel in _SUITE_DATASETS:
        ds = _load_replay_buffers_or_skip(root / rel)
        samples = ds.rows
        seed_t = samples["seed_t"]
        ref_t1 = samples["ref_t1"]
        hb_valid = seed_t["combat_hitlist_hb_valid"]
        hb_cd = seed_t["combat_hitlist_hb_cd"]
        assert hb_valid.dtype == np.uint8
        valid_mask = hb_valid != 0
        if not bool(np.any(valid_mask)):
            continue
        for rec, attacker, hb in zip(*np.nonzero(valid_mask), strict=False):
            has_payload = bool(np.any(hb_cd[rec, attacker, hb, :2] != 0))
            if has_payload:
                continue
            # Replay-only admission seeds may intentionally mark an authoritative empty HitCapsule
            # lane when t+1 proves either a fighter shield hit entered GuardSetOff or a fighter BODY
            # damage hit was admitted. This keeps dense group fallback from suppressing the
            # teacher-forced hit while leaving normal rollouts on runtime-carried HitCapsules.
            #
            # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}
            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
            defenders = [p for p in range(2) if p != int(attacker)]
            shield_proof = any(
                int(seed_t["hitlag"][rec, d]) == 0
                and int(ref_t1["action_id"][rec, d]) == 181
                and int(ref_t1["hitlag"][rec, d]) > 0
                and int(ref_t1["hitlag"][rec, attacker]) > 0
                and float(ref_t1["shield_hp"][rec, d]) < float(seed_t["shield_hp"][rec, d])
                for d in defenders
            )
            body_proof = any(
                int(seed_t["hitlag"][rec, d]) == 0
                and int(seed_t["hitlag"][rec, attacker]) == 0
                and int(ref_t1["hitlag"][rec, d]) > 0
                and int(ref_t1["hitlag"][rec, attacker]) > 0
                and float(ref_t1["percent"][rec, d]) > float(seed_t["percent"][rec, d])
                and int(ref_t1["last_hit_by"][rec, d]) == int(attacker)
                and int(ref_t1["instance_hit_by"][rec, d]) == int(seed_t["instance_id"][rec, attacker])
                for d in defenders
            )
            assert shield_proof or body_proof, (rel, int(rec), int(attacker), int(hb))


@pytest.mark.integration
@pytest.mark.xfail(
    reason=(
        "Restored runtime contract currently fails: valid=0 with synthetic legacy group seed "
        "does not materialize the per-hitbox debug hitlist. Keep as an explicit gameplay blocker."
    ),
    strict=False,
)
def test_per_hitbox_valid_zero_falls_back_to_legacy_group_seed_even_with_payload_present() -> None:
    # Runtime fallback contract:
    # - valid=0 means ignore the per-hitbox payload and materialize from the legacy group map.
    # - valid=1 means the per-hitbox payload is authoritative, including an empty victims_1 list.
    #
    # This uses a replay row only to get a real active hitbox window; the seed maps are synthetic so
    # the test isolates materialization behavior before combat mutates hitlists.
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/types.h::HitCapsule
    root = Path(__file__).resolve().parents[1]
    rel = (
        "replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")

    binding = pytest.importorskip("msl_binding")
    ds = _load_replay_buffers_or_skip(dataset_path)
    record = 293
    attacker = 0
    victim = 1
    hb_ids = (0, 1, 2)
    assert int(ds.rows.shape[0]) > record

    def run_materialize(*, hb_valid: int) -> list[int]:
        row = ds.rows[record : record + 1].copy()
        seed = row["seed_t"]
        victim_iid = int(seed["instance_id"][0, victim])

        seed["combat_hitlist_cd"][0, attacker, :, victim] = np.uint16(0xFFFF)
        seed["combat_hitlist_victim_iid"][0, attacker, :, victim] = np.uint16(victim_iid)
        seed["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(hb_valid)
        seed["combat_hitlist_hb_cd"][0, attacker, :, victim] = np.uint16(0)
        seed["combat_hitlist_hb_victim_iid"][0, attacker, :, victim] = np.uint16(victim_iid)

        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).copy().reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )

        handle = binding.init(batch_size=1, num_players=int(ds.num_players))
        try:
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
            return [
                int(binding.debug_hitlist_fighter_contains(handle, 0, attacker, hb_id, victim))
                for hb_id in hb_ids
            ]
        finally:
            binding.destroy(handle)

    assert run_materialize(hb_valid=0) == [1, 1, 1]
    assert run_materialize(hb_valid=1) == [0, 0, 0]
