from __future__ import annotations

import numpy as np
import pytest
from pathlib import Path

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tests.test_combat_ownership_seed_guardrail_locks import _DEBUG_SHIELD_CANDIDATE_DTYPE
from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


def _step_one_row_with_seed(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _step_one_row_with_seed_one_step(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _dataset_byte_views(ds):
    samples = ds.samples
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    return (
        samples_u8,
        int(samples.dtype.fields["seed_t"][1]),
        int(samples.dtype.fields["prev_input_t"][1]),
        int(samples.dtype.fields["input_t"][1]),
    )


def _step_one_row_with_inputs(
    dataset_path: Path, record: int, prev_input: np.ndarray, cur_input: np.ndarray
) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(cur_input.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _step_one_sample(ds, record: int) -> tuple[np.void, np.void, np.void]:
    samples = ds.samples
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return seed, ref, out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _run_slp_rollout_records(
    slp_path: Path, *, start: int, records: tuple[int, ...], ports: list[int]
) -> dict[int, tuple[np.void, np.void]]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    assert int(samples.shape[0]) > max(records)
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    out_by_record: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, max(records) + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out_by_record[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
    finally:
        binding.destroy(handle)
    return out_by_record


@pytest.mark.integration
def test_attackairlw_sustained_tiplog_contact_defers_grounded_dash_damage_selfplay_181413() -> None:
    # Self-play 181413 rec316 is a source-general fighter BODY tip-log boundary:
    # - p0 Fox AttackAirLw has a sustained HitCapsule whose exact lbColl_80006E58 matrix overlap
    #   against grounded p1 Dash is below common x7A8.
    # - ftColl_80076ED8 therefore takes checkTipLog/inlineB1, starting victim hitlag and attribution
    #   without percent/KB/damage-state entry.
    # - rec318 is the adjacent negative: the later deeper contact is a full BODY damage hit.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,checkTipLog,inlineB1}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    attacker = 0
    defender = 1
    seed, ref, out = _step_one_sample(ds, 316)
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 18  # Turn.
    assert int(ref["action_id"][defender]) == 20  # Dash.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0

    seed, ref, out = _step_one_sample(ds, 318)
    assert int(seed["action_id"][defender]) == 20  # Dash.
    assert int(ref["action_id"][defender]) == 76  # DamageHi2.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 3
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 17
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_attackairlw_primary_hitbox_tiny_overlap_stays_full_body_damage_prh() -> None:
    # Negative boundary for the retained AttackAirLw BODY tip-log lane:
    # PRH rec5032 is Fox AttackAirLw primary hitbox slot 0 against grounded Wait. The current
    # matrix-radius overlap is tiny, but vanilla takes full BODY damage on hb0; only non-primary
    # limb slots consume the retained matrix-overlap tip-log reconstruction until the exact
    # lbColl_80006E58/victims_2 scalar owner is ported for every slot.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,checkTipLog,inlineB1}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[5032]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 5032, seed)

    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 14  # Wait.
    assert int(ref["action_id"][defender]) == 80  # DamageN3.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 27
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_attackairlw_commonfall_matrix_only_positive_rejects_false_body_ewt_1019() -> None:
    # ElatedWearyTermite rec1019 is a FoD rollout false hit: p0 Fox AttackAirLw hb1 sweeps near
    # p1 Falco Fall. The ordinary world capsule misses, and Dolphin lbColl probes show vanilla
    # lbColl_8000805C/lbColl_80006E58 also rejects the live Fall JObj pose. MSL's generated
    # state-age Fall pose is not source-complete enough to create a matrix-only BODY positive; it
    # may still reject contacts when the world geometry already overlaps.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # reports/triage/grape_item05_ewt1019_dolphin_collision_probe/
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1019]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 1019, seed)

    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(ref["action_id"][defender]) == 29  # Fall; no BODY hit.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_commonfall_entry_matrix_positive_still_admits_body_dcc_5574() -> None:
    # Boundary control for the CommonFall matrix-only BODY veto: Fall entry is still eligible for
    # source matrix positives because Fighter_ChangeMotionState has just initialized the JObj chain.
    # DistinctCaringCobra rec5574 is a direct frame-1 Fall BODY admission from p0 AttackAirF.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[5574]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 5574, seed)

    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 50  # AttackAirF.
    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(seed["action_frame"][defender]) == 1
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 4
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 4
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 38
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_grounded_attack_restart_clears_sustained_hitcapsule_latch_selfplay_181413() -> None:
    # Same-action grounded Attack restart HitCapsule clear:
    # - p0 Fox re-enters AttackHi3 without a Slippi action_id change; action_frame rewinds from
    #   22 to 1 while the old same-source victim attribution remains visible on p1 DamageFlyN.
    # - Source still ran Fighter_ChangeMotionState -> ftColl_8007AFF8 and the fresh
    #   ftAction_8007121C create edge owns lbColl_80008440 clear/copy, so BODY attribution alone
    #   must not re-materialize the old victims_1 latch for the new AttackHi3.
    # - rec1348 is the adjacent negative: the fresh hitboxes exist but vanilla has not taken the
    #   new BODY hit yet. rec1349 is the positive: the same fresh hitboxes enter DamageFlyTop.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
    # MSLMSO01: MSL_MS_CLASS_GROUNDED_ATTACK.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 0
    stop = 1349
    attacker = 0
    defender = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            if record == 1348:
                out = out_view[0].copy()
                ref = samples["ref_t1"][record]
                assert int(samples["seed_t"][record]["action_id"][attacker]) == 56  # AttackHi3
                assert int(ref["action_id"][defender]) == 88  # DamageFlyN
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
                assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
                assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
                assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(samples["seed_t"][stop]["action_id"][attacker]) == 56  # AttackHi3
        assert int(samples["seed_t"][stop]["action_frame"][attacker]) == 5
        assert int(ref["action_id"][defender]) == 90  # DamageFlyTop
        for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damagefly_terminal_fall_entry_blocks_fresh_enable_edge_body_selfplay_181413() -> None:
    # Self-play 181413 rec2361 is a terminal DamageFlyTop -> Fall IASA row. The attacker has a
    # freshly enabled AttackHi4 BODY capsule, but source order does not let that create-edge
    # HitCapsule consume the post-IASA Fall target until the next collision frame. The rollout lock
    # proves the terminal row stays in Fall with no immediate rehit, while the following frame still
    # admits the real BODY hit instead of suppressing the attack family.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008F744,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    rows = _run_slp_rollout_records(slp_path, start=0, records=(2361, 2362), ports=[1, 2])
    defender = 0

    out_2361, ref_2361 = rows[2361]
    assert int(ref_2361["action_id"][defender]) == 29  # Fall.
    assert int(out_2361["action_id"][defender]) == int(ref_2361["action_id"][defender])
    assert int(out_2361["hitlag"][defender]) == int(ref_2361["hitlag"][defender]) == 0
    assert int(out_2361["hitstun"][defender]) == int(ref_2361["hitstun"][defender]) == 0
    assert float(out_2361["percent"][defender]) == pytest.approx(
        float(ref_2361["percent"][defender]), abs=1e-6
    )

    out_2362, ref_2362 = rows[2362]
    assert int(ref_2362["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(out_2362["action_id"][defender]) == int(ref_2362["action_id"][defender])
    assert int(out_2362["hitlag"][defender]) == int(ref_2362["hitlag"][defender]) == 9
    assert float(out_2362["percent"][defender]) > float(out_2361["percent"][defender])


@pytest.mark.integration
def test_damagefly_terminal_fall_entry_does_not_block_same_frame_shine_entry_ppa() -> None:
    # Negative boundary for terminal DamageFly/DamageFall -> Fall create-edge suppression:
    # PriceyPartialAlbatross rec2656 has p1 enter grounded SpecialLwStart from Squat on the same
    # frame p0 is on the terminal DamageFlyTop hitstun tick. Vanilla admits the fresh Shine BODY
    # hit before p0 can settle into Fall. The retained suppression is therefore limited to
    # same-action attack create edges, not all newly-enabled BODY capsules near terminal damage.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Action
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2656)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(seed["hitstun"][defender]) == 1
    assert int(seed["action_id"][attacker]) == 39  # Squat.
    assert int(ref["action_id"][attacker]) == 360  # Fox SpecialLwStart.
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop re-entry from Shine.
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 5
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_grounded_attack_restart_clear_applies_to_attackdash_tch() -> None:
    # Grounded Attack* same-action restart clear outside the self-play Fox AttackHi3 row:
    # TubbyCurlyHerring rec1817 is Fox AttackDash against Falco DamageFlyN. The attacker remains
    # in AttackDash, but source has created a fresh grounded Attack* HitCapsule list for this
    # action instance, so same-source stale BODY attribution from earlier rows cannot suppress the
    # new hit. This proves the retained owner is the MSLMSO01 grounded-attack class boundary, not a
    # Fox AttackHi3 replay slice.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
    # MSLMSO01: MSL_MS_CLASS_GROUNDED_ATTACK.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 1817)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 50  # AttackDash.
    assert int(seed["action_frame"][attacker]) == 4
    assert int(seed["action_id"][defender]) == 70  # FallSpecial.
    assert int(ref["action_id"][defender]) == 77  # DamageHi3.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("DistinctCaringCobra.msl", 9272, 1),
        ("TubbyCurlyHerring.msl", 3186, 1),
    ],
)
def test_enable_edge_tiplog_phantom_rows_do_not_enter_damage(dataset_name: str, record: int, defender: int) -> None:
    # Enable-edge BODY phantom/tip-log lock:
    # - The selected HitCapsule overlaps by less than p_ftCommonData->x7A8.
    # - Vanilla starts hitlag through ftColl_80076ED8's tip-log lane but does not enter a damage
    #   motion state or write hitstun.
    # - The runtime branch is decomp-shaped and uses the collision matrix helper, not a replay row
    #   or post-admission admission bridge.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8,ftColl_8007AD18}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_specialairsstart_pre_anim_pose_rejects_dtilt_body_cnm_5292() -> None:
    # Fox/Falco Side-B Start collision-pose ownership:
    # - BODY collision consumes the same frame's pre-Anim JObj pose on SpecialAirSStart startup.
    # - The generic post-Anim pose plus x58/x4C sweep admits a false Falco dtilt BODY hit against
    #   Fox Side-B startup low hurtcaps on CNM:5292; vanilla keeps Fox in SpecialAirSStart with no
    #   hitlag/hitstun.
    # - Adjacent real Falco BODY hits on the same flagless parts stay admitted by the row-level
    #   validation diff; this is not a generic attacker model-scale or part-id filter.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialSStart_Anim,ftFx_SpecialAirSStart_Anim,ftFx_SpecialSStart_Coll,
    #   ftFx_SpecialAirSStart_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5292)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 350  # SpecialAirSStart
    assert int(seed["action_id"][attacker]) == 57  # AttackLw3
    assert int(seed["action_frame"][attacker]) == 7
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_id", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_id"):
        assert int(out[field][attacker]) == int(ref[field][attacker]), f"attacker field={field}"


