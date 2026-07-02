from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.eval.streaming_validation import _load_binding


_HIS = Path("replays/validation/aggregate_recent/HungryImportantSnake.slpz")
_HHG = Path("replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz")

ACT_LANDING = 42
ACT_ATTACK_DASH = 50
ACT_ATTACK_LW4 = 64
ACT_ATTACK_AIR_F = 66
ACT_ATTACK_AIR_B = 67
ACT_ATTACK_S3_HI = 56
ACT_DAMAGE_N_2 = 79
ACT_DAMAGE_FLY_N = 88
ACT_DAMAGE_FLY_LW = 89
ACT_DAMAGE_FLY_TOP = 90
ACT_GUARD_ON = 178
ACT_REBOUND_STOP = 237


def _byte_views(ds):
    views = replay_buffer_byte_views(ds)
    return views.seed_t, views.prev_input_t, views.input_t


def _rollout_one(
    dataset_path: Path, record: int, *, clear_same_source_provenance: bool = False
) -> tuple[np.void, np.void, np.void]:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = samples["seed_t"][record].copy()
    if clear_same_source_provenance:
        seed["instance_hit_by"][0] = np.uint16(0)
        seed["last_hit_by"][0] = np.uint8(6)
    seed_bytes = seed.reshape((1,)).view(np.uint8).reshape((1, seed_stride)).copy()
    prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
    input_bytes = input_u8[record : record + 1, :input_stride].copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return seed, out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), samples["ref_t1"][record]


def _rollout_window(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), samples["ref_t1"][stop]


@pytest.mark.integration
def test_attackdash_dense_hitlist_survives_victim_action_instance_change_rollout() -> None:
    # HIS 5024 starts after p1 AttackDash already hit p0, with p0 transitioning Landing -> AttackLw4
    # on this step. Slippi's instance_id changes on that motion-state entry, but decomp HitCapsule
    # victim lists key the stable fighter object pointer, so the same AttackDash hitbox must stay
    # suppressed in replay rollout.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    # refs/melee/src/melee/lb/types.h::HitCapsule
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HIS
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _rollout_one(dataset_path, 5024)
    assert int(seed["action_id"][0]) == ACT_LANDING
    assert int(seed["action_id"][1]) == ACT_ATTACK_DASH
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][1, 0, 0]) == int(seed["instance_id"][0])
    assert int(ref["action_id"][0]) == ACT_ATTACK_LW4

    assert int(out["action_id"][0]) == ACT_ATTACK_LW4
    assert int(out["instance_id"][0]) == int(ref["instance_id"][0])
    assert int(out["hitlag"][0]) == int(out["hitlag"][1]) == 0
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1e-6)


@pytest.mark.integration
def test_attackdash_dense_hitlist_rebind_requires_same_source_provenance() -> None:
    # Negative control for the rollout-only rebind: if same-source BODY provenance is absent, a stale
    # dense Slippi instance-id proxy is not enough to suppress current BODY contact.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HIS
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, _ref = _rollout_one(dataset_path, 5024, clear_same_source_provenance=True)
    assert int(out["action_id"][0]) == ACT_DAMAGE_N_2
    assert int(out["hitlag"][0]) > 0
    assert int(out["hitlag"][1]) > 0


@pytest.mark.integration
def test_exact_rollout_reseed_preserves_dense_hitlist_before_guardon_hhg_7970() -> None:
    # Exact-row replay rollout reseeds are still teacher-forced seed materialization: the dense
    # victims_1 latch from the seed row must suppress the stale AttackAirF BODY contact before p0's
    # current-frame shield input enters GuardOn. The stricter same-source replay-rollout filter
    # applies only after the rollout has advanced beyond the reseed frame.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HHG
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _rollout_one(dataset_path, 7970)
    assert int(seed["action_id"][0]) == ACT_LANDING
    assert int(seed["action_id"][1]) == ACT_ATTACK_AIR_F
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][1, 0, 0]) == int(seed["instance_id"][0])

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_GUARD_ON
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 0
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 0


