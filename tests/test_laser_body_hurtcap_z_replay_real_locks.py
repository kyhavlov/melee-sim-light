from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def test_falco_laser_airborne_fall_body_contact_stays_positive_without_broad_z_force() -> None:
    # Replay-real positive for ordinary item BODY geometry:
    # ftColl_8007925C routes item BODY through lbColl_8000805C. TBK:2901 is a state0
    # Falco-laser/Fall row whose BODY hit remains source-owned by the item x58->x4C packet without
    # inferring the optional ftCommon_8007F804 x34_scale.z matrix branch from visible Fall alone.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 2901, 0)
    p = 0
    slot = 0

    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hurtbox_state"][p]) == 0
    assert int(seed["last_attack_landed"][p]) != int(seed["items"][slot]["attack_id"])
    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(ref["items"][slot]["exists"]) == 0

    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), field


def test_falco_laser_airborne_fall_body_keeps_adjacent_no_hit_alive() -> None:
    # Adjacent negative: one frame before the BODY hit, the laser must remain alive and Fall must
    # continue. This protects against broad airborne/Fall or tolerance gates.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 2900, 0)
    p = 0
    slot = 0

    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(seed["items"][slot]["type"]) == 55
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0


def test_marth_fall_high_cap_laser_near_miss_does_not_infer_x34_scale_z() -> None:
    # Official-suite Marth negative for the optional ftCommon_8007F804 item BODY branch:
    # VSA:2008 has a state0 Falco laser near Marth Fall cap2 (high bucket). Visible airborne Fall
    # does not prove non-unit fp->x34_scale.z, so lbColl_8000805C must stay on the seed-visible
    # hurtcap depth instead of flattening endpoints to cur_pos.z. Vanilla keeps Marth in Fall and
    # keeps the laser alive.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 2008, 0)
    p = 0
    slot = 1

    assert int(seed["char_id"][p]) == 18  # Marth
    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hurtbox_state"][p]) == 0
    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(seed["items"][slot]["state"]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6)
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert int(out["items"][slot]["exists"]) == int(ref["items"][slot]["exists"]) == 1


def test_falco_laser_airborne_fall_z_lane_respects_same_attack_hitlist_carry() -> None:
    # Negative for item victim-ring/callback timing: PPA:4124 has Fall/Falco-laser geometry, but the
    # defender already carries the same owner attack id. Decomp item hitlists are updated via
    # it_8026FAC4/lbColl_80008688, so source must not create a fresh BODY consume here.
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 4124, 0)
    p = 0
    slot = 1

    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(seed["items"][slot]["type"]) == 55
    assert int(seed["last_attack_landed"][p]) == int(seed["items"][slot]["attack_id"])
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0