@pytest.mark.integration
def test_damageflyhi_terminal_aobj_pose_selects_high_hurtcap_dcc_8565() -> None:
    # DamageFly terminal AObj collision pose:
    # - ftCo_DamageFly_Anim keeps the victim in active hitstun after the non-looping AObj reaches
    #   end_frame.
    # - HSD_AObjInterpretAnim marks those stopped JObjs AOBJ_NO_ANIM, while lb_8000B1CC still
    #   consumes their final live local SRT for BODY hurtcaps in ftColl_80078C70.
    # - DCC:8565 is Falco DamageFlyHi at end_frame=29; the high hurtcap must use that stopped
    #   terminal AObj pose so Fox AttackAirB selects the strong high hitbox, not the weak mid one.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 8565)
    defender = 1
    assert int(seed["action_id"][defender]) == 87  # ftCo_MS_DamageFlyHi
    assert int(seed["animation_index"][defender]) == 177  # ftCo_SM_DamageFlyHi
    assert int(seed["action_frame"][defender]) == 29
    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 8
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 57
    assert int(out["percent"][defender]) == int(ref["percent"][defender])


@pytest.mark.integration
def test_damageflyhi_terminal_aobj_pose_does_not_pre_admit_dcc_8564() -> None:
    # Negative neighbor for the stopped-AObj terminal pose: the immediately preceding DCC row has
    # the same DamageFlyHi terminal victim but no accepted BODY hit yet. The extractor/runtime
    # change must not become a broad DamageFlyHi hit admission shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 8564)
    defender = 1
    assert int(seed["action_id"][defender]) == 87
    assert int(seed["animation_index"][defender]) == 177
    assert int(seed["action_frame"][defender]) == 29
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_expires_hurtbox_state_gat_9068() -> None:
    # RebirthWait -> Fall x1994 seed owner:
    # - RebirthWait_Anim / IASA call ftColl_8007B7A4(..., p_ftCommonData->x5D8) before Fall.
    # - GAT:9068 is the terminal seeded x1994=1 frame several actions after RebirthWait -> Fall.
    # - Fighter_8006A360 must decrement that timer and clear x198C before post-frame compare;
    #   stale-carrying the merged Slippi hurtbox_state leaves p1 invincible one frame too long.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_{Anim,IASA}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 9068)
    defender = 1
    assert int(seed["action_id"][defender]) == 39  # ftCo_MS_Squat
    assert int(seed["hurtbox_state"][defender]) == 1
    assert int(seed["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed["colanim_timer_x1994"][defender]) == 1
    assert int(seed["colanim_rebirth_fall_x1994_seed"][defender]) == 1
    assert int(out["hurtbox_state"][defender]) == int(ref["hurtbox_state"][defender]) == 0


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_requires_rebirth_source_flag_gat_9068() -> None:
    # Boundary guard: a nonzero x1994 timer is not trusted as generic gameplay state unless the
    # replay-history lane proves the RebirthWait -> Fall owner. This keeps the fix from becoming a
    # broad "timer means clear hurtbox_state" shortcut for unrelated x1994 sources.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[9068:9069]["seed_t"].copy()
    defender = 1
    assert int(seed[0]["colanim_rebirth_fall_x1994_seed"][defender]) == 1
    seed[0]["colanim_rebirth_fall_x1994_seed"][defender] = np.uint8(0)
    out, ref = _step_one_row_with_seed(dataset_path, 9068, seed)
    assert int(ref["hurtbox_state"][defender]) == 0
    assert int(out["hurtbox_state"][defender]) == 1


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_fixes_gat_9063_rollout_window() -> None:
    # Rollout-real lock for the disruptive F08b GAT cluster:
    # starting at GAT:9063, p1 is still in the RebirthWait -> Fall invincibility window. The hidden
    # x1994 timer must expire at GAT:9068, otherwise p1 remains invincible and later BODY/contact
    # selection diverges into the high-scoring rollout blast radius.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    start = 9063
    stop = 9125
    defender = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "hitlag", "hitstun", "hurtbox_state"):
                assert int(out[field][defender]) == int(ref[field][defender]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_peer_kneebend_nudge_requires_frame_start_grounded_tbk_1426() -> None:
    # Fighter_8006A360 runs ftCommon_8007E0E4 per fighter after that fighter's Anim callback.
    # On TBK:1426 p0 GuardReflect still sees p1's frame-start grounded KneeBend pushbox before
    # p1's later KneeBend_Anim enters JumpF. Mutating the peer to start airborne must remove that
    # x450 player-nudge lane rather than applying it from a broad current-action shortcut.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1426:1427]["seed_t"].copy()
    assert int(seed[0]["action_id"][0]) == 182  # GuardReflect
    assert int(seed[0]["action_id"][1]) == 24  # KneeBend
    assert int(seed[0]["on_ground"][1]) == 1

    out, ref = _step_one_row_with_seed(dataset_path, 1426, seed)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 182
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=3e-5)

    seed[0]["on_ground"][1] = np.uint8(0)
    seed[0]["ground_id"][1] = np.uint16(0xFFFF)
    out_air, _ = _step_one_row_with_seed(dataset_path, 1426, seed)
    assert float(out_air["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]) + 0.3, abs=3e-5)


@pytest.mark.integration
def test_guardreflect_final_x14_shine_body_rollout_rejects_broad_shield_extent_tbk_1403() -> None:
    # Rollout-real lock for the TBK F08b disruptive row:
    # - p0's GuardReflect final-x14 no-submotion ShieldDesc is near the p1 shine hitbox rim.
    # - The broad ShieldDesc extent/model-scale proxy falsely turns the row into GuardSetOff.
    # - Source-shaped final-x14 fighter-vs-fighter shield admission must reject that shield
    #   candidate so the BODY contact enters DamageFlyTop.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 1403
    stop = 1427
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(ref["action_id"][0]) == 90  # DamageFlyTop
        for field in ("action_id", "animation_index", "hitlag", "hitstun"):
            assert int(out[field][0]) == int(ref[field][0]), f"field={field}"
        assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1e-6)
        assert float(out["shield_hp"][0]) == pytest.approx(float(ref["shield_hp"][0]), abs=1e-6)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_final_x14_shine_precombat_shield_candidate_is_rejected_tbk_1427() -> None:
    # Negative boundary for the same owner: at the collision snapshot, the p1 shine hitbox is a
    # valid BODY candidate but must not be accepted as a GuardReflect shield hit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 1403
    target = 1427
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)

        prev_input_bytes = samples_u8[
            target : target + 1, prev_input_off : prev_input_off + input_stride
        ].copy()
        input_bytes = samples_u8[target : target + 1, input_off : input_off + input_stride].copy()
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw_cand, count_cand = binding.debug_shield_candidate_decisions(handle, 0, 128)
    finally:
        binding.destroy(handle)

    cand = raw_cand.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count_cand]
    accepted = [
        c
        for c in cand
        if int(c["attacker"]) == 1
        and int(c["defender"]) == 0
        and int(c["hitbox_id"]) == 0
        and int(c["reject_reason"]) == 0
        and int(c["overlap_shield"]) == 1
    ]
    assert not accepted