@pytest.mark.integration
def test_damage_episode_dense_hitlist_requires_current_source_instance_lim_5673() -> None:
    # LIM 5673 is inside p0's active DamageFlyTop episode. The dense group hitlist seed still names
    # p0's current Slippi instance_id, but `instance_hit_by` names a different attacker action
    # instance than the live p1 AttackAirB. That stale dense proxy must not suppress the fresh BODY
    # hit that vanilla resolves into DamageFlyN hitlag.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / Path(
        "replays/validation/yoshis_story_recent/"
        "LawfulInsistentMeerkat.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _rollout_one(dataset_path, 5673)
    assert int(seed["action_id"][0]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["action_id"][1]) == ACT_ATTACK_AIR_B
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][1, 0, 0]) == int(seed["instance_id"][0])
    assert int(seed["instance_hit_by"][0]) != int(seed["instance_id"][1])

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_DAMAGE_FLY_N
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 6
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 6


@pytest.mark.integration
def test_attackdash_hitlist_suppresses_stale_clank_and_downsmash_selects_low_hit_his_5029() -> None:
    # HIS 5029 protects the decomp clank/body ordering for two already-contacting grounded
    # hitbox owners:
    # - ftColl_80078C70's hitbox-vs-hitbox candidate setup first rejects HitCapsules whose
    #   victims_1 list already contains the opponent (`lbColl_8000ACFC(...) != 0`), so p1's stale
    #   AttackDash hitbox does not clank with p0's fresh DownSmash.
    # - A Dolphin ftColl primitive probe of this row selects p0 AttackLw4 hb3 (damage 12) against
    #   Fox low hurtcap bone 12 with coll_distance=0.1833438873; AttackDash is not an SSDYNN01 v4
    #   dynamic-collision owner because the static collision chain is the probed source owner here.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HIS
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _rollout_one(dataset_path, 5029)
    assert int(seed["action_id"][0]) == ACT_ATTACK_LW4
    assert int(seed["action_id"][1]) == ACT_ATTACK_DASH

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_ATTACK_LW4
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == ACT_DAMAGE_FLY_LW
    assert int(out["hitlag"][0]) == int(out["hitlag"][1]) == 7
    assert int(ref["hitlag"][0]) == int(ref["hitlag"][1]) == 7
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1]) == 35
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
    assert float(out["percent"][1] - seed["percent"][1]) == pytest.approx(12.0, abs=1e-5)


@pytest.mark.integration
def test_attackdash_dense_hitlist_rollout_suppresses_stale_clank_his_4991_window() -> None:
    # Rollout-real boundary for the same AttackDash victim-pointer owner:
    # starting before p0 Landing -> AttackLw4 advances Slippi instance_id, p1 AttackDash's dense
    # victims_1 seed must still suppress p1's stale clank candidate at HIS 5029. Otherwise both
    # fighters enter ReboundStop instead of p0's DownSmash launching p1 into DamageFlyLw.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HIS
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    out, ref = _rollout_window(dataset_path, 4991, 5029)

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_ATTACK_LW4
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == ACT_DAMAGE_FLY_LW
    assert int(out["hitlag"][0]) == int(out["hitlag"][1]) == 7
    assert int(ref["hitlag"][0]) == int(ref["hitlag"][1]) == 7
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1]) == 35
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)


@pytest.mark.integration
def test_attackdash_dense_seed_fallback_does_not_suppress_fresh_clank_hhg_8674() -> None:
    # HHG 8674 is a reciprocal dense-group seed row where both fighters still clank into
    # ReboundStop. The dense seed map is not concrete HitCapsule provenance for clank candidate
    # setup; otherwise p0 AttackDash is incorrectly suppressed and p1 AttackS3Hi BODY-hits instead.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HHG
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _rollout_one(dataset_path, 8674)
    assert int(seed["action_id"][0]) == ACT_ATTACK_DASH
    assert int(seed["action_id"][1]) == ACT_ATTACK_S3_HI
    assert int(seed["combat_hitlist_cd"][0, 0, 1]) == 0xFFFF
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_REBOUND_STOP
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == ACT_REBOUND_STOP
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 4
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 6
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1e-6)
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
