from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    negative_record: int
    target_record: int
    p: int
    seed_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            negative_record=4042,
            target_record=4044,
            p=1,
            seed_action=16,
            note="AGN locomotion->GuardSetOff laser handoff",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            negative_record=3491,
            target_record=3493,
            p=0,
            seed_action=21,
            note="TBK locomotion->GuardSetOff laser handoff",
        ),
    ],
)
def test_locomotion_laser_guardsetoff_family_lock(case: _Case) -> None:
    # Replay-real lock for the kept locomotion -> GuardSetOff projectile-shield handoff:
    # - grounded locomotion shield admission can hand projectile contact directly to the regular
    #   shield-hit / GuardSetOff owner lane without exposing an intermediate replay-visible
    #   GuardReflect frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    p = case.p

    neg_seed, neg_out, neg_ref = _step_one_row(dataset_path, case.negative_record)
    assert int(neg_out["action_id"][p]) == int(neg_ref["action_id"][p]), case.note
    assert int(neg_out["action_frame"][p]) == int(neg_ref["action_frame"][p]), case.note
    assert int(neg_out["animation_index"][p]) == int(neg_ref["animation_index"][p]), case.note
    assert int(neg_out["hitlag"][p]) == int(neg_ref["hitlag"][p]) == 0, case.note
    assert int(neg_out["instance_id"][p]) == int(neg_ref["instance_id"][p]), case.note
    assert [int(x) for x in neg_out["state_flags"][p]] == [int(x) for x in neg_ref["state_flags"][p]]
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(neg_out["items"][0][field]) == int(neg_ref["items"][0][field]), case.note

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        seed_t, out_t, ref_t = _step_one_row(dataset_path, record)

        if record == case.target_record:
            assert int(seed_t["action_id"][p]) == case.seed_action, case.note
            assert int(ref_t["action_id"][p]) == 181, case.note  # GuardSetOff
            assert int(ref_t["action_frame"][p]) == 0, case.note
            assert int(ref_t["animation_index"][p]) == 40, case.note  # GuardDamage
            assert int(ref_t["hitlag"][p]) == 3, case.note
            assert float(ref_t["shield_hp"][p]) < float(seed_t["shield_hp"][p]), case.note

            for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
                assert int(out_t[field][p]) == int(ref_t[field][p]), case.note
            assert float(out_t["shield_hp"][p]) == float(ref_t["shield_hp"][p]), case.note
            for field in ("exists", "type", "owner", "instance_id"):
                assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), case.note

            assert [int(x) for x in out_t["state_flags"][p]] == [int(x) for x in ref_t["state_flags"][p]], case.note
            continue

        assert int(out_t["action_id"][p]) == int(ref_t["action_id"][p]), case.note
        assert int(out_t["action_frame"][p]) == int(ref_t["action_frame"][p]), case.note
        assert int(out_t["animation_index"][p]) == int(ref_t["animation_index"][p]), case.note
        assert int(out_t["hitlag"][p]) == int(ref_t["hitlag"][p]), case.note
        assert int(out_t["instance_id"][p]) == int(ref_t["instance_id"][p]), case.note
        assert [int(x) for x in out_t["state_flags"][p]] == [int(x) for x in ref_t["state_flags"][p]]
        for field in ("exists", "type", "owner", "instance_id"):
            assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), case.note


@pytest.mark.integration
def test_landing_guardreflect_laser_no_contact_row_stays_replay_real() -> None:
    # Replay-real negative lock for the adjacent Landing -> GuardReflect no-contact family:
    # - Landing IASA also delegates to ftCo_80091A4C, but this row stays on the GuardReflect
    #   no-contact lane in replay; the incoming laser must not be forced onto GuardSetOff /
    #   shield-hit ownership.
    # - This keeps the fresh GuardReflect item-contact family scoped to true grounded locomotion
    #   pose owners until a landing-specific shield-pose source is extracted.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 132
    p = 1
    seed_t, out_t, ref_t = _step_one_row(dataset_path, record)

    assert int(seed_t["action_id"][p]) == 42  # Landing
    assert int(ref_t["action_id"][p]) == 182  # GuardReflect
    assert int(ref_t["action_frame"][p]) == -1
    assert int(ref_t["hitlag"][p]) == 0

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), dataset_rel

    for slot in (0, 1):
        for field in ("exists", "type", "owner", "instance_id"):
            assert int(out_t["items"][slot][field]) == int(ref_t["items"][slot][field]), (
                f"{dataset_rel}: slot={slot} field={field}"
            )


