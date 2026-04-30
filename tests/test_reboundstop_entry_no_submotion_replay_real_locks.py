from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset
from tools.eval.dataset import COMPARE_DTYPE


def _run_rollout_to_record(dataset_path: Path, start_record: int, target_record: int):
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
        msl_binding.reseed_seed_rollout(handle, field_bytes(start_record, seed_off, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(int(start_record), int(target_record) + 1):
            msl_binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        msl_binding.destroy(handle)

    return samples["ref_t1"][target_record], out


@dataclass(frozen=True)
class _ReboundStopEntryCase:
    records: tuple[int, int, int]
    rebound_port: int
    seed_action: int
    note: str


@dataclass(frozen=True)
class _GuardSetOffShieldDamageCase:
    dataset_rel: str
    record: int
    defender: int
    expected_x19a4: int
    expected_x19a0: int
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
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
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
    # - The same source ordering also prevents later same-group clank candidates from raising p0's
    #   hitlag with the 12-damage high hitbox after the first accepted p1-hb0/p0-hb3 clank.
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
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][0]) == 56  # AttackHi3
    assert int(seed_t["action_id"][1]) == 50  # AttackDash
    assert int(ref_t1["action_id"][0]) == 237  # ReboundStop
    assert int(ref_t1["action_id"][1]) == 237  # ReboundStop
    assert int(ref_t1["hitlag"][0]) == 6
    assert int(ref_t1["hitlag"][1]) == 5
    assert int(ref_t1["hitstun"][0]) == 0
    assert int(ref_t1["hitstun"][1]) == 0

    for p in (0, 1):
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
            "speed_ground_x_self",
        ):
            got = float(out_row[field][p]) if field in ("percent", "speed_ground_x_self") else int(out_row[field][p])
            exp = float(ref_row[field][p]) if field in ("percent", "speed_ground_x_self") else int(ref_row[field][p])
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
def test_reboundstop_clank_damage_stale_excludes_current_attack_instance_gat_10025() -> None:
    # Replay-real negative for clank/ReboundStop stale provenance:
    # - GAT:10025 has the current AttackS3 instance already in the stale queue from an earlier
    #   same-attack contact.
    # - ftColl_8007ABD0 writes HitCapsule.damage when the capsule is produced/refreshed; later
    #   same-instance stale queue entries must not retroactively lower that live capsule before
    #   ftColl_8007699C's clank threshold.
    # - Older stale instances remain counted by the FSP positive above.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_8007699C}
    # refs/melee/src/melee/ft/ft_0881.c::ft_80089228
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 10025
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    p = 0
    assert int(seed_t["action_id"][p]) == 63
    assert int(seed_t["attack_id"][p]) == 11
    assert int(seed_t["attack_instance"][p]) in set(int(x) for x in seed_t["stale_attack_instance"][p])
    assert int(ref_t1["action_id"][p]) == 90  # DamageFlyTop, not ReboundStop

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "percent",
        "on_ground",
        "instance_hit_by",
        "last_hit_by",
    ):
        got = float(out_row[field][p]) if field == "percent" else int(out_row[field][p])
        exp = float(ref_row[field][p]) if field == "percent" else int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
def test_reboundstop_hitlag_tail_seeds_queued_ground_accel_fsp_472() -> None:
    # Replay-real lock for the hidden ReboundStop -> Rebound xE8 lane:
    # - ftCo_80099D9C writes `mv.co.rebound.x0` through ftCommon_800804A0 on clank entry.
    # - ReboundStop hitlag freezes that lane; ftCo_ReboundStop_Anim enters Rebound on hitlag exit.
    # - The first Rebound Phys frame uses old ground speed for movement, then applies xE8 to
    #   post-frame ground velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    #   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_Rebound_Phys}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 472
    p = 1
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p]) == 237  # ReboundStop
    assert int(seed_t["hitlag"][p]) == 1
    assert int(ref_t1["action_id"][p]) == 238  # Rebound
    assert int(ref_t1["hitlag"][p]) == 0
    assert float(seed_t["rebound_ground_accel_2_f32"][p]) != 0.0
    assert float(seed_t["rebound_anim_rate_f32"][p]) > 0.0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    for field in ("pos_x", "speed_ground_x_self", "speed_air_x_self"):
        got = float(out_row[field][p])
        exp = float(ref_row[field][p])
        assert got == pytest.approx(exp, abs=2e-6), (
            f"record={record} p={p} field={field} expected={exp} got={got}"
        )


