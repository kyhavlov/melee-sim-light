from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_PJO = Path("replays/validation/aggregate_recent/PutridJoyousOryx.slpz")
_HIS = Path("replays/validation/aggregate_recent/HungryImportantSnake.slpz")


def _bytes(row: np.ndarray, *, seed_stride: int, input_stride: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_bytes = (
        np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )
    return seed_bytes, prev_input_bytes, input_bytes


def _run_one_step(binding: object, row: np.ndarray, *, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes, prev_input_bytes, input_bytes = _bytes(
        row, seed_stride=seed_stride, input_stride=input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_rollout_until(
    binding: object,
    samples: np.ndarray,
    *,
    start: int,
    stop: int,
    num_players: int,
) -> dict[int, np.void]:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    start_row = samples[start : start + 1]
    seed_bytes = (
        np.frombuffer(start_row["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        for record in range(start, stop + 1):
            row = samples[record : record + 1]
            prev_input_bytes = (
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out[record] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)
    return out


def _precombat_hitlist(binding: object, row: np.ndarray, *, num_players: int) -> list[int]:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes, prev_input_bytes, input_bytes = _bytes(
        row, seed_stride=seed_stride, input_stride=input_stride
    )

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        return [int(binding.debug_hitlist_fighter_contains(handle, 0, 1, hb_id, 0)) for hb_id in range(4)]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_downattacku_create_edge_honors_seeded_hitlist_snapshot() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PJO
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    record = 4092
    row = ds.rows[record : record + 1].copy()
    seed = row["seed_t"][0]
    ref = ds.rows["ref_t1"][record]

    # Replay-real positive:
    # p1 DownAttackU reaches a create/enable edge for hitboxes 0..2. Regenerated replays now carry
    # an explicit empty hitlist seed for this row; the no-false-hit result is owned by the live
    # collision pose/source path, not by a stale dense victims_1 fallback.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
    # refs/melee/src/melee/lb/types.h::HitCapsule
    assert int(seed["action_id"][1]) == 187  # ftCo_SM_DownAttackU
    assert [int(x) for x in seed["combat_hitlist_cd"][1, :, 0].tolist()] == [0] * 8
    assert [int(x) for x in seed["combat_hitlist_victim_iid"][1, :, 0].tolist()] == [0] * 8
    assert [int(x) for x in seed["combat_hitlist_hb_valid"][1].tolist()] == [0] * 4

    # The empty seed is intentionally not materialized into victims_1 at pre-combat time.
    assert _precombat_hitlist(binding, row, num_players=int(ds.num_players)) == [0, 0, 0, 0]

    out = _run_one_step(binding, row, num_players=int(ds.num_players))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "percent"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"


@pytest.mark.integration
def test_downattacku_dense_seed_rebinds_live_victim_instance_proxy() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PJO
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    record = 4092
    row = ds.rows[record : record + 1].copy()
    seed = row["seed_t"][0]
    ref = ds.rows["ref_t1"][record]

    # Source-backed identity boundary:
    # This regenerated row no longer seeds a stale dense instance-id proxy. It still locks that an
    # explicit empty seed does not suppress the live DownAttackU source behavior.
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    assert [int(x) for x in seed["combat_hitlist_cd"][1, :, 0].tolist()] == [0] * 8
    assert [int(x) for x in seed["combat_hitlist_victim_iid"][1, :, 0].tolist()] == [0] * 8

    out = _run_one_step(binding, row, num_players=int(ds.num_players))
    for field in ("action_id", "hitlag", "hitstun", "percent"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"


@pytest.mark.integration
def test_downattacku_rollout_preserves_dense_seed_through_live_instance_rebind() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PJO
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    start = 4084
    target = 4092

    # Replay-real rollout positive:
    # starting from the earlier LandingFallSpecial/DownAttackU seed now carries an explicit empty
    # hitlist seed, then reaches the hb0..2 frame-17 create edge at record 4092. The rollout lock
    # protects the live collision-pose/source owner without relying on legacy dense suppression.
    seed_start = ds.rows["seed_t"][start]
    seed_target = ds.rows["seed_t"][target]
    assert [int(x) for x in seed_start["combat_hitlist_cd"][1, :, 0].tolist()] == [0] * 8
    assert [int(x) for x in seed_start["combat_hitlist_victim_iid"][1, :, 0].tolist()] == [0] * 8
    assert int(seed_target["instance_id"][0]) == 928

    out = _run_rollout_until(
        binding, ds.rows, start=start, stop=target, num_players=int(ds.num_players)
    )[target]
    ref = ds.rows["ref_t1"][target]
    for field in ("action_id", "hitlag", "hitstun", "percent"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"


@pytest.mark.integration
def test_downattacku_landingfallspecial_dynamic_pose_prevents_false_body_without_dense_seed() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PJO
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    record = 4092
    row = ds.rows[record : record + 1].copy()
    seed = row["seed_t"]
    ref = ds.rows["ref_t1"][record]

    # Dynamic-pose owner lock:
    # PJO:4092 originally looked like a dense-hitlist carry problem, but a vanilla
    # pre-ftColl engine dump showed Fox LandingFallSpecial hurtcap 12 lower/back from the static
    # SSANIM pose. Clearing the explicit dense hitlist seed must still avoid the false DownAttackU
    # BODY hit because SSDYNN01 marks Fox LandingFallSpecial as consuming the x2C dynamic chain.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # reports/triage/item19_pjo4092_downattack_p1_engine_dump/
    seed["combat_hitlist_cd"][0, 1, :, 0] = np.uint16(0)

    assert _precombat_hitlist(binding, row, num_players=int(ds.num_players)) == [0, 0, 0, 0]

    out = _run_one_step(binding, row, num_players=int(ds.num_players))
    for field in ("action_id", "action_frame", "hitlag", "hitstun", "percent"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"


@pytest.mark.integration
def test_downattacku_landingfallspecial_dynamic_pose_rollout_pjo_4057() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _PJO
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    start = 4057
    target = 4092

    # Replay-real rollout positive:
    # starting at LandingAirHi frame 16 previously reached PJO:4092 with a static
    # LandingFallSpecial tail pose and false-hit p0 into DamageFlyN. The fix is data-owned by the
    # SSDYNN01 collision-msid index rather than a row-local DownAttackU suppression.
    seed_start = ds.rows["seed_t"][start]
    assert int(seed_start["action_id"][0]) == 73  # ftCo_SM_LandingAirHi
    assert int(ds.rows["seed_t"][target]["action_id"][0]) == 43  # ftCo_SM_LandingFallSpecial
    assert int(ds.rows["seed_t"][target]["action_id"][1]) == 187  # ftCo_SM_DownAttackU

    out = _run_rollout_until(
        binding, ds.rows, start=start, stop=target, num_players=int(ds.num_players)
    )[target]
    ref = ds.rows["ref_t1"][target]
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"


@pytest.mark.integration
def test_downattacku_landingfallspecial_entry_does_not_trust_dense_seed() -> None:
    root = Path(__file__).resolve().parents[1]
    ds_path = root / _HIS
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {ds_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(ds_path))
    record = 7485
    row = ds.rows[record : record + 1].copy()
    seed = row["seed_t"][0]
    ref = ds.rows["ref_t1"][record]

    # Replay-real negative/control for the empty fallback boundary:
    # p1 DownAttackU has no authoritative hitlist seed and p0 has just entered LandingFallSpecial
    # this frame. Vanilla admits the BODY hit, so same-frame victim action-entry rows cannot invent
    # a dense group seed as proof of the per-HitCapsule victims_1 list.
    assert int(seed["action_id"][1]) == 187  # ftCo_SM_DownAttackU
    assert int(seed["action_id"][0]) == 43  # ftCo_SM_LandingFallSpecial
    assert int(seed["action_frame"][0]) == 0
    assert [int(x) for x in seed["combat_hitlist_cd"][1, :, 0].tolist()] == [0] * 8
    assert [int(x) for x in seed["combat_hitlist_victim_iid"][1, :, 0].tolist()] == [0] * 8

    out = _run_one_step(binding, row, num_players=int(ds.num_players))
    # This row still has a separate DamageFlyN vs DamageFlyRoll residual, but the BODY admission
    # fields must match; otherwise the dense seed bridge has over-suppressed the hit.
    assert int(out["action_id"][0]) != 43
    for field in ("action_frame", "hitlag", "hitstun", "percent", "instance_hit_by", "last_hit_by"):
        assert out[field][0] == ref[field][0], f"field={field}"
        assert out[field][1] == ref[field][1], f"field={field}"
