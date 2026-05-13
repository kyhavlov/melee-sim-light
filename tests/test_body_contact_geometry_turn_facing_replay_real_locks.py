from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _run_pre_combat_debug_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_f08b_body_contact_geometry_bhh1599_attackhi3_live_hsd_pose_positive_lock() -> None:
    # F08b BODY collision-space positive lock:
    # - Vanilla selects p0 AttackAirN hb1 against p1 AttackHi3 hurtcap 12 before BODY admission.
    # - The sim must supply the live HSD JObj/AObj/dynamics hurtcap primitive and still let the
    #   normal ftColl_80078C70 -> lbColl_8000805C/80006E58 predicate decide the hit.
    #
    # Decomp/data anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # - refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # - refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # - reports/triage/20260416_bhh1599_collision_probe11/
    #   BlondHardHippopotamus_rec1599_p1_f1473_1480_collision_probe.jsonl
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1599
    attacker = 0
    defender = 1
    hitbox_id = 1
    hurtcap_id = 12

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["char_id"][attacker]) == 1  # Fox
    assert int(seed_t["char_id"][defender]) == 1  # Fox
    assert int(seed_t["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed_t["action_id"][defender]) == 56  # AttackHi3
    assert int(seed_t["animation_index"][attacker]) == 68
    assert int(seed_t["animation_index"][defender]) == 58
    assert int(ref_t1["action_id"][defender]) == 88  # DamageFlyN
    assert float(ref_t1["percent"][defender]) > float(seed_t["percent"][defender])

    _, contacts, _, timing = _run_pre_combat_debug_row(dataset_path, record, attacker, hitbox_id)
    assert int(timing["msid"]) == 68  # ftCo_SM_AttackAirN
    assert int(timing["enabled_cur"]) == 1
    body_contacts = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == hitbox_id
        and int(c["hurtcap_id"]) == hurtcap_id
        and int(c["contact_kind"]) == 0
    ]
    assert body_contacts, "expected live HSD pose hurtcap to admit AttackAirN hb1 BODY overlap"

    contact = body_contacts[0]
    # The dynamic owner is not a captured primitive overlay: keep a broad probe-neighborhood guard
    # on the selected part-18 primitive, and let the one-step lock below prove ftColl_80076ED8
    # selects vanilla DamageFlyN rather than the rejected low-hurtcap DamageFlyLw bridge.
    assert 18.0 <= float(contact["hurtcap_ax"]) <= 21.0
    assert 6.0 <= float(contact["hurtcap_ay"]) <= 9.5
    assert -3.5 <= float(contact["hurtcap_az"]) <= -0.4
    assert 17.5 <= float(contact["hurtcap_bx"]) <= 20.0
    assert 6.0 <= float(contact["hurtcap_by"]) <= 9.5
    assert -3.5 <= float(contact["hurtcap_bz"]) <= -0.4
    assert float(contact["hurtcap_radius"]) == pytest.approx(1.5552, abs=1e-6)

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
            assert int(out_row[field][p]) == int(ref_row[field][p])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))
    assert int(out_row["last_hit_by"][defender]) == attacker
    assert int(out_row["instance_hit_by"][defender]) == int(seed_t["instance_id"][attacker])


@pytest.mark.integration
def test_f08b_body_contact_geometry_bhh1163_turn_internal_facing_positive_lock() -> None:
    # F08b BODY collision-space positive lock:
    # - Replay proves an AttackAirB BODY hit at t+1: defender percent increases, both fighters
    #   enter hitlag, and source identity points at the attacker.
    # - The sim must place the defender Turn hurtcaps using ftCo_Turn's internal has_turned
    #   orientation, admit the normal BODY overlap, and then run ftColl_80076ED8 followup.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #     ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1163
    attacker = 0
    defender = 1
    hitbox_id = 2

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 18  # Turn
    assert int(seed_t["turn_has_turned"][defender]) == 1
    assert int(ref_t1["action_id"][defender]) == 76  # DamageHi2
    assert float(ref_t1["percent"][defender]) > float(seed_t["percent"][defender])
    assert int(ref_t1["hitlag"][attacker]) > 0
    assert int(ref_t1["hitlag"][defender]) > 0
    assert int(ref_t1["last_hit_by"][defender]) == attacker
    assert int(ref_t1["instance_hit_by"][defender]) == int(seed_t["instance_id"][attacker])

    _, contacts, _, timing = _run_pre_combat_debug_row(dataset_path, record, attacker, hitbox_id)
    assert int(timing["msid"]) == 70  # ftCo_SM_AttackAirB
    assert int(timing["enabled_cur"]) == 1
    body_contacts = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == hitbox_id
        and int(c["contact_kind"]) == 0
    ]
    assert body_contacts, "expected Turn internal-facing hurtcaps to admit the BODY overlap"

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
            assert int(out_row[field][p]) == int(ref_row[field][p])
    for field in ("percent",):
        assert float(out_row[field][defender]) == pytest.approx(float(ref_row[field][defender]))
    assert int(out_row["last_hit_by"][defender]) == attacker
    assert int(out_row["instance_hit_by"][defender]) == int(seed_t["instance_id"][attacker])


