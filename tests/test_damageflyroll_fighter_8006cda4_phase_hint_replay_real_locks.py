from __future__ import annotations

import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_pre_combat_debug_row,
    _run_rollout_window_rows_with_trace,
    _assert_transition_identity_lock_fields_match_ref,
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def test_attackairb_damageflytop_runtime_predicate_matches_extracted_source_data() -> None:
    root = Path(__file__).resolve().parents[1]
    for char_name in ("fox", "falco"):
        moves = json.loads((root / "data/moves" / f"{char_name}.json").read_text(encoding="utf-8"))
        bair_events = moves["moves"]["ftCo_SM_AttackAirB"]["events"]
        creates = [ev for ev in bair_events if ev["kind"] == "create_hitbox"]
        strong = [
            (int(ev["frame"]), int(ev["data"]["hitbox"]["hitbox_id"]))
            for ev in creates
            if float(ev["data"]["hitbox"]["damage"]) == pytest.approx(15.0)
        ]
        weak = [
            (int(ev["frame"]), int(ev["data"]["hitbox"]["hitbox_id"]))
            for ev in creates
            if float(ev["data"]["hitbox"]["damage"]) == pytest.approx(9.0)
        ]
        assert strong == [(4, 0), (4, 1)]
        assert weak == [(4, 2), (8, 0), (8, 1), (8, 2)]

        hurtcaps = json.loads(
            (root / "data/hurtcaps" / f"{char_name}.json").read_text(encoding="utf-8")
        )
        cap2 = hurtcaps["capsules"][2]
        cap12 = hurtcaps["capsules"][12]
        assert int(cap2["height"]) == 2
        assert int(cap12["height"]) == 1
        assert float(cap12["scale"]) == pytest.approx(1.62, abs=1e-6)

        nair_events = moves["moves"]["ftCo_SM_AttackAirN"]["events"]
        nair_creates = [ev for ev in nair_events if ev["kind"] == "create_hitbox"]
        nair_strong = [
            (int(ev["frame"]), int(ev["data"]["hitbox"]["hitbox_id"]))
            for ev in nair_creates
            if float(ev["data"]["hitbox"]["damage"]) == pytest.approx(12.0)
        ]
        assert nair_strong == [(4, 0), (4, 1), (4, 2)]

        fair_events = moves["moves"]["ftCo_SM_AttackAirF"]["events"]
        fair_creates = [ev for ev in fair_events if ev["kind"] == "create_hitbox"]
        fair_mid = [
            (
                int(ev["frame"]),
                int(ev["data"]["hitbox"]["hitbox_id"]),
                int(ev["data"]["hitbox"]["damage"]),
                int(ev["data"]["hitbox"]["angle"]),
                int(ev["data"]["hitbox"]["kbg"]),
                int(ev["data"]["hitbox"]["bkb"]),
            )
            for ev in fair_creates
            if int(ev["data"]["hitbox"]["damage"]) in {8, 9}
        ]
        expected_fair_mid = [(6, 0, 9, 361, 100, 10), (6, 1, 9, 361, 100, 10)]
        if char_name == "fox":
            expected_fair_mid = []
        else:
            expected_fair_mid.extend([(16, 0, 8, 361, 100, 10), (16, 1, 8, 361, 100, 10)])
        assert fair_mid == expected_fair_mid

        dair_events = moves["moves"]["ftCo_SM_AttackAirLw"]["events"]
        dair_creates = [ev for ev in dair_events if ev["kind"] == "create_hitbox"]
        dair_meteor = [
            (
                int(ev["frame"]),
                int(ev["data"]["hitbox"]["hitbox_id"]),
                int(ev["data"]["hitbox"]["damage"]),
                int(ev["data"]["hitbox"]["angle"]),
                int(ev["data"]["hitbox"]["kbg"]),
                int(ev["data"]["hitbox"]["bkb"]),
            )
            for ev in dair_creates
            if int(ev["data"]["hitbox"]["damage"]) in {9, 12}
        ]
        expected_dair_meteor = []
        if char_name == "falco":
            expected_dair_meteor = [
                (5, 0, 12, 290, 100, 10),
                (5, 1, 12, 290, 100, 10),
                (15, 0, 9, 290, 100, 20),
                (15, 1, 9, 290, 100, 20),
            ]
        assert dair_meteor == expected_dair_meteor

        dsmash_events = moves["moves"]["ftCo_SM_AttackLw4"]["events"]
        dsmash_creates = [ev for ev in dsmash_events if ev["kind"] == "create_hitbox"]
        dsmash_strong = [
            (
                int(ev["frame"]),
                int(ev["data"]["hitbox"]["hitbox_id"]),
                int(ev["data"]["hitbox"]["damage"]),
                int(ev["data"]["hitbox"]["angle"]),
                int(ev["data"]["hitbox"]["kbg"]),
                int(ev["data"]["hitbox"]["bkb"]),
            )
            for ev in dsmash_creates
            if int(ev["data"]["hitbox"]["hitbox_id"]) in {0, 1}
        ]
        expected_dsmash_strong = [(6, 0, 15, 25, 65, 20), (6, 1, 15, 25, 65, 20)]
        if char_name == "falco":
            expected_dsmash_strong = [(6, 0, 16, 25, 70, 20), (6, 1, 16, 25, 70, 20)]
        assert dsmash_strong == expected_dsmash_strong


def _trace_site_count(trace_path: Path, *, start_record: int, record: int, site_id: int) -> int:
    total = 0
    with trace_path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        for row in reader:
            if int(row["site_id"]) != int(site_id):
                continue
            if start_record + int(row["step"]) != int(record):
                continue
            total += int(row["call_count"])
    return total


def _selected_body_hitbox_hurtcap(
    dataset_path: Path, record: int, attacker: int, defender: int
) -> tuple[int, int]:
    import numpy as np

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    selected_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("defender", "u1"),
            ("hitbox_id", "u1"),
            ("hurtcap_id", "u1"),
            ("attacker_msid", "<u2"),
            ("attacker_action_frame", "<i2"),
            ("hitbox_x", "<f4"),
            ("hitbox_y", "<f4"),
            ("hitbox_z", "<f4"),
            ("hitbox_radius", "<f4"),
            ("hitbox_damage", "<f4"),
            ("hurtcap_ax", "<f4"),
            ("hurtcap_ay", "<f4"),
            ("hurtcap_az", "<f4"),
            ("hurtcap_bx", "<f4"),
            ("hurtcap_by", "<f4"),
            ("hurtcap_bz", "<f4"),
            ("hurtcap_radius", "<f4"),
        ],
        align=False,
    )
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw, count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)
    assert int(raw.shape[1]) == int(selected_dtype.itemsize)
    selected = raw.reshape(-1).view(selected_dtype)[:count]
    for row_selected in selected:
        if int(row_selected["attacker"]) == attacker and int(row_selected["defender"]) == defender:
            return int(row_selected["hitbox_id"]), int(row_selected["hurtcap_id"])
    raise AssertionError(
        f"record={record} attacker={attacker} defender={defender} has no selected BODY hit"
    )


