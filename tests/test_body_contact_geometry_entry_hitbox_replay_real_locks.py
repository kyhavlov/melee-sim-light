from __future__ import annotations

import numpy as np
import pytest
from pathlib import Path

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


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
        binding.reseed_seed(handle, seed_bytes)
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

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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
def test_fox_attackdash_dynamic_chain_rejects_false_attackairhi_fsp_7078() -> None:
    # Fox AttackDash dynamic-chain collision pose:
    # - Fox part 18 is in ftData.x2C's dynamic chain. Dolphin pre-ftColl primitive probes on
    #   FSP:7078 show the defender's live AttackDash hurtcap-12 endpoints consume that dynamic
    #   JObj state before lb_8000B1CC/lbColl_80006E58, rejecting the false AttackAirHi overlap.
    # - This is data-owned via SSDYNN01's collision-msid predicate; runtime gameplay does not branch
    #   on this replay row or on a hardcoded C msid gate.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,lb_8001044C}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 7078)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


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