@pytest.mark.integration
def test_guardreflect_expired_x14_attackhi3_enable_edge_reaches_guardsetoff_tbk_3585() -> None:
    # Rollout-real positive for the expired-x14 no-submotion ShieldDesc create-edge lane:
    # - p1 is a GuardReflect no-submotion snapshot whose x14 has expired and whose ShieldDesc is
    #   replay-visible before collision.
    # - p0 AttackHi3 creates fresh HitCapsules this frame; ftColl_8007AD18 forwards ShieldDesc.size
    #   to the lbColl overlap helper for that create-edge capsule.
    # - Persistent capsules near the same GuardReflect rim stay covered by the TBK:1427 shine
    #   negative above, which must not borrow this lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 3585
    stop = 3610
    attacker = 0
    defender = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(samples["seed_t"][stop]["action_id"][attacker]) == 56  # AttackHi3
        assert int(samples["seed_t"][stop]["action_id"][defender]) == 182  # GuardReflect
        assert int(ref["action_id"][defender]) == 181  # GuardSetOff
        for field in ("action_id", "animation_index", "hitlag", "hitstun"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
        assert float(out["shield_hp"][defender]) == pytest.approx(
            float(ref["shield_hp"][defender]), abs=1e-5
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_expired_x14_no_submotion_body_uses_guardon_hurtcaps_gat_11085() -> None:
    # Expired-x14 GuardReflect no-submotion BODY handoff:
    # - ftCo_GuardReflect_Anim calls ftCo_80093BC0 before the fighter-vs-fighter collision pass.
    # - Once mv.co.guard.x14 expires, ftCo_80093BC0 recreates ShieldDesc via ftCo_80092450 while
    #   x18 can remain live. If the shield overlap misses, BODY still consumes the GuardReflect
    #   motion-state submotion hurtcaps instead of an empty serialized snapshot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 11085
    defender = 0
    attacker = 1
    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise

    assert int(seed["action_id"][defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][defender]) == -2
    assert int(seed["guard_reflect_timer_x14"][defender]) == 0
    assert int(seed["guard_reflect_timer_x18"][defender]) > 0
    assert int(seed["seed_prev_action_id"][defender]) == 182
    assert int(seed["action_id"][attacker]) == 69  # AttackAirHi

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 87  # DamageFlyHi
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 7
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 45
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-5)
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 7


@pytest.mark.integration
def test_guardreflect_active_x14_no_submotion_without_guardon_provenance_stays_no_body_gat_11085() -> None:
    # Boundary for the expired-x14 owner above: active x14 remains a ReflectDesc/raw-snapshot phase
    # unless the transition provenance is GuardOn. Mutating only x14 back to active must not turn
    # this no-submotion snapshot into generic GuardReflect BODY hurtcaps.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 11085
    defender = 0
    attacker = 1
    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    seed = ds.samples["seed_t"][record : record + 1].copy()
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(1)
    out, _ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(seed["action_id"][0, defender]) == 182
    assert int(seed["seed_prev_action_id"][0, defender]) == 182
    assert int(seed["guard_reflect_timer_x14"][0, defender]) == 1
    assert int(out["action_id"][defender]) == 182
    assert int(out["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == 0
    assert int(out["hitlag"][attacker]) == 0


@pytest.mark.integration
def test_guardreflect_active_x14_no_guardon_allows_later_seeded_shield_ewt_1218() -> None:
    # FoD rollout lock for the active-x14 no-submotion GuardReflect boundary:
    # - p1 is a carried GuardReflect raw snapshot with active x14 but no GuardOn provenance.
    # - The early p0 AttackAirB BODY candidates overlap stale serialized hurtcaps, but source
    #   ownership is still ReflectDesc until ftCo_80093BC0 expires x14.
    # - A later hitbox has replay-proven ShieldDesc contact and must still enter GuardSetOff with
    #   the lower 9-damage shield hitlag. This is the source-order complement to the GAT no-BODY
    #   negative above, not a blanket GuardReflect contact reject.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise

    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 1020
    target = 1218
    attacker = 0
    defender = 1
    seed = samples["seed_t"][target]
    ref = samples["ref_t1"][target]
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][defender]) < 0
    assert int(seed["guard_reflect_timer_x14"][defender]) > 0
    assert int(seed["guard_reflect_origin_guardon_u8"][defender]) == 0
    assert int(seed["seed_prev_action_id"][defender]) != 178  # not GuardOn
    assert int(ref["action_id"][defender]) == 181  # GuardSetOff
    assert int(ref["hitlag"][defender]) == 6

    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
        out = out_view[0].copy()
    finally:
        binding.destroy(handle)

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["animation_index"][defender]) == int(ref["animation_index"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]), abs=1e-5)