@dataclass(frozen=True)
class _DamageFlyRoll8006CDA4Case:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_seed_count: int
    expect_action_id: int
    note: str
    expect_hb0_enable_edge: int | None = None


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2694,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackAirB carry now lands DamageFlyRoll (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=5717,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume grounded ThrownF hitlag carry now lands DamageFlyRoll (GAT)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6020,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="DamageFlyTop <- AttackAirB positive control remains DamageFlyRoll without early create-window carry (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6929,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume DamageFlyTop <- AttackAirB carry now lands DamageFlyRoll (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=11134,
            victim_port=1,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume LandingAirLw carry now lands DamageFlyRoll (GAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=5391,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackLw3 carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=1338,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume Fox SpecialLwEnd carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=2933,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=87,
            note="SpecialLwEnd entry control does not consume before the DamageFlyHi branch (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            target_record=11154,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=87,
            note="late LandingAirLw double-consume carry now lands DamageFlyHi (IAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=6002,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="LandingAirLw entry double-consume carry now lands DamageFlyRoll (PPA aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4065,
            victim_port=0,
            expect_seed_count=4,
            expect_action_id=91,
            note="AttackHi4 carry uses explicit zero-consume DamageFlyRoll phase (BHH aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=2105,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="strong AttackAirB root BODY x14 DamageFlyTop carry reaches DamageFlyRoll (BHH)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=6057,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=88,
            note="Catch-shaped late AttackAirN carry consumes Fighter_8006CDA4 prefix without rolling (BHH)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=4024,
            victim_port=0,
            expect_seed_count=3,
            expect_action_id=91,
            note="early AttackAirB create-edge full Fighter_8006CDA4 path lands DamageFlyRoll (PPA aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=1033,
            victim_port=0,
            expect_seed_count=4,
            expect_action_id=91,
            note="early AttackAirB DamageFlyTop uses explicit zero-consume frame-start roll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2635,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=87,
            note="early AttackAirB DamageFlyTop consumes once to avoid false DamageFlyRoll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6380,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="early AttackAirB DamageFlyTop consumes twice to reach DamageFlyRoll",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7875,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="late victim DamageFlyTop still uses early AttackAirB two-consume stream phase",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7185,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="SpecialHiFall <- AttackAirB enable-edge admits DamageFlyRoll (PPA aggregate)",
            expect_hb0_enable_edge=1,
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/ThisVioletRaccoon.msl",
            target_record=6927,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="SpecialHiFall entry-window AttackAirB enable-edge stays terminal-owned (TVR aggregate)",
            expect_hb0_enable_edge=1,
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            target_record=1718,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=87,
            note="late SpecialHiFall steady AttackAirB contact stays seed-owned (MVP aggregate)",
            expect_hb0_enable_edge=1,
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2367,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="AttackAirN pre-action carries double Fighter_8006CDA4 stream phase (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2752,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="active-hitlag DamageFlyN <- ThrowHi state1 laser reaches DamageFlyRoll gate (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            target_record=10207,
            victim_port=0,
            expect_seed_count=3,
            expect_action_id=91,
            note="AttackAirN pre-action carries triple Fighter_8006CDA4 stream phase (PRH)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            target_record=4968,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="AttackAirN pre-action follows same-frame reciprocal gate in the global RNG stream (DCC)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=7019,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=88,
            note="SpecialHiFall <- steady AttackAirB contact does not admit DamageFlyRoll (PPA aggregate)",
            expect_hb0_enable_edge=0,
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            target_record=10049,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="JumpF victim uses live late AttackHi4 hb0/cap12 owner despite stale source-motion residue (STM)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=2542,
            victim_port=1,
            expect_seed_count=4,
            expect_action_id=91,
            note="grounded Dash severe-airborne entry uses explicit zero-consume DamageFlyRoll phase (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=2706,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="grounded AttackHi3 severe-airborne entry carries double Fighter_8006CDA4 phase (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=3137,
            victim_port=0,
            expect_seed_count=4,
            expect_action_id=91,
            note="grounded AttackDash severe-airborne entry uses explicit zero-consume DamageFlyRoll phase (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=157,
            victim_port=1,
            expect_seed_count=0,
            expect_action_id=90,
            note="grounded Dash without explicit phase remains DamageFlyTop (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=4669,
            victim_port=1,
            expect_seed_count=4,
            expect_action_id=91,
            note="grounded KneeBend severe-airborne entry uses explicit zero-consume DamageFlyRoll phase (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.msl",
            target_record=4446,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=90,
            note="grounded KneeBend without explicit phase remains DamageFlyTop (EWT FoD)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl",
            target_record=5041,
            victim_port=0,
            expect_seed_count=4,
            expect_action_id=91,
            note="grounded KneeBend zero-consume DamageFlyRoll phase generalizes across FoD replays (MGS)",
        ),
    ],
)
def test_fighter_8006cda4_pre_gate_consume_count_replay_real_locks(
    case: _DamageFlyRoll8006CDA4Case,
) -> None:
    # Replay-real seed and one-step locks for the explicit Fighter_8006CDA4 pre-gate stream-phase
    # owner.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[int(case.target_record)]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    victim = int(case.victim_port)

    assert (
        int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == int(case.expect_seed_count)
    ), case.note

    if int(case.expect_seed_count) == 1 and int(seed["action_id"][victim]) != 90:
        assert int(seed["action_id"][victim]) in {57, 65, 67, 74, 363}, case.note
        assert int(seed["hitlag"][victim]) == 0, case.note
    if int(case.expect_seed_count) == 1 and int(seed["action_id"][victim]) == 90:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) == 4, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 239:
        assert int(seed["action_id"][victim]) == 239, case.note  # ThrownF
        assert int(seed["hitlag"][victim]) > 0, case.note
        assert (int(seed["state_flags"][victim, 1]) & 0x10) != 0, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 90:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 90, case.note  # DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) >= 3, case.note
    if int(case.expect_seed_count) == 3:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) in {
            24,  # KneeBend
            65,  # AttackAirN
            90,  # DamageFlyTop
        }, case.note
        assert int(seed["hitlag"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert int(seed["hitstun"][victim]) > 0, case.note
        else:
            assert int(seed["hitstun"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 24:
            assert int(seed["on_ground"][victim]) == 1, case.note
        else:
            assert int(seed["on_ground"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert attacker in (0, 1), case.note
            assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
            assert int(seed["action_frame"][attacker]) == 3, case.note
    if int(case.expect_seed_count) == 4:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) in {
            20,  # Dash
            24,  # KneeBend
            50,  # AttackDash
            63,  # AttackHi4
            65,  # AttackAirN
            90,  # DamageFlyTop
        }, case.note
        assert int(seed["hitlag"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert int(seed["hitstun"][victim]) > 0, case.note
        else:
            assert int(seed["hitstun"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) in {20, 24, 50, 63}:
            assert int(seed["on_ground"][victim]) == 1, case.note
        else:
            assert int(seed["on_ground"][victim]) == 0, case.note
        if int(seed["action_id"][victim]) == 90:
            assert attacker in (0, 1), case.note
    if int(case.expect_seed_count) in {2, 3} and int(seed["action_id"][victim]) == 65:
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) == 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
    if case.expect_hb0_enable_edge is not None:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 358, case.note  # Fox SpecialHiFall
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        _, _, _, timing = _run_pre_combat_debug_row(dataset_path, int(case.target_record), attacker, 0)
        assert int(timing["enable_edge"]) == int(case.expect_hb0_enable_edge), case.note

    if "BlondHardHippopotamus.msl" in case.dataset_rel and int(case.target_record) == 6057:
        hb_id, cap_id = _selected_body_hitbox_hurtcap(dataset_path, int(case.target_record), 1, 0)
        assert (hb_id, cap_id) == (0, 1), case.note

    if "SweatyThisMallard.msl" in case.dataset_rel and int(case.target_record) == 3998:
        hb_id, cap_id = _selected_body_hitbox_hurtcap(dataset_path, int(case.target_record), 1, 0)
        assert (hb_id, cap_id) == (1, 2), case.note

    if "SweatyThisMallard.msl" in case.dataset_rel and int(case.target_record) == 10049:
        hb_id, cap_id = _selected_body_hitbox_hurtcap(dataset_path, int(case.target_record), 1, 0)
        assert (hb_id, cap_id) == (0, 12), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, int(case.target_record), victim)
    assert int(out_row["action_id"][victim]) == int(case.expect_action_id), case.note
    if int(case.expect_action_id) == int(ref["action_id"][victim]):
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=int(case.target_record),
                p=p,
            )


@pytest.mark.integration
def test_stm_attacklw4_strong_attackairn_zero_marker_is_selected_source_owned() -> None:
    # STM rec3998 starts with a grounded AttackLw4 victim and no seeded Fighter_8006CDA4 prefix.
    # ftColl selects the current authored strong NAir hb1 HitCapsule against cap2/head-high; after
    # Fighter_ProcessHit launches the victim airborne, ftCo_8008DCE0 reaches the severe-airborne
    # DamageFlyRoll gate. Clearing the replay seed lane must still materialize only the
    # zero-consume marker: site 1 runs, while ftColl effect and Fighter_8006CDA4 prefix sites do not.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 3998
    victim = 0
    attacker = 1
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 64  # AttackLw4.
    assert int(seed["on_ground"][victim]) == 1
    assert int(seed["hitlag"][victim]) == 0
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 4
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN.
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (1, 2)
    assert int(ref["action_id"][victim]) == 91  # DamageFlyRoll.

    def clear_seed_count(seed_t):
        seed_t["fighter_8006cda4_pre_gate_consume_count"][0, victim] = 0

    trace_path = root / "reports/triage/stm3998_attacklw4_nair_zero_marker.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        seed_mutator=clear_seed_count,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, site1_count = rows[record]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=5) == 0
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=6) == 0
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=7) == 0
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=24) == 0
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )


@pytest.mark.integration
def test_tvr_attackhi4_weak_attackairb_selected_contact_owns_damageflyroll_prefix() -> None:
    # TVR rec11016 has p0 grounded AttackHi4 struck by p1 weak BackAir. The selected BODY contact
    # is the authored weak BAir hb1 against hurtcap slot 3; that concrete DmgLog source owns the
    # normal-hit effect prefix plus five Fighter_8006CDA4 primary consumes before
    # ftCo_8008DCE0's DamageFlyRoll gate. The neighboring rec11015 row has the same actions before
    # contact and must not pre-admit damage.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    victim = 0
    attacker = 1
    record = 11016
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 63  # AttackHi4.
    assert int(seed["on_ground"][victim]) == 1
    assert int(seed["hitlag"][victim]) == 0
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB.
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (1, 3)
    assert int(ref["action_id"][victim]) == 91  # DamageFlyRoll.

    combat_c = (root / "src/combat.c").read_text(encoding="utf-8")
    helper = re.search(
        r"static inline uint8_t combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner"
        r"\([\s\S]*?\n}\n\nstatic inline void "
        r"combat_damageflyroll_consume_attackhi4_weak_attackairb_count",
        combat_c,
    )
    assert helper is not None
    helper_body = helper.group(0)
    assert "(void)source_cap_i" not in helper_body
    assert "(void)source_cap_valid" not in helper_body
    assert "source_cap_valid == 0u" in helper_body
    assert "cap_id != 3u" in helper_body

    trace_path = root / "reports/triage/tvr11016_attackhi4_weak_bair_damageflyroll.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10893,
        window_records=(11015, 11016),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_prev, out_prev, prev_site1 = rows[11015]
    assert prev_site1 == 0
    assert int(out_prev["action_id"][victim]) == int(ref_prev["action_id"][victim]) == 63

    ref_row, out_row, site1_count = rows[record]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=10893, record=record, site_id=24) == 4
    assert _trace_site_count(trace_path, start_record=10893, record=record, site_id=5) == 5
    assert _trace_site_count(trace_path, start_record=10893, record=record, site_id=6) == 0
    assert _trace_site_count(trace_path, start_record=10893, record=record, site_id=7) == 0
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )


@pytest.mark.integration
def test_fsp_speciallwend_strong_attackairb_runtime_prefix_is_selected_source_owned() -> None:
    # FSP rec1338 is a grounded Reflector-end victim struck by the selected strong BackAir hb0
    # HitCapsule against cap2/head-high. Clearing the explicit one-step seed lane still reaches
    # DamageFlyRoll because the current ProcessHit source owns one Fighter_8006CDA4 primary
    # pre-gate consume. FSP rec2627 is the already-live weak hb2/cap2 sibling and owns the normal
    # ftColl effect prefix plus all three Fighter_8006CDA4 callsites. FSP rec2933 keeps the same
    # broad SpecialLwEnd/AttackAirB action shape, but selected create-edge weak hb2/cap2 does not
    # own that continuing-HitCapsule prefix and remains DamageFlyHi.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwEnd_Anim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    victim = 0
    attacker = 1
    strong_record = 1338
    continuing_weak_record = 2627
    weak_record = 2933
    strong_seed = ds.samples[strong_record]["seed_t"]
    continuing_weak_seed = ds.samples[continuing_weak_record]["seed_t"]
    weak_seed = ds.samples[weak_record]["seed_t"]
    assert int(strong_seed["action_id"][victim]) == 363  # SpecialLwEnd.
    assert int(strong_seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 1
    assert _selected_body_hitbox_hurtcap(dataset_path, strong_record, attacker, victim) == (0, 2)
    assert int(ds.samples[strong_record]["ref_t1"]["action_id"][victim]) == 91
    assert int(continuing_weak_seed["action_id"][victim]) == 363
    assert int(continuing_weak_seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 1
    assert _selected_body_hitbox_hurtcap(dataset_path, continuing_weak_record, attacker, victim) == (
        2,
        2,
    )
    assert int(ds.samples[continuing_weak_record]["ref_t1"]["action_id"][victim]) == 91
    assert int(weak_seed["action_id"][victim]) == 363
    assert int(weak_seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert _selected_body_hitbox_hurtcap(dataset_path, weak_record, attacker, victim) == (2, 2)
    assert int(ds.samples[weak_record]["ref_t1"]["action_id"][victim]) == 87

    def clear_seed_count(seed_t):
      seed_t["fighter_8006cda4_pre_gate_consume_count"][0, victim] = 0

    strong_trace = root / "reports/triage/fsp1338_speciallwend_bair_runtime_prefix.tsv"
    strong_rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=strong_record,
        window_records=(strong_record,),
        rng_damage_fly_roll_gate=True,
        trace_path=strong_trace,
        seed_mutator=clear_seed_count,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, site1_count = strong_rows[strong_record]
    assert site1_count == 1
    assert _trace_site_count(strong_trace, start_record=strong_record, record=strong_record, site_id=5) == 1
    assert _trace_site_count(strong_trace, start_record=strong_record, record=strong_record, site_id=6) == 0
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=strong_record,
            p=p,
        )

    continuing_weak_trace = root / "reports/triage/fsp2627_speciallwend_continuing_weak_bair.tsv"
    continuing_weak_rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=continuing_weak_record,
        window_records=(continuing_weak_record,),
        rng_damage_fly_roll_gate=True,
        trace_path=continuing_weak_trace,
        seed_mutator=clear_seed_count,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, site1_count = continuing_weak_rows[continuing_weak_record]
    assert site1_count == 1
    assert (
        _trace_site_count(
            continuing_weak_trace,
            start_record=continuing_weak_record,
            record=continuing_weak_record,
            site_id=24,
        )
        == 4
    )
    assert (
        _trace_site_count(
            continuing_weak_trace,
            start_record=continuing_weak_record,
            record=continuing_weak_record,
            site_id=5,
        )
        == 1
    )
    assert (
        _trace_site_count(
            continuing_weak_trace,
            start_record=continuing_weak_record,
            record=continuing_weak_record,
            site_id=6,
        )
        == 1
    )
    assert (
        _trace_site_count(
            continuing_weak_trace,
            start_record=continuing_weak_record,
            record=continuing_weak_record,
            site_id=7,
        )
        == 1
    )
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=continuing_weak_record,
            p=p,
        )

    weak_trace = root / "reports/triage/fsp2933_speciallwend_weak_bair_runtime_negative.tsv"
    weak_rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=weak_record,
        window_records=(weak_record,),
        rng_damage_fly_roll_gate=True,
        trace_path=weak_trace,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, site1_count = weak_rows[weak_record]
    assert site1_count == 1
    assert _trace_site_count(weak_trace, start_record=weak_record, record=weak_record, site_id=24) == 0
    assert _trace_site_count(weak_trace, start_record=weak_record, record=weak_record, site_id=5) == 0
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=weak_record,
            p=p,
        )


@pytest.mark.integration
def test_fsp_attacklw3_late_attackhi4_runtime_prefix_is_selected_source_owned() -> None:
    # FSP rec5391 has a grounded AttackLw3 victim struck by the selected late AttackHi4 hb0/cap2
    # source. Clearing the explicit one-step seed lane still reaches DamageFlyRoll because that
    # concrete DmgLog source owns one Fighter_8006CDA4 primary pre-gate consume. Mutating the
    # victim out of AttackLw3 keeps the same hit source but removes the owner.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 5391
    victim = 0
    attacker = 1
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][victim]) == 57  # AttackLw3.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 1
    assert int(seed["action_id"][attacker]) == 63  # AttackHi4.
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (0, 2)
    assert int(ds.samples[record]["ref_t1"]["action_id"][victim]) == 91

    def clear_seed_count(seed_t):
        seed_t["fighter_8006cda4_pre_gate_consume_count"][0, victim] = 0

    trace_path = root / "reports/triage/fsp5391_attacklw3_attackhi4_runtime_prefix.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        seed_mutator=clear_seed_count,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, site1_count = rows[record]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=5) == 1
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=6) == 0
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )

    def mutate_victim_action(seed_t):
        clear_seed_count(seed_t)
        seed_t["action_id"][0, victim] = 20  # Dash: grounded but not AttackLw3 source state.

    _, _, mutated_out = _run_one_step_row(dataset_path, record, victim, seed_mutator=mutate_victim_action)
    assert int(mutated_out["action_id"][victim]) != 91


@pytest.mark.integration
def test_bhh_kneebend_attacks3_runtime_prefix_reaches_damageflyroll_gate() -> None:
    # BHH rec5889 is not an explicit seed-count row: the source owner is the selected AttackS3 BODY
    # HitCapsule against a victim that has already entered KneeBend before ProcessHit resolves.
    # Runtime therefore owns the same-frame ftColl effect prefix plus the bounded Fighter_8006CDA4
    # pre-gate prefix before ftCo_8008DCE0 samples DamageFlyRoll.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 5889
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][0]) == 39  # Squat.
    assert int(seed["action_id"][1]) == 53  # AttackS3.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 0
    assert _selected_body_hitbox_hurtcap(dataset_path, record, 1, 0) == (0, 0)

    trace_path = root / "reports/triage/bhh5889_kneebend_attacks3_runtime_prefix.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=5860,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, site1_count = rows[record]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=5860, record=record, site_id=24) == 4
    assert _trace_site_count(trace_path, start_record=5860, record=record, site_id=5) == 5
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 91
    assert int(out_row["hitlag"][0]) == int(ref_row["hitlag"][0])
    assert int(out_row["hitstun"][0]) == int(ref_row["hitstun"][0])


@dataclass(frozen=True)
class _IATDamageFlyRollRuntimeOwnerCase:
    owner: str
    record: int
    victim: int
    attacker: int
    victim_action: int
    attacker_action: int
    selected_hb: int
    selected_cap: int
    site5_count: int
    site24_count: int
    source_action_negative: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="FallSpecial/AttackAirF",
            record=2347,
            victim=0,
            attacker=1,
            victim_action=35,
            attacker_action=66,
            selected_hb=0,
            selected_cap=2,
            site5_count=1,
            site24_count=4,
            source_action_negative=65,
            note="mid ForwardAir hb0 against cap2/head-high owns one Fighter_8006CDA4 primary",
        ),
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="Jump/AttackAirN",
            record=4194,
            victim=1,
            attacker=0,
            victim_action=25,
            attacker_action=65,
            selected_hb=1,
            selected_cap=0,
            site5_count=7,
            site24_count=4,
            source_action_negative=66,
            note="early JumpF strong NAir hb1 against cap0/root-body owns seven primaries",
        ),
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="SpecialAirHi/AttackLw4",
            record=7632,
            victim=0,
            attacker=1,
            victim_action=356,
            attacker_action=64,
            selected_hb=1,
            selected_cap=0,
            site5_count=2,
            site24_count=0,
            source_action_negative=65,
            note="SpecialAirHi struck by strong down-smash hb1/cap0 owns two primaries",
        ),
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="SpecialAirN/AttackAirLw",
            record=8874,
            victim=1,
            attacker=0,
            victim_action=345,
            attacker_action=69,
            selected_hb=0,
            selected_cap=6,
            site5_count=0,
            site24_count=4,
            source_action_negative=65,
            note="SpecialAirNLoop struck by strong DAir meteor hb0/cap6 admits the gate via effect prefix",
        ),
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="Catch/AttackAirF",
            record=10734,
            victim=0,
            attacker=1,
            victim_action=212,
            attacker_action=66,
            selected_hb=0,
            selected_cap=0,
            site5_count=3,
            site24_count=4,
            source_action_negative=65,
            note="grounded Catch struck by mid ForwardAir hb0/cap0 owns three primaries",
        ),
        _IATDamageFlyRollRuntimeOwnerCase(
            owner="KneeBend/weak AttackAirB",
            record=10974,
            victim=0,
            attacker=1,
            victim_action=24,
            attacker_action=67,
            selected_hb=1,
            selected_cap=1,
            site5_count=3,
            site24_count=4,
            source_action_negative=65,
            note="KneeBend struck by weak BackAir hb1/cap1 owns three primaries",
        ),
    ],
)
def test_iat_damageflyroll_runtime_owners_are_selected_source_owned(
    case: _IATDamageFlyRollRuntimeOwnerCase,
) -> None:
    # Each retained IAT runtime owner is locked on the concrete selected DmgLog source:
    # hitbox payload/source motion from extracted move data, selected BODY hurtcap provenance from
    # ftColl_80076ED8, and the named RNG site ledger count. Mutating the attacker action keeps the
    # visible victim row but removes the selected source-motion owner, proving this is not a
    # replay-row or action-shape bridge.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[case.record]["seed_t"]
    assert int(seed["action_id"][case.victim]) == int(case.victim_action), case.owner
    assert int(seed["action_id"][case.attacker]) == int(case.attacker_action), case.owner
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][case.victim]) == 0, case.owner
    assert _selected_body_hitbox_hurtcap(
        dataset_path, case.record, case.attacker, case.victim
    ) == (case.selected_hb, case.selected_cap), case.note

    trace_path = root / f"reports/triage/iat{case.record}_{case.owner.lower().replace('/', '_')}.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=case.record,
        window_records=(case.record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, site1_count = rows[case.record]
    assert site1_count == 1, case.note
    assert (
        _trace_site_count(trace_path, start_record=case.record, record=case.record, site_id=5)
        == case.site5_count
    ), case.note
    assert (
        _trace_site_count(trace_path, start_record=case.record, record=case.record, site_id=24)
        == case.site24_count
    ), case.note
    assert int(out_row["action_id"][case.victim]) == int(ref_row["action_id"][case.victim]) == 91
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=case.record,
            p=p,
        )

    def mutate_source_motion(seed_t):
        seed_t["action_id"][0, case.attacker] = case.source_action_negative

    negative_trace_path = (
        root / f"reports/triage/iat{case.record}_{case.owner.lower().replace('/', '_')}_negative.tsv"
    )
    negative_rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=case.record,
        window_records=(case.record,),
        rng_damage_fly_roll_gate=True,
        trace_path=negative_trace_path,
        seed_mutator=mutate_source_motion,
    )
    _, _, negative_site1_count = negative_rows[case.record]
    assert negative_site1_count == 0, case.note
    assert (
        _trace_site_count(
            negative_trace_path, start_record=case.record, record=case.record, site_id=5
        )
        == 0
    ), case.note
    assert (
        _trace_site_count(
            negative_trace_path, start_record=case.record, record=case.record, site_id=24
        )
        == 0
    ), case.note