@pytest.mark.integration
def test_rebound_first_frame_seed_and_rollout_keep_hidden_anim_rate_fsp() -> None:
    # Replay-real positive + rollout lock for `mv.co.rebound.anim_start`.
    # The first Rebound row has already consumed xE8, but Rebound's anim rate is still the value
    # stored by ftCo_80099D9C before ReboundStop hitlag. Do not reconstruct it from post-xE8 gr_vel.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    #   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 473
    p = 1
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p]) == 238  # Rebound
    assert int(seed_t["action_frame"][p]) == 0
    assert float(seed_t["rebound_anim_rate_f32"][p]) > 0.0
    assert int(ref_t1["action_frame"][p]) == 3

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p])
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=2e-6
    )

    rollout_ref, rollout_out = _run_rollout_to_record(dataset_path, 450, 474)
    assert int(rollout_out["action_id"][p]) == int(rollout_ref["action_id"][p])
    assert int(rollout_out["action_frame"][p]) == int(rollout_ref["action_frame"][p])
    assert float(rollout_out["speed_ground_x_self"][p]) == pytest.approx(
        float(rollout_ref["speed_ground_x_self"][p]), abs=2e-6
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _GuardSetOffShieldDamageCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "FavorableSuperficialPig.msl"
            ),
            record=3100,
            defender=0,
            expected_x19a4=6,
            expected_x19a0=7,
            note="FSP AttackAir shield hit needs x19A0 > x19A4 for exact shield HP",
        ),
        _GuardSetOffShieldDamageCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HilariousVillainousGiraffe.msl"
            ),
            record=5544,
            defender=0,
            expected_x19a4=6,
            expected_x19a0=8,
            note="HHG AttackAir shield hit needs the separate x19A0 shield-damage accumulator",
        ),
        _GuardSetOffShieldDamageCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=7124,
            defender=0,
            expected_x19a4=6,
            expected_x19a0=8,
            note="PRH AttackAir shield hit keeps hitlag scalar and shield HP accumulator split",
        ),
    ],
)
def test_guardsetoff_shield_damage_taken_seed_is_separate_from_hitlag_scalar(
    case: _GuardSetOffShieldDamageCase,
) -> None:
    # Replay-real positive lock for the GuardSetOff shield-hit scalar split:
    # - ftColl_80076CBC writes x19A4 from max getEnvDmg(hit0->damage) for hitlag/shieldstun.
    # - The same contact frame also accumulates x19A0_shieldDamageTaken, which may exceed x19A4
    #   when hit0->x34 shield damage contributes before Fighter_ProcessHit drains shield HP.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > case.record, f"dataset too short for lock row: record={case.record}"

    seed_t = ds.samples[case.record]["seed_t"]
    ref_t1 = ds.samples[case.record]["ref_t1"]
    p = int(case.defender)
    assert int(seed_t["combat_shield_hit_int_damage"][p]) == case.expected_x19a4, case.note
    assert int(seed_t["combat_shield_damage_taken"][p]) == case.expected_x19a0, case.note
    assert case.expected_x19a0 > case.expected_x19a4, case.note
    assert int(ref_t1["action_id"][p]) == 181, case.note  # GuardSetOff
    assert int(ref_t1["hitlag"][p]) > 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    for field in ("action_id", "hitlag", "hitstun"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={case.record} p={p} field={field} expected={exp} got={got}"
    assert float(out_row["shield_hp"][p]) == pytest.approx(float(ref_row["shield_hp"][p]), abs=1e-6)


@pytest.mark.integration
def test_guardsetoff_shield_damage_taken_seed_does_not_override_runtime_selected_contact_qgd_3938() -> None:
    # Replay-real negative lock for the x19A0 producer boundary:
    # - QGD 3938 needs the x19A4 hitlag scalar seed, but replay-visible shield HP is already exact
    #   through runtime selected-contact ownership.
    # - A broad x19A0 seed for x19A0<=x19A4 rows would replace the selected contact and regress
    #   shield HP. Keep those multi-contact ordering rows unseeded until exact per-HitCapsule
    #   shield-contact order is exposed.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3938
    defender = 0
    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed_t = ds.samples[record]["seed_t"]
    assert int(seed_t["combat_shield_hit_int_damage"][defender]) == 15
    assert int(seed_t["combat_shield_damage_taken"][defender]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for field in ("action_id", "hitlag", "hitstun"):
        got = int(out_row[field][defender])
        exp = int(ref_row[field][defender])
        assert got == exp, f"record={record} p={defender} field={field} expected={exp} got={got}"
    assert float(out_row["shield_hp"][defender]) == pytest.approx(
        float(ref_row["shield_hp"][defender]), abs=1e-6
    )