@pytest.mark.integration
def test_guardreflect_expired_x14_rollout_orders_lower_body_before_later_shield_gat_11080() -> None:
    # Rollout-real lock for the same expired-x14 GuardReflect handoff:
    # - p1 AttackAirHi hitbox 0 misses ShieldDesc but overlaps p0's GuardOn hurtcap fallback.
    # - hitbox 1 would overlap the shield bubble in the simulator proxy, but decomp processes
    #   shield/BODY per HitCapsule in order, so the lower-index BODY hit commits first.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 11080
    stop = 11086
    defender = 0
    attacker = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "hitlag", "hitstun", "shield_hp"):
                if out[field].dtype.kind in "iu":
                    assert int(out[field][defender]) == int(ref[field][defender]), (
                        f"record={record} field={field}"
                    )
                else:
                    assert float(out[field][defender]) == pytest.approx(
                        float(ref[field][defender]), abs=1e-5
                    ), f"record={record} field={field}"
        assert int(out_view[0]["action_id"][defender]) == 87  # DamageFlyHi
        assert int(out_view[0]["hitlag"][attacker]) == int(samples["ref_t1"][stop]["hitlag"][attacker])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_expired_x14_later_shield_candidate_reports_earlier_body_gat_11085() -> None:
    # Diagnostic boundary for the rollout owner above: at the pre-combat snapshot, hitbox 1 is not
    # allowed to win as a shield hit because lower hitbox 0 has already reached BODY priority.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 11080
    target = 11085
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)

        prev_input_bytes = samples_u8[
            target : target + 1, prev_input_off : prev_input_off + input_stride
        ].copy()
        input_bytes = samples_u8[target : target + 1, input_off : input_off + input_stride].copy()
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw_cand, count_cand = binding.debug_shield_candidate_decisions(handle, 0, 128)
    finally:
        binding.destroy(handle)

    cand = raw_cand.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count_cand]
    hb1 = [
        c
        for c in cand
        if int(c["attacker"]) == 1 and int(c["defender"]) == 0 and int(c["hitbox_id"]) == 1
    ]
    assert len(hb1) == 1
    assert int(hb1[0]["reject_reason"]) == 12
    assert int(hb1[0]["overlap_shield"]) == 0


@pytest.mark.integration
def test_guardreflect_x14_expiry_recreates_current_shielddesc_tch_5251() -> None:
    # Active-x14 GuardReflect callback boundary:
    # - p1 starts the frame on GuardReflect with x14==1 and x18 still live.
    # - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` expires x14, recreates ShieldDesc via
    #   `ftCo_80092450`, then fighter-vs-fighter collision accepts p0 AttackAirHi into
    #   GuardSetOff without shield HP depletion because x18/x221C_b2 remains active.
    # - The direct one-step row locks the no-shield-damage side of that boundary; the rollout lock
    #   below clears the seed dependence by reaching the same contact from live simulation.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5251
    attacker = 0
    defender = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][record : record + 1].copy()
    out, ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(seed["action_id"][0, attacker]) == 69  # AttackAirHi
    assert int(seed["action_id"][0, defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][0, defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][0, defender]) == -2
    assert int(seed["guard_reflect_timer_x14"][0, defender]) == 1
    assert int(seed["guard_reflect_timer_x18"][0, defender]) > 0

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181  # GuardSetOff
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("x14", [0, 2])
def test_guardreflect_current_shielddesc_requires_x14_expiry_boundary_tch_5251(x14: int) -> None:
    # Boundary negative for the TCH:5251 current-pose ShieldDesc owner:
    # - x14==2 is still in the pre-expiry ReflectDesc-only phase for no-submotion GuardReflect.
    # - x14==0 was already expired before this frame and lacks the x14 seed-to-zero callback.
    # Neither case may borrow the current-pose ShieldDesc handoff when the teacher-forced
    # ShieldDesc seed is absent.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5251
    defender = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][record : record + 1].copy()
    seed["combat_shield_contact_hb_kind"][:] = np.uint8(0)
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(x14)
    out, _ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(out["action_id"][defender]) == 182  # GuardReflect stays no-contact.
    assert int(out["hitlag"][defender]) == 0


@pytest.mark.integration
def test_guardreflect_already_expired_x14_seed_does_not_suppress_shield_damage_tch_5251() -> None:
    # Boundary negative for shield-damage suppression: replay-proven ShieldDesc contact can still
    # be teacher-forced when x14 was already expired, but this test isolates the old x14-only
    # boundary by clearing the independent x221C_b2 powershield owner. Without either source lane it
    # must not borrow x18 as a powershield no-damage owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5251
    defender = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][record : record + 1].copy()
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(0)
    seed["state_flags"][0, defender, 3] = np.uint8(int(seed["state_flags"][0, defender, 3]) & ~0x20)
    out, ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) < float(ref["shield_hp"][defender])