@pytest.mark.integration
def test_damageflyroll_to_damagefall_uses_frame_start_hitstun_one_tick_boundary() -> None:
    # TCH rec3251 starts in DamageFlyRoll with one hitstun tick left and enters DamageFall through
    # DamageFlyRoll_Anim. Raising the replay seed hitstun to two ticks changes only the frame-start
    # hitstun lane and keeps the row in DamageFlyRoll, proving the hidden lane models the one-tick
    # callback boundary rather than a generic DamageFlyRoll/DamageFall shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFlyRoll_Anim,ftCo_DamageFlyRoll_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "TubbyCurlyHerring.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3251
    player = 1
    seed, ref_row, out_row = _run_one_step_row(dataset_path, record, player)
    assert int(seed["action_id"][player]) == 91  # DamageFlyRoll.
    assert int(seed["hitstun"][player]) == 1
    assert int(out_row["action_id"][player]) == int(ref_row["action_id"][player]) == 38

    def extend_frame_start_hitstun(seed_t):
        seed_t["hitstun"][0, player] = 2

    _, _, extended_out = _run_one_step_row(
        dataset_path, record, player, seed_mutator=extend_frame_start_hitstun
    )
    assert int(extended_out["action_id"][player]) == 91


@pytest.mark.integration
def test_gat_top_f26_rollout_advances_replay_frame_rng_clock_to_delayed_damageflyroll() -> None:
    # Replay-reseeded validation rollouts must advance Slippi's frame-start RNG clock instead of
    # freezing it on the seed row. The selected GAT F26 cluster starts far before the eventual
    # AttackAirB hit; the direct one-step at 8633 is exact, but rollout only reaches the
    # ftCo_8008DCE0 DamageFlyRoll branch if the frame-start seed has advanced from the 8586 seed row.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/slippi-ssbm-asm/Recording/SendGamePreFrame.asm
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=8586,
        window_records=(8632, 8633, 8634),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_gat8586_replay_frame_clock_rollout.tsv",
    )
    for rec in (8632, 8633, 8634):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 8633 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[8633]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_qgd_damageflyroll_site8_uses_current_processhit_source_not_stale_last_hit_by() -> None:
    # QGD rec5747 locks the source-owned site-8 carry before the DamageFlyRoll gate. The carry is
    # owned by the current ProcessHit source (`ev->a_idx`/`ev->attacker`), not by the victim's
    # replay-visible `last_hit_by` byte, which is written after damage entry in source order.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def _poison_visible_last_hit_by(seed_t):
        seed_t["last_hit_by"][0, :] = 6

    trace_path = root / "reports/triage/qgd5747_site8_current_processhit_source.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=5747,
        window_records=(5747, 5748, 5749),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        seed_mutator=_poison_visible_last_hit_by,
    )

    ref_row, out_row, site1_count = rows[5747]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=5747, record=5747, site_id=8) == 1
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 91
    assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1])
    assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])


@pytest.mark.integration
def test_side_special_article_body_hit_admits_damageflyroll_without_fighter_effect_prefix() -> None:
    # FEH rec10192 locks the item-article BODY route into Fighter_ProcessHit. The source owner is
    # the generated Illusion/Phantasm article kind, not the victim's Wait pre-action and not a
    # fighter BODY DmgLog entry. It reaches ftCo_8008DCE0's DamageFlyRoll gate without the normal
    # fighter BODY ftColl_80078538 damage-effect RNG prefix.
    # refs/melee/src/melee/it/items/itfoxillusion.c::{itFoxIllusion_Logic14_DmgDealt,it_8029CFF0}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # data/items/articles/fox_falco.bin (MSLITAR1 side_special_illusion_itkind)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[10192]["seed_t"]
    assert int(seed["action_id"][0]) == 14  # Wait
    assert int(seed["action_id"][1]) == 352  # FxSpecialAirSHit
    article = seed["items"][0]
    assert int(article["exists"]) == 1
    assert int(article["type"]) == 57
    assert int(article["state"]) == 1
    assert int(article["owner"]) == 1
    assert int(article["attack_id"]) == 19

    trace_path = root / "reports/triage/feh10192_side_special_article_damageflyroll_lock.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10192,
        window_records=(10192,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )

    ref_row, out_row, site1_count = rows[10192]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=10192, record=10192, site_id=24) == 0
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 91
    assert int(out_row["hitlag"][0]) == int(ref_row["hitlag"][0])
    assert int(out_row["hitstun"][0]) == int(ref_row["hitstun"][0])


@pytest.mark.integration
def test_throwhi_capture_episode_rollout_clock_reaches_delayed_damageflyroll_gate() -> None:
    # Replay-real rollout lock for the TBK capture -> ThrowHi -> throw-laser -> DamageFlyRoll
    # episode selected from the F26 disruptive cluster.
    #
    # Source owner chain:
    # - CatchAttack/CaptureDamageHi can enter ThrowHi/ThrownHi through the common throw owner.
    # - ftCo_ThrowHi_Anim runs ftFx_Throw_Anim and keeps the victim-weight throw rate alive after
    #   release; command-active ThrowHi frame crossings serialize throw-side state1 lasers.
    # - The later item BODY hit reaches ftCo_8008DCE0's DamageFlyRoll RNG gate, which needs the
    #   replay frame-start RNG clock during validation rollout.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD398,ftCo_ThrowHi_Anim,ftCo_800DD724}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 2712
    for rec in (start_record, 2743, 2748, 2751, 2752, 2754):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    start_seed = samples[start_record]["seed_t"]
    assert int(start_seed["action_id"][0]) == 225  # CaptureDamageHi
    assert int(start_seed["grab_owner_port"][0]) == 1
    assert int(start_seed["action_id"][1]) == 217  # CatchAttack

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=start_record,
        window_records=(2743, 2748, 2751, 2752, 2754),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/tbk_throwhi_capture_damageflyroll_clock_lock.tsv",
    )

    ref_2743, out_2743, site_2743 = rows[2743]
    assert site_2743 == 0
    for row in (ref_2743, out_2743):
        state1_count = sum(
            1
            for item in row["items"]
            if int(item["exists"]) != 0
            and int(item["type"]) == 55  # Falco laser shot
            and int(item["state"]) == 1
            and int(item["owner"]) == 1
        )
        assert state1_count == 2

    for rec in (2748, 2751, 2754):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 221
        assert int(out_row["action_frame"][1]) == int(ref_row["action_frame"][1])

    ref_2752, out_2752, site_2752 = rows[2752]
    assert site_2752 == 1
    assert int(out_2752["action_id"][0]) == int(ref_2752["action_id"][0]) == 91
    assert int(out_2752["animation_index"][0]) == int(ref_2752["animation_index"][0])
    assert int(out_2752["hitlag"][0]) == int(ref_2752["hitlag"][0])


@pytest.mark.integration
def test_tbk_damageflytop_segment_carries_fighter_8006cda4_stream_phase_to_delayed_hit() -> None:
    # The TBK F26 cluster seeds long before the eventual AttackAirB contact. The hidden
    # Fighter_8006CDA4 held-item branch state belongs to the victim's DamageFlyTop segment, so the
    # replay seed must carry the pending pre-gate stream phase from the segment start; runtime C must
    # not rediscover it from the later AttackAirB action-frame shape.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[3870]["seed_t"]
    assert int(start_seed["action_id"][1]) == 90
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 2

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=3870,
        window_records=(3906, 3907, 3908),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_tbk3870_damageflytop_segment_rollout.tsv",
    )
    for rec in (3906, 3907, 3908):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 3907 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[3907]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_prh_damageflytop_segment_carries_zero_consume_gate_marker_to_delayed_hit() -> None:
    # The PRH F26 cluster seeds before a DamageFlyTop segment whose later AttackAirB hit uses the
    # source-proven zero-consume DamageFlyRoll gate marker. Marker 4 must carry gate-admission
    # provenance across the same-source DamageFlyTop segment, but it still does not advance the RNG
    # stream before the HSD_Randf gate.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[1593]["seed_t"]
    target_seed = ds.samples[1612]["seed_t"]
    assert int(start_seed["action_id"][1]) == 90
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 4
    assert int(target_seed["action_id"][1]) == 90
    assert int(target_seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 4

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=1593,
        window_records=(1611, 1612, 1613),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh1593_damageflytop_zero_consume_rollout.tsv",
    )
    for rec in (1611, 1612, 1613):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 1612 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[1612]
    assert int(out_target["action_id"][1]) == int(ref_target["action_id"][1]) == 91


@pytest.mark.integration
def test_tbk_damageflyroll_live_xrotn_pose_selects_late_attackairb_height() -> None:
    # DamageFlyRoll live XRotN hurtcap owner:
    # - ftCo_8008DCE0 enters DamageFlyRoll and immediately calls inlineA1, rotating FtPart_XRotN
    #   from current self+KB velocity.
    # - ftCo_DamageFlyRoll_Phys calls doFlyRoll before/after physics, keeping that live XRotN
    #   owner current for ftColl_80076ED8 BODY hurtcap selection.
    # - The adjacent rows prove this is not a broad late-BAir admission: the same AttackAirB
    #   hitboxes stay suppressed until the real high-hurtcap frame, which enters DamageFlyHi.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008DCE0,inlineA1,doFlyRoll,ftCo_DamageFlyRoll_Phys}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    for rec in (6993, 6994):
        seed, ref_row, out_row = _run_one_step_row(dataset_path, rec, 1)
        assert int(seed["action_id"][1]) == 91  # DamageFlyRoll
        assert int(seed["action_id"][0]) == 67  # AttackAirB
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 91
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 0
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])

    seed, ref_row, out_row = _run_one_step_row(dataset_path, 6995, 1)
    assert int(seed["action_id"][1]) == 91  # DamageFlyRoll
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 87  # DamageFlyHi
    assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 5
    assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1]) == 45


