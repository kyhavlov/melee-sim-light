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
