from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


def _rollout_window(*, ds, start_record: int, end_record_inclusive: int) -> dict[int, tuple[np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        rows: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, end_record_inclusive + 1):
            prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            rows[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return rows
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    name: str
    dataset_rel: str
    record: int
    p: int
    family: str


_CASES = [
    _Case(
        name="dcc_attackairb_stale_owner_adj_pre",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3151,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="dcc_attackairb_stale_owner_target",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3152,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="dcc_attackairb_stale_owner_adj_post",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3153,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="qgd_attackairb_control_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8637,
        p=1,
        family="qgd_control",
    ),
    _Case(
        name="qgd_attackairb_phantom_contact",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8638,
        p=1,
        family="qgd_phantom_contact",
    ),
    _Case(
        name="qgd_attackairb_phantom_sdi",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8639,
        p=1,
        family="qgd_phantom_sdi",
    ),
    _Case(
        name="qgd_attackairb_phantom_damage_expire",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8642,
        p=1,
        family="qgd_phantom_expire",
    ),
    _Case(
        name="tbk_attackairb_control_pre0",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=6993,
        p=1,
        family="tbk_control",
    ),
    _Case(
        name="tbk_attackairb_control_pre1",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=6994,
        p=1,
        family="tbk_control",
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: c.name)
def test_attackairb_damageflytop_stale_owner_subset_rows(case: _Case) -> None:
    # Replay-real lock for the kept AttackAirB continuation stale-owner subset.
    #
    # Decomp anchors:
    # - BODY rehit suppression is keyed by HitCapsule.victims_1 presence:
    #   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    # - BODY hit acceptance / attribution rewrite happens on confirmed continuation contact:
    #   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    #
    # Scope kept in runtime:
    # - late DamageFlyTop continuation rows from an older same-port attacker instance (DCC 3152)
    # - QGD 8638..8642 covers the AttackAirB phantom/tip-log branch, generic DamageFlyTop
    #   OnEveryHitlag SDI, and delayed x1898 phantom damage expiry.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, f"dataset too short for record={case.record}"
    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = int(case.p)

    if case.family == "dcc_damageflytop":
        assert int(seed["last_hit_by"][p]) == 1
        if case.record <= 3152:
            assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        else:
            assert int(seed["action_id"][p]) == 88  # DamageFlyN post-hit control
        if case.record == 3152:
            assert int(seed["instance_hit_by"][p]) != int(seed["instance_id"][1])
    elif case.family == "qgd_control":
        assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        assert int(seed["last_hit_by"][p]) == 0
    elif case.family == "qgd_phantom_contact":
        assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        assert int(seed["hitlag"][p]) == 0
        assert int(ref["hitlag"][p]) == 4
        assert float(ref["percent"][p]) == pytest.approx(float(seed["percent"][p]), abs=1e-6)
    elif case.family == "qgd_phantom_sdi":
        assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        assert int(seed["hitlag"][p]) == 4
        assert int(seed["tilt_timer_y"][p]) < 4
        assert float(ref["pos_x"][p]) > float(seed["pos_x"][p])
        assert float(ref["pos_y"][p]) > float(seed["pos_y"][p])
    elif case.family == "qgd_phantom_expire":
        assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        assert int(seed["hitlag"][p]) == 1
        assert float(seed["phantom_damage_pending_x1898"][p]) == pytest.approx(4.5, abs=1e-6)
        assert int(seed["phantom_damage_timer_x189c"][p]) == 1
        assert int(seed["phantom_damage_source_port"][p]) == 0
        assert float(ref["percent"][p] - seed["percent"][p]) == pytest.approx(4.5, abs=1e-6)
    elif case.family == "tbk_control":
        assert int(seed["action_id"][p]) == 91  # DamageFlyN
        assert int(seed["last_hit_by"][p]) == 0
    else:
        raise AssertionError(f"unexpected family: {case.family}")

    out = _one_step_out_compare(ds=ds, row=row)[0]

    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        got = int(out[field][p])
        exp = int(ref[field][p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6), case.name
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5), case.name
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5), case.name
    if case.family == "qgd_phantom_expire":
        assert int(out["last_attack_landed"][0]) == int(ref["last_attack_landed"][0]) == 15


@pytest.mark.integration
def test_phantom_pending_damage_with_invalid_source_is_cleared_at_reseed() -> None:
    # The hidden phantom/tip-log damage carry is source-owned. Dataset contract uses
    # source=0xFF together with amount=0/timer=0 for "no pending phantom"; a positive amount with
    # invalid source must not become a source-less percent-only damage path.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[8642:8643].copy()
    p = 1
    seed = row["seed_t"][0]
    assert float(seed["phantom_damage_pending_x1898"][p]) == pytest.approx(4.5, abs=1e-6)
    assert int(seed["phantom_damage_timer_x189c"][p]) == 1
    assert int(seed["phantom_damage_source_port"][p]) == 0

    row["seed_t"]["phantom_damage_source_port"][0, p] = np.uint8(0xFF)
    out = _one_step_out_compare(ds=ds, row=row)[0]

    assert float(out["percent"][p]) == pytest.approx(float(seed["percent"][p]), abs=1e-6)
    assert int(out["last_attack_landed"][0]) != 15


@pytest.mark.integration
def test_attackairb_damageflytop_stale_owner_rollout_reaches_dcc_followup_hit() -> None:
    # DCC rollout-real lock for the late AttackAirB -> DamageFlyTop followup:
    # - rec3149 starts before the replay-only authoritative per-HitCapsule seed appears.
    # - By rec3152, vanilla admits the inner late BAir full BODY hit while the outer hb1 dense
    #   victim latch remains a valid negative control in QGD.
    # - The rollout must also advance the Slippi frame-start RNG clock so ftCo_8008DCE0's
    #   DamageFlyRoll gate uses the same frame seed as the replay target row.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/slippi-ssbm-asm/Recording/SendGamePreFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    ds = read_dataset(str(dataset_path))
    rows = _rollout_window(ds=ds, start_record=3149, end_record_inclusive=3155)

    for record in range(3149, 3153):
        out, ref = rows[record]
        assert int(out["frame_id"]) == int(ref["frame_id"])
        assert int(out["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
        for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
            assert int(out[field][0]) == int(ref[field][0]), f"record={record} field={field}"

    for record in range(3152, 3155):
        out, ref = rows[record]
        assert int(out["action_id"][0]) == 88  # DamageFlyN, not stale DamageFlyTop or RNG DamageFlyRoll
        assert int(out["action_id"][0]) == int(ref["action_id"][0])
        assert int(out["hitlag"][0]) == int(ref["hitlag"][0])
        assert int(out["hitstun"][0]) == int(ref["hitstun"][0])
        assert int(out["instance_hit_by"][0]) == int(ref["instance_hit_by"][0]) == 707
        assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1e-6)


@pytest.mark.integration
def test_attackairb_damageflytop_rng_carry_maps_last_hit_by_raw_source_port() -> None:
    # Slippi `last_hit_by` is raw source-port domain, while runtime arrays are compact local slots.
    # This synthetic port-remap keeps the same local fighters but moves the AttackAirB attacker from
    # raw source port 1 to raw source port 3. The delayed Fighter_8006CDA4 carry must still find
    # local slot 1 through `source_port0`; old code indexed action_id[3] and failed to advance the
    # replay frame-start RNG clock.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 3149
    rows = samples[start_record:3153].copy()
    rows["seed_t"][0]["source_port0"][1] = np.uint8(3)
    rows["seed_t"][0]["last_hit_by"][0] = np.uint8(3)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    rows_u8 = rows.view(np.uint8).reshape(int(rows.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = rows_u8[0, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for i, record in enumerate(range(start_record, 3153)):
            prev_input_bytes[0, :] = rows_u8[i, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = rows_u8[i, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            assert int(out["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    finally:
        binding.destroy(handle)