@pytest.mark.integration
def test_tbk_damageflyroll_live_xrotn_rollout_waits_for_late_attackairb_height() -> None:
    # Rollout lock for the same TBK F26 disruptive row. Starting at the disruptive seed frame must
    # not admit the late BAir BODY hit early, but must still reach the DamageFlyHi transition once
    # the live DamageFlyRoll XRotN pose and hitbox frame align.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=6941,
        window_records=(6993, 6994, 6995, 6996),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/tbk6941_damageflyroll_live_xrotn_rollout.tsv",
    )
    for rec in (6993, 6994):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 91
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == 0
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])

    for rec, expected_hitlag in ((6995, 5), (6996, 4)):
        ref_row, out_row, site1_count = rows[rec]
        assert site1_count == 0
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 87
        assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1]) == expected_hitlag
        assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1]) == 45


@pytest.mark.integration
def test_tbk_attackairn_segment_carries_fighter_8006cda4_stream_phase_to_delayed_hit() -> None:
    # This TBK segment seeds on the contiguous AttackAirN <- AttackAirLw damage-entry episode.
    # The explicit Fighter_8006CDA4 stream phase must survive until ftCo_8008DCE0 consumes it, but
    # must not be carried backward across unrelated grounded/action/source boundaries.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[2366]["seed_t"]
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 2

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=2366,
        window_records=(2366, 2367, 2368, 2378, 2399),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/rng_tbk2366_attackairn_segment_rollout.tsv",
    )
    for rec in (2366, 2367, 2368, 2378, 2399):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 2367 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"


@pytest.mark.integration
def test_prh_damageflytop_damagefall_iasa_carries_attackairn_stream_phase_to_delayed_hit() -> None:
    # This PRH F26 segment seeds during DamageFlyTop, exits hitstun through DamageFall_IASA into
    # AttackAirN, then takes the next AttackAirB hit through ftCo_8008DCE0. The hidden
    # Fighter_8006CDA4 stream phase belongs to the same source-owned damage episode and must
    # survive the one-frame DamageFall IASA handoff into AttackAirN.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFly_IASA,ftCo_8008DCE0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[10169]["seed_t"]
    damagefall_seed = ds.samples[10206]["seed_t"]
    target_seed = ds.samples[10207]["seed_t"]
    assert int(start_seed["action_id"][0]) == 90  # DamageFlyTop
    assert int(start_seed["hitstun"][0]) > 0
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3
    assert int(damagefall_seed["action_id"][0]) == 38  # DamageFall
    assert int(damagefall_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3
    assert int(target_seed["action_id"][0]) == 65  # AttackAirN
    assert int(target_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 3

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10169,
        window_records=(10205, 10206, 10207, 10208),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh10169_damagefall_attackairn_stream_rollout.tsv",
    )
    for rec in (10205, 10206, 10207, 10208):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 10207 else 0), f"unexpected DamageFlyRoll gate pulse at {rec}"

    ref_target, out_target, _ = rows[10207]
    assert int(out_target["action_id"][0]) == int(ref_target["action_id"][0]) == 91


@pytest.mark.integration
def test_damagefall_iasa_stream_phase_does_not_arm_unproven_damageflytop_controls() -> None:
    # Negative boundary for the same source-family: DamageFlyTop rows with AttackAirB nearby do not
    # receive the explicit stream lane unless a later replay-proven DamageFlyRoll gate identifies
    # the hidden Fighter_8006CDA4 phase. This keeps ordinary DamageFlyN controls outside the bridge.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    for rec in (1656, 1657, 1658):
        seed = ds.samples[rec]["seed_t"]
        assert int(seed["action_id"][0]) == 90  # DamageFlyTop
        assert int(seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, 1658, 0)
    assert int(ref_row["action_id"][0]) == 88  # DamageFlyN, not DamageFlyRoll.
    assert int(out_row["action_id"][0]) == 88


@pytest.mark.integration
def test_damageflytop_f26_runtime_maps_raw_source_port_before_attacker_lookup() -> None:
    # Runtime source-owner lanes store raw Slippi source port, not compact local slot. A non-compact
    # source_port0 mapping must still find the local attacker before applying the F26 stream-phase
    # gate; treating last_hit_by=2 as local slot 2 would reject this two-player row.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def _noncompact_ports(seed_t):
        seed_t["source_port0"][0, 0] = 2
        seed_t["source_port0"][0, 1] = 3
        seed_t["last_hit_by"][0, 1] = 2

    _, ref_row, out_row = _run_one_step_row(dataset_path, 3907, 1, seed_mutator=_noncompact_ports)
    assert int(ref_row["action_id"][1]) == 91
    assert int(out_row["action_id"][1]) == 91


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "attacker",
        "victim",
        "selected_hb",
        "selected_cap",
        "expected_ref_action",
        "expected_out_action",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            3907,
            0,
            1,
            0,
            12,
            91,
            91,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            6380,
            1,
            0,
            1,
            2,
            91,
            91,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            6495,
            1,
            0,
            1,
            0,
            88,
            88,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl",
            5448,
            1,
            0,
            1,
            0,
            91,
            91,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            4024,
            1,
            0,
            1,
            0,
            91,
            91,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            10332,
            1,
            0,
            0,
            0,
            91,
            91,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            7566,
            1,
            0,
            1,
            0,
            88,
            88,
        ),
        (
            "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl",
            3301,
            1,
            0,
            1,
            0,
            88,
            88,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            2461,
            0,
            1,
            0,
            0,
            88,
            88,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            2608,
            0,
            1,
            0,
            0,
            88,
            88,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            3351,
            0,
            1,
            0,
            7,
            88,
            88,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            4541,
            0,
            1,
            0,
            1,
            91,
            91,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            9398,
            1,
            0,
            0,
            2,
            87,
            87,
        ),
    ],
)
def test_attackairb_damageflytop_runtime_phase_requires_selected_hurtcap_provenance(
    dataset_rel: str,
    record: int,
    attacker: int,
    victim: int,
    selected_hb: int,
    selected_cap: int,
    expected_ref_action: int,
    expected_out_action: int,
) -> None:
    # Runtime fallback for AttackAirB -> DamageFlyTop stream phase is owned by the selected
    # ftColl_80076ED8 BODY source, not just visible AttackAirB shape. TBK/PJO/IAT/TVR rows select
    # specific cap12, hb1/cap2, hb1/cap0, hb0/cap0, or terminal hb0/cap1 source pairs from
    # data/hurtcaps/{fox,falco}.json; aggregate unrelated low/root, hb0/cap2, and JumpAerial cap1
    # selected pairs stay on their ordinary DamageFlyN/Hi source path unless an explicit seed lane
    # supplies stream phase.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    got_hb, got_cap = _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim)
    assert (got_hb, got_cap) == (selected_hb, selected_cap)
    seed_row = read_dataset(str(dataset_path)).samples[record]["seed_t"]
    if dataset_rel.endswith("PutridJoyousOryx.msl") and record == 5448:
        assert int(seed_row["damage_jump_buffer_x14"][victim]) > 0
        assert int(seed_row["state_flags"][attacker][0]) & 0x40
    if dataset_rel.endswith("PriceyPartialAlbatross.msl") and record == 4024:
        assert int(seed_row["damage_jump_buffer_x14"][victim]) == 0
        assert int(seed_row["state_flags"][attacker][0]) & 0x40 == 0
    if dataset_rel.endswith("ImpassionedAlarmedTarsier.msl") and record == 10332:
        assert int(seed_row["damage_jump_buffer_x14"][victim]) > 0
        # At frame start the replay seed has x14 aligned to hitstun; during the runtime step the
        # DamageFlyTop callback decrements x14 once before ProcessHit reaches the DamageFlyRoll gate.
        assert int(seed_row["damage_jump_buffer_x14"][victim]) == int(seed_row["hitstun"][victim])
        assert int(seed_row["state_flags"][victim][0]) & 0x40
    if record in {3301, 7566}:
        assert int(seed_row["damage_jump_buffer_x14"][victim]) == 0
        assert int(seed_row["state_flags"][attacker][0]) & 0x40

    def _clear_seed_phase(seed_t):
        seed_t["fighter_8006cda4_pre_gate_consume_count"][0, victim] = 0

    _, ref_row, out_row = _run_one_step_row(
        dataset_path,
        record,
        victim,
        seed_mutator=_clear_seed_phase,
    )
    assert int(ref_row["action_id"][victim]) == expected_ref_action
    assert int(out_row["action_id"][victim]) == expected_out_action


@pytest.mark.integration
def test_damageflytop_attackairb_root_x14_primary_owner_closes_iat_10332_rollout() -> None:
    # IAT rec10332 is an active DamageFlyTop victim hit by selected strong BAir hb0/root-body.
    # The live DamageFlyTop callback has decremented mv.co.damage.x14 once before ProcessHit, so
    # x14 is one tick below frame-start hitstun. That callback-phase proof admits exactly the
    # primary Fighter_8006CDA4 consume before ftCo_8008DCE0's DamageFlyRoll gate; visible
    # AttackAirB/DamageFlyTop shape alone remains covered by the table-driven negatives above.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 10332
    victim = 0
    attacker = 1
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][victim]) == 90  # DamageFlyTop.
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 1
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (0, 0)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10209,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/iat10332_damageflytop_bair_root_x14_primary.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert int(out_row["action_id"][victim]) == 91
    assert site1_count == 1


