from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class FreezeRehitCase:
    dataset_rel: str
    target_record: int
    attacker: int
    defender: int
    attacker_char: int
    defender_char: int
    attacker_action: int
    plus_overlap_persists: bool
    negative_record_delta: int


def _precombat_body_counts_after_one_step(dataset_path: Path, record: int) -> tuple[int, int]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record + 1, f"dataset too short for rollout lock: record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    row = samples[record : record + 1]
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    next_prev_input_bytes = np.frombuffer(samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    next_input_bytes = np.frombuffer(samples[record + 1]["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.debug_step_input_pre_combat(handle, next_prev_input_bytes, next_input_bytes)
        _, select_count = binding.debug_combat_select_body_hits(handle, 0, 64)
        _, filtered_count = binding.debug_combat_contacts_filtered(handle, 0, 64)
        return int(select_count), int(filtered_count)
    finally:
        binding.destroy(handle)


def _run_rollout_window(dataset_path: Path, start: int, stop: int):
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > stop, f"dataset too short for rollout lock: record={stop}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
        return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, field_bytes(start, seed_off, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(int(start), int(stop) + 1):
            binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    return samples["ref_t1"][stop], out


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=5088,
            attacker=1,
            defender=0,
            attacker_char=22,
            defender_char=1,
            attacker_action=69,
            plus_overlap_persists=False,
            negative_record_delta=2,
        ),
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.msl"
            ),
            target_record=3907,
            attacker=0,
            defender=1,
            attacker_char=1,
            defender_char=22,
            attacker_action=67,
            plus_overlap_persists=True,
            negative_record_delta=7,
        ),
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=9911,
            attacker=1,
            defender=0,
            attacker_char=1,
            defender_char=22,
            attacker_action=65,
            plus_overlap_persists=False,
            negative_record_delta=2,
        ),
    ],
)
def test_hitlag_freeze_keeps_rehit_suppression_on_followup_frame(case: FreezeRehitCase) -> None:
    # Replay-real short-rollout lock for hitlag-frozen hitbox ownership.
    #
    # Decomp ownership:
    # - Fighter_8006A360 freezes anim/script/collision callbacks while hitlag is active.
    # - ftAction_8007121C owns hitbox create/clear writes; those events do not re-run on frozen
    #   hitlag frames.
    # - lbColl_8000ACFC consumes the per-hitbox victims_1 ring to suppress same-window re-hits.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in (
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
        case.target_record + case.negative_record_delta,
    ):
        assert int(samples.shape[0]) > rec + 1, f"dataset too short for rollout lock: record={rec}"

    target = samples[case.target_record]
    assert int(target["seed_t"]["char_id"][case.attacker]) == case.attacker_char
    assert int(target["seed_t"]["char_id"][case.defender]) == case.defender_char
    assert int(target["seed_t"]["action_id"][case.attacker]) == case.attacker_action
    assert float(target["ref_t1"]["percent"][case.defender]) > float(target["seed_t"]["percent"][case.defender])

    minus_select, minus_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record - 1)
    target_select, target_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record)
    plus_select, plus_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record + 1)
    neg_select, neg_filtered = _precombat_body_counts_after_one_step(
        dataset_path, case.target_record + case.negative_record_delta
    )

    # target-1 control: the contact is still live before the first accepted hit.
    assert minus_select > 0
    assert minus_filtered >= minus_select

    # target: after the accepted hit, the next frozen-hitlag frame still has geometric overlap but
    # must not select a BODY hit again.
    assert target_filtered > 0
    assert target_select == 0

    # target+1 control: selection stays suppressed on the next replay frame even if geometric
    # overlap persists through hitlag.
    assert plus_select == 0
    if case.plus_overlap_persists:
        assert plus_filtered > 0
    else:
        assert plus_filtered == 0

    # explicit negative control: later row in the same local window has fully cleared.
    assert neg_select == 0
    assert neg_filtered == 0


@pytest.mark.integration
def test_hitlag_frozen_create_frame_materializes_authoritative_hitlist_seed_on_rollout() -> None:
    # Replay-real rollout lock for active-hitlag create-frame reseeds:
    # - BHH 1390 starts with Fox BAir hitboxes in active hitlag on the create-frame pose.
    # - Fighter_8006A360 freezes ftAction_8007121C during hitlag, so the create event must not
    #   clear the authoritative per-HitCapsule victims_1 seed.
    # - When hitlag exits at 1392, overlapping hitboxes must stay suppressed instead of re-hitting
    #   Falco and freezing DamageFlyN action_frame at 1.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008440}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    attacker = 0
    defender = 1
    start = 1390
    stop = 1395
    seed = samples["seed_t"][start]

    assert int(seed["action_id"][attacker]) == 67  # ftCo_MS_AttackAirB
    assert int(seed["action_frame"][attacker]) == 4
    assert int(seed["hitlag"][attacker]) > 0
    assert int(seed["action_id"][defender]) == 88  # ftCo_MS_DamageFlyN
    assert int(seed["hitlag"][defender]) > 0
    assert all(int(seed["combat_hitlist_hb_valid"][attacker][hb]) == 1 for hb in range(3))
    assert all(int(seed["combat_hitlist_hb_cd"][attacker][hb][defender]) == 0xFFFF for hb in range(3))

    ref, out = _run_rollout_window(dataset_path, start, stop)

    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), field
    assert float(out["pos_x"][defender]) == pytest.approx(float(ref["pos_x"][defender]), abs=2e-5)
    assert float(out["pos_y"][defender]) == pytest.approx(float(ref["pos_y"][defender]), abs=2e-5)