@pytest.mark.integration
def test_landing_turn_guardon_followup_reflect_miss_preserves_laser_for_body_hit() -> None:
    # Replay-real lock for the PPA GuardOn -> GuardReflect follow-up miss:
    # - Landing -> Turn can enter GuardOn through ftCo_80091A4C before the next item pass.
    # - The following GuardOn IASA enters GuardReflect through ftCo_8009388C, but the aged laser
    #   misses the live ReflectDesc. That row remains powershield-window keepalive, not a false
    #   reflect owner or GuardSetOff shield hit.
    # - On the next row, the same laser reaches BODY ownership and puts Fox into DamageFlyTop.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_PickedUp
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0

    seed_2341, out_2341, ref_2341 = _step_one_row(dataset_path, 2341)
    assert int(seed_2341["action_id"][p]) == 18  # Turn
    assert int(ref_2341["action_id"][p]) == 178  # GuardOn
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_2341[field][p]) == int(ref_2341[field][p]), field

    seed_2342, out_2342, ref_2342 = _step_one_row(dataset_path, 2342)
    assert int(seed_2342["action_id"][p]) == 178  # GuardOn
    assert int(ref_2342["action_id"][p]) == 182  # GuardReflect
    assert int(ref_2342["hitlag"][p]) == 0
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_2342[field][p]) == int(ref_2342[field][p]), field
    assert [int(x) for x in out_2342["state_flags"][p]] == [int(x) for x in ref_2342["state_flags"][p]]
    assert float(out_2342["shield_hp"][p]) == pytest.approx(float(ref_2342["shield_hp"][p]), abs=1e-3)
    assert int(out_2342["items"][0]["exists"]) == int(ref_2342["items"][0]["exists"]) == 1
    assert int(out_2342["items"][0]["owner"]) == int(ref_2342["items"][0]["owner"]) == 1
    assert float(out_2342["items"][0]["vel_x"]) == pytest.approx(-5.0, abs=1e-6)

    seed_2343, out_2343, ref_2343 = _step_one_row(dataset_path, 2343)
    assert int(seed_2343["action_id"][p]) == 182  # GuardReflect
    assert int(ref_2343["action_id"][p]) == 75  # DamageFlyTop
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_2343[field][p]) == int(ref_2343[field][p]), field
    assert float(out_2343["percent"][p]) == pytest.approx(float(ref_2343["percent"][p]), abs=1e-6)
    assert int(out_2343["items"][0]["exists"]) == int(ref_2343["items"][0]["exists"]) == 0


def _step_one_row_with_seed_prev_action(
    dataset_path: Path, record: int, *, player: int, seed_prev_action_id: int
) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(record), f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1].copy()
    row["seed_t"]["seed_prev_action_id"][0, player] = np.uint16(seed_prev_action_id)
    seed = row["seed_t"][0].copy()
    ref = row["ref_t1"][0].copy()

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_bytes = (
        np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return seed, out, ref


@pytest.mark.integration
def test_guardon_guardreflect_no_submotion_hurtcaps_allow_shine_body_hit() -> None:
    # Replay-real BODY lock for the GuardOn -> GuardReflect (`ftCo_8009388C`) no-submotion path:
    # - Slippi exposes GuardReflect with animation_index=-1/action_frame=-2, but this source path
    #   keeps a live GuardOn JObj pose for fighter-vs-fighter BODY collision.
    # - The seeded frame-start previous action is the provenance lane; live prev_action_id has been
    #   advanced by the time hurtcaps refresh runs.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 7531
    defender = 0
    attacker = 1

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][defender]) == 182  # GuardReflect
    assert int(seed["seed_prev_action_id"][defender]) == 178  # GuardOn
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][defender]) == -2
    assert int(seed["action_id"][attacker]) == 39  # Squat
    assert int(ref["action_id"][attacker]) == 360  # SpecialLwStart

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 90
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 7
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 52
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-5)
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 5
    assert int(out["last_attack_landed"][attacker]) == int(ref["last_attack_landed"][attacker])

    # Boundary: without the frame-start GuardOn provenance, the same no-submotion snapshot must not
    # materialize generic GuardReflect hurtcaps and turn every active-x14 row into BODY contact.
    mut_seed, mut_out, _mut_ref = _step_one_row_with_seed_prev_action(
        dataset_path, record, player=defender, seed_prev_action_id=16
    )
    assert int(mut_seed["seed_prev_action_id"][defender]) == 16  # WalkMiddle control
    assert int(mut_out["action_id"][defender]) == 182  # GuardReflect stays no-contact
    assert int(mut_out["hitlag"][defender]) == 0
    assert int(mut_out["hitstun"][defender]) == 0
    assert float(mut_out["percent"][defender]) == pytest.approx(float(mut_seed["percent"][defender]), abs=1e-6)
    assert int(mut_out["hitlag"][attacker]) == 0