@pytest.mark.integration
def test_damageflytop_attackairb_hb0_cap1_effect_prefix_closes_tvr_4541_rollout() -> None:
    # TVR rec4541 is active DamageFlyTop hit by selected authored strong BAir hb0/cap1. That
    # source owns ftColl_80078538's normal-hit visual-effect prefix before ftCo_8008DCE0 samples
    # DamageFlyRoll, but owns no Fighter_8006CDA4 pre-gate prefix. Mutating the victim out of
    # DamageFlyTop removes this owner and keeps JumpAerial cap1 outside the bridge.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    # data/hurtcaps/{fox,falco}.json cap1
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 4541
    victim = 1
    attacker = 0
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][victim]) == 90  # DamageFlyTop.
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB.
    assert int(seed["damage_jump_buffer_x14"][victim]) == 0
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (0, 1)

    trace_path = root / "reports/triage/tvr4541_damageflytop_bair_hb0_cap1_effect_prefix.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, site1_count = rows[record]
    assert site1_count == 1
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=24) == 4
    for site_id in (5, 6, 7):
        assert _trace_site_count(trace_path, start_record=record, record=record, site_id=site_id) == 0
    assert int(out_row["action_id"][victim]) == int(ref_row["action_id"][victim]) == 91

    def mutate_victim_action(seed_t):
        seed_t["action_id"][0, victim] = 27  # JumpAerialF: cap1 is covered by the TCH negative.

    negative_trace = (
        root / "reports/triage/tvr4541_damageflytop_bair_hb0_cap1_effect_prefix_negative.tsv"
    )
    negative_rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=negative_trace,
        seed_mutator=mutate_victim_action,
    )
    _negative_ref, negative_out, negative_site1_count = negative_rows[record]
    assert negative_site1_count == 0
    assert _trace_site_count(negative_trace, start_record=record, record=record, site_id=24) == 0
    assert int(negative_out["action_id"][victim]) != 91


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_rejects_attackairb_cap12_xrotn_owner_ppa_7332() -> None:
    # SpecialAirHi current-hit DamageFlyRoll admission requires a concrete selected BODY owner, but
    # cap12/XRotN is the high-pose DamageFlyTop provenance path used by the narrowed AttackAirB
    # owner. It must stay seed-owned / ordinary DamageFlyN here rather than using visible
    # SpecialAirHi shape to enter DamageFlyRoll.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # data/hurtcaps/{fox,falco}.json cap12
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 7332
    attacker = 1
    victim = 0
    got_hb, got_cap = _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim)
    assert (got_hb, got_cap) == (0, 12)

    seed, ref_row, out_row = _run_one_step_row(
        dataset_path, record, victim, rng_damage_fly_roll_gate=True
    )
    assert int(seed["action_id"][victim]) == 356  # SpecialAirHi.
    assert int(ref_row["action_id"][victim]) == int(out_row["action_id"][victim]) == 88

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/ppa7332_specialairhi_cap12_reject.tsv",
    )
    ref_roll, out_roll, site1_count = rows[record]
    assert site1_count == 0
    assert int(ref_roll["action_id"][victim]) == int(out_roll["action_id"][victim]) == 88


@pytest.mark.integration
def test_specialhi_attackairb_cap2_current_payload_enters_damageflyroll() -> None:
    # SpecialAirHi current-hit DamageFlyRoll admission:
    # - FSP 11707 selects weak BackAir hb2/cap2 against SpecialAirHi.
    # - FSP 11924 selects strong BackAir hb1/cap2 against SpecialAirHi.
    # cap2/head-high is admitted only for these concrete selected BAir payloads: early weak hb2 and
    # strong hb1. The existing AGG 2864 late weak hb1 seed-frame negative and nearby PPA 7332
    # cap12/XRotN negative remain ordinary DamageFlyHi/N. TVR's SpecialHiFall hb1/cap2 row remains
    # open in this checkpoint and is not claimed as closed.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    fsp_dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not fsp_dataset_path.exists():
        pytest.skip("missing local SpecialHi DamageFlyRoll dataset")

    victim = 0
    attacker = 1
    cases = ((11707, (2, 2), 5), (11924, (1, 2), 6))
    ds = read_dataset(str(fsp_dataset_path))
    for record, selected, expected_hitlag in cases:
        assert _selected_body_hitbox_hurtcap(fsp_dataset_path, record, attacker, victim) == selected
        seed = ds.samples[record]["seed_t"]
        ref_row = ds.samples[record]["ref_t1"]
        assert int(seed["action_id"][victim]) == 356  # SpecialAirHi.
        assert int(seed["action_id"][attacker]) == 67  # AttackAirB.
        assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
        assert int(ref_row["action_id"][victim]) == 91
        assert int(ref_row["hitlag"][victim]) == expected_hitlag

    rows = _run_rollout_window_rows_with_trace(
        fsp_dataset_path,
        start_record=5391,
        window_records=(11707, 11924),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/fsp_specialairhi_cap2_bair_owner.tsv",
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    for record, _selected, expected_hitlag in cases:
        ref_roll, out_roll, site1_count = rows[record]
        assert site1_count == 1
        assert int(ref_roll["action_id"][victim]) == int(out_roll["action_id"][victim]) == 91
        assert int(out_roll["hitlag"][victim]) == expected_hitlag


@pytest.mark.integration
def test_specialairs_damageflyroll_gate_uses_attackairb_create_edge_root_body_owner_ppa_6123() -> None:
    # PPA rec6123 covers the side-special startup gate owner:
    # - defender is airborne SpecialAirS with no Fighter_8006CDA4 pre-gate seed count,
    # - ftColl selected the current create-edge strong AttackAirB hb1/root-body HitCapsule,
    # - this admits only the DamageFlyRoll gate; it does not synthesize pre-gate consumes and is not
    #   inferred from visible SpecialAirS state alone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    # data/hurtcaps/{fox,falco}.json cap0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 6123
    attacker = 0
    victim = 1
    got_hb, got_cap = _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim)
    assert (got_hb, got_cap) == (1, 0)
    seed_row = read_dataset(str(dataset_path)).samples[record]["seed_t"]
    assert int(seed_row["action_id"][victim]) == 351  # FxSpecialAirS
    assert int(seed_row["on_ground"][victim]) == 0
    assert int(seed_row["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0

    _, ref_row, out_row = _run_one_step_row(
        dataset_path, record, victim, rng_damage_fly_roll_gate=True
    )
    assert int(ref_row["action_id"][victim]) == int(out_row["action_id"][victim]) == 91


@pytest.mark.integration
def test_pjo_rollout_rng_sites_are_source_owned_without_exceptions() -> None:
    # PJO RAW-CLEAN RNG closure in validator-shaped rollout:
    # - rec2654: weak AttackAirB current-hit owner consumes the pre-gate Fighter_8006CDA4 site once.
    # - rec3012: Wait terminal animation variant consumes ftwaitanim's HSD_Randi(100).
    # - rec5097: grounded Catch is interrupted by current strong AttackAirN; hitstun is already
    #   installed when ftCo_8008DCE0 selects DamageFlyRoll, so the source proof is the captured
    #   Catch-family pre-action plus the selected strong NAir HitCapsule.
    # - rec5448: active DamageFlyTop selects first-create strong AttackAirB hb1/cap0 and consumes
    #   the two pre-gate Fighter_8006CDA4 stream sites.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    trace_path = root / "reports/triage/pjo_rollout_rng_sites_source_owned.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(2654, 3012, 5097, 5448),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    for rec in (2654, 3012, 5097, 5448):
        ref_row, out_row, _ = rows[rec]
        for p in (0, 1):
            for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
                got = int(out_row[field][p])
                exp = int(ref_row[field][p])
                assert got == exp, f"record={rec} p={p} field={field} expected={exp} got={got}"

    assert _trace_site_count(trace_path, start_record=0, record=2654, site_id=5) == 1
    assert _trace_site_count(trace_path, start_record=0, record=3012, site_id=3) == 1
    assert _trace_site_count(trace_path, start_record=0, record=3012, site_id=25) == 1
    assert _trace_site_count(trace_path, start_record=0, record=5097, site_id=5) == 1
    assert _trace_site_count(trace_path, start_record=0, record=5448, site_id=5) == 1
    assert _trace_site_count(trace_path, start_record=0, record=5448, site_id=6) == 1


@pytest.mark.integration
def test_wait_variant_replay_frame_rng_accounts_for_earlier_deadupstar_effect_prefix() -> None:
    # QGD rec4435 is the regression control for replay-frame Wait RNG admission: p0's earlier
    # same-frame DeadUpStar_Anim source callback adds the bounded two-step creation prefix plus the
    # live generator step before p1 reaches Wait_Anim/getAnimID. STM rec10395 is the direct
    # rollout-clock positive for the same late phase-1 generator window. BHH rec10161 is the
    # stale-late negative: a later DeadUpStar action-frame row must not keep carrying the prefix.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    # refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    trace_path = root / "reports/triage/qgd_wait_deadupstar_effect_prefix_rng.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(4435,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, _ = rows[4435]
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 4
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 14
    assert int(out_row["animation_index"][1]) == int(ref_row["animation_index"][1]) == 2
    assert _trace_site_count(trace_path, start_record=0, record=4435, site_id=25) == 3
    assert _trace_site_count(trace_path, start_record=0, record=4435, site_id=3) == 1

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "SweatyThisMallard.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    trace_path = root / "reports/triage/stm_wait_deadupstar_active_prefix_rng.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10395,
        window_records=(10395,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    ref_row, out_row, _ = rows[10395]
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]) == 14
    assert int(out_row["animation_index"][1]) == int(ref_row["animation_index"][1]) == 3
    assert _trace_site_count(trace_path, start_record=10395, record=10395, site_id=25) == 3
    assert _trace_site_count(trace_path, start_record=10395, record=10395, site_id=3) == 1

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "BlondHardHippopotamus.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    trace_path = root / "reports/triage/bhh_wait_deadupstar_stale_late_rng.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(10161,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, _ = rows[10161]
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 14
    assert int(out_row["animation_index"][0]) == int(ref_row["animation_index"][0]) == 2
    assert _trace_site_count(trace_path, start_record=0, record=10161, site_id=25) == 0
    assert _trace_site_count(trace_path, start_record=0, record=10161, site_id=3) == 1


@pytest.mark.integration
def test_frozenps_top_blast_replay_frame_rng_selects_deadupfall_4665() -> None:
    # Top-blast replay-frame RNG owner:
    # Game_20260515T182447_frozenps:4665 crosses the top blast line from DamageFlyTop with
    # aligned deterministic pre-gate state. Source `ftCo_800D3158` then samples
    # `HSD_Randi(100)+1`; under replay playback the current row's Slippi frame-start seed is the
    # authoritative stream input, so rollout must consume site 23 and choose DeadUpFall.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "Game_20260515T182447_frozenps.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    trace_path = root / "reports/triage/frozenps_top_blast_deadupfall_rng.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=4541,
        window_records=(4665,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, _site1_count = rows[4665]
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 6
    assert _trace_site_count(trace_path, start_record=4541, record=4665, site_id=23) == 1


@pytest.mark.integration
def test_frozenps_top_blast_plain_step_does_not_pull_replay_frame_rng_4665() -> None:
    # Negative boundary: ordinary step_input / RL-style stepping must not pull the replay row's
    # frame-start RNG as hidden future state. Without `step_input_replay_frame_rng`, the top-blast
    # site remains seed/internal-stream owned and this direct rollout step stays on DeadUpStar.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "Game_20260515T182447_frozenps.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[4665:4666]
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out_row = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref_row = row["ref_t1"][0]
    assert int(ref_row["action_id"][0]) == 6
    assert int(out_row["action_id"][0]) == 4


@pytest.mark.integration
def test_top_blast_replay_frame_rng_rejects_later_player_without_prefix_owner_qgd_6480() -> None:
    # Player-order prefix control:
    # QGD:6480 has p1 in a top-blast DamageFlyTop source row whose raw frame-start seed would choose
    # DeadUpFall. The source callback reaches p1 after p0's same-frame fighter callback
    # opportunities, so replay validation must not admit the raw p1 top-blast RNG site until those
    # earlier-player prefix owners are explicitly modeled. This keeps the source outcome DeadUpStar
    # rather than fitting a broad current-row top-blast predicate.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 6480
    trace_path = root / "reports/triage/qgd_top_blast_later_player_prefix_negative.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, _site1_count = rows[record]
    assert int(ref_row["action_id"][1]) == 4
    assert int(out_row["action_id"][1]) == 4
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=23) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "top_player", "note"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            2653,
            0,
            "other-player Wait getAnimID prefix",
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            7810,
            0,
            "other-player Fighter_8006CDA4 prefix lane",
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            10272,
            0,
            "other-player attack-script/combat RNG prefix",
        ),
    ],
)
def test_top_blast_replay_frame_rng_rejects_known_pre_matchflow_prefixes(
    dataset_rel: str, record: int, top_player: int, note: str
) -> None:
    # Prefix controls for p0 top-blast rows:
    # These rows have raw frame-start seeds that would choose DeadUpFall, but the other fighter owns
    # a same-frame RNG prefix before match-flow reaches ftCo_800D3158. Runtime must reject site 23
    # until that prefix is modeled, leaving the source DeadUpStar outcome intact.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,ftCo_8008A6D8,getAnimID}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078538,ftColl_8007699C}
    del note
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    trace_path = root / f"reports/triage/top_blast_prefix_negative_{record}.tsv"
    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=trace_path,
    )
    ref_row, out_row, _site1_count = rows[record]
    assert int(ref_row["action_id"][top_player]) == 4
    assert int(out_row["action_id"][top_player]) == 4
    assert _trace_site_count(trace_path, start_record=record, record=record, site_id=23) == 0


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_uses_live_rollout_rng_stream_his_1598() -> None:
    # HIS rollout lock for ftCo_8008DCE0's generic severe-airborne DamageFlyRoll gate:
    # p1 is still in SpecialAirHi when p0's AttackAirB hits. SpecialAirHi is not excluded by
    # Fighter_8006CDA4 or ftCo_8008DCE0, so replay playback must admit the gate from the current
    # ProcessHit source and consume ftColl_80078538's normal BODY damage-effect prefix before the
    # DamageFlyRoll Randf gate.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HungryImportantSnake.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1598]["seed_t"]
    ref = ds.samples[1598]["ref_t1"]
    assert int(seed["action_id"][1]) == 356  # SpecialAirHi
    assert int(seed["action_frame"][1]) == 15
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(ref["action_id"][1]) == 91  # DamageFlyRoll

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(1597, 1598, 1599),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/his1598_specialairhi_damageflyroll_rollout.tsv",
    )
    for rec in (1597, 1598, 1599):
        ref_row, out_row, site1_count = rows[rec]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
        assert site1_count == (1 if rec == 1598 else 0), (
            f"unexpected DamageFlyRoll gate pulse at {rec}"
        )
    assert _trace_site_count(
        root / "reports/triage/his1598_specialairhi_damageflyroll_rollout.tsv",
        start_record=0,
        record=1598,
        site_id=24,
    ) == 4


