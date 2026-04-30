from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def _run_rollout_window(dataset_path: Path, start: int, stop: int):
    import msl_binding

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
        return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        msl_binding.reseed_seed_rollout(handle, field_bytes(start, seed_off, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(int(start), int(stop) + 1):
            msl_binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        msl_binding.destroy(handle)

    return samples["ref_t1"][stop], out


def _run_rollout_window_with_seed_mutator(dataset_path: Path, start: int, stop: int, seed_mutator):
    import msl_binding

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_t = samples[start : start + 1]["seed_t"].copy()
    seed_mutator(seed_t)
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
        return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(int(start), int(stop) + 1):
            msl_binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        msl_binding.destroy(handle)

    return samples["ref_t1"][stop], out


@pytest.mark.integration
def test_guard_shielddesc_seed_accepts_replay_proven_guardsetoff_contact() -> None:
    # Replay-visible GuardSetOff + attacker/defender hitlag proves the hidden
    # `lbColl_80007BCC` ShieldDesc path accepted before BODY selection.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    attacker = 1
    seed, ref, out = _run_one_step_row(dataset_path, 11352, defender)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert all(
        int(seed["combat_shield_contact_hb_kind"][attacker][hb][defender]) == 2 for hb in range(4)
    )


@pytest.mark.integration
def test_guard_shielddesc_seed_rejects_replay_proven_body_damage_contact() -> None:
    # Replay-visible BODY damage + attacker/defender hitlag proves the hidden ShieldDesc path did
    # not accept: `ftColl_80078C70` tests shield first, and accepted shield contact suppresses BODY.
    # This prevents near-rim Guard rows from becoming false GuardSetOff hits under one-step reseed.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 0
    defender = 1
    seed, ref, out = _run_one_step_row(dataset_path, 3702, defender)

    assert int(seed["action_id"][defender]) == 179
    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
    assert all(
        int(seed["combat_shield_contact_hb_kind"][attacker][hb][defender]) == 1 for hb in range(4)
    )


@pytest.mark.integration
def test_guard_shielddesc_runtime_pose_accepts_prh_rollout_contact() -> None:
    # Runtime-positive boundary: no one-step ShieldDesc seed is available in this natural rollout
    # window. The decomp-shaped live Guard pose must still place Falco's ShieldDesc where
    # `lbColl_80007BCC` accepts the shield contact before BODY.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091E78
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    ref, out = _run_rollout_window(dataset_path, 11336, 11352)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_attackairf_dense_hitlist_rollout_suppresses_guardon_reentry_hvg() -> None:
    # AttackAirF -> GuardOn dense HitCapsule carry:
    # - HVG:7959 starts before p1 Fair reaches p0's GuardOn admission frame.
    # - The rollout seed carries only the coarse dense victims_1 map for p0; by the time p0 enters
    #   GuardOn at HVG:7970, the visible instance_id proxy has advanced but the decomp victim
    #   pointer is still the same fighter object.
    # - Runtime must materialize that hidden HitCapsule latch at the create edge so
    #   lbColl_8000ACFC suppresses the otherwise false shield hit.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 7959
    target = 7970
    attacker = 1
    defender = 0
    assert int(ds.samples["seed_t"][start]["action_id"][attacker]) == 24  # KneeBend
    assert int(ds.samples["seed_t"][start]["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(ds.samples["seed_t"][start]["combat_hitlist_victim_iid"][attacker, 0, defender]) != int(
        ds.samples["seed_t"][target]["instance_id"][defender]
    )

    ref, out = _run_rollout_window(dataset_path, start, target)
    assert int(ref["action_id"][defender]) == 178  # GuardOn, not GuardSetOff.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0
    assert int(out["instance_id"][defender]) == int(ref["instance_id"][defender])

    def clear_dense(seed_t: np.ndarray) -> None:
        seed_t["combat_hitlist_cd"][0, attacker, :, defender] = np.uint16(0)
        seed_t["combat_hitlist_victim_iid"][0, attacker, :, defender] = np.uint16(0)

    _ref, out_without_latch = _run_rollout_window_with_seed_mutator(
        dataset_path, start, target, clear_dense
    )
    assert int(out_without_latch["action_id"][defender]) == 181  # GuardSetOff.
    assert int(out_without_latch["hitlag"][defender]) > 0
    assert int(out_without_latch["hitlag"][attacker]) > 0


@pytest.mark.integration
def test_attackairn_stale_dense_hitlist_create_edge_clears_for_new_hit_agn() -> None:
    # AttackAirN stale dense HitCapsule boundary:
    # - AGN:5167 seeds p1 NAir with a coarse dense victims_1 latch against p0's old JumpF
    #   instance.
    # - p0 enters a new aerial motion before p1's next create edge. Decomp's ftColl_800768A0
    #   clear/copy edge owns the concrete HitCapsule state here, so the stale dense seed must fail
    #   closed and allow the real NAir hit at AGN:5169.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 5167
    target = 5168
    attacker = 1
    defender = 0
    assert int(ds.samples["seed_t"][start]["action_id"][attacker]) == 65  # AttackAirN
    assert int(ds.samples["seed_t"][start]["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(ds.samples["seed_t"][start]["combat_hitlist_victim_iid"][attacker, 0, defender]) != int(
        ds.samples["seed_t"][target]["instance_id"][defender]
    )

    ref, out = _run_rollout_window(dataset_path, start, target)
    assert int(ref["action_id"][defender]) == 87  # DamageFlyHi from the NAir hit.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 48
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_guardreflect_shielddesc_runtime_pose_rejects_high_dair_body_rollout_contact() -> None:
    # Runtime-negative ShieldDesc boundary for fighter-vs-fighter collision. PRH 1510 is a
    # GuardReflect no-submotion frame whose frame-start x14 is still active. `ftCo_8009388C` owns
    # ReflectDesc only during that phase; ShieldDesc is recreated after x14 expiry, so stale shield
    # geometry must not suppress the BODY pass that applies DamageFlyHi.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    ref, out = _run_rollout_window(dataset_path, 1483, 1510)

    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 61
    assert int(out["on_ground"][defender]) == int(ref["on_ground"][defender]) == 0


@pytest.mark.integration
def test_locomotion_guardreflect_entry_keeps_shielddesc_for_same_frame_dair_contact() -> None:
    # Runtime-positive ShieldDesc boundary for locomotion -> GuardReflect. Unlike
    # `ftCo_8009388C` guard-origin entry, `ftCo_80093A50` calls `ftCo_80092450` before creating
    # ReflectDesc, so the first no-submotion GuardReflect frame still exposes ShieldDesc to
    # fighter-vs-fighter collision and takes the x221C_b2 powershield damage gate.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    ref, out = _run_rollout_window(dataset_path, 2201, 2217)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 5
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert int(out["on_ground"][defender]) == int(ref["on_ground"][defender]) == 1
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))
    assert int(out["state_flags"][defender][3]) == int(ref["state_flags"][defender][3])


@pytest.mark.integration
def test_guard_body_phantom_x189c_timer_survives_guardsetoff_rollout() -> None:
    # PRH 6830..6834 is a shield-poke phantom/tip-log boundary followed by a real shield hit:
    # - rec6830: BODY contact starts victim-only phantom hitlag and x1898/x189C, but must not enter
    #   DamageFly or reduce shield HP as a full BODY hit.
    # - rec6831: a later shield contact enters GuardSetOff while the hidden x189C countdown remains
    #   live.
    # - rec6834: Fighter_ProcessHit applies x1898 percent when x189C expires, even though
    #   replay-visible shield hitlag is still active.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007BE3C}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    seed, ref, out = _run_one_step_row(dataset_path, 6830, defender)
    assert int(seed["action_id"][defender]) == 179  # Guard
    assert int(seed["hitlag"][defender]) == 0
    assert int(ref["action_id"][defender]) == 179
    assert int(out["action_id"][defender]) == 179
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 4
    assert int(out["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(seed["percent"][defender]))
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))

    ds = read_dataset(str(dataset_path))
    hidden_seed = ds.samples[6831]["seed_t"]
    assert float(hidden_seed["phantom_damage_pending_x1898"][defender]) == pytest.approx(4.5)
    assert int(hidden_seed["phantom_damage_timer_x189c"][defender]) == 4
    assert int(hidden_seed["phantom_damage_source_port"][defender]) == 1

    ref, out = _run_rollout_window(dataset_path, 6815, 6834)
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_attackairb_dense_hitlist_rollout_trim_allows_late_guard_contact_prh() -> None:
    # PRH 6822 starts before Falco BAir reaches the guarding defender. The replay seed carries a
    # legacy dense victims_1 entry for the same hit_group, but the accepted shield/body owner is not
    # proven until rec6830. Rollout must trim only that dense seed provenance so the late Guard
    # contact is admitted; authoritative per-HitCapsule victims_1 remains covered by the negative
    # below.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    ref, out = _run_rollout_window(dataset_path, 6822, 6830)
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 179
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 4
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))

    ref, out = _run_rollout_window(dataset_path, 6822, 6831)
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_attackairb_dense_hitlist_trim_preserves_authoritative_per_hitbox_seed() -> None:
    # Boundary negative for the rollout dense-seed trim: valid per-HitCapsule victims_1 seed lanes
    # are exact HitCapsule provenance, so they must continue suppressing the same PRH late BAir
    # contact even under reseed_seed_rollout().
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/types.h::HitCapsule
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[6830:6831].copy()
    attacker = 1
    defender = 0
    victim_iid = int(row["seed_t"]["instance_id"][0, defender])

    row["seed_t"]["combat_hitlist_cd"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(0)
    row["seed_t"]["combat_hitlist_hb_cd"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_hb_victim_iid"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_hb_valid"][0, attacker, 2] = np.uint8(1)
    row["seed_t"]["combat_hitlist_hb_cd"][0, attacker, 2, defender] = np.uint16(0xFFFF)
    row["seed_t"]["combat_hitlist_hb_victim_iid"][0, attacker, 2, defender] = np.uint16(victim_iid)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0]
    assert int(out["action_id"][defender]) == 179
    assert int(out["hitlag"][defender]) == 0
    assert int(out["instance_hit_by"][defender]) != int(row["seed_t"]["instance_id"][0, attacker])