@pytest.mark.integration
def test_dash_91ad8_guardon_stale_trailing_laser_misses_same_step_shield() -> None:
    # Replay-real lock for Dash_IASA's mid guard helper:
    # - `dash.x4 != 0 && cur_anim_frame <= x44` stays in the early Dash_IASA branch and must not
    #   enter GuardReflect.
    # - the following mid branch enters GuardOn through ftCo_80091AD8 -> ftCo_800923B4, and the
    #   live ShieldDesc can participate in item collision. This row is the scaleZ endpoint negative:
    #   the reduced identity-cap proxy would over-admit a stale trailing laser sample here.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_800923B4,ftCo_800924C0}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1

    seed_733, out_733, ref_733 = _step_one_row(dataset_path, 733)
    assert int(seed_733["action_id"][p]) == 20  # Dash
    assert int(seed_733["dash_x4"][p]) == 1
    assert int(ref_733["action_id"][p]) == 20
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_733[field][p]) == int(ref_733[field][p]), field
    assert [int(x) for x in out_733["state_flags"][p]] == [
        int(x) for x in ref_733["state_flags"][p]
    ]

    seed_734, out_734, ref_734 = _step_one_row(dataset_path, 734)
    assert int(seed_734["action_id"][p]) == 20  # Dash
    assert int(ref_734["action_id"][p]) == 178  # GuardOn
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_734[field][p]) == int(ref_734[field][p]), field
    assert [int(x) for x in out_734["state_flags"][p]] == [
        int(x) for x in ref_734["state_flags"][p]
    ]

    _, out_735, ref_735 = _step_one_row(dataset_path, 735)
    assert int(ref_735["action_id"][p]) == 181  # GuardSetOff
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_735[field][p]) == int(ref_735[field][p]), field


@pytest.mark.integration
def test_dash_91ad8_guardon_same_step_laser_shield_contact_uses_scaled_endpoint() -> None:
    # Positive counterpart to the MAJ stale-trailing-laser negative above:
    # held-shield Dash `x4` enters GuardOn via ftCo_80091AD8, and the Falco laser's real scaleZ
    # endpoint lane overlaps the newly installed ShieldDesc in the same item pass.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_t, out_t, ref_t = _step_one_row(dataset_path, 5142)
    assert int(seed_t["action_id"][p]) == 20  # Dash
    assert int(seed_t["dash_x4"][p]) == 1
    assert int(seed_t["action_frame"][p]) == 4
    assert int(ref_t["action_id"][p]) == 181  # GuardSetOff
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
    assert float(out_t["shield_hp"][p]) == pytest.approx(float(ref_t["shield_hp"][p]), abs=1e-5)
    assert float(out_t["percent"][p]) == pytest.approx(float(ref_t["percent"][p]), abs=1e-5)
    assert int(out_t["items"]["exists"][0]) == int(ref_t["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_dash_x4_clear_guardon_can_take_same_step_laser_shield_contact() -> None:
    # Boundary control for the Dash_IASA x4 split:
    # - Dash `x4=0` reaches the mid `ftCo_80091AD8` helper directly and can still expose same-step
    #   laser shield contact.
    # - The same-step deferral is limited to the `x4 != 0` early-branch handoff repaired above.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_t, out_t, ref_t = _step_one_row(dataset_path, 773)
    assert int(seed_t["action_id"][p]) == 20  # Dash
    assert int(seed_t["dash_x4"][p]) == 0
    assert int(seed_t["action_frame"][p]) > 4
    assert int(ref_t["action_id"][p]) == 181  # GuardSetOff
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
    assert [int(x) for x in out_t["state_flags"][p]] == [
        int(x) for x in ref_t["state_flags"][p]
    ]


@pytest.mark.integration
@pytest.mark.parametrize("record", [117, 258])
def test_dash_91ad8_guardreflect_controls_still_reflect(record: int) -> None:
    # Negative controls: not every fresh shield input from Dash is suppressed. Dash rows outside
    # the early `dash.x4` branch still reach GuardReflect through the decomp guard helper.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_t, out_t, ref_t = _step_one_row(dataset_path, record)
    assert int(seed_t["action_id"][p]) == 20  # Dash
    assert int(ref_t["action_id"][p]) == 182  # GuardReflect
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