@pytest.mark.integration
def test_guardreflect_x14_expiry_rollout_reaches_jumpf_tch_5224() -> None:
    # Rollout-real lock for the disruptive TCH cluster:
    # - Starting at TCH:5224, p1 reaches GuardReflect at 5250.
    # - At 5251 the x14-expiry ShieldDesc handoff must accept p0 AttackAirHi into GuardSetOff.
    # - The later row 5283 then remains replay-aligned as JumpF instead of the previous false
    #   DamageAir3 divergence from the missed shield contact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 5224
    stop = 5284
    defender = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        seen_5251 = False
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            if record == 5251:
                seen_5251 = True
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
                assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
                assert float(out["shield_hp"][defender]) == pytest.approx(
                    float(ref["shield_hp"][defender]), abs=1e-6
                )
            if record == 5283:
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 25
                assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
        assert seen_5251
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_shieldbreakstand_furafura_entry_clears_colanim_hit_status_prh_11549_rollout() -> None:
    # Rollout-real lock for the disruptive former-F08b PRH cluster:
    # ShieldBreakStand keeps colanim hit status while standing up from shield break, but
    # ftCo_80099010 enters Furafura without KeepColAnimHitStatus / SkipColAnim. The destination
    # row must clear x198C-derived hurtbox_state rather than carrying invulnerability through the
    # dazed action.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    start = 11549
    stop = 11565
    p = 0
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "action_frame", "hurtbox_state"):
                assert int(out[field][p]) == int(ref[field][p]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_landingairb_lcancel_rate_uses_hitlag_latched_lr_edge_fsp_6448_rollout() -> None:
    # Rollout-real lock for the disruptive former-F08b FSP cluster:
    # an LR press during AttackAirB hitlag is held in input.x668 while fp->x2219_b5 is active, so
    # x67F remains inside the L-cancel window when AttackAirB_Coll enters LandingAirB. Runtime must
    # carry that hitlag-latched input-history owner, otherwise LandingAirB runs at full landing lag
    # and action_frame falls behind replay.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    start = 6448
    stop = 6470
    p = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "action_frame", "hitlag", "l_cancel"):
                assert int(out[field][p]) == int(ref[field][p]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_apply_with_variant_pose_agg_5611() -> None:
    # Angled side-tilt event ownership:
    # - AttackS3Hi/HiS/S/LwS/Lw use distinct submotions/poses but share the ftCo_AttackS3
    #   callbacks. The extracted command table stores the common hitbox events under
    #   ftCo_SM_AttackS3, so runtime aliases only the command-event lookup and still samples
    #   hitbox centers from the live angled submotion pose.
    # - AGG:5611 is Falco AttackS3Lw hitting Fox during SpecialAirHi. Without the command-event
    #   alias, no Falco hitboxes exist and Fox incorrectly continues SpecialAirHi.
    # refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* table entries)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c
    # data/moves/{fox,falco}.json::moves["ftCo_SM_AttackS3"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5611)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 55  # ftCo_MS_AttackS3Lw
    assert int(seed["animation_index"][attacker]) == 57  # ftCo_SM_AttackS3Lw pose
    assert int(seed["action_frame"][attacker]) == 5
    assert int(ref["action_id"][defender]) == 88  # DamageFlyN
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_do_not_pre_admit_agg_5610() -> None:
    # Negative neighbor for the AttackS3 angle event alias: the frame before AGG:5611 has the same
    # angled side-tilt owner but no accepted BODY hit. The alias must not become a broad
    # side-tilt-frame admission shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5610)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 55  # ftCo_MS_AttackS3Lw
    assert int(seed["animation_index"][attacker]) == 57  # ftCo_SM_AttackS3Lw pose
    assert int(ref["action_id"][defender]) == 356  # SpecialAirHi
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_fix_rollout_agg_5577() -> None:
    # Rollout-real lock for the disruptive AGG cluster: starting from SpecialHiHoldAir at 5577,
    # the first visible split was Fox continuing SpecialAirHi through Falco's AttackS3Lw. The
    # source-shaped command-event alias should make the rollout hit match replay at 5611 without
    # needing any row-local bridge.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    start_record = 5577
    defender = 1
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, 5612):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out_row = out_view[0].copy()
            ref_row = samples["ref_t1"][record]
            if record < 5611:
                assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender]), record
            else:
                assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender]) == 88
                assert int(out_row["hitlag"][defender]) == int(ref_row["hitlag"][defender]) == 6
                assert int(out_row["hitstun"][defender]) == int(ref_row["hitstun"][defender]) == 47
                break
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_sustained_attackairn_edge_near_x7a8_still_enters_damage_qgd_8332() -> None:
    # Negative sentinel for the enable-edge phantom subset. QGD:8332 is a sustained AttackAirN
    # capsule near the x7A8 boundary; vanilla enters DamageAir2, so the tip-log subset must not
    # broaden into a generic overlap-margin suppression.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 8332)
    defender = 0
    assert int(ref["action_id"][defender]) == 85  # DamageAir2
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_enable_edge_x58_x4c_no_translation_sweep_tch_5010() -> None:
    # Newly created HitCapsules use ftColl_8007AD18's HitCapsule_Enabled case: x4C is sampled
    # from the refreshed current pose and x58 is copied from x4C before BODY collision. A
    # teacher-forced reseed must not synthesize x58 by sweeping backward through this frame's
    # fighter translation on a per-hitbox enable edge.
    #
    # TCH:5010 is an enable-edge AttackAirLw row in the remaining exact lbColl narrowphase split;
    # the direct sweep check below protects the shared x58/x4C owner while that scalar residual is
    # worked separately.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = pytest.importorskip("tools.eval.dataset").read_dataset(str(dataset_path))
    row = ds.samples[5010:5011]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        timing_raw = binding.debug_hitbox_event_timing(handle, 0, 1, 0)
        sweep_raw = binding.debug_hitbox_sweep_proxy(handle, 0, 1, 0)
    finally:
        binding.destroy(handle)

    timing_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("hb_id", "u1"),
            ("char_id", "u1"),
            ("_pad0", "u1"),
            ("msid", "<u2"),
            ("pose_frame", "<u2"),
            ("anim_frame_f32", "<f4"),
            ("frame_speed_mul_f32", "<f4"),
            ("start_frame", "<i2"),
            ("end_frame", "<i2"),
            ("enabled_prev", "u1"),
            ("enabled_cur", "u1"),
            ("prev_hit_group", "u1"),
            ("cur_hit_group", "u1"),
            ("pose_create_count", "u1"),
            ("pose_clear_count", "u1"),
            ("pose_clear_all_count", "u1"),
            ("enable_edge", "u1"),
            ("last_affect_kind_le", "u1"),
            ("last_affect_kind_eq", "u1"),
            ("last_affect_frame_le", "<u2"),
            ("last_affect_frame_eq", "<u2"),
            ("last_affect_u16_7_le", "<u2"),
            ("last_affect_u16_7_eq", "<u2"),
        ],
        align=False,
    )
    timing = timing_raw.reshape(-1).view(timing_dtype)[0]
    assert int(timing["msid"]) == 72  # AttackAirLw
    assert int(timing["enabled_prev"]) == 0
    assert int(timing["enabled_cur"]) == 1
    assert int(timing["enable_edge"]) == 1
    sweep_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("hb_id", "u1"),
            ("enabled_prev", "u1"),
            ("enabled_cur", "u1"),
            ("prev_valid", "u1"),
            ("cur_valid", "u1"),
            ("_pad0", "u1", (2,)),
            ("msid", "<u2"),
            ("pose_prev", "<u2"),
            ("pose_cur", "<u2"),
            ("char_id", "u1"),
            ("_pad1", "u1"),
            ("anim_frame_f32", "<f4"),
            ("prev_anim_frame_f32", "<f4"),
            ("frame_speed_mul_f32", "<f4"),
            ("prev_x", "<f4"),
            ("prev_y", "<f4"),
            ("prev_z", "<f4"),
            ("prev_radius", "<f4"),
            ("cur_x", "<f4"),
            ("cur_y", "<f4"),
            ("cur_z", "<f4"),
            ("cur_radius", "<f4"),
            ("u16_6_prev", "<u2"),
            ("u16_7_prev", "<u2"),
            ("u16_6_cur", "<u2"),
            ("u16_7_cur", "<u2"),
            ("arg3_var_r22_known", "u1"),
            ("arg3_var_r22_from_extracted", "u1"),
            ("arg3_var_r22_gates_collision", "u1"),
            ("_pad2", "u1"),
        ],
        align=False,
    )
    sweep = sweep_raw.reshape(-1).view(sweep_dtype)[0]
    assert int(sweep["enabled_prev"]) == 0
    assert int(sweep["enabled_cur"]) == 1
    assert int(sweep["prev_valid"]) == 0
    assert int(sweep["cur_valid"]) == 1


