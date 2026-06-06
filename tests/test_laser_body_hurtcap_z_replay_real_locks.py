from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def test_falco_laser_airborne_fall_body_uses_flattened_hurtcap_z() -> None:
    # Replay-real lock for item BODY hurtcap-Z ownership:
    # ftColl_8007925C routes item BODY through lbColl_8000805C, passing fp->cur_pos.z; lbColl
    # rewrites hurtcap endpoint Z before the segment/capsule test. TBK:2901 is the narrow state0
    # Falco-laser/Fall row that needs that source lane to apply the BODY hit and consume the shot.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
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


def test_falco_laser_airborne_fall_z_lane_keeps_adjacent_no_hit_alive() -> None:
    # Adjacent negative: one frame before the flattened-Z BODY hit, the laser must remain alive and
    # Fall must continue. This protects against broad airborne/Fall or tolerance gates.
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


def test_falco_laser_airborne_fall_z_lane_respects_same_attack_hitlist_carry() -> None:
    # Negative for item victim-ring/callback timing: PPA:4124 has similar Fall/Falco-laser geometry,
    # but the defender already carries the same owner attack id. Decomp item hitlists are updated via
    # it_8026FAC4/lbColl_80008688, so the Z lane must not create a fresh BODY consume here.
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
