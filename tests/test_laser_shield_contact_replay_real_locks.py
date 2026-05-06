from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


def _live_laser_keys(items) -> set[tuple[int, int, int, int]]:
    keys: set[tuple[int, int, int, int]] = set()
    for it in items:
        if int(it["exists"]) != 1 or int(it["type"]) != 55:
            continue
        keys.add((int(it["owner"]), int(it["instance_id"]), int(it["spawn_id"]), int(it["state"])))
    return keys


def _assert_live_laser_set_matches_ref(*, out_row, ref_row, record: int) -> None:
    got = _live_laser_keys(out_row["items"])
    exp = _live_laser_keys(ref_row["items"])
    assert got == exp, f"record={record} live laser set expected={exp} got={got}"


def _find_item_by_spawn(row, *, item_type: int, owner: int, instance_id: int, spawn_id: int):
    for it in row["items"]:
        if (
            int(it["exists"]) == 1
            and int(it["type"]) == item_type
            and int(it["owner"]) == owner
            and int(it["instance_id"]) == instance_id
            and int(it["spawn_id"]) == spawn_id
        ):
            return it
    return None


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
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            259,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            5631,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            7294,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            7327,
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
    # - MAJ:259 covers the post-GuardReflect_Anim timer boundary: seed x14=2 has already ticked to
    #   live x14=1 by item collision, so ReflectDesc misses must hand off to HitShield.
    # - MAJ:5631 covers fresh Dash -> GuardReflect without the Dash_IASA terminal-scalar owner; the
    #   aged laser overlaps ShieldDesc and must route to HitShield / GuardSetOff rather than staging
    #   immediate reflected owner transfer.
    # - MAJ:7327 covers same-step Wait -> GuardReflect where the already-live laser uses the fresh
    #   ShieldDesc center and must go to HitShield/GuardSetOff rather than reflect owner transfer.
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

    if record == 7294:
        assert int(seed_row["action_id"][p]) == 182  # GuardReflect
        assert int(seed_row["seed_prev_action_id"][p]) == 20  # Dash
        assert int(seed_row["guard_reflect_timer_x14"][p]) == 2
        assert int(seed_row["item_reflect_transfer_port"][0]) == 0xFF
    if record == 259:
        assert int(seed_row["action_id"][p]) == 182  # GuardReflect
        assert int(seed_row["seed_prev_action_id"][p]) == 20  # Dash
        assert int(seed_row["guard_reflect_timer_x14"][p]) == 2
    if record == 5631:
        assert int(seed_row["action_id"][p]) == 20  # Dash
        assert int(ref_row["action_id"][p]) == 181  # GuardSetOff
        assert int(seed_row["items"][0]["exists"]) == 1
        assert int(seed_row["items"][0]["type"]) == 55
    if record == 7327:
        assert int(seed_row["action_id"][p]) == 14  # Wait
        assert int(seed_row["seed_prev_action_id"][p]) == 14
        assert int(seed_row["items"][0]["exists"]) == 1
        assert int(seed_row["items"][0]["timer"]) < 95

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
    _assert_live_laser_set_matches_ref(out_row=out_row, ref_row=ref_row, record=record)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "should_transfer"),
    [
        (118, True),
        (294, True),
        (201, False),
    ],
)
def test_dash_guardreflect_high_laser_reflectdesc_transfer_is_runtime_owned(
    record: int, should_transfer: bool
) -> None:
    # MAJ:118/294 are high-Falco-laser GuardReflect rows where Dolphin probes show
    # ftColl_80077464 writing reflected owner/xDA8 before Item_80269F14 consumes it. Clear the
    # teacher-forced transfer seed so the test proves runtime ReflectDesc ownership; MAJ:201 is the
    # adjacent high-laser control that has not crossed the ReflectDesc center.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def _clear_seeded_transfer(seed_t) -> None:
        seed_t["item_reflect_transfer_port"][0, 0] = 0xFF
        seed_t["item_reflect_transfer_iid"][0, 0] = 0

    seed_row, ref_row, out_row = _run_one_step_row(
        dataset_path, record, 1, seed_mutator=_clear_seeded_transfer
    )

    assert int(seed_row["action_id"][1]) == 182  # GuardReflect
    assert int(seed_row["seed_prev_action_id"][1]) in (14, 20)  # Fall/Dash source rows.
    assert int(seed_row["guard_reflect_timer_x14"][1]) == 2
    assert int(seed_row["items"][0]["type"]) == 55
    if should_transfer:
        assert int(ref_row["items"][0]["owner"]) == 1
        assert int(out_row["items"][0]["owner"]) == int(ref_row["items"][0]["owner"])
        assert int(out_row["items"][0]["instance_id"]) == int(ref_row["items"][0]["instance_id"])
        assert float(out_row["items"][0]["direction"]) == float(ref_row["items"][0]["direction"])
    else:
        assert int(ref_row["items"][0]["owner"]) == 0
        assert int(out_row["items"][0]["owner"]) == 0
        assert int(out_row["items"][0]["instance_id"]) == int(ref_row["items"][0]["instance_id"])


