from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _ReboundStopEntryCase:
    records: tuple[int, int, int]
    rebound_port: int
    seed_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ReboundStopEntryCase(
            records=(2731, 2732, 2733),
            rebound_port=1,
            seed_action=44,
            note="QGD jab clank -> ReboundStop no-submotion entry",
        ),
        _ReboundStopEntryCase(
            records=(2989, 2990, 2991),
            rebound_port=0,
            seed_action=50,
            note="QGD dash-attack clank -> ReboundStop no-submotion entry (p0)",
        ),
        _ReboundStopEntryCase(
            records=(2989, 2990, 2991),
            rebound_port=1,
            seed_action=44,
            note="QGD jab clank -> ReboundStop no-submotion entry (p1)",
        ),
    ],
)
def test_reboundstop_entry_no_submotion_target_pm1_rows_are_replay_exact(
    case: _ReboundStopEntryCase,
) -> None:
    # Replay-real lock for the clank-owned ReboundStop entry snapshot:
    # - ftCo_80099D9C enters ReboundStop.
    # - The destination replay-visible entry row keeps no submotion (animation_index=-1) and
    #   action_frame=-1 until ReboundStop_Anim consumes into Rebound on the first !hitlag callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    #   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in case.records:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = case.records[1]
    seed_t = samples[target]["seed_t"]
    ref_t1 = samples[target]["ref_t1"]
    p = int(case.rebound_port)

    assert int(seed_t["action_id"][p]) == int(case.seed_action), case.note
    assert int(seed_t["hitlag"][p]) == 0, case.note
    assert int(ref_t1["action_id"][p]) == 237, case.note  # ReboundStop
    assert int(ref_t1["action_frame"][p]) == -1, case.note
    assert int(ref_t1["animation_index"][p]) == 0xFFFFFFFF, case.note
    assert int(ref_t1["hitlag"][p]) > 0, case.note

    for rec in case.records:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=rec,
            p=p,
        )


@pytest.mark.integration
def test_reboundstop_entry_uses_swept_hitbox_hitbox_clank_fsp_5466() -> None:
    # Replay-real lock for the BODY-geometry clank sub-owner:
    # - ftColl_80078C70 checks hitbox-vs-hitbox clank before BODY hitbox-vs-hurtcap admission.
    # - The hitbox-vs-hitbox predicate is lbColl_80007AFC, which consumes swept HitCapsule
    #   x58->x4C segments, not only current-frame centers.
    # - FSP:5466 previously fell through to BODY DamageFlyTop because the clank overlap used
    #   current centers only; vanilla enters ReboundStop with no damage-state entry.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007AFC,lbColl_80006094}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 5466
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = samples[record]["seed_t"]
    ref_t1 = samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p]) == 50  # AttackDash
    assert int(seed_t["hitlag"][p]) == 0
    assert int(seed_t["hitstun"][p]) == 0
    assert int(ref_t1["action_id"][p]) == 237  # ReboundStop
    assert int(ref_t1["animation_index"][p]) == 0xFFFFFFFF
    assert int(ref_t1["hitstun"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    got_sf = out_row["state_flags"][p].tolist()
    exp_sf = ref_row["state_flags"][p].tolist()
    assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


@pytest.mark.integration
def test_reboundstop_entry_clank_precedes_stale_body_hitlist_hhg_8674() -> None:
    # Replay-real lock for the remaining former-F08h1 collision-order slice:
    # - p0 AttackDash hb1 carries a seeded BODY victim ring from the prior active window.
    # - p1 AttackHi3 creates a new same-group clanking hitbox this frame.
    # - Vanilla still resolves hitbox-vs-hitbox clank/ReboundStop before p1 hb1 can fall through to
    #   BODY DamageFlyTop on p0.
    #
    # This protects the runtime distinction between live HitCapsule clank geometry and dense
    # replay-reconstructed BODY victim rings.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C,inlineA0,inlineA1}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007AFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 8674
    p = 0
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p]) == 50  # AttackDash
    assert int(ref_t1["action_id"][p]) == 237  # ReboundStop
    assert int(ref_t1["hitstun"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "hitstun"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert int(out_row["hitlag"][p]) > 0


@pytest.mark.integration
def test_reboundstop_same_group_clank_suppresses_enable_edge_body_fsp_467() -> None:
    # Replay-real lock for degenerate swept-capsule clank geometry plus same-group clank
    # registration:
    # - p1 AttackDash hitboxes are newly enabled, so ftColl_8007AD18 has x58 == x4C.
    # - lbColl_80007AFC must still test that point capsule against p0's swept AttackHi3 capsule.
    # - ftColl_8007699C inlineA0/inlineA1 register the clank victim across every active
    #   HitCapsule with the same hit_group, so p1 remains in ReboundStop instead of being
    #   overwritten by a BODY DamageHi3 follow-up from an earlier same-group p0 slot.
    # refs/melee/src/melee/ft/ftcoll.c::{
    #   ftColl_80078C70,ftColl_8007699C,inlineA0,inlineA1,ftColl_8007AD18}
    # refs/melee/src/melee/lb/lbcollision.c::{
    #   lbColl_80007AFC,lbColl_80006094,lbColl_80008688,lbColl_8000ACFC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 467
    p = 1
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][0]) == 56  # AttackHi3
    assert int(seed_t["action_id"][1]) == 50  # AttackDash
    assert int(ref_t1["action_id"][0]) == 237  # ReboundStop
    assert int(ref_t1["action_id"][1]) == 237  # ReboundStop
    assert int(ref_t1["hitstun"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "percent",
        "instance_id",
        "instance_hit_by",
        "last_hit_by",
        "last_attack_landed",
        "on_ground",
    ):
        got = int(out_row[field][p]) if field != "percent" else float(out_row[field][p])
        exp = int(ref_row[field][p]) if field != "percent" else float(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()