@pytest.mark.integration
def test_f08b_body_contact_geometry_bhh1169_turn_hitlist_continuation_stays_suppressed() -> None:
    # Followup lock for the same accepted BODY hit:
    # - BHH:1163 admits the first AttackAirB BODY hit.
    # - Later reseeded rows must carry the HitCapsule victim list created by that accepted hit,
    #   so the still-active AttackAirB capsule does not re-hit after hitlag exits.
    #
    # Seed-history owner:
    # - runtime advances anim_frame_f32 before hitbox/hurtcap refresh;
    # - ftColl_80076ED8 registers the victim after BODY damage admission;
    # - lbColl_8000ACFC suppresses subsequent frames while the victim is present.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1169
    attacker = 0
    defender = 1

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 76  # DamageHi2
    assert int(seed_t["combat_hitlist_cd"][attacker, 0, defender]) != 0
    assert int(ref_t1["hitlag"][attacker]) == 0
    assert int(ref_t1["hitlag"][defender]) == 0
    assert float(ref_t1["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))

    for p in (attacker, defender):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "hitlag",
            "hitstun",
            "last_hit_by",
            "instance_hit_by",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p])
        assert float(out_row["percent"][p]) == pytest.approx(float(ref_row["percent"][p]))


@pytest.mark.integration
@pytest.mark.parametrize("record", [1830, 1831, 1832, 1833, 1834])
def test_f08b_attackdash_kneebend_hidden_hitcapsule_shield_lineage_stays_suppressed(
    record: int,
) -> None:
    # F08b residual split lock:
    # - PositiveRevolvingHyena has Falco AttackDash hb1 hit p1's shield, then the defender exits
    #   GuardSetOff/Guard into KneeBend while the same AttackDash HitCapsule remains active.
    # - Vanilla does not admit a BODY hit on the KneeBend rows because lbColl_8000ACFC still sees
    #   the defender in HitCapsule.victims_1 from the prior shield hit.
    # - Reseed must preserve that hidden per-HitCapsule list across visible defender action and
    #   instance_id proxy changes; a coarse group stale-latch cleanup must not erase it.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        / "PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 0
    defender = 1
    hitbox_id = 1

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["char_id"][attacker]) == 22  # Falco
    assert int(seed_t["action_id"][attacker]) == 50  # AttackDash
    assert int(seed_t["action_id"][defender]) == 24  # KneeBend
    assert int(seed_t["animation_index"][attacker]) == 52
    assert int(seed_t["combat_hitlist_hb_valid"][attacker, hitbox_id]) == 1
    assert int(seed_t["combat_hitlist_hb_cd"][attacker, hitbox_id, defender]) == 0xFFFF
    assert int(ref_t1["hitlag"][defender]) == 0
    assert float(ref_t1["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out_row[field][defender]) == int(ref_row[field][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))


@pytest.mark.integration
@pytest.mark.parametrize("record", [1803, 1804])
def test_f08b_attackdash_escapef_guardsetoff_onset_hitcapsule_lineage_stays_suppressed(
    record: int,
) -> None:
    # Same hidden-HitCapsule owner as the Guard/KneeBend rows, but the replay-visible shield-hit
    # onset enters GuardSetOff from a non-Guard previous visible action (DownStandD). The
    # provenance proof is GuardSetOff hitlag onset plus shield HP loss, not a Guard-family pre-row.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        / "BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 0
    defender = 1

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["char_id"][attacker]) == 1  # Fox
    assert int(seed_t["action_id"][attacker]) == 50  # AttackDash
    assert int(seed_t["action_id"][defender]) == 233  # EscapeF
    assert int(seed_t["animation_index"][attacker]) == 52
    for hitbox_id in (0, 1):
        assert int(seed_t["combat_hitlist_hb_valid"][attacker, hitbox_id]) == 1
        assert int(seed_t["combat_hitlist_hb_cd"][attacker, hitbox_id, defender]) == 0xFFFF
    assert int(ref_t1["hitlag"][defender]) == 0
    assert float(ref_t1["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out_row[field][defender]) == int(ref_row[field][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))


@pytest.mark.integration
@pytest.mark.parametrize("record", [11146, 11147])
def test_f08b_attackairlw_kneebend_authoritative_hitcapsule_seed_bypasses_stale_trim(
    record: int,
) -> None:
    # Same hidden-HitCapsule owner, but this row exercises the runtime stale-trim boundary:
    # the seed already has authoritative per-hitbox victims_1 for AttackAirLw hb0/hb1, so
    # src/hitboxes.c must not apply the legacy dense-group stale cleanup and erase the latch.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        / "ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 0
    defender = 1

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed_t["action_id"][defender]) == 24  # KneeBend
    assert int(seed_t["animation_index"][attacker]) == 72
    for hitbox_id in (0, 1):
        assert int(seed_t["combat_hitlist_hb_valid"][attacker, hitbox_id]) == 1
        assert int(seed_t["combat_hitlist_hb_cd"][attacker, hitbox_id, defender]) == 0xFFFF
    assert int(ref_t1["hitlag"][defender]) == 0
    assert float(ref_t1["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out_row[field][defender]) == int(ref_row[field][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "attacker", "defender"),
    [
        (2535, 3, 2),
        (6150, 2, 3),
    ],
)
def test_doubles_escapef_root_facing_body_pose_admits_aerial_hit(record: int, attacker: int, defender: int) -> None:
    # EscapeF root-facing BODY owner:
    # - EscapeF frame 20 emits both `set_hit_status(0)` and `set_throw_flags(hit_idx=0)`.
    # - These doubles rows are post-frame-20 EscapeF snapshots where visible scalar facing has
    #   diverged from the motion-entry root facing lane (`facing_dir1`).
    # - Vanilla samples the BODY hurtcaps from the motion-entry root-facing collision matrix and
    #   admits the aerial hit; using visible scalar facing misses it.
    #
    # Source anchors:
    # - data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"].events
    # - refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState copies facing_dir -> facing_dir1)
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][defender]) == 233  # EscapeF
    assert int(seed_t["action_frame"][defender]) >= 20
    assert int(seed_t["facing_dir1"][defender]) != (1 if int(seed_t["facing"][defender]) else -1)
    assert int(ref_t1["action_id"][defender]) in (75, 76)  # DamageHi1/2
    assert int(ref_t1["hitlag"][defender]) > 0
    assert int(ref_t1["last_hit_by"][defender]) == attacker

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out_row[field][defender]) == int(ref_row[field][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))

    def use_visible_facing(seed_t_mut) -> None:
        seed_t_mut["facing_dir1"][0, defender] = 1 if int(seed_t_mut["facing"][0, defender]) else -1

    _, _, visible_facing_out = _run_one_step_row(dataset_path, record, defender, seed_mutator=use_visible_facing)
    assert int(visible_facing_out["action_id"][defender]) == 233
    assert int(visible_facing_out["hitlag"][defender]) == 0
    assert float(visible_facing_out["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))


@pytest.mark.integration
def test_f08b_body_contact_geometry_cardinal_attackairb_turn_control_stays_suppressed() -> None:
    # Negative same-shape control:
    # Cardinal TBK:5523 also has a steady AttackAirB attacker and grounded Turn defender, but replay
    # has no BODY hit on this frame. The Turn internal-facing owner must not become a broad
    # AttackAirB/Turn admission bridge, especially before ftCo_Turn has flipped internally.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #     ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5523
    attacker = 0
    defender = 1
    hitbox_id = 2

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 18  # Turn
    assert int(seed_t["turn_has_turned"][defender]) == 0

    _, contacts, _, _ = _run_pre_combat_debug_row(dataset_path, record, attacker, hitbox_id)
    body_contacts = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == hitbox_id
        and int(c["contact_kind"]) == 0
    ]
    assert not body_contacts, "pre-turn-facing BODY overlap must remain suppressed"

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    assert int(ref_row["hitlag"][defender]) == 0
    assert int(out_row["hitlag"][defender]) == 0
    assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))