@pytest.mark.integration
def test_guard_body_phantom_live_pose_gap_does_not_cover_nonzero_tilt_body() -> None:
    # Boundary for the retained no-tilt Guard phantom admission: PRH 6830 is near the x7A8 scalar
    # cutoff, but the extra admission is only for `ftCo_80091E78`'s current/no-tilt pose gap.
    # Mutating the same real row to the nonzero-x4 angled Guard branch must fall back to the exact
    # x7A8 predicate and take the ordinary BODY damage path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091E78
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[6830:6831].copy()
    defender = 0
    row["seed_t"]["guard_tilt_x4"][0, defender] = np.float32(1.0)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitstun"][defender]) > 0
    assert float(out["percent"][defender]) > float(row["seed_t"]["percent"][0, defender])


@pytest.mark.integration
def test_guardon_entry_shielddesc_current_pose_accepts_iat_attackairlw_rollout_contact() -> None:
    # Runtime-positive boundary for no-submotion GuardOn entry:
    # - ftCo_800924C0 creates ShieldDesc, then ftCo_800921DC zeroes the shield joint translate and
    #   calls ftCo_80091E78(..., 0).
    # - Slippi exposes this first visible GuardOn snapshot as animation_index=-1/action_frame=-1,
    #   but vanilla lbColl_80007BCC still uses the live GuardOn current-pose ShieldDesc bone.
    # This rollout has no teacher-forced combat_shield_contact_hb_kind seed at the break frame, so
    # the runtime GuardOn entry pose must produce GuardSetOff before the following BODY path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_800924C0,ftCo_800921DC,ftCo_80091E78}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    # data/shields/{fox,falco}.bin::guard_on_x20_xyz
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    ref, out = _run_rollout_window(dataset_path, 3831, 3847)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_guardon_entry_shielddesc_current_pose_rejects_steady_guardon_negative() -> None:
    # Negative boundary for the GuardOn current-pose owner:
    # PRH 11242 is also a no-submotion GuardOn snapshot, but its previous post-frame owner is
    # already GuardOn. That is not the ftCo_800924C0 entry snapshot, so it must not reuse
    # guard_on_x20_xyz as a broad shield-rim shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_800924C0,ftCo_800921DC,ftCo_80091E78}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    seed, ref, out = _run_one_step_row(dataset_path, 11242, defender)

    assert int(seed["action_id"][defender]) == 178
    assert int(seed["seed_prev_action_id"][defender]) == 178
    assert int(ref["action_id"][defender]) == 178
    assert int(out["action_id"][defender]) == 178
    assert int(out["hitlag"][defender]) == 0
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_guard_shielddesc_runtime_pose_keeps_iat_body_negative() -> None:
    # Runtime-negative boundary: the same live Guard pose correction must not become a broad
    # shield-rim suppressor. This window was the false GuardSetOff control for the rejected broad
    # ShieldDesc extent experiment.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    ref, out = _run_rollout_window(dataset_path, 3686, 3702)

    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])


