from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


_DEBUG_CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),  # 0=BODY, 1=SHIELD
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)


_DEBUG_HITBOX_EVENT_TIMING_DTYPE = np.dtype(
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

_DEBUG_SHIELD_CANDIDATE_DTYPE = np.dtype(
    [
        ("source_kind", "u1"),
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("reject_reason", "u1"),
        ("attacker_hitlag_started_frame", "u1"),
        ("defender_hitlag_started_frame", "u1"),
        ("shield_active", "u1"),
        ("hitbox_enabled", "u1"),
        ("defender_on_ground", "u1"),
        ("hitlist_allows", "u1"),
        ("overlap_shield", "u1"),
        ("element", "u1"),
        ("hb_flags", "<u2"),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_damage", "<f4"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
        ("shield_overlap_margin", "<f4"),
    ],
    align=False,
)


def _shield_contact_without_body_conflict(contacts: np.ndarray, defender: int) -> np.void | None:
    for c in contacts:
        if int(c["defender"]) != int(defender):
            continue
        if int(c["contact_kind"]) != 1:
            continue
        hb_id = int(c["hitbox_id"])
        attacker = int(c["attacker"])
        body_conflict = False
        for b in contacts:
            if int(b["defender"]) != int(defender):
                continue
            if int(b["contact_kind"]) != 0:
                continue
            if int(b["attacker"]) != attacker or int(b["hitbox_id"]) != hb_id:
                continue
            body_conflict = True
            break
        if not body_conflict:
            return c
    return None


def _run_one_step_row(ds_path: Path, record: int, p: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


def _run_pre_combat_debug_row(
    ds_path: Path, record: int, attacker: int, hb_id: int
) -> tuple[np.void, np.ndarray, np.ndarray, np.void]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]

    binding = pytest.importorskip("msl_binding")
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
        contacts_raw, count = binding.debug_combat_contacts_classified(handle, 0, 256)
        contacts = contacts_raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:count].copy()
        shield_world = np.array(binding.debug_shield_bubbles_world(handle, 0), copy=True)
        timing_raw = binding.debug_hitbox_event_timing(handle, 0, attacker, hb_id)
        timing = timing_raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0].copy()
    finally:
        binding.destroy(handle)
    return seed, contacts, shield_world, timing


def _seed_bridge_expected_hitlag_from_damage(dmg: float) -> int:
    common = json.loads((Path(__file__).resolve().parents[1] / "data/common/ft_common_data.json").read_text())
    dmg_i = int(dmg)
    env_dmg = 0 if dmg == 0.0 else (dmg_i if dmg_i != 0 else 1)
    if env_dmg <= 0:
        return 0
    return int((float(env_dmg) * float(common["hitlag_dmg_mul"])) + float(common["hitlag_base"]))


def _assert_body_overlap_lock_fields_match_ref(*, out_row: np.void, ref_row: np.void, record: int, p: int) -> None:
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    got_sf = out_row["state_flags"][p].tolist()
    exp_sf = ref_row["state_flags"][p].tolist()
    assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


def _assert_throw_release_action_anim_match_ref(
    *, out_row: np.void, ref_row: np.void, record: int, p: int
) -> None:
    for field in ("action_id", "animation_index", "jumps_left"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"


def _assert_transition_lock_fields_match_ref(*, out_row: np.void, ref_row: np.void, record: int, p: int) -> None:
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id"):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    got_sf = out_row["state_flags"][p].tolist()
    exp_sf = ref_row["state_flags"][p].tolist()
    assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


def _assert_transition_identity_lock_fields_match_ref(
    *, out_row: np.void, ref_row: np.void, record: int, p: int
) -> None:
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "instance_id",
        "instance_hit_by",
        "last_hit_by",
        "combo_count",
        "last_attack_landed",
    ):
        got = int(out_row[field][p])
        exp = int(ref_row[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    got_sf = out_row["state_flags"][p].tolist()
    exp_sf = ref_row["state_flags"][p].tolist()
    assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


def _find_unique_combo_victim_from_seed(seed_row: np.void, attacker_p: int) -> int | None:
    num_players = int(seed_row["num_players"])
    if attacker_p < 0 or attacker_p >= num_players:
        return None
    attacker_instance = int(seed_row["instance_id"][attacker_p])
    matches: list[int] = []
    for victim_p in range(num_players):
        if victim_p == attacker_p:
            continue
        if int(seed_row["last_hit_by"][victim_p]) != attacker_p:
            continue
        if int(seed_row["instance_hit_by"][victim_p]) != attacker_instance:
            continue
        matches.append(victim_p)
    if len(matches) != 1:
        return None
    return matches[0]


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        # Fixed family (A-vs-B): grounded Damage* -> KneeBend enter ownership.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            329,
            0,
            79,
            24,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            161,
            0,
            78,
            24,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            129,
            1,
            75,
            24,
        ),
        # Adjacent controls (same local transition window, strict replay parity).
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            328,
            0,
            79,
            79,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            330,
            0,
            24,
            24,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            160,
            0,
            78,
            78,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            162,
            0,
            24,
            24,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            128,
            1,
            75,
            75,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            130,
            1,
            24,
            90,
        ),
    ],
)
def test_damage_ground_to_kneebend_transition_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    assert int(seed_row["action_id"][p]) == int(seed_action)
    assert int(ref_row["action_id"][p]) == int(ref_action)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_guardreflect_frozen_powershield_shield_damage_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock family for src/combat.c:339 lane:
    # GuardReflect frozen no-submotion snapshot with x14 expired and x18 still set, where
    # shieldDamageTaken suppression must be disabled for GuardSetOff/hitlag ownership parity.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # target-1, target, target+1
    rows = (115, 116, 117)
    target = 116
    p_target = 1

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed_t = samples["seed_t"][target]
    ref_t1 = samples["ref_t1"][target]
    assert int(seed_t["action_id"][p_target]) == 0x00B6  # GuardReflect
    assert int(seed_t["action_frame"][p_target]) <= -2
    assert int(seed_t["animation_index"][p_target]) == 0xFFFFFFFF
    assert int(seed_t["guard_reflect_timer_x14"][p_target]) == 0
    assert int(seed_t["guard_reflect_timer_x18"][p_target]) == 1
    assert int(ref_t1["action_id"][p_target]) == 0x00B5  # GuardSetOff
    assert int(ref_t1["hitlag"][p_target]) > 0

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)


@pytest.mark.integration
def test_catchpull_capturewait_owner_before_victim_tick_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock family for src/grab_flow.c:61 lane:
    # CatchPull owner callback forces victim CaptureWait entry; victim receives one local anim tick
    # only when owner port < victim port, matching callback/tick ordering.
    #
    # Decomp refs:
    # - refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #     ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8,fn_800DB790,fn_800DBAE4}
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # target-1, target, target+1
    rows = (219, 220, 221)
    target = 220
    p_target = 1

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed_t = samples["seed_t"][target]
    ref_t1 = samples["ref_t1"][target]
    assert int(seed_t["grab_owner_port"][p_target]) == 0  # owner-before-victim ordering lane
    assert int(seed_t["action_id"][p_target]) == 0x00E2  # CapturePulledHi
    assert int(seed_t["action_frame"][p_target]) == 2
    assert int(ref_t1["action_id"][p_target]) == 0x00E3  # CaptureWaitHi
    assert int(ref_t1["action_frame"][p_target]) == 1

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)