@pytest.mark.integration
def test_steady_guardreflect_does_not_reown_new_specialn_laser_spawn_frame() -> None:
    # MAJ:6336 is a SpecialAirNLoop script-spawned Falco laser born while Fox is already in a
    # seeded GuardReflect x14/x18 window with raw fp+0x2218_b1 set. Vanilla records the new item as
    # shooter-owned on the birth frame; immediate owner transfer is only proven for Run/Dash-family
    # GuardReflect rows where that raw command bit is clear.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 6336, 0)

    assert int(seed_row["action_id"][0]) == 345  # Falco SpecialAirNLoop.
    assert int(seed_row["action_id"][1]) == 182  # Fox GuardReflect.
    assert int(seed_row["guard_reflect_timer_x14"][1]) == 2
    assert int(seed_row["state_flags"][1][0]) & 0x40
    assert int(ref_row["items"][1]["exists"]) == 1
    assert int(ref_row["items"][1]["type"]) == 55
    assert int(out_row["items"][1]["owner"]) == int(ref_row["items"][1]["owner"]) == 0
    assert int(out_row["items"][1]["instance_id"]) == int(ref_row["items"][1]["instance_id"])
    assert float(out_row["items"][1]["direction"]) == float(ref_row["items"][1]["direction"])


@pytest.mark.integration
def test_steady_guardon_x2218_command_bit_keeps_birth_laser_out_of_hitshield() -> None:
    # DSG:8216 covers the same birth-frame SpecialAirNLoop laser callback boundary, but through
    # steady GuardOn / Item_80269DC8 rather than GuardReflect owner transfer. Raw fp+0x2218_b1 keeps
    # the ShieldDesc bone on the GuardOn live command pose, so the high newborn laser misses instead
    # of using the settled neutral Guard bubble and entering GuardSetOff.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/battlefield_recent/"
        "DelayedSuperbGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 8216, 0)

    assert int(seed_row["action_id"][1]) == 345  # Falco SpecialAirNLoop.
    assert int(seed_row["action_frame"][1]) == 7
    assert int(seed_row["action_id"][0]) == 178  # GuardOn.
    assert int(seed_row["seed_prev_action_id"][0]) == 178
    assert int(seed_row["state_flags"][0][0]) & 0x40

    assert int(ref_row["action_id"][0]) == 178
    assert int(out_row["action_id"][0]) == 178
    assert int(out_row["hitlag"][0]) == int(ref_row["hitlag"][0]) == 0
    assert int(ref_row["items"][1]["exists"]) == 1
    assert int(ref_row["items"][1]["type"]) == 55
    assert int(ref_row["items"][1]["owner"]) == 1
    for field in ("exists", "type", "state", "owner", "instance_id", "spawn_id", "timer"):
        assert out_row["items"][1][field] == ref_row["items"][1][field]
    assert float(out_row["items"][1]["pos_x"]) == pytest.approx(
        float(ref_row["items"][1]["pos_x"]), abs=1.0e-4
    )
    assert float(out_row["items"][1]["pos_y"]) == pytest.approx(
        float(ref_row["items"][1]["pos_y"]), abs=1.0e-4
    )