@pytest.mark.integration
def test_guard_shielddesc_runtime_pose_keeps_dcc_body_rollout_negative() -> None:
    # Runtime-negative boundary for steady Guard ShieldDesc geometry:
    # DCC 9094 has a live BODY hit while the defender is in steady Guard with near-zero guard tilt
    # inertia. `ftCo_80091E78` blends nonzero x4 against the current no-tilt pose, and
    # `lbColl_80006E58` consumes the shield JObj matrix scale. Using the x8 neutral pose plus an
    # unscaled shield-radius proxy over-admits the rim and incorrectly enters GuardSetOff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091BC4,ftCo_80091E78}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    ref, out = _run_rollout_window(dataset_path, 9072, 9094)

    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 7
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 50
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_guardsetoff_post_hitlag_asdi_applies_floor_tangent_displacement() -> None:
    # Replay-real positive lock for GuardSetOff post-hitlag ASDI:
    # - ftCo_80092F2C installs post_hitlag_cb = ftCo_800932DC on GuardSetOff entry.
    # - Fighter_8006A1BC calls Fighter_8006D10C when hitlag reaches zero before current-frame
    #   input refresh, so the prior input snapshot owns the displacement.
    # - ftCo_800932DC applies the grounded floor-tangent X displacement using x4C0 * x4BC.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_800932DC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    seed, ref, out = _run_one_step_row(dataset_path, 4047, defender)

    assert int(seed["action_id"][defender]) == 181
    assert int(seed["hitlag"][defender]) == 1
    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert float(out["pos_x"][defender]) == pytest.approx(float(ref["pos_x"][defender]))
    assert float(out["pos_y"][defender]) == pytest.approx(float(ref["pos_y"][defender]))


