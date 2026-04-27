from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            3116,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            773,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            202,
            1,
        ),
    ],
)
def test_falco_laser_shield_contact_enters_guardsetoff_and_despawns_laser(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for Falco laser shield-contact acceptance:
    # - itFoxlaser_UnkMotion1_Phys snapshots the previous laser position.
    # - it_8029C4D4 resolves collision over the authoritative prev->cur segment.
    # - itFoxLaser_Logic94_HitShield consumes the projectile and enters GuardSetOff on shield hit.
    # - MAJ:202 covers the final-x14 GuardReflect handoff where a normal shield overlap is already
    #   selected; the pure reflect-descriptor x2218 byte must still disable ShieldBounced keepalive.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   itFoxlaser_UnkMotion1_Phys,it_8029C4D4,itFoxLaser_Logic94_HitShield}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_Anim
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, p)

    assert int(ref_row["action_id"][p]) == 181
    assert int(out_row["action_id"][p]) == 181
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4

    for i in range(2):
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][i][field]) == int(ref_row["items"][i][field]), (
                f"record={record} item={i} field={field} "
                f"expected={int(ref_row['items'][i][field])} got={int(out_row['items'][i][field])}"
            )


@pytest.mark.integration
def test_guardreflect_shield_bounce_keepalive_uses_hidden_item_bounce_owner() -> None:
    # Replay-real lock for Item_80269DC8 ShieldBounced keepalive on an established GuardReflect
    # snapshot: the laser enters GuardSetOff shield hitlag but remains live with reflected velocity.
    # v10 Dolphin dump for MAJ:6337 shows the item survives the shield callback with per-item
    # HitCapsule victim-ring entries, so shield HP must not be the keepalive discriminator here.
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 6337, p)

    assert int(seed_row["action_id"][p]) == 182
    assert int(seed_row["guard_reflect_timer_x14"][p]) == 1
    assert int(ref_row["action_id"][p]) == 181
    assert int(out_row["action_id"][p]) == 181
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4

    # The blaster gun slot is adjacent state, but the laser slot must survive rather than taking
    # the HitShield destroy path.
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_row["items"][1][field]) == int(ref_row["items"][1][field]), (
            f"field={field} expected={int(ref_row['items'][1][field])} "
            f"got={int(out_row['items'][1][field])}"
        )
    assert float(out_row["items"][1]["vel_y"]) > 0.0


@pytest.mark.integration
def test_pure_x2218_laser_can_hit_steady_guard_no_submotion_snapshot() -> None:
    # Replay-real positive for the steady-Guard no-submotion boundary:
    # - IAT:3575 starts with defender already serialized as settled Guard (`af=-1`, `anim=-1`).
    # - The accepted shield callback is from an aged carried laser while the defender's raw
    #   fp+0x2218 byte is the pure behavior lane (`0x04` with high command bits clear), so the item
    #   callback uses authored HitCapsule offsets for shield collision.
    # - This is distinct from carried lasers in the negative test below, which have high fp+0x2218
    #   command/interrupt bits and must remain on the settled point sample.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 3575, p)

    assert int(seed_row["action_id"][p]) == 179
    assert int(seed_row["action_frame"][p]) == -1
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed_row["action_id"][0]) == 345
    assert int(seed_row["state_flags"][p][0]) == 0x04
    assert int(seed_row["items"][0]["exists"]) == 1
    assert int(seed_row["items"][0]["type"]) == 55
    assert int(ref_row["action_id"][p]) == 181

    assert int(out_row["action_id"][p]) == 181
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4

    for i in range(2):
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][i][field]) == int(ref_row["items"][i][field]), (
                f"item={i} field={field} expected={int(ref_row['items'][i][field])} "
                f"got={int(out_row['items'][i][field])}"
            )


@pytest.mark.integration
def test_high_flag_laser_steady_guard_does_not_hit_immediately() -> None:
    # Negative for the same steady-Guard shape:
    # - DCC:6517 also has a defender serialized as settled Guard and an attacker in
    #   SpecialAirNLoop frame 7, but the defender raw fp+0x2218 byte has high command bits set.
    # - Native serializes the new shot at t+1 instead of routing it through the same-frame shield
    #   callback, so the immediate authored-offset lane must stay limited to the pure behavior byte.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNLoop_Phys}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 6517, p)

    assert int(seed_row["action_id"][p]) == 179
    assert int(seed_row["action_frame"][p]) == -1
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed_row["action_id"][1]) == 345
    assert int(seed_row["state_flags"][p][0]) == 0x24
    assert int(ref_row["action_id"][1]) == 345
    assert int(ref_row["action_id"][p]) == 179

    assert int(out_row["action_id"][p]) == 179
    assert int(out_row["hitlag"][p]) == 0
    assert int(out_row["hitstun"][p]) == 0
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4

    assert int(ref_row["items"][1]["exists"]) == 1
    assert int(ref_row["items"][1]["type"]) == 55
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_row["items"][1][field]) == int(ref_row["items"][1][field]), (
            f"field={field} expected={int(ref_row['items'][1][field])} "
            f"got={int(out_row['items'][1][field])}"
        )


@pytest.mark.integration
def test_landing_carried_laser_pure_x2218_steady_guard_stays_point_sample() -> None:
    # Negative for the pure-x2218 steady-Guard branch:
    # - DCC:2231 has the pure fp+0x2218 behavior byte, but the projectile is already in the
    #   carried LandingFallSpecial owner phase rather than the SpecialN loop callback that owns
    #   IAT:3575's immediate shield admission.
    # - The authored-offset lane must stay phase-limited; otherwise this row falsely enters
    #   GuardSetOff one frame early and over-damages shield HP.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 2231, p)

    assert int(seed_row["action_id"][p]) == 179
    assert int(seed_row["action_frame"][p]) == -1
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed_row["state_flags"][p][0]) == 0x04
    assert int(seed_row["action_id"][1]) == 42
    assert int(seed_row["items"][1]["exists"]) == 1
    assert int(seed_row["items"][1]["type"]) == 55
    assert int(ref_row["action_id"][p]) == 179

    assert int(out_row["action_id"][p]) == 179
    assert int(out_row["hitlag"][p]) == 0
    assert int(out_row["hitstun"][p]) == 0
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            3599,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2571,
            1,
        ),
    ],
)
def test_falco_laser_steady_guard_no_submotion_snapshot_does_not_enter_guardsetoff(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real negative lock for the projectile-side steady Guard snapshot boundary:
    # - once GuardOn has already settled into Guard before post-frame, the frozen Guard snapshot
    #   must not invent a new same-frame laser->shield sweep into GuardSetOff.
    # - keep the real shield-hit sweep for fresh Guard carry rows only.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_800928CC}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, p)

    assert int(seed_row["action_id"][p]) == 179
    assert int(seed_row["action_frame"][p]) == -1
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert int(ref_row["action_id"][p]) == 179
    assert int(ref_row["hitlag"][p]) == 0
    assert int(ref_row["hitstun"][p]) == 0

    assert int(out_row["action_id"][p]) == 179, f"record={record} expected Guard hold to persist"
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 1e-4

    for i in range(2):
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][i][field]) == int(ref_row["items"][i][field]), (
                f"record={record} item={i} field={field} "
                f"expected={int(ref_row['items'][i][field])} got={int(out_row['items'][i][field])}"
            )