def test_sds_falco_laser_airborne_fall_exact_lbcoll_keeps_edge_no_hit_alive() -> None:
    # SDS:148/149 covers an airborne Fall state0 laser edge: the first frame is a live lbColl edge
    # miss and the next frame is the BODY hit. The source path must use the exact x58->x4C
    # HitCapsule packet instead of a broad 2D swept approximation that consumes the shot one frame
    # early.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/dream_land_recent/ShadyDecimalStarling.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    edge_seed, edge_ref, edge_out = _run_one_step_row(dataset_path, 148, 0)
    p = 0
    slot = 0
    assert int(edge_seed["action_id"][p]) == 29  # Fall
    assert int(edge_seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(edge_seed["items"][slot]["state"]) == 0
    assert int(edge_ref["items"][slot]["exists"]) == 1
    assert int(edge_out["items"][slot]["exists"]) == 1
    assert int(edge_out["action_id"][p]) == int(edge_ref["action_id"][p]) == 29
    assert int(edge_out["hitlag"][p]) == int(edge_ref["hitlag"][p]) == 0
    assert int(edge_out["hitstun"][p]) == int(edge_ref["hitstun"][p]) == 0

    hit_seed, hit_ref, hit_out = _run_one_step_row(dataset_path, 149, 0)
    assert int(hit_seed["action_id"][p]) == 29
    assert int(hit_seed["items"][slot]["type"]) == 55
    assert int(hit_seed["items"][slot]["state"]) == 0
    assert int(hit_ref["items"][slot]["exists"]) == 0
    assert int(hit_out["items"][slot]["exists"]) == 0
    assert int(hit_out["action_id"][p]) == int(hit_ref["action_id"][p]) == 84  # DamageAir1
    assert int(hit_out["hitlag"][p]) == int(hit_ref["hitlag"][p]) == 4
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 9


def test_ppa_same_attack_low_leg_laser_body_remains_eligible() -> None:
    # Adjacent aggregate control for the SDS hb1/cap11 shallow-miss owner: PPA:4140 is also a
    # state0 Falco laser against airborne Fall low-leg geometry, but the defender is carrying the
    # same source attack id from a prior DamageAir sequence and source accepts the live shot. The
    # low-leg miss owner must not become a broad hb1/cap11 suppressor.
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 4140, 0)
    p = 0
    slot = 1
    assert int(seed["action_id"][p]) == 29  # Fall
    assert int(seed["seed_prev_action_id"][p]) == 84  # DamageAir1
    assert int(seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(seed["last_attack_landed"][p]) == int(seed["items"][slot]["attack_id"])
    assert int(ref["items"][slot]["exists"]) == 0
    assert int(out["items"][slot]["exists"]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 84
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 4
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 9


def test_tvr_jumpf_reflect_behavior_shallow_laser_contact_waits_for_deep_body() -> None:
    # TVR:3905/3906 covers early JumpF with stale reflect-behavior state and a Falco laser exact
    # lbColl edge candidate. The shallow frame must not consume the shot; the next deeper overlap
    # owns the normal BODY hit. This protects the source owner from becoming a broad JumpF or
    # laser-age suppressor.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/items/lasers.bin (MSLLASR1 laser_size/state0 HitCapsule)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    shallow_seed, shallow_ref, shallow_out = _run_one_step_row(dataset_path, 3905, 0)
    p = 0
    assert int(shallow_seed["action_id"][p]) == 25  # JumpF
    assert int(shallow_seed["items"][0]["type"]) == 55  # Falco laser
    assert int(shallow_seed["items"][0]["instance_id"]) == 772
    assert int(shallow_ref["action_id"][p]) == 25
    assert int(shallow_out["action_id"][p]) == 25
    assert int(shallow_out["hitlag"][p]) == int(shallow_ref["hitlag"][p]) == 0
    assert int(shallow_out["items"][0]["exists"]) == int(shallow_ref["items"][0]["exists"]) == 1

    deep_seed, deep_ref, deep_out = _run_one_step_row(dataset_path, 3906, 0)
    assert int(deep_seed["action_id"][p]) == 25  # JumpF
    assert int(deep_seed["items"][0]["type"]) == 55
    assert int(deep_seed["items"][0]["instance_id"]) == 772
    assert int(deep_ref["action_id"][p]) == 84  # DamageAir1
    assert int(deep_out["action_id"][p]) == 84
    assert int(deep_out["hitlag"][p]) == int(deep_ref["hitlag"][p]) == 4
    assert int(deep_out["items"][0]["exists"]) == int(deep_ref["items"][0]["exists"]) == 0


def test_tvr_jumpaerial_tail_laser_shallow_contact_waits_for_deep_body() -> None:
    # TVR:9511/9512 covers the state0 Falco-laser trailing-half HitCapsule against extracted
    # cap12/FtPart-18 tail. The shallow edge frame must stay in JumpAerialF with the laser alive,
    # while the next deeper overlap consumes the projectile. TVR:11488 is an adjacent younger hb1
    # candidate that must remain BODY-eligible, proving this is not a generic cap12 suppressor.
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    # data/items/lasers.bin (MSLLASR1 state0 size/offsets)
    # data/hurtcaps/{fox,falco}.bin cap12 -> FtPart 18.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    shallow_seed, shallow_ref, shallow_out = _run_one_step_row(dataset_path, 9511, 0)
    p = 0
    slot = 1
    assert int(shallow_seed["action_id"][p]) == 27  # JumpAerialF
    assert int(shallow_seed["items"][slot]["type"]) == 55  # Falco laser
    assert int(shallow_seed["items"][slot]["instance_id"]) == 1959
    assert int(shallow_ref["action_id"][p]) == 27
    assert int(shallow_out["action_id"][p]) == 27
    assert int(shallow_out["hitlag"][p]) == int(shallow_ref["hitlag"][p]) == 0
    assert int(shallow_out["items"][slot]["exists"]) == int(shallow_ref["items"][slot]["exists"]) == 1

    deep_seed, deep_ref, deep_out = _run_one_step_row(dataset_path, 9512, 0)
    assert int(deep_seed["action_id"][p]) == 27
    assert int(deep_seed["items"][slot]["type"]) == 55
    assert int(deep_seed["items"][slot]["instance_id"]) == 1959
    assert int(deep_ref["action_id"][p]) == 84  # DamageAir1
    assert int(deep_out["action_id"][p]) == 84
    assert int(deep_out["hitlag"][p]) == int(deep_ref["hitlag"][p]) == 3
    assert int(deep_out["items"][slot]["exists"]) == int(deep_ref["items"][slot]["exists"]) == 0

    younger_seed, younger_ref, younger_out = _run_one_step_row(dataset_path, 11488, 0)
    assert int(younger_seed["action_id"][p]) == 27
    assert int(younger_seed["items"][slot]["type"]) == 55
    assert int(younger_seed["items"][slot]["instance_id"]) == 2449
    assert int(younger_ref["action_id"][p]) == 84
    assert int(younger_out["action_id"][p]) == 84
    assert int(younger_out["hitlag"][p]) == int(younger_ref["hitlag"][p]) == 3


def test_fox_laser_state0_script_damage_update_hits_for_two_damage_ppa() -> None:
    # PPA rec1151 is a state0 Fox laser after the article script updates only hb1's HitCapsule
    # damage from 3 to 2. Runtime damage is owned by the extracted item script update lane, not by
    # stale state or replay-row percent fitting; the adjacent rec1150 laser remains alive with no
    # BODY hit yet.
    # refs/melee/src/melee/it/it_2725.c::it_80275158
    # data/items/lasers.bin (MSLLASR1 v7 damage_update_* fields)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_pre, ref_pre, out_pre = _run_one_step_row(dataset_path, 1150, 1)
    assert int(seed_pre["items"][2]["type"]) == 54  # Fox laser.
    assert int(seed_pre["items"][2]["state"]) == 0
    assert float(out_pre["percent"][1]) == pytest.approx(float(ref_pre["percent"][1]), abs=1e-6)
    assert float(ref_pre["percent"][1]) == pytest.approx(float(seed_pre["percent"][1]), abs=1e-6)

    seed, ref, out = _run_one_step_row(dataset_path, 1151, 1)
    assert int(seed["items"][2]["type"]) == 54
    assert int(seed["items"][2]["state"]) == 0
    assert float(ref["percent"][1] - seed["percent"][1]) == pytest.approx(2.0, abs=1e-6)
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
    assert int(out["last_attack_landed"][0]) == int(ref["last_attack_landed"][0])


@pytest.mark.integration
def test_fox_laser_specialairn_landing_damagefall_body_uses_lbcoll_radius() -> None:
    # SpecialAirNLoop -> Landing handoff positive:
    # ftColl_8007925C routes item BODY through lbColl_8000805C, whose hurt-radius argument is
    # lbColl_804D7A38 * defender scale. PJO:2235 is the narrow landing handoff where the old Fox
    # laser hits an airborne DamageFall defender and consumes before the newer shot compacts slots.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 2235, 1)
    shooter = 0
    defender = 1

    assert int(seed["action_id"][shooter]) == 345  # FxSpecialAirNLoop
    assert int(ref["action_id"][shooter]) == 42  # Landing
    assert int(seed["action_id"][defender]) == 38  # DamageFall
    assert int(seed["items"][1]["type"]) == 54  # Fox laser

    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender]) == 452
    assert int(out["last_attack_landed"][shooter]) == int(ref["last_attack_landed"][shooter]) == 18
    assert int(out["items"][1]["type"]) == int(ref["items"][1]["type"]) == 54
    assert int(out["items"][1]["instance_id"]) == int(ref["items"][1]["instance_id"]) == 454


@pytest.mark.integration
def test_fox_laser_specialairn_loop_damagefall_radius_lane_keeps_adjacent_no_hit_alive() -> None:
    # Adjacent negative: one frame earlier the shooter is still in SpecialAirNLoop. The promoted
    # lbColl hurt-radius lane must not admit the old laser before the landing handoff.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, 2234, 1)
    shooter = 0
    defender = 1

    assert int(seed["action_id"][shooter]) == 345
    assert int(ref["action_id"][shooter]) == 345
    assert int(seed["action_id"][defender]) == 38
    assert int(seed["items"][1]["type"]) == 54

    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender]) == 445
    assert int(out["items"][1]["exists"]) == int(ref["items"][1]["exists"]) == 1
    assert int(out["items"][1]["instance_id"]) == int(ref["items"][1]["instance_id"]) == 452