@pytest.mark.integration
def test_items_guardreflect_no_submotion_shield_sweep_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock family for src/items.c:1366 lane:
    # GuardReflect no-submotion snapshots route shield overlap through swept segment + cap gate
    # split (shield cap disabled for this lane).
    #
    # Decomp refs:
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # - refs/melee/src/melee/it/itcoll.c::it_8027137C
    # - refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # target-1, target, target+1
    rows = (217, 218, 219)
    target = 218
    p_target = 0

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    seed_t = samples["seed_t"][target]
    ref_t1 = samples["ref_t1"][target]
    assert int(seed_t["action_id"][p_target]) == 0x00B6  # GuardReflect
    assert int(seed_t["action_frame"][p_target]) < 0
    assert int(seed_t["animation_index"][p_target]) == 0xFFFFFFFF
    assert int(seed_t["hitlag"][p_target]) == 0
    assert int(seed_t["hitstun"][p_target]) == 0
    assert int(np.count_nonzero(seed_t["items"]["exists"])) > 0
    assert int(np.count_nonzero(ref_t1["items"]["exists"])) == 0
    assert int(ref_t1["action_id"][p_target]) == 0x00B5  # GuardSetOff
    assert int(ref_t1["hitlag"][p_target]) > 0

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)


@pytest.mark.integration
def test_items_guardreflect_seed_x14_stale_lane_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock family for the kept GuardReflect no-submotion stale-x18 lane:
    # - frozen snapshot keeps x18 non-zero while seed x14 is already 0,
    # - reflect ownership must follow the pre-tick x14 seed lane and resolve as regular shield hit.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{it_2725_Logic94_HitShield,it_2725_Logic94_Reflected}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    families = [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            (548, 549, 550),
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            (2275, 2276, 2277),
            0,
        ),
    ]

    for dataset_rel, rows, p_target in families:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        for rec in rows:
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

        target = rows[1]
        seed_t = samples["seed_t"][target]
        ref_t1 = samples["ref_t1"][target]
        assert int(seed_t["action_id"][p_target]) == 0x00B6  # GuardReflect
        assert int(seed_t["action_frame"][p_target]) <= -2
        assert int(seed_t["animation_index"][p_target]) == 0xFFFFFFFF
        assert int(seed_t["guard_reflect_timer_x14"][p_target]) == 0
        assert int(seed_t["guard_reflect_timer_x18"][p_target]) > 0
        assert int(ref_t1["action_id"][p_target]) == 0x00B5  # GuardSetOff
        assert int(ref_t1["hitlag"][p_target]) > 0

        for rec in rows:
            _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
            for p in (0, 1):
                _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)


@pytest.mark.integration
def test_guardreflect_no_submotion_x14_expired_transition_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock families for kept GuardReflect no-submotion x14-expired shield bridge lanes:
    # - QGD 1446/9107 transition to GuardSetOff with shield-hitlag ownership preserved.
    # - Adjacent controls (target-1/target/target+1) remain replay exact.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58,lbColl_8000ACFC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    families = [
        ((1445, 1446, 1447), 1),
        ((9106, 9107, 9108), 1),
    ]
    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    for rows, p_target in families:
        for rec in rows:
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"
        target = rows[1]
        seed_t = samples["seed_t"][target]
        ref_t1 = samples["ref_t1"][target]
        assert int(seed_t["action_id"][p_target]) == 0x00B6  # GuardReflect
        assert int(seed_t["action_frame"][p_target]) < 0
        assert int(seed_t["animation_index"][p_target]) == 0xFFFFFFFF
        assert int(seed_t["guard_reflect_timer_x14"][p_target]) == 0
        assert int(seed_t["guard_reflect_timer_x18"][p_target]) <= 1
        assert int(ref_t1["action_id"][p_target]) == 0x00B5  # GuardSetOff
        assert int(ref_t1["hitlag"][p_target]) > 0

        for rec in rows:
            _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
            for p in (0, 1):
                _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            784,
            0,
            0x00B3,  # Guard hold snapshot that previously drifted to GuardOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            115,
            1,
            0x00B6,  # GuardReflect powershield snapshot that previously dropped to GuardSetOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2275,
            0,
            0x00B6,  # GuardReflect no-submotion lane.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            5167,
            0,
            0x0019,  # Hitlist stale-latch row that previously synthesized hitlag/hitstun.
        ),
    ],
) 
def test_seed_eq_guardrail_rows_stay_replay_exact(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    ref = row["ref_t1"][0]
    for field in ("action_id", "hitlag", "hitstun", "instance_id", "on_ground", "ground_id"):
        got = int(out[field][0, p])
        exp = int(ref[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert out["state_flags"][0, p].tolist() == ref["state_flags"][p].tolist(), (
        f"record={record} p={p} field=state_flags expected={ref['state_flags'][p].tolist()} "
        f"got={out['state_flags'][0, p].tolist()}"
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expect_hitlist_contains"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2485,
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            2219,
            0,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            9848,
            1,
            0,
        ),
    ],
)
def test_guardsetoff_cluster_rows_have_replay_real_shield_hit_context(
    dataset_rel: str,
    record: int,
    p: int,
    expect_hitlist_contains: int,
) -> None:
    # Representative lock rows for remaining action_id cluster 179/181/179.
    #
    # These locks intentionally verify replay-real GuardSetOff context (reference transition into
    # shieldstun + shield contact candidate) before any C-side transition fix is implemented.
    #
    # Decomp anchors for expected ownership:
    # - Shield hit resolution path: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - GuardSetOff entry bundle: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3  # Guard
    assert int(ref["action_id"][p]) == 0x00B5  # GuardSetOff
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitlag"][p]) > 0
    assert int(ref["hitstun"][p]) == 0
    assert float(ref["shield_hp"][p]) < float(seed["shield_hp"][p])
    assert int(seed["state_flags"][p, 1]) == 0x01
    assert int(ref["state_flags"][p, 1]) == 0x21

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        # Classify shield/body contacts on the pre-combat snapshot for this replay row.
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw, count = binding.debug_combat_contacts_classified(handle, 0, 64)
    finally:
        binding.destroy(handle)

    assert int(raw.shape[1]) == int(_DEBUG_CONTACT_CLASSIFIED_DTYPE.itemsize)
    contacts = raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:count]
    shield_contacts_on_p = [c for c in contacts if int(c["defender"]) == int(p) and int(c["contact_kind"]) == 1]
    assert shield_contacts_on_p, (
        f"record={record} p={p} expected at least one SHIELD contact candidate before combat; "
        f"got count={int(count)}"
    )
    chosen = _shield_contact_without_body_conflict(contacts, p)
    assert chosen is not None, (
        f"record={record} p={p} expected SHIELD contact context without same-hitbox BODY conflict; "
        f"got contacts={int(count)}"
    )
    assert float(chosen["hitbox_damage"]) > 0.0
    # Re-run once to query hitlist containment while handle is alive.
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        hitlist_contains = int(
            binding.debug_hitlist_fighter_contains(
                handle, 0, int(chosen["attacker"]), int(chosen["hitbox_id"]), int(p)
            )
        )
    finally:
        binding.destroy(handle)
    assert hitlist_contains == int(expect_hitlist_contains), (
        f"record={record} p={p} expected hitlist_contains={int(expect_hitlist_contains)} "
        f"got={hitlist_contains}"
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "attacker", "ref_hitlag"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            5614,
            0,
            1,
            5,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            8297,
            1,
            0,
            8,
        ),
    ],
)
def test_guardsetoff_cluster_rows_runtime_shield_overlap_and_exact_parity(
    dataset_rel: str,
    record: int,
    p: int,
    attacker: int,
    ref_hitlag: int,
) -> None:
    # Residual GuardSetOff rows: lock that the runtime shield path reaches overlap acceptance on at
    # least one fighter hitbox candidate and that t+1 discrete outputs match replay exactly.
    #
    # Decomp owners:
    # - ftColl_80078C70 shields branch + GuardSetOff entry dispatch.
    # - lbColl_80007BCC shield overlap geometry (consumes x58->x4C hitcapsule sweep).
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3  # Guard
    assert int(ref["action_id"][p]) == 0x00B5  # GuardSetOff
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitlag"][p]) > 0
    assert int(ref["hitstun"][p]) == 0
    assert float(ref["shield_hp"][p]) < float(seed["shield_hp"][p])
    assert int(seed["state_flags"][p, 1]) == 0x01
    assert int(ref["state_flags"][p, 1]) == 0x21
    assert int(np.count_nonzero(seed["items"]["exists"])) == 0
    assert int(np.count_nonzero(ref["items"]["exists"])) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw_cand, count_cand = binding.debug_shield_candidate_decisions(handle, 0, 128)

        active_hitboxes = 0
        for hb_id in range(4):
            timing_raw = binding.debug_hitbox_event_timing(handle, 0, attacker, hb_id)
            timing = timing_raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0]
            if int(timing["enabled_cur"]) != 0:
                active_hitboxes += 1
    finally:
        binding.destroy(handle)

    assert active_hitboxes > 0, f"record={record} p={p} attacker={attacker} expected active hitboxes"
    assert int(raw_cand.shape[1]) == int(_DEBUG_SHIELD_CANDIDATE_DTYPE.itemsize)
    cand = raw_cand.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count_cand]
    accepted = [
        c
        for c in cand
        if int(c["defender"]) == int(p)
        and int(c["source_kind"]) == 0
        and int(c["reject_reason"]) == 0
        and int(c["overlap_shield"]) == 1
    ]
    assert accepted, (
        f"record={record} p={p} expected at least one accepted SHIELD candidate in pre-combat "
        f"shield path; got accepted={len(accepted)} total={int(count_cand)}"
    )

    # Exact discrete t+1 parity lock for residual rows.
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    assert int(seed["action_id"][p]) == 0x00B3
    assert int(ref["action_id"][p]) == 0x00B5
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p]) == 0x00B5
    assert int(seed["hitlag"][p]) == 0
    assert int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(out["hitlag"][0, p]) == int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitstun"][p]) == 0
    assert int(out["hitstun"][0, p]) == int(ref["hitstun"][p]) == 0
    assert int(seed["state_flags"][p, 1]) == 0x01
    assert int(ref["state_flags"][p, 1]) == 0x21
    assert int(out["state_flags"][0, p, 1]) == int(ref["state_flags"][p, 1]) == 0x21


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2485,
            1,
        ),
    ],
)
def test_guardsetoff_cluster_rows_resolve_to_reference_outputs(
    dataset_rel: str,
    record: int,
    p: int,
) -> None:
    # Cluster lock: action_id (seed/ref/out)=179/181/179 must resolve to GuardSetOff.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3
    assert int(ref["action_id"][p]) == 0x00B5
    assert int(seed["hitlag"][p]) == 0
    assert int(ref["hitlag"][p]) > 0
    assert float(ref["shield_hp"][p]) < float(seed["shield_hp"][p])

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        attacker = 1 - p
        timing_raw = binding.debug_hitbox_event_timing(handle, 0, attacker, 0)
        timing = timing_raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0]
        assert int(timing["enabled_cur"]) == 1
        assert int(timing["last_affect_u16_7_le"]) & 0xFF == 0  # rehit_frames=0 lane

        # Decomp-corroborated stale-latch context:
        # - active window age is shorter than shield-hit hitlag (so a real prior hit would still
        #   leave defender in hitlag),
        # - yet seed hitlag/hitstun are neutral.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        window_age = int(timing["pose_frame"]) - int(timing["start_frame"])
        assert window_age >= 0
        assert window_age < int(ref["hitlag"][p])

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][0, p]) > 0
    assert int(out["hitstun"][0, p]) == int(ref["hitstun"][p])
    assert out["state_flags"][0, p].tolist() == ref["state_flags"][p].tolist()
    assert int(out["instance_id"][0, p]) == int(ref["instance_id"][p])
    assert int(out["on_ground"][0, p]) == int(ref["on_ground"][p])
    assert int(out["ground_id"][0, p]) == int(ref["ground_id"][p])