@pytest.mark.integration
def test_steady_guardon_x2218_command_bit_still_hits_when_guardon_pose_overlaps() -> None:
    # Shady:3700 is the paired positive for the GuardOn x2218 command-pose lane. The same raw byte
    # and birth-frame laser are present, but the GuardOn live shield-bone pose overlaps the low laser
    # and vanilla enters GuardSetOff through Item_80269DC8.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80091E78}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "ShadyDecimalStarling.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 3700, 0)

    assert int(seed_row["action_id"][1]) == 345  # Falco SpecialAirNLoop.
    assert int(seed_row["action_id"][0]) == 178  # GuardOn.
    assert int(seed_row["seed_prev_action_id"][0]) == 178
    assert int(seed_row["state_flags"][0][0]) & 0x40
    assert int(ref_row["action_id"][0]) == 181  # GuardSetOff.
    assert int(out_row["action_id"][0]) == 181
    assert int(out_row["hitlag"][0]) == int(ref_row["hitlag"][0])
    assert int(out_row["items"][1]["exists"]) == int(ref_row["items"][1]["exists"]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "owner", "item_type", "instance_id", "spawn_id"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            3594,
            1,
            54,
            710,
            26,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            3599,
            1,
            54,
            710,
            28,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "LoyalDishonestWren.msl",
            1407,
            1,
            54,
            287,
            15,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            3441,
            0,
            54,
            674,
            23,
        ),
    ],
)
def test_upward_laser_stage_floor_crossing_does_not_preempt_article_delete(
    dataset_rel: str, record: int, owner: int, item_type: int, instance_id: int, spawn_id: int
) -> None:
    # itfoxlaser stage collision is owned by mpCheckAllRemap. Horizontal floors use
    # mpCheckFloorRemap's downward-only branch; upward state1 throw-laser segments crossing through a
    # platform/floor line must not be converted to lifetime=1 before the later item/body/delete owner.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
    # refs/melee/src/melee/it/it_266F.c::it_8026E9A4
    # refs/melee/src/melee/mp/mplib.c::{mpCheckAllRemap,mpCheckFloorRemap}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, 0)

    assert _find_item_by_spawn(
        seed_row, item_type=item_type, owner=owner, instance_id=instance_id, spawn_id=spawn_id
    )
    assert (
        _find_item_by_spawn(
            ref_row, item_type=item_type, owner=owner, instance_id=instance_id, spawn_id=spawn_id
        )
        is None
    )
    assert (
        _find_item_by_spawn(
            out_row, item_type=item_type, owner=owner, instance_id=instance_id, spawn_id=spawn_id
        )
        is None
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "owner", "item_type", "instance_id", "spawn_id"),
    [
        (530, 1, 55, 117, 8),
        (6272, 0, 55, 1182, 91),
    ],
)
def test_downward_laser_stage_floor_crossing_preserves_lifetime_one_keepalive(
    record: int, owner: int, item_type: int, instance_id: int, spawn_id: int
) -> None:
    # Negative for the directional floor fix: downward horizontal-floor crossings still hit
    # mpCheckFloorRemap and serialize the source lifetime=1 keepalive.
    # refs/melee/src/melee/mp/mplib.c::mpCheckFloorRemap
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Coll
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, 0)
    ref_item = _find_item_by_spawn(
        ref_row, item_type=item_type, owner=owner, instance_id=instance_id, spawn_id=spawn_id
    )
    out_item = _find_item_by_spawn(
        out_row, item_type=item_type, owner=owner, instance_id=instance_id, spawn_id=spawn_id
    )

    assert ref_item is not None
    assert out_item is not None
    assert float(out_item["timer"]) == pytest.approx(float(ref_item["timer"]), abs=1.0e-6)
    assert float(out_item["timer"]) == pytest.approx(1.0, abs=1.0e-6)