@pytest.mark.integration
def test_his_replay_rollout_uses_current_frame_rng_seed_for_damageflyroll_4397() -> None:
    # Rollout validation is replay playback: it feeds replay inputs and the replay frame-start RNG
    # seed for each row. HIS rec4213 carries an unrelated LandingAirLw seed lane, but rec4397's
    # DamageFlyRoll gate must sample from rec4397's own Slippi frame_pre_random_seed.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HungryImportantSnake.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_seed = ds.samples[4213]["seed_t"]
    target_seed = ds.samples[4397]["seed_t"]
    target_ref = ds.samples[4397]["ref_t1"]
    assert int(start_seed["action_id"][0]) == 74  # LandingAirLw
    assert int(start_seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 1
    assert int(target_seed["action_id"][0]) == 90  # DamageFlyN
    assert int(target_ref["action_id"][0]) == 91  # DamageFlyRoll

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=4213,
        window_records=(4397,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/his4213_replayfed_rng_rollout.tsv",
    )
    ref_row, out_row, site1_count = rows[4397]
    for p in (0, 1):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=4397,
            p=p,
    )
    assert int(out_row["action_id"][0]) == int(ref_row["action_id"][0]) == 91
    assert site1_count == 1
    with (root / "reports/triage/his4213_replayfed_rng_rollout.tsv").open("r", encoding="utf-8") as fh:
        trace_rows = csv.DictReader(fh, delimiter="\t")
        gate_row = next(
            row for row in trace_rows if int(row["frame_id"]) == 4274 and int(row["site_id"]) == 1
        )
    assert int(gate_row["seed_in"]) == int(target_seed["frame_pre_random_seed"])


@pytest.mark.integration
def test_his_landingfallspecial_downattacku_current_hitcapsule_owns_damageflyroll_rng_7485() -> None:
    # LandingFallSpecial frame 0 has no explicit replay seed lane for Fighter_8006CDA4, but this row
    # enters damage through ftColl's selected current DownAttackU ground-sweep HitCapsule. That
    # concrete source owns the hidden pre-gate consume before ftCo_8008DCE0's DamageFlyRoll gate.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_DownAttackU.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HungryImportantSnake.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 7485
    victim = 0
    attacker = 1
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 43  # LandingFallSpecial
    assert int(seed["action_frame"][victim]) == 0
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert int(seed["action_id"][attacker]) == 187  # DownAttackU
    assert int(ref["action_id"][victim]) == 91  # DamageFlyRoll
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (0, 0)

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, victim)
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=6815,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/his7485_landingfallspecial_downattacku_damageflyroll.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert _trace_site_count(
        root / "reports/triage/his7485_landingfallspecial_downattacku_damageflyroll.tsv",
        start_record=6815,
        record=record,
        site_id=5,
    ) == 1
    assert _trace_site_count(
        root / "reports/triage/his7485_landingfallspecial_downattacku_damageflyroll.tsv",
        start_record=6815,
        record=record,
        site_id=24,
    ) == 4
    assert _trace_site_count(
        root / "reports/triage/his7485_landingfallspecial_downattacku_damageflyroll.tsv",
        start_record=6815,
        record=record,
        site_id=6,
    ) == 0
    assert site1_count == 1
    with (root / "reports/triage/his7485_landingfallspecial_downattacku_damageflyroll.tsv").open(
        "r", encoding="utf-8"
    ) as fh:
        trace_rows = csv.DictReader(fh, delimiter="\t")
        gate_row = next(
            row for row in trace_rows if int(row["frame_id"]) == 7362 and int(row["site_id"]) == 1
        )
    assert int(gate_row["seed_in"]) == int(seed["frame_pre_random_seed"])

    def mutate_victim_action(seed_t):
        seed_t["action_id"][0, victim] = 42  # Landing: same grounded landing family, no owner.

    _, _, mutated_out = _run_one_step_row(dataset_path, record, victim, seed_mutator=mutate_victim_action)
    assert int(mutated_out["action_id"][victim]) != 91

    def mutate_attacker_action(seed_t):
        seed_t["action_id"][0, attacker] = 188  # DownForwardU: no authored DownAttackU source.

    _, _, mutated_out = _run_one_step_row(dataset_path, record, victim, seed_mutator=mutate_attacker_action)
    assert int(mutated_out["action_id"][victim]) != 91


@pytest.mark.integration
def test_landingairlw_without_live_source_owner_does_not_replay_feed_damageflyroll_iat_11154() -> None:
    # Replay playback installs the current Slippi frame-start RNG seed, but it does not replay-feed
    # hidden Fighter_8006CDA4 seed lanes every step. IAT rec11154 has a LandingAirLw victim and a
    # replay seed-lane count on the row, but the rolled-out live state reaches the hit without a
    # current selected pre-gate source owner. The BAir hb0/cap2 BODY source stays on the ordinary
    # DamageFlyHi path instead of using LandingAirLw action shape to admit ftCo_8008DCE0's
    # DamageFlyRoll RNG gate.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 11154
    victim = 0
    attacker = 1
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 74  # LandingAirLw.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 2
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB.
    assert int(seed["action_frame"][attacker]) == 3
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (0, 2)
    assert int(ref["action_id"][victim]) == 87  # DamageFlyHi, not DamageFlyRoll.

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=10975,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/iat11154_landingairlw_no_live_rng_owner.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert int(out_row["action_id"][victim]) == 87
    assert site1_count == 0