@pytest.mark.integration
def test_hitboxes_seed_bridge_early_window_owner_mismatch_trim_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Lock families for src/hitboxes.c::hitboxes_seed_bridge_trim_impossible_indefinite
    # early-window stale-suppression trim:
    # - stale indefinite seed lane (cd=0xFFFF) on attacker group-0 victim slot
    # - victim neutral hitlag/hitstun + no live shield descriptor radius
    # - victim BODY owner mismatch (instance_hit_by != attacker instance_id)
    # - early window age (pose_frame-start_frame+1) still inside expected hitlag horizon.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076CBC,ftColl_800768A0}
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C,lbColl_80007BCC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    # Causal row families from locate diff (baseline 0bba852 -> current):
    # - AGN rec=4059 p0 transition 20->90 with stale lane a=1,hb=1,v=0.
    # - GAT rec=1982 p1 transition 25->86 with stale lane a=0,hb=1,v=1.
    families = [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            (4058, 4059, 4060),
            0,  # attacked victim
            1,  # attacker
            1,  # hitbox id
            20,
            90,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            (1981, 1982, 1983),
            1,  # attacked victim
            0,  # attacker
            1,  # hitbox id
            25,
            86,
        ),
    ]

    for dataset_rel, rows, attacked, attacker, hb_id, seed_action, ref_action in families:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        for rec in rows:
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"
        target = rows[1]
        seed_t = samples["seed_t"][target]
        ref_t1 = samples["ref_t1"][target]

        # Target preconditions proving this specific stale-suppression branch context.
        assert int(seed_t["combat_hitlist_cd"][attacker, 0, attacked]) == 0xFFFF
        assert int(seed_t["hitlag"][attacked]) == 0
        assert int(seed_t["hitstun"][attacked]) == 0
        assert int(seed_t["instance_hit_by"][attacked]) != int(seed_t["instance_id"][attacker])
        assert int(seed_t["action_id"][attacked]) == int(seed_action)
        assert int(ref_t1["action_id"][attacked]) == int(ref_action)

        seed_dbg, contacts, shield_world, timing = _run_pre_combat_debug_row(dataset_path, target, attacker, hb_id)
        assert int(seed_dbg["hitlag"][attacked]) == 0
        assert int(seed_dbg["hitstun"][attacked]) == 0
        assert float(shield_world[attacked, 3]) <= 0.0
        assert int(timing["enabled_cur"]) == 1
        age_1based = int(timing["pose_frame"]) - int(timing["start_frame"]) + 1
        assert age_1based > 0

        body_hits = [
            c
            for c in contacts
            if int(c["attacker"]) == attacker
            and int(c["defender"]) == attacked
            and int(c["hitbox_id"]) == hb_id
            and int(c["contact_kind"]) == 0
        ]
        assert body_hits, f"record={target} expected BODY contact for a={attacker} d={attacked} hb={hb_id}"
        expected_hitlag = _seed_bridge_expected_hitlag_from_damage(float(body_hits[0]["hitbox_damage"]))
        assert expected_hitlag > 0
        assert age_1based <= expected_hitlag

        for rec in rows:
            _, ref_row, out_row = _run_one_step_row(dataset_path, rec, attacked)
            for p in (0, 1):
                _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)
            for fld in ("instance_hit_by", "last_hit_by"):
                got = int(out_row[fld][attacked])
                exp = int(ref_row[fld][attacked])
                assert got == exp, (
                    f"record={rec} p={attacked} field={fld} expected={exp} got={got}"
                )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            2219,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            9848,
            1,
        ),
    ],
)
def test_guardsetoff_cluster_tbk_family_rows_lock_guardsetoff_action_exact(
    dataset_rel: str,
    record: int,
    p: int,
) -> None:
    # Replay-real lock for the stale-hitlist seed-materialization family:
    # action_id at t+1 must resolve Guard -> GuardSetOff on these rows.
    #
    # Decomp ownership anchors for the governing branch:
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3
    assert int(ref["action_id"][p]) == 0x00B5

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p]) == 0x00B5
    assert int(out["hitlag"][0, p]) == int(ref["hitlag"][p])
    assert int(out["hitstun"][0, p]) == int(ref["hitstun"][p])
    assert int(out["state_flags"][0, p, 1]) == int(ref["state_flags"][p, 1]) == 0x21