@pytest.mark.integration
def test_grounded_overlap_z_depth_rejects_false_attackdash_hhg_6740() -> None:
    # Grounded fighter-overlap depth lane:
    # - ftCommon_8007DD7C writes xF8_playerNudgeVel.y from p_ftCommonData->x454.
    # - ftCommon_8007E0E4 clamps that hidden engine-space Z lane with x458.
    # - Fighter_procUpdate applies the lane before collision primitives are refreshed, so
    #   ftColl_80078C70/lbColl_80006E58 see separated grounded BODY primitives.
    # HHG:6740 was a false AttackDash->KneeBend BODY hit while replay-visible Slippi pos_z was 0.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 6740)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_landingairn_float_aobj_hurtcaps_reject_false_attackdash_tch_1816() -> None:
    # LandingAirN can carry a non-integer AObj frame because its animation rate is set from
    # landing lag. Hurtcap endpoints must sample the live HSD AObj/FObj local-SRT pose before
    # lb_8000B1CC; integer SSANIM floor admitted a false AttackDash BODY hit here.
    #
    # This is not a replay admission bridge: the runtime path evaluates extracted SSANIMT1 FObj
    # tracks and then uses the normal BODY selector.
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = pytest.importorskip("tools.eval.dataset").read_dataset(str(dataset_path))
    row = ds.samples[1816:1817]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        selected_raw, selected_count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)

    assert int(row["seed_t"]["action_id"][0, 1]) == 70  # LandingAirN
    assert float(row["seed_t"]["anim_frame_f32"][0, 1]) != float(int(row["seed_t"]["anim_frame_f32"][0, 1]))
    assert int(selected_count) == 0
    assert selected_raw.shape[0] >= 1

    _seed, out, ref = _step_one_row(dataset_path, 1816)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_fox_attackdash_static_collision_pose_rejects_false_attackairhi_fsp_7078() -> None:
    # AttackAirHi vs AttackDash allow-interrupt tail boundary:
    # - FSP:7078 has the selected late AttackAirHi hb2 source overlapping only the defender's
    #   part-18 tail cap while grounded AttackDash has crossed its generated allow_interrupt event.
    # - Clear the seeded HitCapsule victim rings before stepping so this lock exercises the BODY
    #   geometry owner directly; otherwise one-step replay seed state can mask the false contact.
    # - The adjacent FSP:5765 positive keeps pre-allow-interrupt AttackDash BODY admission intact,
    #   and FSP:7080 remains the later Wait-frame UpAir hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirHi/events.ftCo_SM_AttackDash
    # data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[7078:7079]["seed_t"].copy()
    attacker = 0
    defender = 1
    assert int(seed["action_id"][0, attacker]) == 68  # AttackAirHi
    assert int(seed["action_id"][0, defender]) == 50  # AttackDash
    assert int(ds.samples[7080]["ref_t1"]["action_id"][defender]) == 90  # DamageFlyTop.
    seed["combat_hitlist_cd"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(0)
    seed["combat_hitlist_hb_cd"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_hb_victim_iid"][0, attacker, :, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed(dataset_path, 7078, seed)
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairlw_attackdash_allow_interrupt_tail_rejects_high_capsule_fsp_1270() -> None:
    # AttackAirLw vs AttackDash allow-interrupt tail boundary:
    # - FSP:1270 has Fox AttackAirLw hb0/hb1 active against a grounded Fox AttackDash defender
    #   exactly at the generated AttackDash allow_interrupt phase.
    # - Source rejects the part-18 tail/hb0 BODY owner and keeps the lower same-group hb1/body
    #   contact eligible, producing the 2-damage hitlag/percent lane.
    # - This is bounded by extracted data, not a row branch: data/moves/{fox,falco}.json provides
    #   the AttackAirLw same-group 3/2-damage payload and AttackDash allow_interrupt event, while
    #   data/hurtcaps/{fox,falco}.json identifies cap12 as part 18. The nearby FSP:5765 positive
    #   and FSP:7078 negative keep the broader AttackDash collision-pose policy unchanged.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 1270)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 79  # DamageN2
    assert float(ref["percent"][defender]) == 25.0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent", "instance_hit_by", "last_hit_by"):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_fox_attackdash_static_collision_pose_rejects_false_attackairhi_fsp_7078_rollout() -> None:
    # Rollout-mode negative lock for the former AttackDash frame-34 over-admit:
    # FSP:7078/7079 has the same visible AttackDash family as the positive frame-34 slice, but it
    # is after allow_interrupt and must reject the part-18 tail-only AttackAirHi hb2 BODY contact
    # through the frame-0 AttackDash -> Wait callback boundary. FSP:7080 is the later ordinary
    # Wait-frame UpAir hit and remains positive.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # Source of timing windows: data/scripts/{fox,falco}.bin (MSLFTSC1).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    start_record = 5391
    target_records = {7079, 7080}
    target_record = max(target_records)
    seed = ds.samples[start_record : start_record + 1]["seed_t"].copy()
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        rows: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_record + 1):
            row = ds.samples[record : record + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            frame_seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, seed_stride
            )
            binding.step_input_replay_frame_rng(handle, frame_seed_bytes, prev_input_bytes, input_bytes)
            if record in target_records:
                binding.write_compare(handle, out_compare_bytes)
                rows[record] = (
                    out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                    row["ref_t1"][0].copy(),
                )
    finally:
        binding.destroy(handle)

    defender = 1
    assert set(rows) == target_records
    out, ref = rows[7079]
    assert int(ref["action_id"][defender]) == 14  # Wait, not same-frame DamageFlyTop.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"record=7079 field={field}"
    out, ref = rows[7080]
    assert int(ref["action_id"][defender]) == 90  # Later ordinary Wait-frame DamageFlyTop hit.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"record=7080 field={field}"


@pytest.mark.integration
def test_attackdash_post_hitbox_pre_iasa_pose_selects_downsmash_body_fsp_5765() -> None:
    # AttackDash late collision-pose phase:
    # - Fox AttackDash has already run its script hitbox clear, but has not crossed its
    #   command-script allow_interrupt frame.
    # - BODY collision consumes the post-clear JObj collision pose for the defender capsules; the
    #   ordinary replay-visible frame pose misses this DownSmash low hit.
    # - The adjacent FSP:7078 negative above is after the AttackDash allow_interrupt frame and
    #   stays exact, proving this is not a broad AttackDash pose offset.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5765)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 67  # AttackLw4
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 80  # DamageHi1
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_fox_jumpb_dynamic_chain_selects_attackairb_body_ppa_3182() -> None:
    # Fox JumpB dynamic-chain collision pose:
    # - Dolphin pre-ftColl primitive probes on PPA:3182 show Falco AttackAirB's hitbox already
    #   matches runtime, while Fox hurtcap-12 endpoints consume the live ftData.x2C dynamic JObj
    #   chain before lb_8000B1CC/lbColl_80006E58.
    # - SSDYNN01 v4's data-owned collision-msid predicate includes JumpB; this is not a broad
    #   JumpB facing, distance, or row-local BODY admission shortcut.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 3182)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 26  # JumpB
    assert int(seed["animation_index"][defender]) == 17
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(ref["action_id"][defender]) == 85  # DamageAir3
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 7


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("FavorableSuperficialPig.msl", 1499, 1),
        ("ImpassionedAlarmedTarsier.msl", 2161, 0),
        ("ImpassionedAlarmedTarsier.msl", 6196, 1),
        ("TubbyCurlyHerring.msl", 5740, 1),
    ],
)
def test_fox_jumpb_dynamic_chain_does_not_broaden_body_admission_controls(
    dataset_name: str, record: int, defender: int
) -> None:
    # Negative controls from the rejected broad JumpB-facing experiment. These rows were exact
    # before the retained SSDYNN01 data predicate and must stay exact, proving the owner is not a
    # generic JumpF/B pose flip or BODY tolerance.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action", "expected_hitlag"),
    [
        (4921, 212, 0),
        (4922, 212, 0),
        (4923, 79, 6),
    ],
)
def test_fox_catch_dynamic_tail_chain_keeps_dair_body_timing_mgs(
    record: int, expected_action: int, expected_hitlag: int
) -> None:
    # Fox Catch dynamic-chain collision pose:
    # - Falco DAir's active hitbox overlaps Fox's part-18 tail cap near Catch frames 8..10.
    # - Vanilla ftColl consumes Fox's ftData.x2C dynamic JObj chain through ftCo_8009E0A8 before
    #   lbColl BODY tests; the descriptor cone clamp from lb_8001044C keeps frames 8 and 9 out,
    #   while frame 10 reaches DamageN2.
    # - The SSDYNN01 collision-owner predicate names ftCo_SM_Catch, so this is not a local
    #   AttackAirLw, FoD, or record-window BODY suppression.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E0A8}
    # refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    # data/anims/fox.dyn.bin::SSDYNN01 collision_motion_state_ids(ftCo_SM_Catch)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 212  # Catch
    assert int(seed["animation_index"][defender]) == 242
    assert int(ref["action_id"][defender]) == expected_action
    assert int(ref["hitlag"][defender]) == expected_hitlag
    for field in (
        "action_id",
        "animation_index",
        "hitlag",
        "hitstun",
        "percent",
        "instance_hit_by",
        "last_hit_by",
    ):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_late_attackairhi_hitcapsule_latch_rejects_false_wait_hit_fsp_7079() -> None:
    # Late AttackAirHi victim-list owner:
    # - Fox/Falco UpAir clears the early hitboxes and recreates same-group late hitboxes.
    # - After that recreate edge, lbColl_8000ACFC owns repeat suppression by HitCapsule victim
    #   pointer. Slippi BODY attribution can still name an older source and the victim instance_id
    #   can advance on a same-frame Wait entry, so the dense seed latch must survive the stale
    #   attribution trim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 7079)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 68  # AttackAirHi
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 14  # Wait, not DamageFlyTop
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_jumpf_tap_crossing_iasa_preempts_false_attacklw4_body_fsp_4852() -> None:
    # JumpF/B live-input IASA tap crossing:
    # - ftCo_Jump_IASA reaches ftCo_800CB870 before BODY collision.
    # - FSP:4852 seeds Fox in JumpF frame 0 from a pre-input KneeBend Anim entry. The later
    #   Fighter_Spaghetti input-history pass overwrites ftCo_Jump_Enter's transient x671=0xFE to a
    #   low timer because the stick freshly crossed the tilt threshold, and the current frame then
    #   crosses tap-jump threshold. Source enters JumpAerialF before Fox AttackLw4 BODY selection,
    #   so the false hit must not be admitted.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 4852)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][defender]) == 25  # JumpF
    assert int(seed["action_frame"][defender]) == 0
    assert int(seed["tilt_timer_y"][defender]) < 4
    assert int(ref["action_id"][defender]) == 27  # JumpAerialF
    for field in ("action_id", "animation_index", "jumps_left", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0


@pytest.mark.integration
def test_jumpf_tap_crossing_iasa_requires_fresh_tap_threshold_crossing_fsp_2123() -> None:
    # Negative boundary for the JumpF/B tap-crossing reconstruction: held-up snapshots whose
    # previous stick is already above the tap threshold must continue to rely on x671/XY edges,
    # not become a broad "JumpF plus high Y means double jump" shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2123)
    defender = 0
    assert int(seed["action_id"][defender]) == 25  # JumpF
    assert int(seed["action_frame"][defender]) == 0
    assert int(seed["tilt_timer_y"][defender]) == 0xFE
    assert int(ref["action_id"][defender]) == 25
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])


