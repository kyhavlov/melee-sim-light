from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _run_rollout_window_rows
from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_seed_damage_min: int
    expected_ref_action_frame: int
    expected_ref_state_flags_3: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1212,
            p=1,
            expected_seed_damage_min=9,
            expected_ref_action_frame=2,
            expected_ref_state_flags_3=0,
            note="AGN row A carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1477,
            p=0,
            expected_seed_damage_min=9,
            expected_ref_action_frame=3,
            expected_ref_state_flags_3=0,
            note="AGN row B carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2393,
            p=0,
            expected_seed_damage_min=1,
            expected_ref_action_frame=6,
            expected_ref_state_flags_3=32,
            note="AGN row C still carries the GuardSetOff entry hitlag-damage lower bound through powershield-active hitlag after the x221C_b1 lane is corrected",
        ),
    ],
)
def test_guardsetoff_hitlag_damage_seed_lane_locks_blockers_and_adjacent_controls(case: _Case) -> None:
    # Foundational F02 seed-owner locks:
    # - GuardSetOff entry rate in ftCo_80092F2C reads fp->x19A4, a hidden shield-hit int damage lane.
    # - Slippi does not expose x19A4 directly, so preprocessing carries a causal lower bound from
    #   the segment's entry hitlag, while the causal frame-speed derivation reconstructs the hidden
    #   timebase owner needed for action_frame parity on the hitlag-exit row.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        assert int(samples.shape[0]) > rec, f"dataset too short: record={rec}"
        seed = samples[rec]["seed_t"]
        assert int(seed["guard_setoff_hitlag_damage_min"][p]) == case.expected_seed_damage_min, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note
    assert int(out_target["action_frame"][p]) == int(ref_target["action_frame"][p]), case.note
    assert int(ref_target["state_flags"][p, 3]) == case.expected_ref_state_flags_3, case.note
    assert int(out_target["state_flags"][p, 3]) == int(ref_target["state_flags"][p, 3]), case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
def test_guardsetoff_damage_lane_seeds_post_hitlag_attackair_rehit_suppression() -> None:
    # Replay-real aggregate lock for the GuardSetOff damage lane's fighter-hitlist bridge:
    # - ftColl_80076CBC / ftColl_80076808 latch the accepted same-group shield victim in the
    #   HitCapsule, not just while the defender's visible hitlag scalar is nonzero.
    # - A reseed on the first post-hitlag GuardSetOff row still needs that hidden victims_1 carry
    #   to prevent the active AttackAirN hitbox from immediately re-entering shield hitlag.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 10271
    defender = 0
    attacker = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][defender]) == 181  # GuardSetOff
    assert int(seed["hitlag"][defender]) == 0
    assert int(seed["guard_setoff_hitlag_damage_min"][defender]) == 9
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed["hitlag"][attacker]) == 0

    _, ref_attacker, out_attacker = _run_one_step_row(dataset_path, record, attacker)
    assert int(ref_attacker["hitlag"][attacker]) == 0
    assert int(out_attacker["hitlag"][attacker]) == 0
    assert int(out_attacker["state_flags"][attacker, 1]) == int(ref_attacker["state_flags"][attacker, 1])


@pytest.mark.integration
def test_guardsetoff_x19a4_seed_does_not_rewrite_attacker_x1924_hitlag() -> None:
    # SnarlingHelplessBeaver:5072 exposes the split source lanes in ftColl_80076CBC:
    # - defender GuardSetOff consumes the replay-recovered x19A4 lower bound,
    # - attacker hitlag consumes its own dmg.x1924 value from the live accepted HitCapsule packet.
    # Treating the defender seed as a shared shield-hit scalar leaves Fox's AttackS3 attacker hitlag
    # one frame short while Marth's GuardSetOff lanes are already correct.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/sheik/replays/validation/sheik/SnarlingHelplessBeaver.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 5072
    defender = 0
    attacker = 1

    seed, ref, out = _run_one_step_row(dataset_path, record, defender)
    assert int(seed["combat_shield_hit_int_damage"][defender]) == 6
    assert int(seed["combat_shield_damage_taken"][defender]) == 8
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 5
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))

    def raise_defender_x19a4(seed_t) -> None:
        seed_t["combat_shield_hit_int_damage"][0, defender] = 12

    _, _ref_mut, out_mut = _run_one_step_row(
        dataset_path, record, defender, seed_mutator=raise_defender_x19a4
    )
    assert int(out_mut["hitlag"][defender]) > int(out["hitlag"][defender])
    assert int(out_mut["hitlag"][attacker]) == int(out["hitlag"][attacker]) == 6