@pytest.mark.integration
def test_guardsetoff_tbk_2219_one_step_and_rollout_resolve_guardsetoff() -> None:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    record = 2219
    p = 0
    window_before = 24
    window_after = 24

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    # One-step reseed-at-row must now resolve GuardSetOff action.
    one_step_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
    finally:
        binding.destroy(one_step_handle)

    seed_row = samples["seed_t"][record]
    ref_row = samples["ref_t1"][record]
    assert int(seed_row["action_id"][p]) == 0x00B3
    assert int(ref_row["action_id"][p]) == 0x00B5
    assert int(out["action_id"][0, p]) == int(ref_row["action_id"][p]) == 0x00B5
    assert int(out["state_flags"][0, p, 1]) == int(ref_row["state_flags"][p, 1]) == 0x21

    # Continuous rollout crossing the same row remains GuardSetOff-consistent.
    start = max(0, record - window_before)
    end = min(int(samples.shape[0]) - 1, record + window_after)
    rollout_rows: list[dict[str, int]] = []

    rollout_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
        binding.reseed_seed(rollout_handle, seed_bytes)
        for j in range(start, end + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            binding.write_compare(rollout_handle, out_compare_bytes)

            ref_j = samples["ref_t1"][j]
            rollout_rows.append(
                {
                    "record": int(j),
                    "ref_action": int(ref_j["action_id"][p]),
                    "out_action": int(out["action_id"][0, p]),
                    "ref_sf1": int(ref_j["state_flags"][p, 1]),
                    "out_sf1": int(out["state_flags"][0, p, 1]),
                }
            )
    finally:
        binding.destroy(rollout_handle)

    target = next(r for r in rollout_rows if int(r["record"]) == int(record))
    assert int(target["out_action"]) == int(target["ref_action"]) == 0x00B5
    assert int(target["out_sf1"]) == int(target["ref_sf1"]) == 0x21


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            5813,
            1,
            0x005A,  # DamageFlyTop
            0x00DF,  # CapturePulledHi
        ),
    ],
)
def test_damage_exit_rows_clear_hitstun_on_non_damage_entry(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
) -> None:
    # Replay-real lock for residual hitstun cluster (seed/ref/out=5/0/4):
    # when Damage* exits into non-Damage CapturePulled* entry, t+1 hitstun must be 0.
    #
    # Decomp ownership:
    # - Catch connect victim entry uses CapturePulled* motion states.
    #   refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAA10
    # - Slippi misc AS var (hitstun lane in Damage*) is sampled from fp+0x2340.
    #   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hitstun"][p]) == 5
    assert int(ref["hitstun"][p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p]) == int(ref_action)
    assert int(out["hitlag"][0, p]) == int(ref["hitlag"][p])
    assert int(out["hitstun"][0, p]) == int(ref["hitstun"][p]) == 0
    assert int(out["state_flags"][0, p, 3] & 0x02) == int(ref["state_flags"][p, 3] & 0x02) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "seed_action",
        "ref_action",
        "ref_hitlag",
        "ref_hitstun",
        "ref_sf1",
    ),
    [
        # Runtime-minority containment row fixed by attacker-owned hitlag pair gate narrowing.
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            6822,
            1,
            0x0041,  # AttackAirN
            0x0041,  # AttackAirN
            7,
            0,
            0x20,
        ),
        # Runtime fix target for this slice (strict parity).
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            3347,
            0,
            0x002A,  # Landing
            0x004B,  # DamageHi1
            3,
            9,
            0x30,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            6206,
            0,
            0x002A,  # Landing
            0x00B2,  # DamageFlyN
            0,
            0,
            0x01,
        ),
    ],
)
def test_runtime_hitlag_clusters_rows_resolve_to_exact_ref_t1(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    ref_hitlag: int,
    ref_hitstun: int,
    ref_sf1: int,
) -> None:
    # Runtime-only lock pack for the selected hitlag/hitstun clusters.
    # This test is the strict parity lock for runtime-contained rows, including the
    # attacker-owned hitlag gate row family.
    #
    # Decomp ownership anchors:
    # - fighter collision/contact gate + apply path: refs/melee/src/melee/ft/ftcoll.c::{
    #   ftColl_8007B868,ftColl_80076ED8,ftColl_80076CBC}
    # - item-vs-fighter collision/apply path: refs/melee/src/melee/it/itcoll.c::{
    #   it_802703E8,it_80272460}
    # - hitlag start/decrement ordering: refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)

    # Minimal causal context.
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(ref["hitstun"][p]) == int(ref_hitstun)
    assert int(ref["state_flags"][p, 1]) == int(ref_sf1)

    # Row-family context:
    # - AGG target row enters DamageHi1 from Landing under live laser context.
    # - QGD runtime-minority containment row is fighter-only in this window.
    if "AttachedGoodNaturedGuanaco.msl" in dataset_rel:
        assert int(np.count_nonzero(seed["items"]["exists"])) > 0
    if "QuerulousGrandDinosaur.msl" in dataset_rel:
        assert int(np.count_nonzero(seed["items"]["exists"])) == 0

    # Exact t+1 parity lock.
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(ref_action)
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == int(ref_hitstun)
    assert int(out["state_flags"][p, 1]) == int(ref["state_flags"][p, 1]) == int(ref_sf1)


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "seed_action",
        "ref_action",
        "out_action",
        "ref_hitlag",
        "out_hitlag",
        "out_hitstun",
        "ref_sf1",
        "out_sf1",
    ),
    [
        # Context-only rows (explicitly non-parity targets for this slice).
        # Keep these as deterministic signatures to preserve triage context while runtime rows land.
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            7173,
            0,
            0x0038,  # AttackS3LwS
            0x0038,  # AttackS3LwS
            0x0038,  # AttackS3LwS
            0,
            6,
            0,
            0x00,
            0x20,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            8222,
            1,
            0x002B,  # LandingFallSpecial
            0x002B,  # LandingFallSpecial
            0x005A,  # DamageN2
            0,
            6,
            52,
            0x00,
            0x30,
        ),
    ],
)
def test_runtime_hitlag_clusters_context_rows_keep_current_signatures(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    out_action: int,
    ref_hitlag: int,
    out_hitlag: int,
    out_hitstun: int,
    ref_sf1: int,
    out_sf1: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)

    # Context preconditions.
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(ref["state_flags"][p, 1]) == int(ref_sf1)

    if "GracefulAttachedTurtle.msl" in dataset_rel:
        assert int(np.count_nonzero(seed["items"]["exists"])) > 0
        assert int(np.count_nonzero(ref["items"]["exists"])) > 0
    if "QuerulousGrandDinosaur.msl" in dataset_rel:
        assert int(np.count_nonzero(seed["items"]["exists"])) == 0
        assert int(np.count_nonzero(ref["items"]["exists"])) == 0
        assert int(seed["colanim_hit_status_x198c"][1]) == 1

    # Context signature (non-parity by design for this slice).
    assert int(out["action_id"][p]) == int(out_action)
    assert int(out["hitlag"][p]) == int(out_hitlag)
    assert int(out["hitstun"][p]) == int(out_hitstun)
    assert int(out["state_flags"][p, 1]) == int(out_sf1)
    assert (
        int(out["action_id"][p]) != int(ref["action_id"][p])
        or int(out["hitlag"][p]) != int(ref["hitlag"][p])
        or int(out["hitstun"][p]) != int(ref["hitstun"][p])
        or int(out["state_flags"][p, 1]) != int(ref["state_flags"][p, 1])
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "seed_action",
        "ref_action",
        "ref_hitlag",
        "ref_hitstun",
        "ref_sf1",
    ),
    [
        # Adjacent negative control in the same local QGD window as the runtime fix row above.
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            6821,
            1,
            0x0041,  # AttackAirN
            0x0041,  # AttackAirN
            0,
            0,
            0x00,
        ),
        # Negative controls adjacent to the target rows.
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            6205,
            0,
            0x002A,  # Landing
            0x002A,  # Landing
            0,
            0,
            0x00,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            3348,
            0,
            0x004B,  # DamageHi1
            0x004B,  # DamageHi1
            2,
            9,
            0x30,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            7172,
            0,
            0x0038,  # AttackS3LwS
            0x0038,  # AttackS3LwS
            0,
            0,
            0x00,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            8221,
            1,
            0x002B,  # LandingFallSpecial
            0x002B,  # LandingFallSpecial
            0,
            0,
            0x00,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            1248,
            0,
            0x0015,  # Run
            0x00B5,  # GuardSetOff
            3,
            0,
            0x21,
        ),
    ],
)
def test_runtime_hitlag_clusters_negative_controls_remain_exact(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    ref_hitlag: int,
    ref_hitstun: int,
    ref_sf1: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(ref["hitstun"][p]) == int(ref_hitstun)
    assert int(ref["state_flags"][p, 1]) == int(ref_sf1)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(ref_action)
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == int(ref_hitlag)
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == int(ref_hitstun)
    assert int(out["state_flags"][p, 1]) == int(ref["state_flags"][p, 1]) == int(ref_sf1)


@pytest.mark.integration
def test_qgd_body_overlap_sweep_family_rows_and_adjacent_controls_stay_replay_exact() -> None:
    # Lock the exact QGD family regressed by the failed A2 BODY sweep lane:
    # - target rows: 4646/4647/4648
    # - adjacent controls: 4645 and 4649
    #
    # Required strict lock fields:
    # action_id, action_frame, animation_index, hitlag, hitstun, state_flags, instance_id.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in (4645, 4646, 4647, 4648, 4649):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    # Replay-real family preconditions for the target rows (no hit expected at t+1).
    for rec in (4646, 4647, 4648):
        row = samples[rec : rec + 1]
        assert int(row["seed_t"]["hitlag"][0, 0]) == int(row["ref_t1"]["hitlag"][0, 0]) == 0
        assert int(row["seed_t"]["hitlag"][0, 1]) == int(row["ref_t1"]["hitlag"][0, 1]) == 0
        assert int(row["seed_t"]["hitstun"][0, 1]) == int(row["ref_t1"]["hitstun"][0, 1]) == 0

    for rec in (4645, 4646, 4647, 4648, 4649):
        _, ref, out = _run_one_step_row(dataset_path, rec, 0)
        for p in (0, 1):
            _assert_body_overlap_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_qgd_body_overlap_reseed_bootstrap_x58_x4c_continuity_is_handle_independent() -> None:
    # Decomp ownership: BODY overlap consumes HitCapsule previous/current centers (x58/x4C)
    # via ftColl_8007AD18 -> lbColl_8000805C -> lbColl_80006E58.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    #
    # Reseed lock: target row output must be identical whether evaluated on a fresh handle or after
    # a prior row on the same handle (x58/x4C continuity bootstrap after reseed).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    prime_record = 4645
    target_record = 4646
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    # Replay-real precondition: target row is a no-hit frame in reference for both players.
    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["hitlag"][0, 0]) == int(target_row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(target_row["seed_t"]["hitlag"][0, 1]) == int(target_row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(target_row["seed_t"]["hitstun"][0, 1]) == int(target_row["ref_t1"]["hitstun"][0, 1]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    def _step_row(handle: object, record: int) -> np.void:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()

    fresh_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        out_target_fresh = _step_row(fresh_handle, target_record)
    finally:
        binding.destroy(fresh_handle)

    reused_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        out_prime = _step_row(reused_handle, prime_record)
        out_target_after_prime = _step_row(reused_handle, target_record)
    finally:
        binding.destroy(reused_handle)

    ref_prime = samples["ref_t1"][prime_record]
    ref_target = samples["ref_t1"][target_record]

    # Adjacent control row parity on reused handle.
    for p in (0, 1):
        _assert_body_overlap_lock_fields_match_ref(out_row=out_prime, ref_row=ref_prime, record=prime_record, p=p)

    # Target row parity on both handle paths.
    for p in (0, 1):
        _assert_body_overlap_lock_fields_match_ref(
            out_row=out_target_fresh, ref_row=ref_target, record=target_record, p=p
        )
        _assert_body_overlap_lock_fields_match_ref(
            out_row=out_target_after_prime, ref_row=ref_target, record=target_record, p=p
        )

    # Bootstrap continuity: target output is handle-independent across reseeds.
    for p in (0, 1):
        for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id"):
            a = int(out_target_fresh[field][p])
            b = int(out_target_after_prime[field][p])
            assert a == b, (
                f"record={target_record} p={p} field={field} fresh={a} after_prime={b}"
            )
        assert out_target_fresh["state_flags"][p].tolist() == out_target_after_prime["state_flags"][p].tolist(), (
            f"record={target_record} p={p} field=state_flags "
            f"fresh={out_target_fresh['state_flags'][p].tolist()} "
            f"after_prime={out_target_after_prime['state_flags'][p].tolist()}"
        )


@pytest.mark.integration
def test_body_overlap_rollout_plus4_regression_families_and_adjacent_controls_stay_replay_exact() -> None:
    # Failed BODY sweep A/B dossier (first_mismatch_total +4) families:
    # - AGG: 2864:p0 (action_id)
    # - QGD: 652:p0 (hitlag), 8637:p1 (action_id), 9627:p1 (action_id)
    # - TBK: 4592:p0 (action_id)
    #
    # Lock each target row and adjacent controls with strict replay parity on BODY ownership fields.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    families = {
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl": [
            (2864, 0, "action_id", 356, 87, (2863, 2865)),
        ],
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl": [
            (652, 0, "hitlag", 67, 67, (651, 653)),
            # Immediate +1 row (8638) is not replay-exact on baseline for locked fields; use the
            # nearest replay-exact post control row (8639) instead.
            (8637, 1, "action_id", 90, 90, (8636, 8639)),
            (9627, 1, "action_id", 14, 14, (9626, 9628)),
        ],
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl": [
            (4592, 0, "action_id", 87, 87, (4591, 4593)),
        ],
    }

    for dataset_rel, target_rows in families.items():
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")

        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        rows_to_lock: set[int] = set()
        for rec, p, field, seed_action, ref_action, controls in target_rows:
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"
            row = samples[rec : rec + 1]
            assert int(row["seed_t"]["action_id"][0, p]) == int(seed_action), (
                f"dataset={dataset_rel} record={rec} p={p} expected seed action {seed_action}"
            )
            assert int(row["ref_t1"]["action_id"][0, p]) == int(ref_action), (
                f"dataset={dataset_rel} record={rec} p={p} expected ref action {ref_action}"
            )
            # Guard the exact first-mismatch field families identified in the +4 dossier.
            assert field in ("action_id", "hitlag")
            rows_to_lock.add(rec)
            for ctl in controls:
                rows_to_lock.add(int(ctl))

        for rec in sorted(rows_to_lock):
            assert rec >= 0, f"invalid negative control row: record={rec}"
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"
            _, ref, out = _run_one_step_row(dataset_path, rec, 0)
            for p in (0, 1):
                _assert_body_overlap_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_body_overlap_subset_attachedgoodnaturedguanaco_2533_family_stays_replay_exact() -> None:
    # BODY subset activation lock: AGG rec=2533:p1 is the rollout first-mismatch family resolved by
    # the kept lbColl_80006E58 subset lane (with adjacent controls).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = 2533
    controls = (2532, 2534)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    row = samples[target_record : target_record + 1]
    assert int(row["seed_t"]["action_id"][0, 1]) == 68
    assert int(row["ref_t1"]["action_id"][0, 1]) == 85

    for rec in (target_record, *controls):
        _, ref, out = _run_one_step_row(dataset_path, rec, 0)
        for p in (0, 1):
            _assert_body_overlap_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_body_overlap_enable_edge_qgd_645_seeded_family_and_controls_stay_replay_exact() -> None:
    # Enable-edge BODY subset lock:
    # - QGD rec=645:p1 is the seeded rollout first-mismatch family removed by enabling the
    #   ftColl_800768A0/ftColl_8007AD18 edge lane in the lbColl_80006E58 subset.
    # - Adjacent controls: 644 and 646.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = 645
    controls = (644, 646)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    row = samples[target_record : target_record + 1]
    assert int(row["seed_t"]["action_id"][0, 1]) == 356
    assert int(row["ref_t1"]["action_id"][0, 1]) == 88

    for rec in (target_record, *controls):
        _, ref, out = _run_one_step_row(dataset_path, rec, 0)
        for p in (0, 1):
            _assert_body_overlap_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "target_record",
        "p",
        "seed_action",
        "ref_action",
        "seed_anim",
        "ref_anim",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            573,
            1,
            241,
            90,
            264,
            180,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            444,
            1,
            241,
            90,
            264,
            180,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2513,
            0,
            240,
            88,
            263,
            178,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            8286,
            0,
            240,
            88,
            263,
            178,
        ),
    ],
)
def test_throw_release_transition_families_and_adjacent_controls_action_anim_stay_replay_exact(
    dataset_rel: str,
    target_record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    seed_anim: int,
    ref_anim: int,
) -> None:
    # Throw-release lock families resolved by the decomp-backed parity bundle:
    # - action_id 241->90 with animation 264->180
    # - action_id 240->88 with animation 263->178
    # - jumps_left 2->1 parity from grounded/throw release ftCommon_8007D5D4 ownership lane.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    #
    # Adjacent controls assert transition continuity:
    # - target-1 remains in pre-release Thrown*
    # - target+1 remains in released Damage* state
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(ref_action)
    assert int(target_row["seed_t"]["animation_index"][0, p]) == int(seed_anim)
    assert int(target_row["ref_t1"]["animation_index"][0, p]) == int(ref_anim)

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == int(seed_action)
    assert int(pre_row["seed_t"]["animation_index"][0, p]) == int(seed_anim)
    assert int(pre_row["ref_t1"]["animation_index"][0, p]) == int(seed_anim)

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == int(ref_action)
    assert int(post_row["ref_t1"]["action_id"][0, p]) == int(ref_action)
    assert int(post_row["seed_t"]["animation_index"][0, p]) == int(ref_anim)
    assert int(post_row["ref_t1"]["animation_index"][0, p]) == int(ref_anim)

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_throw_release_action_anim_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            2237,
            0,
            360,  # ftFx_MS_SpecialLwStart
            361,  # ftFx_MS_SpecialLwLoop
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            1592,
            0,
            360,  # ftFx_MS_SpecialLwStart
            361,  # ftFx_MS_SpecialLwLoop
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            1761,
            0,
            365,  # ftFx_MS_SpecialAirLwStart
            366,  # ftFx_MS_SpecialAirLwLoop
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            3677,
            1,
            365,  # ftFx_MS_SpecialAirLwStart
            366,  # ftFx_MS_SpecialAirLwLoop
        ),
    ],
)
def test_shine_reflect_behavior_transition_rows_and_adjacent_controls_stay_replay_exact(
    dataset_rel: str,
    target_record: int,
    p: int,
    seed_action: int,
    ref_action: int,
) -> None:
    # Lock the Shine Start->Loop transition lane where state_flags[0] bit0x04 (fp+0x2218_b5) must
    # clear while bit0x10 (fp->reflecting) stays set on entry.
    #
    # Decomp ownership:
    # - Shine loop enter path creates reflect hit from Special attrs ReflectDesc.
    # - ftColl_CreateReflectHit copies ReflectDesc.x20_behavior -> fp->x2218_b5.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_CreateReflectHit
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
    #
    # Adjacent controls:
    # - target-1 remains in Start
    # - target+1 remains in Loop
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(ref_action)
    seed_flags_target = int(target_row["seed_t"]["state_flags"][0, p, 0])
    ref_flags_target = int(target_row["ref_t1"]["state_flags"][0, p, 0])
    assert (seed_flags_target & 0x04) != 0  # x2218_b5 set in Start snapshot.
    assert (ref_flags_target & 0x04) == 0  # x2218_b5 cleared by ReflectDesc.x20_behavior.
    assert (ref_flags_target & 0x10) != 0  # reflecting bit stays active in Loop.

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == int(seed_action)

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == int(ref_action)
    assert int(post_row["ref_t1"]["action_id"][0, p]) == int(ref_action)
    post_ref_flags = int(post_row["ref_t1"]["state_flags"][0, p, 0])
    assert (post_ref_flags & 0x04) == 0
    assert (post_ref_flags & 0x10) != 0

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_body_overlap_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            5403,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            9658,
            0,
        ),
    ],
)
def test_attack11_jab_chain_transition_rows_and_adjacent_controls_stay_replay_exact(
    dataset_rel: str,
    target_record: int,
    p: int,
) -> None:
    # Attack11 -> Attack12 transition lock for the kept decomp-backed jab-chain ownership lane:
    # - Attack11_IASA evaluates checkAttack12 outside allow_interrupt.
    # - checkAttack12 requires x2218_b1 (set_jab_combo) and A-edge input intent.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,checkAttack12}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
    #
    # Adjacent controls:
    # - target-1 stays in Attack11
    # - target+1 stays in Attack12
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == 44
    assert int(target_row["ref_t1"]["action_id"][0, p]) == 45
    assert int(target_row["seed_t"]["animation_index"][0, p]) == 46
    assert int(target_row["ref_t1"]["animation_index"][0, p]) == 47

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == 44
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == 44

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == 45
    assert int(post_row["ref_t1"]["action_id"][0, p]) == 45

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p", "ref_action", "ref_frame", "ref_anim"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            2349,
            1,
            50,
            36,
            52,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            6017,
            0,
            50,
            36,
            52,
        ),
    ],
)
def test_attackdash_iasa_pregate_transition_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str,
    target_record: int,
    p: int,
    ref_action: int,
    ref_frame: int,
    ref_anim: int,
) -> None:
    # AttackDash IASA lock for the kept decomp-backed pre-gate lane:
    # - ftCo_AttackDash_IASA executes ftCo_800D8AE0 before Wait_IASA delegation.
    # - this lock captures the action_frame==35 hold window where pre-gate ownership runs and the
    #   lane remains in AttackDash for t+1 when no consume condition is met.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
    #
    # Adjacent controls:
    # - target-1 remains AttackDash
    # - target+1 remains in the transitioned action
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == 50  # AttackDash
    assert int(target_row["seed_t"]["action_frame"][0, p]) == 35
    assert int(target_row["seed_t"]["animation_index"][0, p]) == 52
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(ref_action)
    assert int(target_row["ref_t1"]["action_frame"][0, p]) == int(ref_frame)
    assert int(target_row["ref_t1"]["animation_index"][0, p]) == int(ref_anim)

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == 50
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == 50

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == 50
    assert int(post_row["ref_t1"]["action_id"][0, p]) == 50

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_attackdash_iasa_pregate_transition_to_kneebend_row_and_adjacent_controls_are_replay_exact() -> None:
    # AttackDash transition family lock (improved lane): 50 -> 39 (AttackDash -> KneeBend).
    # - ftCo_AttackDash_IASA executes ftCo_800D8AE0 before Wait_IASA delegation.
    # - At the action_frame==35 pre-gate boundary, consume condition ownership transitions to
    #   KneeBend on t+1 in this family.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    target_record = 917
    p = 0
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == 50  # AttackDash
    assert int(target_row["seed_t"]["action_frame"][0, p]) == 35
    assert int(target_row["seed_t"]["animation_index"][0, p]) == 52
    assert int(target_row["ref_t1"]["action_id"][0, p]) == 39  # KneeBend
    assert int(target_row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(target_row["ref_t1"]["animation_index"][0, p]) == 30

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == 50
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == 50

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == 39
    assert int(post_row["ref_t1"]["action_id"][0, p]) == 39

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_attackdash_iasa_guardon_identity_continuity_row_and_adjacent_controls_are_replay_exact() -> None:
    # AttackDash -> GuardOn identity lock (improved lane): 50 -> 178.
    # - AttackDash IASA delegates to Wait-style interrupt checks.
    # - Wait IASA checks guard entry (ftCo_80091A4C) before jump/dash/squat/turn/walk.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    target_record = 10910
    p = 0
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == 50  # AttackDash
    assert int(target_row["ref_t1"]["action_id"][0, p]) == 178  # GuardOn

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == 50
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == 50

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == 178
    assert int(post_row["ref_t1"]["action_id"][0, p]) == 178

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_identity_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
def test_attackdash_guardon_entry_countdown_row_and_adjacent_controls_are_replay_exact() -> None:
    # AttackDash -> GuardOn countdown ownership lock:
    # - AttackDash IASA delegates into Wait-style interrupt checks once.
    # - GuardOn callback/IASA ordering should not be double-applied in the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    target_record = 10911
    p = 0
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == 50  # AttackDash
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == 178  # GuardOn entry

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == 178  # GuardOn
    assert int(target_row["ref_t1"]["action_id"][0, p]) == 178  # GuardOn hold

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == 178  # GuardOn hold
    assert int(post_row["ref_t1"]["action_id"][0, p]) == 178  # GuardOn hold

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "target_record",
        "p",
        "target_seed_action",
        "target_ref_action",
        "pre_seed_action",
        "pre_ref_action",
        "post_seed_action",
        "post_ref_action",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            593,
            1,
            38,
            354,
            38,
            38,
            354,
            354,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            718,
            1,
            38,
            354,
            38,
            38,
            354,
            354,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            5807,
            1,
            38,
            354,
            38,
            38,
            354,
            354,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            6596,
            0,
            88,
            354,
            88,
            88,
            354,
            354,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            7204,
            1,
            38,
            354,
            88,
            38,
            354,
            354,
        ),
    ],
)
def test_damagefall_specialhi_transition_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str,
    target_record: int,
    p: int,
    target_seed_action: int,
    target_ref_action: int,
    pre_seed_action: int,
    pre_ref_action: int,
    post_seed_action: int,
    post_ref_action: int,
) -> None:
    # DamageFall IASA B-special lock families for the kept narrow air-special lane:
    # - ftCo_DamageFall_IASA delegates to ftCo_SpecialAir_CheckInput for B-special checks.
    # - This lock covers the improved transition families that now enter SpecialHiHoldAir (354)
    #   with strict replay parity, plus adjacent controls around each target row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(target_seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(target_ref_action)

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == int(pre_seed_action)
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == int(pre_ref_action)

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == int(post_seed_action)
    assert int(post_row["ref_t1"]["action_id"][0, p]) == int(post_ref_action)

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p", "target_seed_action", "target_ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            1151,
            1,
            15,
            16,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            7373,
            0,
            21,
            21,
        ),
    ],
)
def test_walk_run_timebase_ownership_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, p: int, target_seed_action: int, target_ref_action: int
) -> None:
    # Walk/Run timebase ownership lock families:
    # - Walk type-switch preserves remapped phase via ftWalkCommon_800DFEC8 + ftCo_Walk_Enter.
    # - Walk/Run Anim callbacks own next-frame ftAnim_SetAnimRate after ftAnim tick.
    # refs/melee/src/melee/ft/ftwalkcommon.c::{ftWalkCommon_800DFEC8,ftWalkCommon_800DFDDC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::{ftCo_Walk_Enter,ftCo_Walk_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(target_seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(target_ref_action)

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            370,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            567,
            1,
        ),
    ],
)
def test_capturewait_rate_ownership_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, p: int
) -> None:
    # CaptureWait ownership/rate bridge lock families:
    # - CatchPull/CatchWait callback ordering can drive CapturePulled* -> CaptureWait* on victim.
    # - CaptureWait Anim callback owns frame_speed_mul update after Fighter_8006A360 anim tick.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) in (226, 227)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == 227

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p", "target_seed_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            224,
            1,
            241,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            424,
            1,
            242,
        ),
    ],
)
def test_throw_deferred_anim_tick_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, p: int, target_seed_action: int
) -> None:
    # Throw deferred anim-rate/tick lock families:
    # - Throw release damage-entry path applies immediate ftAnim_8006EBA4 on Damage* entry.
    # - Post-items throw-hit apply lane defers one extra victim tick for Throw{Hi,Lw} windows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_ThrowLw_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(target_seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(target_seed_action)

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "owner_p", "victim_p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            989,
            0,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            974,
            0,
            1,
        ),
    ],
)
def test_throw_owner_before_victim_deferred_tick_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, owner_p: int, victim_p: int
) -> None:
    # Owner-before-victim ThrowLw deferred tick lock families (causal subset):
    # - Post-items deferred throw-hit apply adds one victim tick only for Throw{Hi,Lw} when
    #   thrower callback ownership precedes victim callback in Fighter_procUpdate order.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    seed_t = target_row["seed_t"][0]
    ref_t1 = target_row["ref_t1"][0]

    assert owner_p < victim_p
    assert int(seed_t["action_id"][owner_p]) == 241  # ThrowLw
    assert int(ref_t1["action_id"][owner_p]) == 90   # DamageLw2
    assert int(seed_t["action_id"][victim_p]) == 221  # ThrownLw
    assert int(seed_t["action_frame"][owner_p]) >= 6
    assert int(ref_t1["action_frame"][owner_p]) == 1

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, owner_p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=owner_p)
        _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=victim_p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "rows", "owner_p", "victim_p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            (226, 227, 228),
            0,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            (443, 444, 445),
            0,
            1,
        ),
    ],
)
def test_throwhi_owner_before_victim_deferred_hitstun_tick_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, rows: tuple[int, int, int], owner_p: int, victim_p: int
) -> None:
    # ThrowHi owner-before-victim deferred hitstun-tick lock families:
    # - ThrowHi release/hit consume runs in thrower Anim callback (ftCo_ThrowHi_Anim -> ftCo_800DD724).
    # - Damage* callback ownership decrements hitstun in ftCo_8008F744.
    # - Deferred post-items throw-hit apply must preserve same-frame owner-before-victim callback order.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowHi_Anim,ftCo_800DD724,ftCo_800DE7C0,ftCo_800DDDE4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = rows[1]
    seed_t = samples["seed_t"][target]
    ref_t1 = samples["ref_t1"][target]
    assert int(seed_t["action_id"][owner_p]) == 221  # ThrowHi
    assert int(seed_t["action_id"][victim_p]) == 241  # ThrownHi
    assert int(ref_t1["action_id"][victim_p]) == 90  # DamageLw2
    assert int(ref_t1["action_frame"][victim_p]) == 2
    assert int(ref_t1["hitlag"][victim_p]) == 0
    assert int(ref_t1["hitstun"][victim_p]) > 0

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, victim_p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row, ref_row=ref_row, record=rec, p=victim_p
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "rows", "owner_p", "victim_p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            (437, 438, 439),
            0,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            (447, 448, 449),
            0,
            1,
        ),
    ],
)
def test_throwlw_attached_item_hitlag_x221c_b0_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, rows: tuple[int, int, int], owner_p: int, victim_p: int
) -> None:
    # ThrowLw attached-item hitlag ownership lock families:
    # - While victim remains ThrownLw/attached, attached-hit windows can produce hitlag without
    #   entering Damage*.
    # - Keep x221C_b0 ownership aligned to no-reaction gate usage (inlineB1), avoiding spurious set
    #   on this item-attached lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008EC90,inlineB1}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = rows[1]
    seed_t = samples["seed_t"][target]
    ref_t1 = samples["ref_t1"][target]
    assert int(seed_t["action_id"][owner_p]) == 222  # ThrowLw
    assert int(seed_t["action_id"][victim_p]) == 242  # ThrownLw
    assert int(seed_t["hitlag"][victim_p]) == 0
    assert int(ref_t1["hitlag"][victim_p]) > 0
    assert int(ref_t1["action_id"][victim_p]) == 242  # stays ThrownLw
    assert int(ref_t1["state_flags"][victim_p, 3]) == 0

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, victim_p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row, ref_row=ref_row, record=rec, p=victim_p
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "attacker_p", "victim_p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            999,
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            237,
            0,
            1,
        ),
    ],
)
def test_combo_victim_reseed_bridge_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, attacker_p: int, victim_p: int
) -> None:
    # Combo-victim ownership reseed bridge lock families:
    # - ftColl_800763C0 combo continuation compares stored victim pointer (fp->x2094) + attack id.
    # - Slippi omits fp->x2094; bridge reconstructs victim only when attribution lanes are unique.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    seed_t = target_row["seed_t"][0]
    ref_t1 = target_row["ref_t1"][0]
    num_players = int(seed_t["num_players"])

    assert int(seed_t["combo_count"][attacker_p]) > 0
    seed_combo_victim = int(seed_t["combo_victim_port"][attacker_p])
    assert seed_combo_victim >= num_players or seed_combo_victim == attacker_p
    inferred_victim = _find_unique_combo_victim_from_seed(seed_t, attacker_p)
    assert inferred_victim == victim_p
    assert int(seed_t["last_hit_by"][victim_p]) == attacker_p
    assert int(seed_t["instance_hit_by"][victim_p]) == int(seed_t["instance_id"][attacker_p])
    assert int(ref_t1["combo_count"][attacker_p]) > int(seed_t["combo_count"][attacker_p])

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, attacker_p)
        _assert_transition_identity_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=attacker_p)