@pytest.mark.integration
def test_doubles_attackhi3_powershield_hitlist_latch_suppresses_late_guard_body() -> None:
    # Replay-rollout positive for a hidden powershield HitCapsule victim-list latch:
    # - p0 enters grounded AttackHi3 while p2 is in the GuardOn -> GuardReflect powershield window.
    # - Source `ftColl_80076CBC` registers the attacker HitCapsule victim before the x221C_b2
    #   powershield branch suppresses ordinary shield damage / GuardSetOff effects.
    # - When p2 later becomes visible Guard, the same AttackHi3 hit_group must remain suppressed
    #   by lbColl_8000ACFC instead of falling through to BODY.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80078C70}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80094138
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    start = 2048
    stop = 2064
    attacker = 0
    defender = 2

    def rollout(seed_mutator=None) -> np.void:
        seed = samples["seed_t"][start : start + 1].copy()
        if seed_mutator is not None:
            seed_mutator(seed)
        seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
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
                prev_input_bytes = np.frombuffer(
                    samples["prev_input_t"][record : record + 1].tobytes(order="C"),
                    dtype=np.uint8,
                ).copy().reshape(1, input_stride)
                input_bytes = np.frombuffer(
                    samples["input_t"][record : record + 1].tobytes(order="C"), dtype=np.uint8
                ).copy().reshape(1, input_stride)
                binding.step_input(handle, prev_input_bytes, input_bytes)
                binding.write_compare(handle, out_compare_bytes)
            return out_view[0].copy()
        finally:
            binding.destroy(handle)

    seed_start = samples["seed_t"][start]
    ref_stop = samples["ref_t1"][stop]
    assert int(seed_start["action_id"][attacker]) == 44  # AttackAirHi prior hidden hitlist owner
    assert int(seed_start["combat_hitlist_cd"][attacker, 0, defender]) != 0
    assert int(ref_stop["action_id"][attacker]) == 56  # AttackHi3
    assert int(ref_stop["action_id"][defender]) == 179  # Guard

    out = rollout()
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
      assert int(out[field][defender]) == int(ref_stop[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref_stop["percent"][defender]))

    def clear_hidden_latch(seed_t_mut: np.ndarray) -> None:
        seed_t_mut["combat_hitlist_cd"][0, attacker, 0, defender] = 0
        seed_t_mut["combat_hitlist_victim_iid"][0, attacker, 0, defender] = 0

    no_latch_out = rollout(clear_hidden_latch)
    assert int(no_latch_out["action_id"][defender]) == 88  # DamageFlyN
    assert int(no_latch_out["hitlag"][defender]) > 0
    assert float(no_latch_out["percent"][defender]) > float(seed_start["percent"][defender])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record"),
    [
        ("AttachedGoodNaturedGuanaco.msl", 1471),
        ("QuerulousGrandDinosaur.msl", 7589),
        ("TreasuredBackKangaroo.msl", 870),
    ],
)
def test_f08b_body_contact_geometry_rejects_broad_static_grounded_attack_dynamics(
    dataset_name: str, record: int
) -> None:
    # Regression locks from the rejected broad grounded-common-attack dynamics bake:
    # applying the Fox dynamic-tail descriptor as a static SSANIM bend to all grounded attacks
    # suppressed these vanilla BODY hits, especially Fox ftCo_SM_AttackLw3 (msid 59) hurtcap rows.
    # The real owner is stateful lb_8001044C dynamic-node pose, not a broad static action bucket.
    #
    # Decomp/data anchors:
    # - refs/melee/src/melee/ft/ftdynamics.c::ftCo_8009DD94
    # - refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    # - reports/triage/20260416_hsd_dynamic_regression_broad/
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent"
        / dataset_name
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    assert any(int(seed_t["animation_index"][p]) == 59 for p in range(2))

    for p in range(2):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "hitlag",
            "hitstun",
            "last_attack_landed",
            "instance_hit_by",
            "combo_count",
            "on_ground",
            "hurtbox_state",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p])
        assert float(out_row["percent"][p]) == pytest.approx(float(ref_row["percent"][p]))