@pytest.mark.integration
def test_normal_step_input_rollout_does_not_pull_future_replay_rng_his_7485() -> None:
    # The replay RNG feed is a validation/playback hook, not normal RL/free-running behavior.
    # Starting from HIS rec6815 and calling plain step_input through rec7485 keeps the internal RNG
    # stream and still misses the replay's DamageFlyRoll branch.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HungryImportantSnake.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    def row_bytes(record: int, field: str, stride: int) -> np.ndarray:
        return (
            np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, stride)
        )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, row_bytes(6815, "seed_t", seed_stride))
        for record in range(6815, 7486):
            binding.step_input(
                handle,
                row_bytes(record, "prev_input_t", input_stride),
                row_bytes(record, "input_t", input_stride),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[7485]["ref_t1"]
    assert int(ref["action_id"][0]) == 91
    assert int(out["action_id"][0]) != int(ref["action_id"][0])


@pytest.mark.integration
def test_jumpaerial_attackairb_cap1_does_not_admit_damageflyroll_tch_8437() -> None:
    # JumpAerial + AttackAirB visible state is not enough to admit ftCo_8008DCE0's DamageFlyRoll
    # gate. The retained site-8 carry requires the selected DmgLog BODY hurtcap provenance; TCH
    # selects cap1, so it stays seed-owned and remains DamageFlyN.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # data/hurtcaps/{fox,falco}.json cap1/cap2
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "TubbyCurlyHerring.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 8437
    attacker = 0
    victim = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 27  # JumpAerialF
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert int(ref["action_id"][victim]) == 88  # DamageFlyN, not DamageFlyRoll
    assert _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim) == (1, 1)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/tch8437_jumpaerial_cap1_damageflyroll_negative.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert site1_count == 0
    assert _trace_site_count(
        root / "reports/triage/tch8437_jumpaerial_cap1_damageflyroll_negative.tsv",
        start_record=record,
        record=record,
        site_id=8,
    ) == 0


@pytest.mark.integration
def test_jumpaerial_illusion_article_source_admits_damageflyroll_prh_7739() -> None:
    # JumpAerial victim + no fighter HitCapsule source is still a valid DamageFlyRoll gate when
    # the source is the generated Illusion/Phantasm side-special article. This is not the
    # AttackAirB cap2 carry: item BODY damage reaches `combat_apply_item_hit` with no selected
    # fighter HitCapsule/hurtcap, and the article kind comes from MSLITAR1.
    # refs/melee/src/melee/it/items/itfoxillusion.c::{
    #   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys,itFoxIllusion_Logic14_DmgDealt}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # data/items/articles/fox_falco.bin::MSLITAR1 side_special_illusion_itkind
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 7739
    victim = 0
    attacker = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == 27  # JumpAerialF
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert int(seed["action_id"][attacker]) in {351, 353}  # SpecialS/AirS side-special owner.
    assert int(ref["action_id"][victim]) == 91  # DamageFlyRoll.
    assert any(
        int(item["exists"]) != 0
        and int(item["owner"]) == attacker
        and int(item["type"]) == 57  # Falco Phantasm, table-backed by MSLITAR1.
        and int(item["state"]) < 2
        for item in seed["items"]
    )
    with pytest.raises(AssertionError):
        _selected_body_hitbox_hurtcap(dataset_path, record, attacker, victim)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh7739_jumpaerial_illusion_article_damageflyroll.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert site1_count == 1
    assert _trace_site_count(
        root / "reports/triage/prh7739_jumpaerial_illusion_article_damageflyroll.tsv",
        start_record=record,
        record=record,
        site_id=8,
    ) == 0


@pytest.mark.integration
@pytest.mark.parametrize("record, seed_action", [(12746, 88), (12748, 89)])
def test_throwhi_damageflyn_lw_seed_rows_do_not_use_rollout_rng_fallback_cdo(
    record: int, seed_action: int
) -> None:
    # CDO exposes active ThrowHi-owned DamageFlyN/Lw hitlag rows with no selected current
    # HitCapsule and no explicit Fighter_8006CDA4 seed lane. Replay playback installs the row's
    # frame-start RNG, so these exact seed rows must not use the older advanced-rollout fallback to
    # admit DamageFlyRoll.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowHi_Anim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    victim = 0
    attacker = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][victim]) == seed_action
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 0
    assert int(seed["action_id"][attacker]) == 221  # ThrowHi
    assert int(ref["action_id"][victim]) == 89  # DamageFlyLw, not DamageFlyRoll

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=record,
        window_records=(record,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / f"reports/triage/cdo{record}_throwhi_damagefly_seed_rng_negative.tsv",
    )
    ref_row, out_row, site1_count = rows[record]
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert site1_count == 0


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_rejects_teacher_forced_seed_phase_agg_2864() -> None:
    # Replay-real one-step negative for the SpecialAirHi slice of ftCo_8008DCE0:
    # SpecialAirHi is admitted when a free-running rollout owns the RNG clock, but a teacher-forced
    # one-step seed does not expose enough hidden HSD_Randf stream phase. The generic
    # Fighter_8006CDA4 pre-gate consume-count lane is not sufficient to turn this seeded row into a
    # DamageFlyRoll.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2864]["seed_t"]
    ref = ds.samples[2864]["ref_t1"]
    p = 0
    assert int(seed["action_id"][p]) == 356  # SpecialAirHi
    assert int(ref["action_id"][p]) == 87  # DamageFlyHi, not DamageFlyRoll

    _, ref_row, out_row = _run_one_step_row(dataset_path, 2864, p)
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=2864, p=q)


@pytest.mark.integration
def test_specialairhi_damageflyroll_gate_rejects_exact_rollout_reseed_phase_agg_2864() -> None:
    # Replay-real rollout negative for the same SpecialAirHi owner: `reseed_seed_rollout()` exposes
    # frame-indexed rollout ownership, but a rollout that starts exactly on the hit row has not
    # advanced the hidden HSD_Randf stream beyond the seed frame. It must match the one-step
    # teacher-forced behavior and reject the DamageFlyRoll gate.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=2864,
        window_records=(2864,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/agg2864_specialairhi_exact_reseed_rollout.tsv",
    )
    ref_row, out_row, site1_count = rows[2864]
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=2864, p=q)
    assert site1_count == 0, f"unexpected DamageFlyRoll gate pulse at exact reseed row"


@pytest.mark.integration
def test_damageflyn_without_stream_phase_rejects_exact_reseed_damageflyroll_prh_8390() -> None:
    # DamageFlyN/Lw remain source-eligible for ftCo_8008DCE0's DamageFlyRoll gate, but visible
    # damage-state shape alone does not reconstruct the hidden HSD_Randf phase. PRH 8390 has no
    # Fighter_8006CDA4 pre-gate stream lane, so both one-step and exact rollout reseed must keep
    # the vanilla DamageFlyLw result instead of manufacturing DamageFlyRoll.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[8390]["seed_t"]
    ref = ds.samples[8390]["ref_t1"]
    p = 1
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][p]) == 0
    assert int(ref["action_id"][p]) == 89  # DamageFlyLw, not DamageFlyRoll

    _, ref_row, out_row = _run_one_step_row(dataset_path, 8390, p)
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=8390, p=q)

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=8390,
        window_records=(8390,),
        rng_damage_fly_roll_gate=True,
        trace_path=root / "reports/triage/prh8390_damageflyn_exact_reseed_rollout.tsv",
    )
    ref_row, out_row, site1_count = rows[8390]
    for q in (0, 1):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=8390, p=q)
    assert site1_count == 0, f"unexpected DamageFlyRoll gate pulse at exact reseed row"


@pytest.mark.integration
def test_qgd_downstream_rng_exception_rows_are_one_step_replay_exact() -> None:
    # QGD accepted-exception proof for downstream rows after the open ftCo_8008DCE0 DamageFlyRoll
    # stream branch:
    # - 8387 is a SpecialHiHoldAir/Shine damage-state row in the full rollout, but a live one-step
    #   segment from the same seed selects vanilla DamageFlyN exactly.
    # - 9106 is a later GuardReflect row in the full rollout, but the exact seed row preserves
    #   GuardReflect, proving it is not the rejected GuardReflect action/timer or ShieldDesc
    #   descriptor-lifetime owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    cases = (
        (8387, 0, 354, 88, 6, 32),
        (9106, 1, 182, 182, 0, 0),
    )
    for record, p, seed_action, ref_action, ref_hitlag, ref_hitstun in cases:
        seed, ref, out = _run_one_step_row(dataset_path, record, p)
        assert int(seed["action_id"][p]) == seed_action
        assert int(ref["action_id"][p]) == ref_action
        assert int(ref["hitlag"][p]) == ref_hitlag
        assert int(ref["hitstun"][p]) == ref_hitstun
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out, ref_row=ref, record=record, p=p
        )


@pytest.mark.integration
def test_gat_initial_damageflyroll_row_is_source_owned_one_step_replay_exact() -> None:
    # GAT rec3106 is the first removed exception row: the seed row is a SpecialAirHi victim struck
    # by a concrete strong AttackAirLw ProcessHit source, so the source-owned Fighter_8006CDA4 /
    # ftCo_8008DCE0 stream path now selects replay's DamageFlyRoll instead of the old alternate
    # free-running DamageFlyN phase.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3106
    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record, p)
    assert int(seed["action_id"][p]) == 356  # SpecialAirHi.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][p]) == 0
    assert int(seed["last_hit_by"][p]) == 1
    assert int(seed["action_id"][1]) == 69  # AttackAirLw source.
    assert int(seed["action_frame"][1]) == 4
    assert int(ref["action_id"][p]) == 91  # DamageFlyRoll.
    assert int(out["action_id"][p]) == 91

    for field in (
        "action_frame",
        "hitlag",
        "hitstun",
        "instance_id",
        "instance_hit_by",
        "last_hit_by",
        "combo_count",
        "last_attack_landed",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    for field in (
        "percent",
        "pos_x",
        "pos_y",
        "speed_air_x_self",
        "speed_y_self",
        "speed_x_attack",
        "speed_y_attack",
    ):
        assert float(out[field][p]) == pytest.approx(float(ref[field][p])), field
    assert out["state_flags"][p].tolist() == ref["state_flags"][p].tolist()

    _assert_transition_identity_lock_fields_match_ref(
        out_row=out, ref_row=ref, record=record, p=1
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p", "seed_action", "ref_action"),
    [
        (3629, 0, 178, 178),
        (3630, 0, 178, 181),
        (3639, 0, 181, 181),
        (5717, 0, 239, 91),
        (6213, 1, 24, 75),
        (6713, 1, 43, 43),
        (6805, 0, 358, 87),
        (8597, 1, 352, 43),
        (11134, 1, 74, 91),
    ],
)
def test_gat_downstream_exception_rows_are_one_step_action_exact(
    record: int, p: int, seed_action: int, ref_action: int
) -> None:
    # These rows used to be approved downstream rows after the open GAT rec3106 DamageFlyRoll RNG
    # stream branch. Direct one-step replay seeds choose the reference action, proving the visible
    # later actions are not local combat, shield, collision, or item owners.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)
    assert int(seed["action_id"][p]) == seed_action
    assert int(ref["action_id"][p]) == ref_action
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ref_action