@pytest.mark.integration
def test_guard_damage_and_guardreflect_transition_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Guard transition ownership locks for the kept action lane:
    # 1) DamageHi* -> GuardOn (jump+shield buffered) must not double-consume into KneeBend.
    # 2) GuardReflect no-submotion snapshot fallback must still allow same-frame Guard IASA jump.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #     ftCo_80091A4C,ftCo_800924C0,ftCo_GuardReflect_Anim,ftCo_Guard_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    families = [
        # target-1 / target / target+1
        ((189, 190, 191), 1, 75, 178),  # DamageHi1 -> GuardOn
        ((434, 435, 436), 1, 182, 24),  # GuardReflect -> KneeBend
    ]

    for rows, p_target, seed_action, ref_action in families:
        for rec in rows:
            assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"
        target = rows[1]
        target_row = samples[target : target + 1]
        assert int(target_row["seed_t"]["action_id"][0, p_target]) == int(seed_action)
        assert int(target_row["ref_t1"]["action_id"][0, p_target]) == int(ref_action)

        for rec in rows:
            _, ref, out = _run_one_step_row(dataset_path, rec, p_target)
            for p in (0, 1):
                _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            3020,
            0,
            0,   # DeadDown
            12,  # Rebirth
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            3646,
            1,
            0,   # DeadDown
            12,  # Rebirth
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            961,
            1,
            0,   # DeadDown
            12,  # Rebirth
        ),
    ],
)
def test_death_to_rebirth_identity_reset_rows_and_adjacent_controls_are_replay_exact(
    dataset_rel: str, target_record: int, p: int, seed_action: int, ref_action: int
) -> None:
    # Death->Rebirth identity reset lock:
    # - Fighter_UnkProcessDeath runs Fighter_UnkInitReset + ft_800892D4 before Rebirth entry.
    # - That call chain owns `instance_hit_by`, `last_hit_by`, and combo/last-attack fields.
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkProcessDeath_80068354
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800892D4
    #
    # Adjacent controls:
    # - target-1 remains in DeadDown (pre-transition)
    # - target+1 remains in Rebirth (post-transition)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    controls = (target_record - 1, target_record + 1)
    for rec in (target_record, *controls):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_row = samples[target_record : target_record + 1]
    assert int(target_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(target_row["ref_t1"]["action_id"][0, p]) == int(ref_action)

    pre_row = samples[controls[0] : controls[0] + 1]
    assert int(pre_row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(pre_row["ref_t1"]["action_id"][0, p]) == int(seed_action)

    post_row = samples[controls[1] : controls[1] + 1]
    assert int(post_row["seed_t"]["action_id"][0, p]) == int(ref_action)
    assert int(post_row["ref_t1"]["action_id"][0, p]) == int(ref_action)

    for rec in (controls[0], target_record, controls[1]):
        _, ref, out = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_identity_lock_fields_match_ref(out_row=out, ref_row=ref, record=rec, p=p)
