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
def test_wait_guardreflect_hitshield_requires_wait_entry_and_laser_overlap() -> None:
    # Negative controls for the MAJ:7327 same-step Wait -> GuardReflect handoff:
    # the retained path is the Wait_IASA -> ftCo_80091A4C digital-powershield owner plus real
    # ShieldDesc overlap, not a broad no-submotion GuardReflect shield-hit suppressor.
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

    def _force_non_wait_entry(seed_t) -> None:
        seed_t["seed_prev_action_id"][0, p] = np.uint16(15)  # WalkSlow

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, 7327, p, seed_mutator=_force_non_wait_entry)
    assert int(seed_row["action_id"][p]) == 14
    assert int(seed_row["seed_prev_action_id"][p]) == 15
    assert int(ref_row["action_id"][p]) == 181
    assert int(out_row["action_id"][p]) == 182
    assert int(out_row["hitlag"][p]) == 0
    assert int(out_row["items"][0]["exists"]) == 1

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