@pytest.mark.integration
def test_guardsetoff_hitlag_exit_materializes_authoritative_hitcapsule_seed_on_rollout() -> None:
    # Replay-real rollout lock for a shield-hit hitlag tail seeded mid-window:
    # - PPA 5355 starts with Fox UpAir and Falco GuardSetOff both in the last visible hitlag tail.
    # - Fighter_8006A360 skipped ftAction_8007121C while hitlag was active, so the existing
    #   HitCapsule victims_1 list must still suppress the first post-decrement collision pass.
    # - The explicit per-HitCapsule seed lane is the provenance. If that lane and dense fallback are
    #   removed in the mutation control, the shield re-hit is allowed, proving this is not a broad
    #   "if hitlag then suppress contact" rule.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    attacker = 0
    defender = 1
    start = 5355
    stop = 5363
    seed = samples["seed_t"][start]

    assert int(seed["action_id"][attacker]) == 68  # ftCo_MS_AttackAirHi
    assert int(seed["action_frame"][attacker]) == 11
    assert int(seed["hitlag"][attacker]) > 0
    assert int(seed["action_id"][defender]) == 181  # ftCo_MS_GuardSetOff
    assert int(seed["hitlag"][defender]) > 0
    assert all(int(seed["combat_hitlist_hb_valid"][attacker][hb]) == 1 for hb in range(3))
    assert all(int(seed["combat_hitlist_hb_cd"][attacker][hb][defender]) == 0xFFFF for hb in range(3))

    ref, out = _run_rollout_window(dataset_path, start, stop)
    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), field
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    raw = samples.view(np.uint8).reshape(len(samples), -1)
    seed_row = samples[start : start + 1].copy()
    seed_row["seed_t"]["combat_hitlist_cd"][0, attacker, :, defender] = np.uint16(0)
    seed_row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, :, defender] = np.uint16(0)
    seed_row["seed_t"]["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(0)
    seed_row["seed_t"]["combat_hitlist_hb_cd"][0, attacker, :, defender] = np.uint16(0)
    seed_row["seed_t"]["combat_hitlist_hb_victim_iid"][0, attacker, :, defender] = np.uint16(0)
    seed_bytes = np.frombuffer(seed_row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        return np.array(raw[record : record + 1, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(start, start + 2):
            binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        mutated = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    ref_5356 = samples["ref_t1"][start + 1]
    assert int(ref_5356["hitlag"][defender]) == 0
    assert int(mutated["hitlag"][defender]) > 0
    assert float(mutated["shield_hp"][defender]) < float(ref_5356["shield_hp"][defender])


@pytest.mark.integration
def test_hitlag_frozen_create_frame_carry_is_per_hitbox_not_attacker_wide() -> None:
    # Mixed-authority negative for the active-hitlag create-frame carry:
    # `combat_hitlist_hb_valid` is per HitCapsule. An authoritative seed on one slot may enable the
    # frozen create-frame owner, but it must not preserve a different slot's previous capsule unless
    # that exact slot is authoritative too.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008440}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[1390:1391].copy()
    attacker = 0
    defender = 1

    # hb0 is the slot under test: it has a previous-capsule seed and dense victims_1 provenance,
    # but its per-HitCapsule lane is not authoritative. hb1 is authoritative only to prove the
    # owner is enabled by another slot without becoming attacker-wide.
    row["seed_t"]["hitstun"][0, defender] = np.uint16(0)
    row["seed_t"]["combat_hitbox_prev_valid"][0, attacker, :] = np.uint8(0)
    row["seed_t"]["combat_hitbox_prev_valid"][0, attacker, 0] = np.uint8(1)
    row["seed_t"]["combat_hitlist_cd"][0, attacker, :, defender] = np.uint16(0)
    row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, :, defender] = np.uint16(0)
    row["seed_t"]["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(0xFFFF)
    row["seed_t"]["combat_hitlist_victim_iid"][0, attacker, 0, defender] = row["seed_t"]["instance_id"][
        0, defender
    ]
    row["seed_t"]["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(0)
    row["seed_t"]["combat_hitlist_hb_cd"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_hb_victim_iid"][0, attacker, :, :] = np.uint16(0)
    row["seed_t"]["combat_hitlist_hb_valid"][0, attacker, 1] = np.uint8(1)
    row["seed_t"]["combat_hitlist_hb_cd"][0, attacker, 1, defender] = np.uint16(0xFFFF)
    row["seed_t"]["combat_hitlist_hb_victim_iid"][0, attacker, 1, defender] = row["seed_t"]["instance_id"][
        0, defender
    ]

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        assert int(binding.debug_hitlist_fighter_contains(handle, 0, attacker, 0, defender)) == 0
    finally:
        binding.destroy(handle)
