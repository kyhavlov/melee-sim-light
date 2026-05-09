from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_GUARD_REFLECT = 0x00B6


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


def _find_item_slot_by_key(items: np.ndarray, *, spawn_id: int, item_type: int) -> int:
    for it in range(int(items.shape[0])):
        row = items[it]
        if int(row["exists"]) == 0:
            continue
        if int(row["spawn_id"]) == int(spawn_id) and int(row["type"]) == int(item_type):
            return it
    return -1


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
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
    return seed, out, ref


@dataclass(frozen=True)
class _TransferCase:
    dataset_rel: str
    record: int
    prev_record: int
    spawn_id: int
    item_type: int
    owner_prev: int
    owner_seed: int
    note: str


@dataclass(frozen=True)
class _TimingCase:
    dataset_rel: str
    control_record: int
    transfer_record: int
    apply_record: int
    spawn_id: int
    item_type: int
    transfer_commits_owner: bool
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _TransferCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=4829,
            prev_record=4828,
            spawn_id=125,
            item_type=55,
            owner_prev=1,
            owner_seed=0,
            note="powershield transfer lane A",
        ),
        _TransferCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=6208,
            prev_record=6207,
            spawn_id=149,
            item_type=55,
            owner_prev=1,
            owner_seed=0,
            note="powershield transfer lane B",
        ),
        _TransferCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=7449,
            prev_record=7448,
            spawn_id=142,
            item_type=55,
            owner_prev=1,
            owner_seed=0,
            note="powershield transfer lane C",
        ),
    ],
)
def test_reflected_laser_transfer_rows_match_replay_real_item_identity(case: _TransferCase) -> None:
    # Replay-real lock for reflected-laser transfer rows:
    # - owner/instance transfer happens in item reflect apply path.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record
    assert int(samples.shape[0]) > case.prev_record
    seed_prev = samples[case.prev_record]["seed_t"]
    seed = samples[case.record]["seed_t"]
    ref = samples[case.record]["ref_t1"]

    prev_slot = _find_item_slot_by_key(seed_prev["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    seed_slot = _find_item_slot_by_key(seed["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    ref_slot = _find_item_slot_by_key(ref["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert prev_slot >= 0, f"{case.note}: expected prior-frame reflected item key"
    assert seed_slot >= 0, f"{case.note}: expected seed reflected item key"
    assert ref_slot >= 0, f"{case.note}: expected ref reflected item key"
    assert int(seed_prev["items"][prev_slot]["owner"]) == int(case.owner_prev), case.note
    assert int(seed["items"][seed_slot]["owner"]) == int(case.owner_seed), case.note
    assert int(seed["action_id"][case.owner_seed]) == ACT_GUARD_REFLECT, case.note
    assert int(seed["guard_reflect_timer_x14"][case.owner_seed]) > 0, case.note
    assert int(seed["guard_reflect_timer_x18"][case.owner_seed]) > 0, case.note

    seed_out, out, ref_out = _step_one_row(dataset_path=dataset_path, record=case.record)
    out_slot = _find_item_slot_by_key(out["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert out_slot >= 0, f"{case.note}: reflected item key must persist through transfer frame"
    assert int(seed_out["items"][seed_slot]["owner"]) == int(case.owner_seed), case.note

    # Strict replay-real discrete lock on transfer-owned item identity lanes.
    for fld in ("exists", "type", "state", "owner", "instance_id"):
        got = int(out["items"][out_slot][fld])
        exp = int(ref_out["items"][ref_slot][fld])
        assert got == exp, f"{case.note}: field={fld} expected={exp} got={got}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _TimingCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            control_record=4830,
            transfer_record=4828,
            apply_record=4829,
            spawn_id=125,
            item_type=55,
            transfer_commits_owner=True,
            note="powershield timing lane A",
        ),
        _TimingCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            control_record=6209,
            transfer_record=6207,
            apply_record=6208,
            spawn_id=149,
            item_type=55,
            transfer_commits_owner=True,
            note="powershield timing lane B",
        ),
        _TimingCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            control_record=7450,
            transfer_record=7448,
            apply_record=7449,
            spawn_id=142,
            item_type=55,
            transfer_commits_owner=True,
            note="powershield timing lane C",
        ),
    ],
)
def test_powershield_reflect_transfer_then_speed_apply_timing_locks(case: _TimingCase) -> None:
    # Replay-real timing lock for powershield reflect:
    # - GuardReflect setup creates reflect snapshot/multipliers.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
    # - Overlap writes owner/xDA8_short + reflect snapshot lanes on transfer frame.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # - Item logic consumes snapshot and applies reflected trajectory update on follow-up frame.
    # refs/melee/src/melee/it/item.c::Item_80269F14
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.apply_record
    assert int(samples.shape[0]) > case.transfer_record
    assert int(samples.shape[0]) > case.control_record

    # Strict replay-real parity: adjacent control row after transfer/apply should stay stable.
    _seed_ctrl, out_ctrl, ref_ctrl = _step_one_row(dataset_path=dataset_path, record=case.control_record)
    ctrl_slot_out = _find_item_slot_by_key(out_ctrl["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    ctrl_slot_ref = _find_item_slot_by_key(ref_ctrl["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert ctrl_slot_out >= 0, f"{case.note}: control row item missing in out"
    assert ctrl_slot_ref >= 0, f"{case.note}: control row item missing in ref"
    for fld in ("owner", "instance_id", "direction"):
        got = int(out_ctrl["items"][ctrl_slot_out][fld])
        exp = int(ref_ctrl["items"][ctrl_slot_ref][fld])
        assert got == exp, f"{case.note}: control field={fld} expected={exp} got={got}"
    for fld in ("vel_x", "vel_y"):
        got = float(out_ctrl["items"][ctrl_slot_out][fld])
        exp = float(ref_ctrl["items"][ctrl_slot_ref][fld])
        assert np.isclose(got, exp, atol=1e-6), f"{case.note}: control field={fld} expected={exp} got={got}"

    # Transfer frame strict parity lanes: direction/velocity.
    seed_tx, out_tx, ref_tx = _step_one_row(dataset_path=dataset_path, record=case.transfer_record)
    tx_slot_seed = _find_item_slot_by_key(seed_tx["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    tx_slot_out = _find_item_slot_by_key(out_tx["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    tx_slot_ref = _find_item_slot_by_key(ref_tx["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert tx_slot_seed >= 0 and tx_slot_out >= 0 and tx_slot_ref >= 0, f"{case.note}: transfer row item key missing"
    for fld in ("direction",):
        got = int(out_tx["items"][tx_slot_out][fld])
        exp = int(ref_tx["items"][tx_slot_ref][fld])
        assert got == exp, f"{case.note}: transfer field={fld} expected={exp} got={got}"
    tx_owner_out = int(out_tx["items"][tx_slot_out]["owner"])
    tx_owner_ref = int(ref_tx["items"][tx_slot_ref]["owner"])
    tx_owner_seed = int(seed_tx["items"][tx_slot_seed]["owner"])
    tx_iid_out = int(out_tx["items"][tx_slot_out]["instance_id"])
    tx_iid_ref = int(ref_tx["items"][tx_slot_ref]["instance_id"])
    tx_iid_seed = int(seed_tx["items"][tx_slot_seed]["instance_id"])
    if case.transfer_commits_owner:
        # Retained ReflectDesc rows commit owner/xDA8 on the overlap frame when the
        # still-approaching laser is inside the shield-bone ReflectDesc vertical lane. This covers
        # both aged GuardReflect rows and the GuardOn -> GuardReflect follow-up row whose x14/x18
        # setup is source-owned but not seed-visible until after this step.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
        # refs/melee/src/melee/it/item.c::Item_80269F14
        assert tx_owner_out == tx_owner_ref, f"{case.note}: transfer owner expected_ref={tx_owner_ref} got={tx_owner_out}"
        assert tx_iid_out == tx_iid_ref, f"{case.note}: transfer instance_id expected_ref={tx_iid_ref} got={tx_iid_out}"
        assert int(out_tx["items"][tx_slot_out]["misc2"]) == int(ref_tx["items"][tx_slot_ref]["misc2"])
        assert int(out_tx["items"][tx_slot_out]["misc3"]) == int(ref_tx["items"][tx_slot_ref]["misc3"])
    else:
        # Known-gap expectation (not parity lock): rows outside the retained ReflectDesc commit
        # lanes keep owner/xDA8 seed-latched while the reflect snapshot is staged for the next item
        # pass.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
        # refs/melee/src/melee/it/item.c::Item_80269F14
        assert tx_owner_out == tx_owner_seed, (
            f"{case.note}: known-gap transfer owner expected_seed={tx_owner_seed} got={tx_owner_out}"
        )
        assert tx_owner_out != tx_owner_ref, (
            f"{case.note}: known-gap expectation requires transfer owner != ref ({tx_owner_ref})"
        )
        assert int(out_tx["items"][tx_slot_out]["misc2"]) == int(seed_tx["items"][tx_slot_seed]["misc2"])
        assert int(out_tx["items"][tx_slot_out]["misc3"]) == int(seed_tx["items"][tx_slot_seed]["misc3"])
        assert tx_iid_out == tx_iid_seed, (
            f"{case.note}: known-gap transfer instance_id expected_seed={tx_iid_seed} got={tx_iid_out}"
        )
        assert tx_iid_out != tx_iid_ref, (
            f"{case.note}: known-gap expectation requires transfer instance_id != ref ({tx_iid_ref})"
        )
    for fld in ("vel_x", "vel_y"):
        got = float(out_tx["items"][tx_slot_out][fld])
        exp = float(ref_tx["items"][tx_slot_ref][fld])
        assert np.isclose(got, exp, atol=1e-6), f"{case.note}: transfer field={fld} expected={exp} got={got}"

    # Strict replay-real parity: follow-up frame after deferred speed apply.
    seed_ap, out_ap, ref_ap = _step_one_row(dataset_path=dataset_path, record=case.apply_record)
    ap_slot_seed = _find_item_slot_by_key(
        seed_ap["items"], spawn_id=case.spawn_id, item_type=case.item_type
    )
    ap_slot_out = _find_item_slot_by_key(
        out_ap["items"], spawn_id=case.spawn_id, item_type=case.item_type
    )
    ap_slot_ref = _find_item_slot_by_key(
        ref_ap["items"], spawn_id=case.spawn_id, item_type=case.item_type
    )
    assert ap_slot_seed >= 0 and ap_slot_out >= 0 and ap_slot_ref >= 0, (
        f"{case.note}: follow-up row item key missing"
    )
    for fld in ("owner", "instance_id", "direction"):
        got = int(out_ap["items"][ap_slot_out][fld])
        exp = int(ref_ap["items"][ap_slot_ref][fld])
        assert got == exp, f"{case.note}: apply field={fld} expected={exp} got={got}"
    for fld in ("vel_x", "vel_y"):
        got = float(out_ap["items"][ap_slot_out][fld])
        exp = float(ref_ap["items"][ap_slot_ref][fld])
        assert np.isclose(got, exp, atol=1e-6), f"{case.note}: apply field={fld} expected={exp} got={got}"
    # Fox/Falco laser reflected callback ignores ReflectDesc.x1C speed_mul (`p_ftCommonData->x2B0`)
    # and only flips the article angle/facing before the next Anim callback rebuilds velocity.
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
    seed_speed = float(
        np.hypot(
            float(seed_ap["items"][ap_slot_seed]["vel_x"]),
            float(seed_ap["items"][ap_slot_seed]["vel_y"]),
        )
    )
    out_speed = float(
        np.hypot(
            float(out_ap["items"][ap_slot_out]["vel_x"]),
            float(out_ap["items"][ap_slot_out]["vel_y"]),
        )
    )
    assert np.isclose(out_speed, seed_speed, atol=1e-6), (
        f"{case.note}: reflected laser speed magnitude expected identity got seed={seed_speed} out={out_speed}"
    )


@dataclass(frozen=True)
class _HitCase:
    dataset_rel: str
    record: int
    spawn_id: int
    item_type: int
    reflected_owner: int
    target_p: int
    expect_ref_hitlag: int
    expect_ref_hitstun: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=4833,
            spawn_id=125,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=0,
            expect_ref_hitstun=0,
            note="adjacent control A (no hit yet)",
        ),
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=4834,
            spawn_id=125,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=3,
            expect_ref_hitstun=9,
            note="reflected hit A",
        ),
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=6212,
            spawn_id=149,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=0,
            expect_ref_hitstun=0,
            note="adjacent control B (no hit yet)",
        ),
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=6213,
            spawn_id=149,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=3,
            expect_ref_hitstun=9,
            note="reflected hit B",
        ),
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=7475,
            spawn_id=142,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=0,
            expect_ref_hitstun=0,
            note="adjacent control C (no hit yet)",
        ),
        _HitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=7476,
            spawn_id=142,
            item_type=55,
            reflected_owner=0,
            target_p=1,
            expect_ref_hitlag=4,
            expect_ref_hitstun=30,
            note="reflected hit C",
        ),
    ],
)
def test_reflected_laser_hit_rows_and_adjacent_controls_lock_replay_real(case: _HitCase) -> None:
    # Replay-real reflected-hit lock rows where the reflected owner is no longer in GuardReflect.
    # This asserts item-owned reflected damage shaping after transfer, not owner action snapshots.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/it/item.c::Item_80269F14
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record
    row = samples[case.record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    seed_slot = _find_item_slot_by_key(seed["items"], spawn_id=case.spawn_id, item_type=case.item_type)
    assert seed_slot >= 0, f"{case.note}: expected reflected item key in seed"
    assert int(seed["items"][seed_slot]["owner"]) == int(case.reflected_owner), case.note
    # The reflected item should remain reflected after owner transfer even when GuardReflect timers
    # have expired on the owner by the time collision lands.
    assert int(seed["guard_reflect_timer_x14"][case.reflected_owner]) == 0, case.note
    assert int(seed["guard_reflect_timer_x18"][case.reflected_owner]) == 0, case.note

    assert int(ref["hitlag"][case.target_p]) == int(case.expect_ref_hitlag), case.note
    assert int(ref["hitstun"][case.target_p]) == int(case.expect_ref_hitstun), case.note

    _seed, out, ref_out = _step_one_row(dataset_path=dataset_path, record=case.record)
    assert int(out["hitlag"][case.target_p]) == int(ref_out["hitlag"][case.target_p]), case.note
    assert int(out["hitstun"][case.target_p]) == int(ref_out["hitstun"][case.target_p]), case.note
    assert int(out["action_id"][case.target_p]) == int(ref_out["action_id"][case.target_p]), case.note
