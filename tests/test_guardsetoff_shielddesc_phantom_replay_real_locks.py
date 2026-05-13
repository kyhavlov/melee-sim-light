from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _DEBUG_SHIELD_CANDIDATE_DTYPE,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_GAT_DOUBLES = Path("datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl")


def _pack(arr: np.ndarray, stride: int) -> np.ndarray:
    return np.frombuffer(arr.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _run_rollout(binding: object, samples: np.ndarray, start: int, target: int, *, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, _pack(samples[start : start + 1]["seed_t"], seed_stride))
        for record in range(start, target + 1):
            binding.step_input(
                handle,
                _pack(samples[record : record + 1]["prev_input_t"], input_stride),
                _pack(samples[record : record + 1]["input_t"], input_stride),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_guardsetoff_guarddamage_shielddesc_accepts_attackairn_hb1_gat_2747() -> None:
    # GAT doubles rec 2747 has p1 already in GuardSetOff/GuardDamage after an earlier shield hit.
    # Vanilla accepts p0 AttackAirN hitbox 1 against the live ShieldDesc, while hb0/hb2 still miss.
    # The owner is the GuardDamage shield-bone matrix on `ft_data->x8->x11`, not a broader shield
    # radius or all-hitbox admission.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80092450,ftCo_80091D58,ftCo_GuardSetOff_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _GAT_DOUBLES
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    row = samples[2747:2748].copy()
    assert int(row["seed_t"]["action_id"][0, 1]) == 181
    assert int(row["seed_t"]["animation_index"][0, 1]) == 40
    assert int(row["seed_t"]["combat_shield_contact_hb_kind"][0, 0, 1, 1]) == 2
    row["seed_t"]["combat_shield_contact_hb_kind"][0, 0, :, 1] = 0
    row["seed_t"]["combat_shield_hit_int_damage"][0, 1] = 0
    row["seed_t"]["combat_shield_damage_taken"][0, 1] = 0

    sizes = binding.sizes()
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, _pack(row["seed_t"], int(sizes["seed"])))
        binding.debug_step_input_pre_combat(
            handle,
            _pack(row["prev_input_t"], int(sizes["input"])),
            _pack(row["input_t"], int(sizes["input"])),
        )
        raw, count = binding.debug_shield_candidate_decisions(handle, 0, 128)
    finally:
        binding.destroy(handle)

    cand = raw.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count]
    rows = [r for r in cand if int(r["attacker"]) == 0 and int(r["defender"]) == 1]
    by_hb = {int(r["hitbox_id"]): r for r in rows}
    assert int(by_hb[1]["reject_reason"]) == 0
    assert int(by_hb[1]["overlap_shield"]) == 1
    assert float(by_hb[1]["shield_overlap_margin"]) > 0.0
    assert int(by_hb[0]["reject_reason"]) == 9
    assert int(by_hb[2]["reject_reason"]) == 9


@pytest.mark.integration
def test_guardsetoff_shielddesc_and_downed_phantom_rollout_matches_gat_2739_to_2754() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _GAT_DOUBLES
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    out = _run_rollout(binding, samples, 2739, 2754, num_players=int(ds.header["num_players"]))
    ref = samples[2754]["ref_t1"]

    for p in range(int(ds.header["num_players"])):
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "last_hit_by", "instance_hit_by"):
            assert int(out[field][p]) == int(ref[field][p]), (field, p)
        assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)
        assert float(out["shield_hp"][p]) == pytest.approx(float(ref["shield_hp"][p]), abs=1e-5)


@pytest.mark.integration
def test_downed_phantom_damage_uses_stale_scaled_hitcapsule_damage_gat_2749() -> None:
    # The p3 -> p2 downed phantom contact stores half of stale-scaled HitCapsule.damage in x1898.
    # Clearing the queued stale move from the attacker seed must therefore produce the raw-damage
    # delayed percent, proving the retained runtime path is not a row-sized fixed percent patch.
    #
    # refs/melee/src/melee/ft/ft_0881.c::ft_80089228
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _GAT_DOUBLES
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    mutated = samples[2739:2740]["seed_t"].copy()
    assert int(mutated["attack_id"][0, 3]) == 15
    assert 15 in {int(v) for v in mutated["stale_move_id"][0, 3]}
    mutated["stale_move_id"][0, 3, 3] = np.uint16(99)

    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, _pack(mutated, int(sizes["seed"])))
        for record in range(2739, 2750):
            binding.step_input(
                handle,
                _pack(samples[record : record + 1]["prev_input_t"], input_stride),
                _pack(samples[record : record + 1]["input_t"], input_stride),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[2749]["ref_t1"]
    assert float(ref["percent"][2]) == pytest.approx(81.68500518798828, abs=1e-5)
    assert float(out["percent"][2]) > float(ref["percent"][2]) + 0.4