@pytest.mark.integration
def test_guardsetoff_carried_shield_packet_overrides_stale_visible_powershield_bit() -> None:
    # AttachedGoodNaturedGuanaco:2395 exposes a GuardSetOff source packet after the visible action
    # has advanced through the GuardSetOff callback boundary. The post-frame fp+0x221C_b2 bit is
    # still visible, but nonzero x19A4/x19A0 proves ftColl_80076CBC accumulated a shield-hit packet
    # that Fighter_ProcessHit consumes on this callback.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80092F2C}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 2395
    defender = 0
    attacker = 1

    seed, ref, out = _run_one_step_row(dataset_path, record, defender)
    assert int(seed["action_id"][defender]) == 181  # GuardSetOff seed snapshot.
    assert int(seed["seed_prev_action_id"][defender]) == 181
    assert int(seed["combat_shield_hit_int_damage"][defender]) == 1
    assert int(seed["combat_shield_damage_taken"][defender]) == 2
    assert int(seed["state_flags"][defender, 3]) & 0x20
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 3
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))

    def clear_x19a0(seed_t: np.ndarray) -> None:
        seed_t["combat_shield_damage_taken"][0, defender] = np.uint8(0)

    _, _ref_no_x19a0, out_no_x19a0 = _run_one_step_row(
        dataset_path, record, defender, seed_mutator=clear_x19a0
    )
    assert int(out_no_x19a0["hitlag"][defender]) == 3
    assert float(out_no_x19a0["shield_hp"][defender]) == pytest.approx(
        float(seed["shield_hp"][defender])
    )

    def clear_x19a4(seed_t: np.ndarray) -> None:
        seed_t["combat_shield_hit_int_damage"][0, defender] = np.uint8(0)

    _, _ref_no_x19a4, out_no_x19a4 = _run_one_step_row(
        dataset_path, record, defender, seed_mutator=clear_x19a4
    )
    assert int(out_no_x19a4["hitlag"][defender]) != int(out["hitlag"][defender])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "target_record", "defender", "attacker"),
    [
        ("datasets/sheik/replays/validation/sheik/TenseSameHummingbird.msl", 9542, 9551, 0, 1),
        ("datasets/sheik/replays/validation/sheik/UselessGlassLoris.msl", 757, 766, 1, 0),
    ],
)
def test_guardon_zero_shield_damage_lower_bound_x19a4_uses_x19a0_rollout_rate(
    dataset_rel: str, start_record: int, target_record: int, defender: int, attacker: int
) -> None:
    # No-submotion GuardOn shield-hit rows can expose only a hitlag-derived x19A4 lower bound:
    # combat_shield_hit_int_damage=6 and combat_shield_damage_taken=8 both produce 5f hitlag, but
    # ftCo_80092F2C's GuardDamage animation rate distinguishes them after hitlag. For
    # replay-proven ShieldDesc contacts whose hitbox shield damage is zero, x19A0 is the tighter
    # HitCapsule.damage int payload and must drive GuardSetOff shieldstun duration.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007ABD0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[start_record]["seed_t"]
    assert int(seed_t["action_id"][defender]) == 178  # GuardOn no-submotion.
    assert int(seed_t["action_frame"][defender]) < 0
    assert int(seed_t["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed_t["combat_shield_hit_int_damage"][defender]) == 6
    assert int(seed_t["combat_shield_damage_taken"][defender]) == 8
    assert any(
        int(seed_t["combat_shield_contact_hb_kind"][attacker, hb, defender]) == 2
        for hb in range(4)
    )

    rows = _run_rollout_window_rows(
        dataset_path,
        start_record=start_record,
        window_records=(target_record, target_record + 1),
    )
    ref_target, out_target = rows[target_record]
    assert int(out_target["action_id"][defender]) == int(ref_target["action_id"][defender]) == 181
    assert int(out_target["action_frame"][defender]) == int(ref_target["action_frame"][defender])
    assert int(out_target["hitlag"][defender]) == int(ref_target["hitlag"][defender]) == 0

    ref_next, out_next = rows[target_record + 1]
    assert int(out_next["action_id"][defender]) == int(ref_next["action_id"][defender]) == 179


def _run_one_step_timebase_with_seed_mutator(dataset_path: Path, record: int, seed_mutator):
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_t = samples[record : record + 1]["seed_t"].copy()
    seed_mutator(seed_t)
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(samples[record : record + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_bytes = (
        np.frombuffer(samples[record : record + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        timebase = binding.debug_timebase(handle, 0).copy()
        return samples[record]["ref_t1"].copy(), out, timebase
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardon_zero_shield_damage_x19a0_rate_requires_shielddesc_provenance() -> None:
    # Adjacent negative: x19A0>x19A4 is not enough to rewrite GuardSetOff rate. Without the
    # replay-proven per-hitbox ShieldDesc contact kind, keep the x19A4 lower-bound path and do not
    # let a synthetic larger x19A0 become the hidden GuardDamage rate owner.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/sheik/replays/validation/sheik/TenseSameHummingbird.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 9542
    defender = 0
    attacker = 1

    def remove_contact_provenance(seed_t: np.ndarray) -> None:
        seed_t["combat_shield_contact_hb_kind"][0, attacker, :, defender] = 0
        seed_t["combat_shield_damage_taken"][0, defender] = 12

    _ref, out, timebase = _run_one_step_timebase_with_seed_mutator(
        dataset_path, record, remove_contact_provenance
    )
    assert int(out["action_id"][defender]) != 181
    assert float(timebase[defender, 4]) != pytest.approx(3.589279, abs=1e-5)