@pytest.mark.integration
def test_steady_guardreflect_clear_x2218_b1_reowns_new_specialn_laser_spawn_frame() -> None:
    # Positive control for the birth-frame reflect owner above: AGN:3345 has the same visible
    # SpecialAirNLoop -> steady GuardReflect overlap, but raw fp+0x2218_b1 is clear and vanilla
    # records the new laser as reflector-owned before post-frame item serialization.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 3345, 0)

    assert int(seed_row["action_id"][0]) == 345  # Falco SpecialAirNLoop.
    assert int(seed_row["action_id"][1]) == 182  # Fox GuardReflect.
    assert int(seed_row["guard_reflect_timer_x14"][1]) == 2
    assert (int(seed_row["state_flags"][1][0]) & 0x40) == 0
    assert int(ref_row["items"][1]["exists"]) == 1
    assert int(ref_row["items"][1]["type"]) == 55
    assert int(out_row["items"][1]["owner"]) == int(ref_row["items"][1]["owner"]) == 1
    assert int(out_row["items"][1]["instance_id"]) == int(ref_row["items"][1]["instance_id"])


@pytest.mark.integration
def test_wait_guardreflect_hitshield_requires_laser_overlap() -> None:
    # Negative controls for the MAJ:7327 same-step Wait -> GuardReflect handoff:
    # the retained path is the Wait_IASA -> ftCo_80091A4C digital-powershield owner plus real
    # ShieldDesc overlap, not a broad no-submotion GuardReflect shield-hit suppressor. Mutating
    # only the seed_prev_action_id is not a valid source negative here because the live action
    # change records the real previous action before item collision.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80093A50}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1

    def _move_laser_outside_shield(seed_t) -> None:
        seed_t["items"][0]["pos_x"][0] = np.float32(float(seed_t["items"][0]["pos_x"][0]) - 20.0)

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 7327, p, seed_mutator=_move_laser_outside_shield)
    assert int(seed_row["seed_prev_action_id"][p]) == 14
    assert int(ref_row["action_id"][p]) == 181
    assert int(out_row["action_id"][p]) == 182
    assert int(out_row["hitlag"][p]) == 0
    assert int(out_row["items"][0]["exists"]) == 1


@pytest.mark.integration
def test_late_dash_guardreflect_hitshield_requires_reflectdesc_stage_x_overlap() -> None:
    # Negative controls for the MAJ:7294 late Dash/GuardReflect handoff: moving the defender's
    # ReflectDesc center away in stage X or Z must not still take the retained HitShield/despawn
    # branch. This guards against testing the ReflectDesc radius around a stale ShieldDesc center.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464,ftColl_80077688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1

    def _separate_reflectdesc(seed_t) -> None:
        seed_t["pos_x"][0, p] = np.float32(float(seed_t["pos_x"][0, p]) + 40.0)

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 7294, p, seed_mutator=_separate_reflectdesc)
    assert int(seed_row["action_id"][p]) == 182
    assert int(ref_row["action_id"][p]) == 181
    assert not (
        int(out_row["action_id"][p]) == 181 and _live_laser_keys(out_row["items"]) == _live_laser_keys(ref_row["items"])
    )


@pytest.mark.integration
def test_late_dash_guardreflect_laser_shield_hit_rollout_crosses_maj7294() -> None:
    # Rollout lock for the same MAJ family as the one-step row above:
    # - the seed at rec=7293 is the hidden Dash -> GuardReflect entry,
    # - rec=7294 must take the Item_80269DC8 HitShield / GuardSetOff path instead of the active
    #   GuardReflect keepalive lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_80077464}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 7286
    target_record = 7294
    assert int(samples.shape[0]) > target_record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out = None
        ref = None
        for record in range(start_record, target_record + 1):
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
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref = samples[record]["ref_t1"]
    finally:
        binding.destroy(handle)

    assert out is not None and ref is not None
    p = 1
    assert int(ref["action_id"][p]) == 181
    assert int(out["action_id"][p]) == 181
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 40
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert abs(float(out["shield_hp"][p]) - float(ref["shield_hp"][p])) <= 5e-4
    assert int(out["items"][0]["exists"]) == int(ref["items"][0]["exists"]) == 0
    _assert_live_laser_set_matches_ref(out_row=out, ref_row=ref, record=target_record)