@pytest.mark.integration
def test_lbcoll_matrix_first_admits_grounded_attackhi3_row_ppa_2614() -> None:
    # Matrix-first lbColl BODY predicate:
    # - lbColl_8000805C forwards to lbColl_80006E58 for BODY admission. The matrix-derived scalar
    #   can accept a contact that a simple world sphere/capsule prefilter rejects.
    # - PPA:2614 protects that the runtime runs the matrix predicate directly when pose data is
    #   available instead of using the simple overlap as a prefilter.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2614)
    defender = 1
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 56  # AttackHi3
    assert int(ref["hitlag"][defender]) > 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_grounded_hidden_depth_carry_rejects_false_attackdash_tch_5649() -> None:
    # Grounded overlap hidden-depth carry:
    # - ftCommon_8007DD7C / ftCommon_8007E0E4 accumulate the hidden engine-space Z lane before
    #   collision primitives are refreshed.
    # - Slippi seeds visible `pos_z` as zero here, but a prefix-causal reconstruction from grounded
    #   pushbox overlap yields p0=-0.4/p1=+0.4, matching the vanilla collision-probe separation and
    #   rejecting the false AttackDash -> Wait BODY hit.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5649)
    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 50  # AttackDash
    assert int(seed["action_id"][defender]) == 14  # Wait
    assert float(seed["pos_z"][defender]) == pytest.approx(-0.4, abs=1e-6)
    assert float(seed["pos_z"][attacker]) == pytest.approx(0.4, abs=1e-6)
    assert int(ref["action_id"][defender]) == 14  # Wait, not DamageFlyTop
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairn_neutral_hitcapsule_latch_suppresses_false_wait_hit_his_2752() -> None:
    # AttackAirN dense-seed victim latch:
    # - lbColl_8000ACFC suppresses by HitCapsule.victims_1 victim presence, not by BODY
    #   `instance_hit_by` attribution.
    # - HIS:2752 has a neutral defender whose dense seed victim iid still matches the live fighter;
    #   preserving that latch rejects the false AttackAirN->Wait BODY hit while later owner slices
    #   continue to clear stale latches on proven refresh/admission rows.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # refs/melee/src/melee/lb/types.h::HitCapsule
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 2752)
    defender = 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairn_wait_rollout_bridge_not_used_by_ordinary_one_step_his_2752() -> None:
    # Package-boundary negative for the retained HIS rollout bridge:
    # - ordinary one-step reseed is allowed to use the explicit dense HitCapsule seed when present,
    #   but it must not consume the x18c8/last_hit_by fallback that exists only to reconstruct long
    #   replay-rollout hidden victim provenance.
    # - Clearing the dense row-local seed leaves only that fallback evidence; ordinary reseed must
    #   admit the live BODY hit rather than suppressing it as if replay_rollout_reseeded were set.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    record = 2752
    attacker = 1
    defender = 0
    seed = ds.samples["seed_t"][record : record + 1].copy()
    assert int(seed["combat_hitlist_cd"][0, attacker, 0, defender]) != 0
    seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed_one_step(dataset_path, record, seed)

    assert int(ref["action_id"][defender]) == 14  # Wait: dense seed suppresses the real replay row.
    assert int(out["action_id"][defender]) != int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) > 0