@pytest.mark.integration
def test_guardsetoff_post_hitlag_asdi_no_stick_negative() -> None:
    # Negative boundary: last-hitlag GuardSetOff with no horizontal stick should still run the
    # normal shieldstun / friction path, but must not receive the ftCo_800932DC ASDI displacement.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    seed, ref, out = _run_one_step_row(dataset_path, 2235, defender)

    assert int(seed["action_id"][defender]) == 181
    assert int(seed["hitlag"][defender]) == 1
    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert float(out["pos_x"][defender]) == pytest.approx(float(ref["pos_x"][defender]))


@pytest.mark.integration
def test_guardsetoff_post_hitlag_asdi_keeps_agn_rollout_from_capture_cascade() -> None:
    # Runtime rollout lock for the AGN disruptive entry point: without the GuardSetOff
    # post-hitlag floor-tangent displacement, Falco's follow-up AttackHi4 misses and the rollout
    # falls into a later Catch/CaptureWait cascade. The remaining ~0.055 X residual in this window
    # is the earlier grounded Damage -> Wait velocity handoff, not this GuardSetOff callback.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 1
    victim = 0
    ref, out = _run_rollout_window(dataset_path, 4035, 4059)

    assert int(ref["action_id"][attacker]) == 63
    assert int(out["action_id"][attacker]) == 63
    assert int(ref["action_id"][victim]) == 90
    assert int(out["action_id"][victim]) == 90
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 9
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 48
    assert float(out["pos_x"][attacker]) == pytest.approx(float(ref["pos_x"][attacker]), abs=0.06)