@pytest.mark.integration
def test_late_dash_guardreflect_laser_shield_hit_rollout_crosses_maj259() -> None:
    # Rollout lock for the high-disruptive MAJ SpecialAirN laser cluster:
    # a Dash -> GuardReflect defender with seed x14=2 must consume the incoming Falco laser through
    # Item_80269DC8 HitShield on the post-callback live x14=1 frame, not stage a reflected owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 250
    target_record = 259
    assert int(samples.shape[0]) > target_record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out = None
        ref = None
        for record in range(start_record, target_record + 1):
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
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref = samples[record]["ref_t1"]
    finally:
        binding.destroy(handle)

    assert out is not None and ref is not None
    p = 1
    assert int(ref["action_id"][p]) == 181
    assert int(out["action_id"][p]) == 181
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["items"][0]["exists"]) == int(ref["items"][0]["exists"]) == 0
    _assert_live_laser_set_matches_ref(out_row=out, ref_row=ref, record=target_record)


@pytest.mark.integration
def test_guardon_origin_guardreflect_final_x14_rollout_enters_guardsetoff() -> None:
    # Rollout lock for the GuardOn-origin GuardReflect final-x14 boundary:
    # - rec=9477 enters GuardReflect from GuardOn through ftCo_8009388C.
    # - rec=9479 starts with x14_seed=1; ftCo_GuardReflect_Anim -> ftCo_80093BC0 expires x14 and
    #   recreates ShieldDesc before item collision, so the laser must resolve through
    #   Item_80269DC8 / GuardSetOff instead of staying in GuardReflect keepalive.
    # The exact ShieldBounced xC58 normal remains a separate residual; this lock owns the callback
    # phase, action, hitlag, shield HP, and live-laser identity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    start_record = 9425
    target_record = 9479
    assert int(samples.shape[0]) > target_record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out = None
        ref = None
        for record in range(start_record, target_record + 1):
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
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref = samples[record]["ref_t1"]
    finally:
        binding.destroy(handle)

    assert out is not None and ref is not None
    p = 0
    assert int(ref["action_id"][p]) == 181
    assert int(out["action_id"][p]) == 181
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 40
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert abs(float(out["shield_hp"][p]) - float(ref["shield_hp"][p])) <= 5e-4
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out["items"][0][field]) == int(ref["items"][0][field]), (
            f"field={field} expected={int(ref['items'][0][field])} "
            f"got={int(out['items'][0][field])}"
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
def test_steady_guard_x2218_b1_laser_uses_hitcapsule_offsets() -> None:
    # Replay-real positive for the settled-Guard x2218_b1 boundary:
    # - DSG:4129 starts with Fox already serialized as settled Guard (`af=-1`, `anim=-1`) while
    #   Falco's SpecialAirNLoop callback spawns a laser from below/behind the shield.
    # - Raw fp+0x2218 carries both behavior and b1 bits (`0x44`). Dolphin/slippi expose that byte as
    #   state_flags[0], but the shield contact is still owned by the source item HitCapsule offset
    #   path rather than the settled projectile point sample.
    # - The x2218_b2/b0 command and interrupt lanes stay covered by the high-flag negative below,
    #   while GuardOn entry command pose has its own birth-frame negative lock.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNLoop_Phys}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Phys}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/battlefield_recent/"
        "DelayedSuperbGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 4129, p)

    assert int(seed_row["action_id"][p]) == 179  # Guard.
    assert int(seed_row["action_frame"][p]) == -1
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert int(seed_row["state_flags"][p][0]) == 0x44
    assert int(seed_row["action_id"][1]) == 345  # Falco SpecialAirNLoop.
    assert int(seed_row["action_frame"][1]) == 7
    assert int(seed_row["items"][0]["exists"]) == 1
    assert int(seed_row["items"][0]["type"]) == 75  # Falco blaster gun.
    assert int(ref_row["action_id"][p]) == 181  # GuardSetOff.

    assert int(out_row["action_id"][p]) == 181
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]) == 3
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 5e-4

    for i in range(2):
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][i][field]) == int(ref_row["items"][i][field]), (
                f"item={i} field={field} expected={int(ref_row['items'][i][field])} "
                f"got={int(out_row['items'][i][field])}"
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "item_slot", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            1524,
            0,
            5,
            14,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            543,
            0,
            0,
            178,
        ),
    ],
)
def test_seeded_pending_reflect_transfer_precedes_proxy_laser_collision(
    dataset_rel: str, record: int, p: int, item_slot: int, seed_action: int
) -> None:
    # Replay-real locks for the hidden pending-reflect priority boundary:
    # - the seed lane is prefix-causal item state, not future action fitting;
    # - ftColl_80077464 has already selected a reflector and Item_8026A294 must consume
    #   Item_80269F14 before the simulator's proxy laser-vs-fighter pass can destroy the item
    #   through HitShield/BODY;
    # - adjacent no-transfer rows stay covered by the GuardSetOff/ShieldBounced tests above.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
    # refs/melee/src/melee/it/item.c::{Item_8026A294,Item_80269F14}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    seed_item = seed_row["items"][item_slot]
    ref_item = ref_row["items"][item_slot]
    out_item = out_row["items"][item_slot]

    assert int(seed_row["action_id"][p]) == seed_action
    assert int(seed_row["item_reflect_transfer_port"][item_slot]) == p
    assert int(seed_row["item_reflect_transfer_iid"][item_slot]) == int(ref_item["instance_id"])
    assert int(seed_item["exists"]) == 1
    assert int(seed_item["type"]) == 55
    assert int(seed_item["owner"]) != p
    assert int(ref_row["action_id"][p]) == 182  # GuardReflect.

    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p])
    assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p])
    assert int(out_row["animation_index"][p]) == int(ref_row["animation_index"][p])
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 1e-3

    for field in ("exists", "type", "state", "owner", "instance_id", "spawn_id"):
      assert int(out_item[field]) == int(ref_item[field]), (
          f"record={record} item={item_slot} field={field} "
          f"expected={int(ref_item[field])} got={int(out_item[field])}"
      )
    for field in ("direction", "timer", "pos_x", "pos_y", "vel_x", "vel_y"):
      assert float(out_item[field]) == pytest.approx(float(ref_item[field]), abs=1e-5), (
          f"record={record} item={item_slot} field={field}"
      )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_laser_slot", "seed_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PositiveRevolvingHyena.msl",
            131,
            1,
            1,
            182,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.msl",
            7493,
            0,
            1,
            179,
        ),
    ],
)
def test_lightshield_no_submotion_guard_body_uses_frame_start_item_sample(
    dataset_rel: str, record: int, p: int, seed_laser_slot: int, seed_action: int
) -> None:
    # Replay-real locks for the lightshield no-submotion item BODY miss boundary:
    # - Item_802697D4 has integrated the laser by post-frame, but Fighter_8006CB94 /
    #   ftColl_8007925C consumes the item HitCapsule BODY sample under the frame-start
    #   `fp+0x2218` guard behavior byte.
    # - Pure behavior/x2218_b1 rows (`0x04` or `0x44`) must not let the post-motion proxy BODY
    #   sample damage through lightshield; command/interrupt rows remain on the current item
    #   sample and are covered by adjacent GuardSetOff/body-hit locks.
    # refs/melee/src/melee/it/item.c::Item_802697D4
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    seed_item = seed_row["items"][seed_laser_slot]

    assert int(seed_row["action_id"][p]) == seed_action
    assert int(seed_row["action_frame"][p]) < 0
    assert int(seed_row["animation_index"][p]) == 0xFFFFFFFF
    assert float(seed_row["lightshield_amount"][p]) > 0.0
    assert int(seed_row["state_flags"][p][2]) & 0x80
    assert (int(seed_row["state_flags"][p][0]) & 0xA4) == 0x04
    assert int(seed_item["exists"]) == 1
    assert int(seed_item["type"]) == 55
    assert int(seed_row["item_reflect_transfer_port"][seed_laser_slot]) == 0xFF

    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p])
    assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p])
    assert int(out_row["animation_index"][p]) == int(ref_row["animation_index"][p])
    assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
    assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p])
    assert int(out_row["state_flags"][p][0]) == int(ref_row["state_flags"][p][0])
    assert int(out_row["state_flags"][p][1]) == int(ref_row["state_flags"][p][1])
    assert int(out_row["state_flags"][p][2]) == int(ref_row["state_flags"][p][2])
    assert abs(float(out_row["shield_hp"][p]) - float(ref_row["shield_hp"][p])) <= 1e-3
    _assert_live_laser_set_matches_ref(out_row=out_row, ref_row=ref_row, record=record)


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