@pytest.mark.integration
@pytest.mark.parametrize(("record", "defender"), [(2753, 0), (3126, 0)])
def test_attackairn_stale_latch_wait_post_entry_and_jumpf_admit_real_hits_his(
    record: int, defender: int
) -> None:
    # Adjacent positive locks for the AttackAirN dense-seed victim latch:
    # - HIS:2752 preserves the Wait entry-frame victims_1 latch.
    # - HIS:2753 and HIS:3126 must still clear stale dense fallback entries so the live late
    #   AttackAirN HitCapsule can enter ftColl_80076ED8.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    attacker = 1
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(ref["hitlag"][defender]) > 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
@pytest.mark.parametrize("record", [5136, 5137])
def test_forensic_optional_attackairb_source_clear_landing_latch_suppresses_false_selfplay_body(
    record: int,
) -> None:
    # Optional replay-forensic self-play check for the AttackAirB source-clear landing latch.
    # Package coverage for this owner is the committed synthetic guard below; this local triage
    # dataset check is intentionally not required for package review.
    #
    # AttackAirB source-clear landing latch:
    # - Fox BAir has already hit the victim and the dense same-hit_group HitCapsule seed still
    #   names the current victim object through the DamageN -> Landing handoff.
    # - Vanilla keeps Landing; without reconstructing the HitCapsule victims_1 owner the simulator
    #   admits a one-frame-early BAir BODY rehit into DamageHi3.
    # - This is hidden HitCapsule provenance, not a BODY geometry tolerance; clearing the dense
    #   seed in the adjacent negative below must admit the contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "reports/triage/mainline_selfplay_datasets/mainline_selfplay_20260514T083640/"
        / "reports/triage/mainline_selfplay_replays/Game_20260514T083640.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing optional local self-play triage dataset: {dataset_path}")

    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale optional local self-play triage dataset: {dataset_path}")
        raise
    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 42  # Landing
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        seed["instance_id"][defender]
    )
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_forensic_optional_attackairb_source_clear_landing_latch_requires_dense_hitcapsule_seed_selfplay() -> None:
    # Optional replay-forensic negative for the BAir Landing source-clear owner. Package coverage
    # for this dense-HitCapsule boundary is the committed synthetic guard below.
    #
    # Source attribution alone is not enough to suppress a BODY contact. The dense HitCapsule
    # victim seed is the hidden owner proof.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "reports/triage/mainline_selfplay_datasets/mainline_selfplay_20260514T083640/"
        / "reports/triage/mainline_selfplay_replays/Game_20260514T083640.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing optional local self-play triage dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale optional local self-play triage dataset: {dataset_path}")
        raise
    record = 5136
    attacker = 1
    defender = 0
    seed = ds.samples["seed_t"][record : record + 1].copy()
    seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed_one_step(dataset_path, record, seed)

    assert int(ref["action_id"][defender]) == 42  # Landing: dense seed suppresses the real row.
    assert int(out["action_id"][defender]) != int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) > 0


def test_attackairb_source_clear_landing_dense_hitcapsule_suppression_synthetic() -> None:
    # Committed package guard for the AttackAirB source-clear Landing owner:
    # same-source Landing + x18C8 + same attacker instance is not enough by itself. The dense
    # HitCapsule victim seed (`combat_hitlist_cd == 0xFFFF`) is the hidden source owner that
    # suppresses the full BODY rehit. Clearing only that dense proof admits the contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    hit_grounded = 1 << 9
    hit_aerial = 1 << 10

    seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
    seed = seed_bytes.view(SEED_DTYPE).reshape(-1)
    attacker = 0
    defender = 1
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["char_id"][0, :2] = np.uint8(1)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["instance_id"][0, attacker] = np.uint16(111)
    seed["instance_id"][0, defender] = np.uint16(222)
    seed["action_id"][0, attacker] = np.uint16(67)  # AttackAirB
    seed["action_frame"][0, attacker] = np.int16(20)
    seed["source_port0"][0, attacker] = np.uint8(attacker)
    seed["action_id"][0, defender] = np.uint16(42)  # Landing
    seed["action_frame"][0, defender] = np.int16(3)
    seed["on_ground"][0, defender] = np.uint8(1)
    seed["source_clear_timer_x18c8"][0, defender] = np.uint8(3)
    seed["last_hit_by"][0, defender] = np.uint8(attacker)
    seed["instance_hit_by"][0, defender] = np.uint16(111)

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        for dense_seed, expected_hitlag, expected_percent in ((1, 0, 0.0), (0, 4, 5.0)):
            seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(
                0xFFFF if dense_seed else 0
            )
            seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(
                222 if dense_seed else 0
            )
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_set_hitbox_world(handle, 0, attacker, 0, 0.0, 0.0, 0.0, 2.0, 5.0, 1)
            binding.debug_set_hitbox_flags(handle, 0, attacker, 0, hit_grounded | hit_aerial)
            binding.debug_set_hitbox_group(handle, 0, attacker, 0, 0)
            binding.debug_set_hurtcap_world(
                handle, 0, defender, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0
            )
            binding.debug_combat_resolve(handle)
            binding.write_compare(handle, out_bytes)
            out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
            assert int(out["action_id"][defender]) == 42
            assert int(out["hitlag"][defender]) == expected_hitlag
            assert float(out["percent"][defender]) == pytest.approx(expected_percent)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender", "expected_seed_action", "expected_ref_action"),
    [
        ("DistinctCaringCobra.msl", 8844, 0, 15, 14),  # WalkSlow -> Wait
        ("ImpassionedAlarmedTarsier.msl", 6287, 1, 20, 21),  # Dash -> Run
        ("TubbyCurlyHerring.msl", 5842, 1, 41, 15),  # SquatRv -> WalkSlow
    ],
)
def test_same_frame_locomotion_entry_hurtcaps_use_previous_jobj_pose_for_body_collision(
    dataset_name: str, record: int, defender: int, expected_seed_action: int, expected_ref_action: int
) -> None:
    # Same-frame common locomotion entry pose order:
    # - Fighter_8006A360 has already interpreted the previous action's JObj pose.
    # - Input/IASA can enter Wait/Run/Walk before collision, but these paths do not perform an
    #   immediate ftAnim_8006EBA4 tick for the new pose before lb_8000B1CC consumers run.
    # - BODY hurtcaps therefore use the previous live JObj pose for this collision pass while the
    #   replay-visible action/timebase has already moved to the new state.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][defender]) == expected_seed_action
    assert int(ref["action_id"][defender]) == expected_ref_action
    assert int(ref["hitlag"][defender]) == 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_turn_to_walkslow_entry_hurtcaps_use_turn_pose_for_shine_body_iat_5052_rollout() -> None:
    # Turn -> WalkSlow entry boundary:
    # - ftCo_Turn_Anim has already interpreted the live Turn JObj pose for the frame.
    # - ftCo_Turn_IASA can then enter WalkSlow before BODY collision, but the collision pass still
    #   consumes the serialized Turn pose rather than a projected next Turn frame or WalkSlow frame.
    # - IAT:5052 rolls to IAT:5092, where projecting Turn+1 admits a false Shine BODY hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Anim,ftCo_Turn_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples_u8, seed_off, prev_input_off, input_off = _dataset_byte_views(ds)

    start = 5052
    target = 5092
    attacker = 0
    defender = 1
    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_view[0].copy()
    ref = samples["ref_t1"][target]
    assert int(ref["action_id"][attacker]) == 360  # Fox SpecialLwStart
    assert int(ref["action_id"][defender]) == 15  # WalkSlow
    assert int(ref["hitlag"][attacker]) == 0
    assert int(ref["hitlag"][defender]) == 0
    assert int(ref["hitstun"][defender]) == 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][attacker]) == int(ref[field][attacker]), f"attacker field={field}"
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)
    assert float(out["pos_x"][defender]) == pytest.approx(float(ref["pos_x"][defender]), abs=3e-5)
